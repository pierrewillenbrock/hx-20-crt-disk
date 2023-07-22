
#include <fstream>
#include "parser.hpp"
#include "teledisk.h"
#include "string.h"

using namespace TeleDiskParser;

Sector::Sector(Sector const &s)
    : chs(s.chs)
    , idLengthCode(s.idLengthCode)
    , size(s.size)
    , flags(s.flags)
    , data(new char[s.size]) {
    memcpy(data,s.data,s.size);
}

Sector::Sector(std::basic_istream<char> &s) {
    SectorHeader sh;
    s.read((char *)&sh,sizeof sh);

    chs.idCylinder = sh.idCylinder;
    chs.idSide = sh.idSide;
    chs.idSector = sh.idSector;
    idLengthCode = sh.idLengthCode;
    size = 128 << idLengthCode;//2^idLengthCode * 128
    flags = (SectorFlags)sh.flags;

    if(flags & NoDataMask) {
        data = NULL;
        return;
    }
    data = new char[size];
    unsigned short clen;
    s.read((char *)&clen,sizeof clen);
    char *cbuf = new char[clen];
    s.read(cbuf,clen);
    //pulled everything, now rle-decompress(or whatever is needed)
    switch(cbuf[0]) {
    case 0:
        if(size+1 < clen)
            throw "Would decode more than sector size";
        memcpy(data,cbuf+1,clen-1);
        break;
    case 1: {
        unsigned int len = ((unsigned char)cbuf[2] << 9) |
                           ((unsigned char)cbuf[1] << 1);
        if(size < len)
            throw "Would decode more than sector size";
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
        while(rpos < clen && wpos < size) {
            switch(cbuf[rpos++]) {
            case 0: {
                unsigned int len = (unsigned char)cbuf[rpos++];
                while(len-- && wpos < size)
                    data[wpos++] = cbuf[rpos++];
                break;
            }
            case 1: {
                unsigned int len = (unsigned char)cbuf[rpos++];
                char d1 = cbuf[rpos++];
                char d2 = cbuf[rpos++];
                while(len-- && wpos < size) {
                    data[wpos++] = d1;
                    data[wpos++] = d2;
                }
                break;
            }
            }
        }
        if(rpos != clen || wpos != size)
            throw "Decoding fail";
        break;
    }
    }

    delete[] cbuf;
}

Sector::~Sector() {
    delete[] data;
}

Track::Track(std::basic_istream<char> &s) {
    TrackHeader th;
    s.read((char *)&th,sizeof th);
    //todo: check crc
    sectorCount = th.sectorCount;
    physCylinder = th.physCylinder;
    physSide = th.physSide;
    if(sectorCount == 255)
        return;

    for(unsigned int i = 0; i < th.sectorCount; i++)
        sectors.push_back(Sector(s));
}

Comment::Comment(std::basic_istream<char> &s) {
    CommentHeader ch;
    s.read((char *)&ch,sizeof ch);
    //todo: check crc
    timestamp.year = ch.year;
    timestamp.month = ch.month;
    timestamp.day = ch.day;
    timestamp.hour = ch.hour;
    timestamp.minute = ch.minute;
    timestamp.second = ch.second;
    char *b = new char[ch.size];
    s.read(b,ch.size);
    comment = std::string(b,ch.size);
    delete[] b;
}

Disk::Disk(const char *filename) {
    std::ifstream s(filename);
    readDisk(s);
}

Disk::Disk(std::basic_istream<char> &s) {
    readDisk(s);
}

void Disk::readDisk(std::basic_istream<char> &s) {
    FileHeader fh;
    s.read((char *)&fh,sizeof fh);
    //check for a good version
    if((fh.ID[0] != 'T' || fh.ID[1] != 'D') &&
            (fh.ID[0] != 't' || fh.ID[1] != 'd'))
        throw "Not a teledisk image";
    //todo: check crc if you know how.
    if(10 > fh.version || fh.version > 21)
        throw "Bad version";
    if(fh.ID[0] != 'T' || fh.ID[1] != 'D')
        throw "Cannot handle \"advanced compression\" yet";
    //todo: create decompressing istream for the rest of the file
    if(fh.trackDensity & 0x80)
        //comment section follows, create it.
        comment = new Comment(s);
    else
        comment = NULL;
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
        }
    } while(tracks.back().sectorCount != 255);
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
