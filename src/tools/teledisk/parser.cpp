
#include <fstream>
#include <sstream>
#include <array>
#include <assert.h>
#include "parser.hpp"
#include "teledisk.h"
#include "string.h"
#include "lzhstreams.hpp"

using namespace TeleDiskParser;

struct CRC {
private:
    std::array<unsigned short,256> crcTable;
    static CRC t;
    CRC() {
        const unsigned short polynomial = 0xa097;
        for(unsigned int i = 0; i < 256; i++)  {
            unsigned short currByte = i << 8;
            for(unsigned int bit = 0; bit < 8; bit++) {
                if((currByte & 0x8000) != 0) {
                    currByte <<= 1;
                    currByte ^= polynomial;
                } else {
                    currByte <<= 1;
                }
            }
            crcTable[i] = currByte;
        }
    }
public:
    /* bytes: 0x54 0x44 0x00 0x28 0x15 0x00 0x01 0x80 0x00 0x02; crc: 0xCF 0x3B (0x3bcf)*/
    /* crc: 0xD8 0x9C(0x9cd8); bytes: 0x1A 0x00 0x50 0x00 0x01 0x00 0x1F 0x31
     *    0x68 0x78 0x32 0x30  0x20 0x64 0x69 0x73
     *    0x6B 0x00 0x61 0x64  0x72 0x65 0x73 0x2E
     *    0x62 0x61 0x73 0x00  0x00 0x00 0x00 0x00
     *    0x00 0x00
     */
    /* bytes: 0x0e, 0x00, 0x00; crc: 0xd8 (crc & 0x00ff: 0x00d8) */
    /* CRC16:
     * polynomial: 0xA097
     * initial: 0
     * final xor: 0
     * no reflections
     */
    static unsigned short calc(unsigned char const *begin,
                               unsigned char const *end,
                               unsigned short initial = 0) {
        unsigned short crc = initial;
        for(;begin != end; begin++) {
            crc = (crc ^ (*begin << 8));
            crc = ((crc << 8) ^ t.crcTable[(crc >> 8) & 0xff]);
        }
        return crc;
    }
};

CRC CRC::t;


Sector::Sector(Sector const &s) =default;

Sector::Sector(std::basic_istream<char> &s) {
    SectorHeader sh;
    s.read((char *)&sh,sizeof sh);
    assert(s.gcount() == sizeof(sh));

    chs.idCylinder = sh.idCylinder;
    chs.idSide = sh.idSide;
    chs.idSector = sh.idSector;
    idLengthCode = sh.idLengthCode;
    flags = (SectorFlags)sh.flags;

    if(flags & NoDataMask) {
            return;
    }
    data.resize(128 << idLengthCode);//2^idLengthCode * 128
    unsigned short clen;
    s.read((char *)&clen,sizeof clen);
    assert(s.gcount() == sizeof(clen));
    if(clen == 0)
        return;
    std::vector<char> cbuf;
    cbuf.resize(clen);
    s.read(cbuf.data(),clen);
    assert(s.gcount() == clen);
    //pulled everything, now rle-decompress(or whatever is needed)
    switch(cbuf[0]) {
    case 0:
        if(data.size()+1 < clen)
            throw FormatError("Would decode more than sector size");
        memcpy(data.data(),cbuf.data()+1,clen-1);
        break;
    case 1: {
        unsigned int len = ((unsigned char)cbuf[2] << 9) |
                           ((unsigned char)cbuf[1] << 1);
        if(data.size() < len)
            throw FormatError("Would decode more than sector size");
        char d1 = cbuf[3], d2 = cbuf[4];
        for(unsigned int i = 0; i < len; i+=2) {
            data[i] = d1;
            data[i+1] = d2;
        }
        break;
    }
    case 2: {
        unsigned int rpos = 1;
        unsigned int wpos = 0;
        while(rpos < clen && wpos < data.size()) {
            switch(cbuf[rpos++]) {
            case 0: {
                unsigned int len = (unsigned char)cbuf[rpos++];
                while(len-- && wpos < data.size())
                    data[wpos++] = cbuf[rpos++];
                break;
            }
            case 1: {
                unsigned int len = (unsigned char)cbuf[rpos++];
                char d1 = cbuf[rpos++];
                char d2 = cbuf[rpos++];
                while(len-- && wpos < data.size()) {
                    data[wpos++] = d1;
                    data[wpos++] = d2;
                }
                break;
            }
            }
        }
        if(rpos != clen || wpos != data.size()) {
            std::stringstream ss;
            ss << "Decoding fail; rpos: " << std::hex << rpos <<
            " clen: " << std::hex << clen <<
            " wpos: " << std::hex << wpos <<
            " data.size: " << std::hex << data.size();
            throw FormatError(ss.str());
        }
        break;
    }
    }
    if((CRC::calc((unsigned char*)data.data(),(unsigned char*)(data.data()+data.size())) & 0xff) != sh.crc)
        throw FormatError("CRC mismatch");
}

Sector::Sector() : chs(255,255,255), idLengthCode(0), flags(SectorFlags(0)) {
}

static bool isTwoBytePattern(std::vector<char> const &in) {
    if(in.size() < 2)
        return false;
    if((in.size() & 1) != 0)
        return false;
    char b1 = in[0];
    char b2 = in[1];
    for(unsigned int i = 2; i < in.size(); i+=2) {
        if(b1 != in[i+0])
            return false;
        if(b2 != in[i+1])
            return false;
    }
    return true;
}

static void compress(std::vector<char> &out, std::vector<char> const &in) {
    //pattern:
    // byte 0 byte <len> bytes <<len> bytes of data>
    // byte 1 byte <len> byte <d1> byte <d2> decodes to: len pairs of d1,d2

    // extreme cases:
    // de be ef be ef be ef ad codes to: 00 08 de be ef be ef be ef ad
    // de be ef be ef be ef ad codes to: 00 01 de 01 03 be ef 00 01 ad
    // be ef be ef ad          codes to: 00 05 be ef be ef ad
    // be ef be ef ad          codes to: 01 02 be ef 00 01 ad
    // => should not try to make a repeating pattern for less than 3 repetitions,
    //    except when the literal byte count is still 0, then 2 repetitions is
    //    acceptable

    unsigned int literal_begin = 0;
    while(literal_begin < in.size()) {
        unsigned int literal_end = literal_begin;
        //collect literal bytes
        while(literal_end+1 < in.size()) {
            char d1 = in[literal_end];
            char d2 = in[literal_end+1];
            unsigned int repetitive_end = literal_end+2;
            while(repetitive_end+1 < in.size() &&
                    in[repetitive_end] == d1 && in[repetitive_end+1] == d2 &&
                    (repetitive_end+2-literal_end) / 2 < 255) {
                repetitive_end += 2;
            }
            unsigned int len = (repetitive_end-literal_end) / 2;
            if(len >= 3 || (literal_begin == literal_end && len >= 2)) {
                if(literal_begin != literal_end) {
                    out.push_back(0);
                    out.push_back(literal_end-literal_begin);
                    unsigned int old_end = out.size();
                    out.resize(old_end+literal_end-literal_begin);
                    memcpy(out.data()+old_end, in.data()+literal_begin, literal_end-literal_begin);
                }
                out.push_back(1);
                out.push_back(len);
                out.push_back(d1);
                out.push_back(d2);
                literal_begin = literal_end = repetitive_end;
            } else {
                literal_end++;
                if(literal_end-literal_begin == 255) {
                    out.push_back(0);
                    out.push_back(literal_end-literal_begin);
                    unsigned int old_end = out.size();
                    out.resize(old_end+literal_end-literal_begin);
                    memcpy(out.data()+old_end, in.data()+literal_begin, literal_end-literal_begin);
                    literal_begin = literal_end;
                }
            }
        }
        if(literal_end+1 >= in.size()) {
            literal_end = in.size();
        }
        if(literal_begin != literal_end) {
            out.push_back(0);
            out.push_back(literal_end-literal_begin);
            unsigned int old_end = out.size();
            out.resize(old_end+literal_end-literal_begin);
            memcpy(out.data()+old_end, in.data()+literal_begin, literal_end-literal_begin);
            literal_begin = literal_end;
        }
    }
}

void Sector::write(std::basic_ostream<char> &s, bool no_compress) {
    SectorHeader sh;
    if(!data.empty())
        flags = SectorFlags(unsigned(flags) & ~NoDataMask);

    sh.idCylinder = chs.idCylinder;
    sh.idSide = chs.idSide;
    sh.idSector = chs.idSector;
    sh.idLengthCode = idLengthCode;
    sh.flags = flags;
    sh.crc = CRC::calc((unsigned char*)data.data(),(unsigned char*)(data.data()+data.size())) & 0xff;
    s.write((char *)&sh,sizeof sh);

    if(flags & NoDataMask) {
        return;
    }
    if(no_compress) {
        unsigned short clen = 1+data.size();
        s.write((char *)&clen,sizeof clen);
        char c = 0;
        s.write(&c,1);
        s.write(data.data(),data.size());
    } else if(isTwoBytePattern(data)) {
        unsigned short clen = 5;
        s.write((char *)&clen,sizeof clen);
        char cbuf[5];
        cbuf[0] = 1;
        cbuf[1] = (data.size() >> 1) & 0xff;
        cbuf[2] = (data.size() >> 9) & 0xff;
        cbuf[3] = data[0];
        cbuf[4] = data[1];
        s.write(cbuf,clen);
    } else {
        std::vector<char> compressed;
        compress(compressed, data);
        char c;
        if(compressed.size() < data.size()) {
            unsigned short clen = 1+compressed.size();
            s.write((char *)&clen,sizeof clen);
            c = 2;
            s.write(&c,1);
            s.write(compressed.data(), compressed.size());
        } else {
            unsigned short clen = 1+data.size();
            s.write((char *)&clen,sizeof clen);
            c = 0;
            s.write(&c,1);
            s.write(data.data(),data.size());
        }
    }
}

Track::Track(std::basic_istream<char> &s) {
    TrackHeader th;
    s.read((char *)&th,sizeof th);
    sectorCount = th.sectorCount;
    physCylinder = th.physCylinder;
    physSide = th.physSide;
    if(sectorCount == 255)
        return;
    if((CRC::calc((unsigned char*)&th,(unsigned char*)&th.crc) & 0xff) != th.crc)
        throw FormatError("CRC mismatch");

    for(unsigned int i = 0; i < th.sectorCount; i++)
        sectors.push_back(Sector(s));
}

Track::Track() : sectorCount(0), physCylinder(0), physSide(0) {
}

void Track::write(std::basic_ostream<char> &s, bool no_compress_sectors) {
    TrackHeader th;
    if(sectorCount != 255 || sectors.size() != 0)
        sectorCount = sectors.size();
    th.sectorCount = sectorCount;
    th.physCylinder = physCylinder;
    th.physSide = physSide;
    th.crc = CRC::calc((unsigned char*)&th,(unsigned char*)&th.crc) & 0xff;
    s.write((char *)&th,sizeof th);
    if(sectorCount == 255)
        return;

    for(auto &sec : sectors) {
        sec.write(s, no_compress_sectors);
    }
}

Comment::Comment(std::basic_istream<char> &s) {
    CommentHeader ch;
    s.read((char *)&ch,sizeof ch);
    unsigned short crc = CRC::calc((unsigned char *)&ch.size,
                                   (unsigned char *)(&ch+1));
    timestamp.year = ch.year;
    timestamp.month = ch.month;
    timestamp.day = ch.day;
    timestamp.hour = ch.hour;
    timestamp.minute = ch.minute;
    timestamp.second = ch.second;
    std::vector<char> b;
    b.resize(ch.size);
    s.read(b.data(),ch.size);
    if(CRC::calc((unsigned char *)b.data(),
                 (unsigned char *)(b.data()+b.size()), crc) != ch.crc)
        throw FormatError("CRC mismatch");
    comment = std::string(b.data(),ch.size);
}

Comment::Comment() {
    timestamp.year = 0;
    timestamp.month = 0;
    timestamp.day = 0;
    timestamp.hour = 0;
    timestamp.minute = 0;
    timestamp.second = 0;
}

void Comment::write(std::basic_ostream<char> &s) {
    CommentHeader ch;
    ch.year = timestamp.year;
    ch.month = timestamp.month;
    ch.day = timestamp.day;
    ch.hour = timestamp.hour;
    ch.minute = timestamp.minute;
    ch.second = timestamp.second;
    ch.size = comment.size();
    unsigned short crc = CRC::calc((unsigned char *)&ch.size,
                                   (unsigned char *)(&ch+1));
    ch.crc = CRC::calc((unsigned char *)comment.data(),
                 (unsigned char *)(comment.data()+comment.size()), crc);
    s.write((char *)&ch,sizeof ch);
    s.write(comment.data(),comment.size());
}

Disk::Disk(const char *filename) {
    std::ifstream s(filename);
    readDisk(s);
}

Disk::Disk(std::basic_istream<char> &s) {
    readDisk(s);
}

Disk::Disk()
    : sourceDensity(D250Kbps),
      driveType(D360K),
      trackDensity(Single),
      comment(nullptr),
      dosMode(false),
      mediaSurfaces(0),
      advancedCompression(false),
      no_compress_sectors(false)
{
}

void Disk::readDiskMain(bool have_comment, std::basic_istream<char> &s) {
    if(have_comment)
        //comment section follows, create it.
        comment.reset(new Comment(s));
    else
        comment.reset();
    min.idCylinder = ~0U;
    min.idSide = ~0U;
    min.idSector = ~0U;
    max.idCylinder = 0;
    max.idSide = 0;
    max.idSector = 0;
    do {
        tracks.push_back(Track(s));
        for(std::vector<Sector>::iterator it =
                tracks.back().sectors.begin();
                it != tracks.back().sectors.end();
                it++) {
            if(min.idCylinder > it->chs.idCylinder)
                min.idCylinder = it->chs.idCylinder;
            if(min.idSide > it->chs.idSide)
                min.idSide = it->chs.idSide;
            if(min.idSector > it->chs.idSector)
                min.idSector = it->chs.idSector;
            if(max.idCylinder < it->chs.idCylinder)
                max.idCylinder = it->chs.idCylinder;
            if(max.idSide < it->chs.idSide)
                max.idSide = it->chs.idSide;
            if(max.idSector < it->chs.idSector)
                max.idSector = it->chs.idSector;
            cylinder_ids.insert(it->chs.idCylinder);
            side_ids.insert(it->chs.idSide);
            sector_ids.insert(it->chs.idSector);
            length_ids.insert(it->idLengthCode);
        }
    } while(tracks.back().sectorCount != 255);
    tracks.pop_back();
}

void Disk::readDisk(std::basic_istream<char> &s) {
    FileHeader fh;
    s.read((char *)&fh,sizeof fh);
    //check for a good version
    advancedCompression = false;
    if(fh.ID[0] == 'T' && fh.ID[1] == 'D')
        advancedCompression = false;
    else if(fh.ID[0] == 't' && fh.ID[1] == 'd')
        advancedCompression = true;
    else
        throw FormatError("Not a teledisk image");
    if(CRC::calc((unsigned char *)&fh,(unsigned char *)&fh.crc) != fh.crc)
        throw FormatError("CRC mismatch");
    if(10 > fh.version || fh.version > 21)
        throw FormatError("Bad version");
    sourceDensity = Density(fh.sourceDensity);
    driveType = DriveType(fh.driveType);
    trackDensity = TrackDensity(fh.trackDensity);
    dosMode = fh.DOSMode;
    mediaSurfaces = fh.mediaSurfaces;

    if(advancedCompression) {
        ilzhstream s2(s);
        readDiskMain(fh.trackDensity & 0x80, s2);
    } else {
        readDiskMain(fh.trackDensity & 0x80, s);
    }
}

void Disk::writeDiskMain(std::basic_ostream<char> &s) {
    if(comment) {
        comment->write(s);
    }
    for(auto &t : tracks) {
        t.write(s, no_compress_sectors);
    }
    Track t;
    t.physCylinder = 255;
    t.physSide = 255;
    t.sectorCount = 255;
    t.write(s);
}

void Disk::writeDisk(std::basic_ostream<char> &s) {
    FileHeader fh;
    memset(&fh, 0, sizeof(fh));
    fh.ID[0] = advancedCompression?'t':'T';
    fh.ID[1] = advancedCompression?'d':'D';
    fh.volSeq = 0;
    fh.checkSig = 0;
    fh.version = 21;
    fh.sourceDensity = sourceDensity;
    fh.driveType = driveType;
    fh.trackDensity = trackDensity;
    fh.DOSMode = dosMode?1:0;
    fh.mediaSurfaces = mediaSurfaces;
    if(comment)
        fh.trackDensity |= 0x80;
    fh.crc = CRC::calc((unsigned char*)&fh,(unsigned char*)&fh.crc);
    s.write((char *)&fh,sizeof fh);

    if(advancedCompression) {
        olzhstream s2(s);
        writeDiskMain(s2);
    } else {
        writeDiskMain(s);
    }
}

void Disk::write(const char *filename) {
    std::ofstream s(filename);
    writeDisk(s);
}

void Disk::write(std::basic_ostream<char> &s) {
    writeDisk(s);
}

Sector *Disk::findSector(CHS const &chs) {
    for(std::vector<Track>::iterator tit =
            tracks.begin();
            tit != tracks.end();
            tit++) {
        for(std::vector<Sector>::iterator sit =
                tit->sectors.begin();
                sit != tit->sectors.end();
                sit++) {
            if(chs == sit->chs)
                return &(*sit);
        }
    }
    return NULL;
}

Track *Disk::findTrack(unsigned int physCylinder, unsigned int physSide, bool create) {
    for(auto &t : tracks) {
        if(t.physCylinder == physCylinder && t.physSide == physSide &&
                t.sectorCount != 255)
            return &t;
    }
    if(!create)
        return nullptr;
    if(tracks.empty()) {
        tracks.push_back(Track());
    } else if(tracks.back().sectorCount != 255) {
        tracks.push_back(Track());
    }
    tracks.back().physCylinder = physCylinder;
    tracks.back().physSide = physSide;
    tracks.back().sectorCount = 0;
    return &tracks.back();
}
