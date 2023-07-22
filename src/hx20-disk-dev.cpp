
/* protocol documentation in tms_10-11.pdf
   DIP4 on the HX-20 must be on for basic to look for the TF-20
 */

#include <stdlib.h>
#include <string.h>
#include <fstream>
#include "hx20-disk-dev.hpp"
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

static void hexdump(char const *buf, unsigned int size) {
    uint16_t addr;
    for(addr = 0; addr < size; addr+=16) {
        printf("%04x:",addr);
        uint16_t a2 = addr;
        for(a2 = addr; a2 < addr+16; a2++) {
            if((a2 & 0x3) == 0)  //4-byte seperator
                printf(" ");
            if(a2 < size)
                printf(" %02x",(unsigned char)buf[a2]);
            else
                printf("   ");
        }
        printf(" ");
        for(a2 = addr; a2 < addr+16; a2++) {
            if(a2 < size) {
                if(0x20 <= buf[a2] && buf[a2] < 0x7f)
                    printf("%c",buf[a2]);
                else
                    printf(".");
            } else
                printf(" ");
        }
        printf("\n");
    }
    if((addr & 0xf) != 0xf)
        printf("\n");
}

int HX20DiskDevice::getDeviceID() const {
    return 0x31;
}

static void hx20ToUnixFilename(char *dst,uint8_t *src) {
    char *fp = dst;
    memcpy(fp,src,8);
    for(fp += 7; *fp == ' ' && fp >= dst; fp--) {}
    fp++;
    *fp = '.';
    fp++;
    memcpy(fp,src+8,3);
    for(fp +=2; *fp == ' '; fp--) {}
    fp++;
    *fp = 0;
}

static bool unixToHx20Filename(uint8_t *dst,char *src) {
    if(*src == '.')  //drop leading '.'
        src++;
    if(src[0] == '.' && src[1] == '\0')
        return false;
    unsigned int i = 0;
    memset(dst,' ',11);
    for(i = 0; i < 8 && *src != '.' && *src; i++, src++) {
        dst[i] = *src;
    }
    if(*src != '.')
        return false;
    src++;
    for(i = 0; i < 3 && *src; i++, src++) {
        dst[i+8] = *src;
    }
    if(*src)
        return false;
    return true;
}

static bool matchFilename(char *name, char *pattern) {
    //chars match exactly, '?' match any char, or '.', but don't eat '.'
    while(*name && *pattern) {
        if(*pattern == *name || *pattern == '?') {
            if(*name != '.' || *pattern != '?')
                name++;
            pattern++;
            continue;
        } else
            return false;
    }
    return true;
}

class DirSearch {
private:
    DIR *dir;
    uint8_t drive;
    char *pattern;
public:
    DirSearch(char const *name, uint8_t drive, char const *pattern);
    ~DirSearch();
    bool good();
    struct dirent *readdir();
    void findNext(uint8_t *obuf);
};

DirSearch::DirSearch(char const *name, uint8_t drive, char const *pattern)
    : dir(opendir(name))
    , drive(drive)
    , pattern(strdup(pattern)) {
}

DirSearch::~DirSearch() {
    if(dir)
        closedir(dir);
    free(pattern);
}

bool DirSearch::good() {
    return dir;
}

struct dirent *DirSearch::readdir() {
    return ::readdir(dir);
}

void DirSearch::findNext(uint8_t *obuf) {
    struct dirent *de;
    while((de = readdir())) {
        if(matchFilename(de->d_name,pattern) &&
                unixToHx20Filename(obuf+2,de->d_name)) {
            printf("found %s\n",de->d_name);
            obuf[0] = 0x00;
            obuf[1] = drive;
            //filename already filled in. may whish to
            //modify r/o and sys flags.
            obuf[13] = 0;//extents
            obuf[14] = 0;
            obuf[15] = 0;
            obuf[16] = 0;//record number
            memset(obuf+17,0,16);
            return;
        }
    }

    obuf[0] = 0xff;//file not found
}

class FCB {
private:
    std::fstream st;
public:
    FCB(char const *filename, bool create = false);
    bool good();
    uint16_t size();//in records
    uint8_t write(uint16_t record,uint8_t *buf);
    uint8_t read(uint16_t record,uint8_t *buf);
};

FCB::FCB(char const *filename, bool create)
    : st((std::string("disk/")+filename).c_str(),
         create? std::ios::app|std::ios::in|std::ios::out
         : std::ios::in|std::ios::out) {
}

bool FCB::good() {
    return st.good();
}

uint16_t FCB::size() {
    st.seekg(0,std::ios::end);
    uint64_t sz = st.tellg();
    if(sz >= (65535 << 7)) {
        sz = (65535 << 7);
    }
    return (sz >> 7) + ((sz & ((1 << 7)-1))?1:0);
}

uint8_t FCB::write(uint16_t record,uint8_t *buf) {
    st.seekp(record*128,std::ios::beg);
    st.write((char *)buf,128);
    return 0;//todo: get the st state and make a bdos error if needed
}

uint8_t FCB::read(uint16_t record,uint8_t *buf) {
    st.seekg(record*128,std::ios::beg);
    st.read((char *)buf,128);
    return 0;//todo: get the st state and make a bdos error if needed
}

/* "bios" error codes:
   fa: read error
   fb: write error
   fc: drive select error
   fd/fe: write protect error
*/

int HX20DiskDevice::gotPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                               uint16_t size, uint8_t *buf,
                               HX20SerialConnection *conn) {
    switch(fnc) {
    case 0x0e: { //reset
        uint8_t obuf[1];
        obuf[0] = 0;
        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x0f: //file open
    case 0x16: { //file create (those two are very similar on unix)
        if(size != 0x0f)
            return 0;
        char filename[13];
        hx20ToUnixFilename(filename,buf+3);
        uint16_t hx20FcbAddress = (buf[0] << 8) | buf[1];//only usable as key into a fcb map, i guess
        uint8_t drive_code = buf[2];
        uint8_t extent = buf[0x0e];
        printf("hx20 tries to open %s(extent %d) on drive %d for fcb at 0x%04x.\n",
               filename,extent,drive_code,hx20FcbAddress);
        uint8_t obuf[1];
        if(drive_code < 1 || drive_code > 2) {
            obuf[0] = 0xfc;//drive select error
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        if(drive_code == 2) {
            obuf[0] = 0xfa;//read error
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        //ios::nocreate and ios::noreplace
        FCB *fcb = new FCB(filename, fnc == 0x16);
        if(!fcb->good()) {
            delete fcb;
            obuf[0] = 0xff;//file not found
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        if(fcbs[hx20FcbAddress])
            delete fcbs[hx20FcbAddress];
        fcbs[hx20FcbAddress] = fcb;
        obuf[0] = 0x00;//file opened
        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x10: { //file close
        if(size != 0x02)
            return 0;
        uint16_t hx20FcbAddress = (buf[0] << 8) | buf[1];//only usable as key into a fcb map, i guess
        printf("hx20 tries to close fcb at 0x%04x.\n",
               hx20FcbAddress);
        uint8_t obuf[1];
        if(!fcbs[hx20FcbAddress]) {
            obuf[0] = 0xff;//fcb unknown
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        delete fcbs[hx20FcbAddress];
        fcbs.erase(hx20FcbAddress);
        obuf[0] = 0x00;//file closed
        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x11: { //findfirst
        if(size != 0x0d)
            return 0;
        char pattern[13];
        hx20ToUnixFilename(pattern,buf+1);
        printf("hx20 requested first file matching %s\n",
               pattern);
        printf("extent number %d\n",buf[1+8+3]);
        printf("from drive %d\n",buf[0]);

        uint8_t obuf[0x21];
        if(buf[0] < 1 || buf[0] > 2) {
            obuf[0] = 0xfc;//drive select error
            return conn->sendPacket(did, sid, fnc, 33, obuf);
        }

        if(buf[0] == 2) {
            obuf[0] = 0xfa;//read error
            return conn->sendPacket(did, sid, fnc, 33, obuf);
        }

        if(dirSearch)
            delete dirSearch;
        dirSearch = new DirSearch("disk",buf[0],pattern);
        if(!dirSearch->good()) {
            obuf[0] = 0xfa;//read error
            return conn->sendPacket(did, sid, fnc, 33, obuf);
        }

        dirSearch->findNext(obuf);

        return conn->sendPacket(did, sid, fnc, 33, obuf);
    }
    case 0x12: { //find next
        if(size != 0x01)
            return 0;
        printf("hx20 requested next file\n");

        uint8_t obuf[0x21];

        dirSearch->findNext(obuf);

        return conn->sendPacket(did, sid, fnc, 33, obuf);
    }
    case 0x13: { //remove a file
        if(size != 0x0d)
            return 0;
        char filename[13];
        hx20ToUnixFilename(filename,buf+1);
        uint8_t drive_code = buf[0];
        uint8_t extent = buf[0x0c];
        printf("hx20 tries to delete %s(extent %d) on drive %d.\n",
               filename,extent,drive_code);

        uint8_t obuf[0x1];

        if(unlink((std::string("disk/")+filename).c_str()) == -1) {
            if(errno == ENOENT)
                obuf[0] = 0xff;//file not found
            else if(errno == EROFS)
                obuf[0] = 0xfd;//write protect error
            else
                obuf[0] = 0xfb;//write error
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }

        obuf[0] = 0x00;
        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x17: { //file rename
        if(size != 0x20)
            return 0;
        char filename_old[13];
        char filename_new[13];
        hx20ToUnixFilename(filename_old,buf+1);
        hx20ToUnixFilename(filename_new,buf+0x11);
        uint8_t drive_code = buf[0];
        uint8_t extent_old = buf[0x0c];
        uint8_t extent_new = buf[0x1c];
        printf("hx20 tries to rename file %s(extent %d) to %s(%d) on drive %d\n",
               filename_old,extent_old,filename_new,extent_new,drive_code);
        uint8_t obuf[1];
        if(drive_code < 1 || drive_code > 2) {
            obuf[0] = 0xfc;//drive select error
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        if(drive_code == 2) {
            obuf[0] = 0xfa;//read error
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        //0xff if source file not existant, or the destination already
        //exists(need to check against basic docs of rename
        //command -- result: basic checks for existing destination)
        //if our unix-rename fails, we do a write error(0xfb)
        if(rename((std::string("disk/")+filename_old).c_str(),
                  (std::string("disk/")+filename_new).c_str()) == -1) {
            if(errno == ENOENT)
                obuf[0] = 0xff;//file not found
            else if(errno == EROFS)
                obuf[0] = 0xfd;//write protect error
            else
                obuf[0] = 0xfb;//write error
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }

        obuf[0] = 0x00;//rename done
        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x21: { //read record from file
        if(size != 0x05)
            return 0;
        uint16_t hx20FcbAddress = (buf[0] << 8) | buf[1];
        uint16_t record = (buf[0x3] << 8) | buf[0x2];
        //buf[0x4] never gets set to anything != 0
        printf("hx20 tries to read record %d to fcb at 0x%04x.\n",
               record,hx20FcbAddress);

        uint8_t obuf[0x83];

        if(!fcbs[hx20FcbAddress]) {
            obuf[2] = 0xff;//fcb unknown
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }

        obuf[0] = record >> 7; //extent number(guessing here, anything goes)
        obuf[1] = record & 0x7f; //current record number(guessing here)
        obuf[0x82] = fcbs[hx20FcbAddress]->read(record,obuf+2);

        return conn->sendPacket(did, sid, fnc, 0x83, obuf);
    }
    case 0x22: { //write record to file
        if(size != 0x85)
            return 0;
        uint16_t hx20FcbAddress = (buf[0] << 8) | buf[1];
        uint16_t record = (buf[0x83] << 8) | buf[0x82];
        //buf[0x84] never gets set to anything != 0
        printf("hx20 tries to write record %d to fcb at 0x%04x.\n",
               record,hx20FcbAddress);


        uint8_t obuf[3];

        if(!fcbs[hx20FcbAddress]) {
            obuf[2] = 0xff;//fcb unknown
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }

        obuf[0] = record >> 7; //extent number(guessing here, anything goes)
        obuf[1] = record & 0x7f; //current record number(guessing here)
        obuf[2] = fcbs[hx20FcbAddress]->write(record,buf+2);

        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x23: { //file size calculation
        if(size != 0x02)
            return 0;
        uint16_t hx20FcbAddress = (buf[0] << 8) | buf[1];//only usable as key into a fcb map, i guess
        printf("hx20 tries to calculate size of fcb at 0x%04x.\n",
               hx20FcbAddress);
        uint8_t obuf[6];
        if(!fcbs[hx20FcbAddress]) {
            obuf[5] = 0xff;//fcb unknown
            return conn->sendPacket(did, sid, fnc, 6, obuf);
        }

        uint16_t sz = fcbs[hx20FcbAddress]->size();

        obuf[0] = 0;//extent number(unused?)
        obuf[1] = 0;//current record number(unused?)
        obuf[2] = sz & 0xff;//R0 (low byte of record count)
        obuf[3] = sz >> 8;//R1 (high byte of record count)
        obuf[4] = 0;//R2 (unused?)
        obuf[5] = 0x00;//everything okay
        return conn->sendPacket(did, sid, fnc, 6, obuf);
    }
    case 0x7a: { // disk all copy
        if(size != 0x1)
            return 0;
        uint8_t drive_code = buf[0];
        printf("hx20 tries to copy disk in drive %d\n",
               drive_code);
        uint8_t obuf[0x3];

        if(drive_code < 1 || drive_code > 2) {
            obuf[0x2] = 0xfc;//drive select error
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }
        if(drive_code == 2) {
            obuf[0x2] = 0xfa;//read error
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }

        obuf[0x0] = 0xff;//msb of currently formatted track number
        obuf[0x1] = 0xff;//lsb of currently formatted track number
        obuf[0x2] = 0xfa;//we don't support disk copy
        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x7b: { //direct write
        if(size != 0x83)
            return 0;
        uint8_t drive_code = buf[0];
        uint8_t track = buf[1];
        uint8_t sector = buf[2];//128 byte sectors
        printf("hx20 tries do direct read from drive %d, track %d, sector %d\n",
               drive_code,track,sector);
        uint8_t obuf[0x1];

        if(drive_code < 1 || drive_code > 2) {
            obuf[0x0] = 0xfc;//drive select error
            return conn->sendPacket(did, sid, fnc, 0x1, obuf);
        }
        if(drive_code == 2) {
            obuf[0x0] = 0xfb;//write error
            return conn->sendPacket(did, sid, fnc, 0x1, obuf);
        }

        //write(buf+3,128);
        obuf[0x0] = 0xfb;//we don't support direct disk writes
        return conn->sendPacket(did, sid, fnc, 0x1, obuf);
    }
    case 0x7c: { // disk formatting
        if(size != 0x1)
            return 0;
        uint8_t drive_code = buf[0];
        printf("hx20 tries to format disk in drive %d\n",
               drive_code);
        uint8_t obuf[0x3];

        if(drive_code < 1 || drive_code > 2) {
            obuf[0x2] = 0xfc;//drive select error
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }
        if(drive_code == 2) {
            obuf[0x2] = 0xfb;//write error
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }

        obuf[0x0] = 0xff;//msb of currently formatted track number
        obuf[0x1] = 0xff;//lsb of currently formatted track number
        obuf[0x2] = 0xfb;//we don't support format
        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x7d: { // new system generation
        if(size != 0x1)
            return 0;
        printf("hx20 tries to create a system disk in 2nd drive from the system in 1st drive\n");
        uint8_t obuf[0x3];

        obuf[0x0] = 0xff;//
        obuf[0x1] = 0xff;//done, 0x0000 => not done
        obuf[0x2] = 0xfb;//write error, we don't support sysgen
        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x7e: { //disk free size calculation
        if(size != 0x01)
            return 0;
        uint8_t drive_code = buf[0];
        printf("hx20 tries to calculate free space of drive %d\n",
               drive_code);
        uint8_t obuf[2];

        if(drive_code < 1 || drive_code > 2) {
            obuf[1] = 0xfc;//drive select error
            return conn->sendPacket(did, sid, fnc, 2, obuf);
        }
        if(drive_code == 2) {
            obuf[1] = 0xfa;//read error
            return conn->sendPacket(did, sid, fnc, 2, obuf);
        }

        obuf[0] = 255;//number of free 2k-sectors
        obuf[1] = 0;//bdos error or 0
        return conn->sendPacket(did, sid, fnc, 2, obuf);
    }
    case 0x7f: { //direct read
        if(size != 0x03)
            return 0;
        uint8_t drive_code = buf[0];
        uint8_t track = buf[1];
        uint8_t sector = buf[2];//128 byte sectors
        printf("hx20 tries do direct read from drive %d, track %d, sector %d\n",
               drive_code,track,sector);
        uint8_t obuf[0x81];

        if(drive_code < 1 || drive_code > 2) {
            obuf[0x80] = 0xfc;//drive select error
            return conn->sendPacket(did, sid, fnc, 0x81, obuf);
        }
        if(drive_code == 2) {
            obuf[0x80] = 0xfa;//read error
            return conn->sendPacket(did, sid, fnc, 0x81, obuf);
        }

        //read(obuf,128);
        obuf[0x80] = 0xfa;//we don't support direct disk reads
        return conn->sendPacket(did, sid, fnc, 0x81, obuf);
    }
    case 0x80: { //disk boot
        if(size != 1)
            return 0;
        char filename[13];
        snprintf(filename,13,"BOOT%02X.SYS",buf[0]);
        printf("hx20 requested %s\n",filename);

        uint8_t obuf[257];
        std::ifstream f((std::string("disk/")+filename).c_str());

        if(!f.good()) {
            obuf[0] = 0xff;//file not found
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        } else {
            //we will put 0 in obuf[0] and then put the rest of
            //BOOT??.SYS in the rest of the buffer, setting size
            //appropriately
            obuf[0] = 0;//file found
            f.read((char *)(obuf+1),256);
            return conn->sendPacket(did, sid, fnc, 256, obuf);
        }
    }
    case 0x81: { //load open
        /*general format of firmware file:
          256 bytes header: {
           0:     unused
           1-2:   code size, little endian
           3-255: unused
          }
          code
          relocation data: bit field determining which code bytes need
                                   relocation, msb first
         */

        char filename[13];
        hx20ToUnixFilename(filename,buf);
        printf("hx20 requested %s\n",
               filename);
        printf("relocation: %s\n",
               buf[11]==0?"none":
               buf[11]==1?"from starting address":
               buf[11]==2?"from ending address":
               "unknown");
        uint16_t reloc_address = buf[13] | (buf[12] << 8);
        printf("address: 0x%04x\n",
               reloc_address
              );

        uint8_t obuf[3];
        std::ifstream f((std::string("disk/")+filename).c_str());

        if(!f.good()) {
            obuf[0] = 0xff;//file not found
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        } else {
            f.seekg(0,std::ios::end);
            unsigned int size = f.tellg();
            f.seekg(0,std::ios::beg);

            if(load_buffer)
                delete[] load_buffer;
            load_buffer = new char[size];
            f.read(load_buffer,size);

            unsigned int code_size = (uint8_t)load_buffer[1] |
                                     ((uint8_t)load_buffer[2] << 8);

            if(buf[11] != 0) {
                //now we need to relocate
                uint8_t reloc_offset;
                if(buf[11] == 2)
                    reloc_offset =
                    (reloc_address - code_size) >> 8;
                else
                    reloc_offset =
                    reloc_address >> 8;
                reloc_offset -= 0x60;
                printf("relocation offset = 0x%02x\n",
                       reloc_offset);

                char *bp = load_buffer+0x100;
                char *mp = bp+code_size;
                printf("buffer: %p, bp: %p, mp: %p, code size: 0x%x\n",
                       load_buffer,bp,mp,code_size);
                uint8_t t = 0x80;
                for(;
                        bp < load_buffer+0x100+code_size;
                        bp++) {
                    if(*mp & t)
                        (*bp)+=reloc_offset;
                    t >>= 1;
                    if(!t) {
                        mp++;
                        t = 0x80;
                    }
                }
            }

            obuf[0] = 0x00;
            obuf[1] = code_size >> 8;
            obuf[2] = code_size;
            hexdump((char *)obuf,3);
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }
    }
    case 0x83: { //read one block(goes with 0x81: load open)
        unsigned int record = (buf[0] << 8) | buf[1];
        printf("hx20 requested record 0x%02x\n",record);
        uint8_t obuf[2+128+1];
        if(!load_buffer) {
            obuf[1] = buf[1]+1;
            obuf[0] = buf[0]+((buf[1] == 0xff)?1:0);
            obuf[2+128] = 0xff;

            hexdump((char *)obuf,128+3);
            return conn->sendPacket(did, sid, fnc, 2+128+1, obuf);
        }

        unsigned int code_size = (unsigned char)load_buffer[1] |
                                 ((unsigned char)load_buffer[2] << 8);

        obuf[1] = buf[1]+1;
        obuf[0] = buf[0]+((buf[1] == 0xff)?1:0);
        memcpy(obuf+2,load_buffer + 0x100 + record*128,128);

        obuf[2+128] = record*128 > code_size?0xff:0x00;

        hexdump((char *)obuf,128+3);
        return conn->sendPacket(did, sid, fnc, 2+128+1, obuf);
    }
    default: {
        printf("got packet: sid = 0x%04x, did = 0x%04x, "
               "fnc = 0x%02x, size = 0x%04x\n",
               sid,did,fnc,size);
        hexdump((char *)buf,size);
        return 0;
    }
    }
}

HX20DiskDevice::HX20DiskDevice()
    : load_buffer(NULL)
    , dirSearch(NULL) {
}


