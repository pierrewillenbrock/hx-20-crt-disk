
/* protocol documentation in tms_10-11.pdf
   DIP4 on the HX-20 must be on for basic to look for the TF-20

   https://electrickery.nl/comp/hx20/epsp.html
   https://electrickery.nl/comp/hx20/doc/
 */

#include "hx20-disk-dev.hpp"

#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <QLabel>
#include <QLineEdit>
#include <QDockWidget>
#include <QMainWindow>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QStyle>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QFileDialog>
#include <QMessageBox>

#include "../../dockwidgettitlebar.hpp"
#include "tf20drivediskimage.hpp"
#include "tf20drivedirectory.hpp"

static void hexdump(char const *buf, unsigned int size) {
    uint16_t addr;
    for(addr = 0; addr < size; addr+=16) {
        printf("%04x:",addr);
        uint16_t a2;
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

static void hx20ToUnixFilename(char *dst,uint8_t const *src) {
    char *fp = dst;
    memcpy(fp,src,8);
    for(fp += 7; *fp == ' ' && fp >= dst; fp--) {}
    fp++;
    *fp = '.';
    fp++;
    for(int i = 0; i < 3; i++)
        fp[i] = (src+8)[i] & 0x7f;
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

HX20DiskDevice::DriveInfo &HX20DiskDevice::drive(uint8_t drive_code) {
    if(drive_code == 1)
        return drive_1;
    else if(drive_code == 2)
        return drive_2;
    else
        throw BDOSError(BDOS_SELECT_ERROR);
}

HX20DiskDevice::DriveInfo const &HX20DiskDevice::drive(uint8_t drive_code) const {
    if(drive_code == 1)
        return drive_1;
    else if(drive_code == 2)
        return drive_2;
    else
        throw std::runtime_error("Drive code invalid");
}

int HX20DiskDevice::getDeviceID() const {
    return 0x31+ddno;
}

void HX20DiskDevice::triggerActivityStatus(int drive_code) {
    drive(drive_code).status_icon->setPixmap(
    activeIcon.pixmap(
    drive(drive_code).status_icon->style()->pixelMetric(QStyle::PM_ButtonIconSize)));
    drive(drive_code).status_timer->start(400);
}

void HX20DiskDevice::setCurrentFilename(int drive_code, std::string const &filename) {
    drive(drive_code).last_file->setText(QString::fromStdString(filename));
}

class FileCloser {
private:
    TF20DriveInterface *drive;
    void *fcb;
public:
    FileCloser(TF20DriveInterface *drive, void *fcb)
        : drive(drive), fcb(fcb) {}
    ~FileCloser() {
        drive->file_close(fcb);
    }
};

int HX20DiskDevice::gotPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                              uint16_t size, uint8_t *ibuf,
                              HX20SerialConnection *conn) {
    switch(fnc) {
    case 0x00: { //system reset
        /*
         * obuf:
         * 0: status
         */
        uint8_t obuf[0x1] = {0};
        obuf[0x0] = BDOS_OK;
        return conn->sendPacket(did, sid, fnc, 0x1, obuf);
    }
    //case 0x01: broken; 0 byte in, 1 byte out. probably: console in. actually: console out.
    //case 0x02: broken; 0 byte in, 1 byte out. probably: console out. actually: aux out.
    //                   also lacks handling for the input byte for either.
    //case 0x03: broken; 0 byte in, 1 byte out. probably: aux in. actually: console direct io.
    //case 0x04: broken; 1 byte in, 1 byte out. probably: aux out. actually: set io byte.
    //case 0x0b: broken; 0 byte in, 1 byte out. probably: console status. actually: file create.
    case 0x0d: { // reset all drives
        /*
         * obuf:
         * 0: status
         */
        triggerActivityStatus(1);
        triggerActivityStatus(2);
        uint8_t obuf[1] = {0};
        obuf[0] = 0;
        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x0e: { // drive select
        /*
         * ibuf:
         * 0: drive code
         *
         * obuf:
         * 0: status
         */
        uint8_t drive_code = ibuf[0];
        triggerActivityStatus(drive_code);
        uint8_t obuf[1] = {0};
        obuf[0] = 0;
        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x0f: //file open
    case 0x16: { //file create (those two are very similar on unix)
        /*
         * ibuf:
         * 0,1: hx-20 filehandle
         * 2: drive code and 3 high bits for matching us field
         * 3-10: file name match (may be '?')
         * 11-13: file type match (may be '?', then matches any flag)
         * 14: ex match, (may be '?'; lowest 1 bit is ignored in any case)
         *
         * obuf:
         * 0: status
         */
        if(size != 0x0f)
            return 0;
        char filename[13];
        hx20ToUnixFilename(filename,ibuf+3);
        uint16_t hx20FcbAddress = (ibuf[0] << 8) | ibuf[1];//only usable as key into a fcb map, i guess
        uint8_t user = 0;
        uint8_t drive_code = ibuf[2] & 0x1f;
        uint8_t us = (ibuf[2] & 0xe0) | user;
        uint8_t extent = ibuf[0x0e];
        triggerActivityStatus(drive_code);
        printf("hx20 tries to %s %s(extent %d) on drive %d for fcb at 0x%04x.\n",
               fnc == 0x0f?"open":"create",
               filename,extent,drive_code,hx20FcbAddress);
        uint8_t obuf[1] = {0};
        try {
            if(!drive(drive_code).drive) {
                throw BDOSError(BDOS_READ_ERROR);
            }
            TF20DriveInterface *drive = this->drive(drive_code).drive.get();
            void *fcb;
            if(fnc == 0x16) {
                fcb = drive->file_create(us, ibuf+3, extent);
            } else {
                fcb = drive->file_open(us, ibuf+3, extent);
            }
            if(!fcb) {
                obuf[0] = BDOS_FILE_NOT_FOUND;
                return conn->sendPacket(did, sid, fnc, 1, obuf);
            }
            if(fcbs[hx20FcbAddress].fcb) {
                fcbs[hx20FcbAddress].drive->file_close(fcbs[hx20FcbAddress].fcb);
            }
            fcbs[hx20FcbAddress].fcb = fcb;
            fcbs[hx20FcbAddress].drive_code = drive_code;
            fcbs[hx20FcbAddress].filename = filename;
            fcbs[hx20FcbAddress].drive = drive;
            setCurrentFilename(drive_code, filename);
            obuf[0] = BDOS_OK;//file opened
        } catch(BDOSError const &e) {
            obuf[0] = e.getBDOSError();
        }
        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x10: { //file close
        /*
         * ibuf:
         * 0,1: hx-20 filehandle
         *
         * obuf:
         * 0: status
         */
        if(size != 0x02)
            return 0;
        uint16_t hx20FcbAddress = (ibuf[0] << 8) | ibuf[1];//only usable as key into a fcb map, i guess
        printf("hx20 tries to close fcb at 0x%04x.\n",
               hx20FcbAddress);
        uint8_t obuf[1] = {0};
        try {
            if(fcbs.find(hx20FcbAddress) == fcbs.end()) {
                //the TFDOS does not handle this case.
                throw BDOSError(BDOS_FILE_NOT_FOUND);//fcb unknown
            }
            triggerActivityStatus(fcbs[hx20FcbAddress].drive_code);
            setCurrentFilename(fcbs[hx20FcbAddress].drive_code, fcbs[hx20FcbAddress].filename);
            fcbs[hx20FcbAddress].drive->file_close(fcbs[hx20FcbAddress].fcb);
            fcbs.erase(hx20FcbAddress);
            obuf[0] = BDOS_OK;//file closed
        } catch(BDOSError const &e) {
            obuf[0] = e.getBDOSError();
        }
        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x11: { //findfirst
        /*
         * ibuf:
         * 0: drive code and 3 high bits for matching us field
         * 1-8: file name match (may be '?')
         * 9-11: file type match (may be '?', then matches any flag)
         * 12: ex match, (may be '?'; lowest 1 bit is ignored in any case)
         *
         * obuf:
         * 0: status
         * 1-32: (full) directory entry
         *   0: us
         *   1-8: filename
         *   9-11: file type and flags
         *   12: ex (3 high bits matter, next 4 are extent group,
         *           lsb tells which actual extent it is filled up to)
         *   13: unused
         *   14: exh (4 more bits for extent group)
         *   15: rc (0-0x80 cpm sectors in the extent(not extent group);
         *           only 0x80 when both extents in this group are full)
         */
        if(size != 0x0d)
            return 0;
        char pattern[13];
        hx20ToUnixFilename(pattern,ibuf+1);
        uint8_t drive_code = ibuf[0];
        uint8_t extent = ibuf[1+8+3];
        printf("hx20 requested first file matching %s\n",
               pattern);
        printf("extent number %d\n",extent);
        printf("from drive %d\n",drive_code);

        triggerActivityStatus(drive_code);

        uint8_t obuf[0x21] = {0};
        try {
            if(!drive(drive_code).drive) {
                throw BDOSError(BDOS_READ_ERROR);
            }
            TF20DriveInterface *drive = this->drive(drive_code).drive.get();

            std::string filename;
            uint8_t user = 0;
            drive->file_find_first((ibuf[0] & 0xe0) | user,
                                   ibuf+1, extent, obuf+1, filename);
            dirSearch.drive = drive;
            dirSearch.drive_code = drive_code;
            setCurrentFilename(drive_code, filename);
            obuf[0] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[0] = e.getBDOSError();
        }

        return conn->sendPacket(did, sid, fnc, 33, obuf);
    }
    case 0x12: { //find next
        /*
         * ibuf:
         * 0: unused
         *
         * obuf:
         * 0: status
         * 1-32: (full) directory entry
         *   0: us
         *   1-8: filename
         *   9-11: file type and flags
         *   12: ex (3 high bits matter, next 4 are extent group,
         *           lsb tells which actual extent it is filled up to)
         *   13: unused
         *   14: exh (4 more bits for extent group)
         *   15: rc (0-0x80 cpm sectors in the extent(not extent group);
         *           only 0x80 when both extents in this group are full)
         */
        if(size != 0x01)
            return 0;
        //would the single byte be the driveCode? protocol says to ignore.
        printf("hx20 requested next file\n");

        uint8_t obuf[0x21] = {0};

        try {
            std::string filename;
            dirSearch.drive->file_find_next(obuf+1, filename);

            setCurrentFilename(dirSearch.drive_code, filename);
            obuf[0] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[0] = e.getBDOSError();
        }

        triggerActivityStatus(dirSearch.drive_code);

        return conn->sendPacket(did, sid, fnc, 33, obuf);
    }
    case 0x13: { //remove a file
        /*
         * ibuf:
         * 0: drive and high 3 bits us field match for file
         * 1-8: file name match of file
         * 9-11: file type match of file
         * 12: ex field match of file(again high 3 bits)
         *
         * obuf:
         * 0: status
         */
        if(size != 0x0d)
            return 0;
        char filename[13];
        hx20ToUnixFilename(filename,ibuf+1);
        uint8_t user = 0;
        uint8_t drive_code = ibuf[0];
        uint8_t us = (ibuf[0] & 0xe0) | user;
        uint8_t extent = ibuf[0x0c];
        printf("hx20 tries to delete %s(extent %d) on drive %d.\n",
               filename,extent,drive_code);
        triggerActivityStatus(drive_code);

        uint8_t obuf[0x1] = {0};

        try {
            if(!drive(drive_code).drive) {
                throw BDOSError(BDOS_READ_ERROR);
            }
            TF20DriveInterface *drive = this->drive(drive_code).drive.get();

            drive->file_remove(us, ibuf+1, extent);
            obuf[0] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[0] = e.getBDOSError();
        }

        setCurrentFilename(drive_code, filename);

        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x14: { // file read at extent and record position
        /*
         * ibuf:
         * 0,1: hx-20 filehandle
         * 2: extent
         * 3: record (0-0x7f)
         *
         * obuf:
         * 0: value of ex field of current directory entry
         * 1: record number, 0-0x80
         * 2-129: sector data
         * 130: status
         */
        if(size != 0x04)
            return 0;
        uint16_t hx20FcbAddress = (ibuf[0] << 8) | ibuf[1];
        uint8_t extent = ibuf[2];
        uint8_t record = ibuf[3];
        printf("hx20 tries to read record %d(%d,%d) to fcb at 0x%04x.\n",
               record+extent*128,extent,record,hx20FcbAddress);

        uint8_t obuf[0x83] = {0};

        try {
            if(fcbs.find(hx20FcbAddress) == fcbs.end()) {
                //the TFDOS does not handle this case.
                throw BDOSError(BDOS_FILE_NOT_FOUND);//fcb unknown
            }
            triggerActivityStatus(fcbs[hx20FcbAddress].drive_code);

            uint8_t cur_extent;
            uint8_t cur_record;
            fcbs[hx20FcbAddress].drive->file_read(fcbs[hx20FcbAddress].fcb,
                                                  record+extent*128, cur_extent, cur_record,
                                                  obuf+2);
            obuf[0] = cur_extent;
            obuf[1] = cur_record;
            setCurrentFilename(fcbs[hx20FcbAddress].drive_code,
                               fcbs[hx20FcbAddress].filename);
            obuf[0x82] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[0x82] = e.getBDOSError();
        }

        return conn->sendPacket(did, sid, fnc, 0x83, obuf);
    }
    case 0x15: { //file write at extent and record position
        /*
         * ibuf:
         * 0,1: hx-20 filehandle
         * 2: extent
         * 3: record (0-0x7f)
         * 4-133: sector data
         *
         * obuf:
         * 0: value of ex field of current directory entry
         * 1: record number, 0-0x80
         * 2: status
         */
        if(size != 0x84)
            return 0;
        uint16_t hx20FcbAddress = (ibuf[0] << 8) | ibuf[1];
        uint8_t extent = ibuf[2];
        uint8_t record = ibuf[3];
        printf("hx20 tries to write record %d(%d,%d) to fcb at 0x%04x.\n",
               record+extent*128,extent,record,hx20FcbAddress);


        uint8_t obuf[3] = {0};

        try {
            if(fcbs.find(hx20FcbAddress) == fcbs.end()) {
                //the TFDOS does not handle this case.
                throw BDOSError(BDOS_FILE_NOT_FOUND);//fcb unknown
            }
            triggerActivityStatus(fcbs[hx20FcbAddress].drive_code);

            uint8_t cur_extent;
            uint8_t cur_record;
            fcbs[hx20FcbAddress].drive->file_write
            (fcbs[hx20FcbAddress].fcb, ibuf+4, record+extent*128,
             cur_extent, cur_record);
            obuf[0] = cur_extent;
            obuf[1] = cur_record;
            obuf[2] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[2] = e.getBDOSError();
        }
        setCurrentFilename(fcbs[hx20FcbAddress].drive_code,
                           fcbs[hx20FcbAddress].filename);

        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x17: { //file rename
        /*
         * ibuf:
         * 0: drive and high 3 bits us field match for old file
         * 1-8: file name match of old file
         * 9-11: file type match of old file
         * 12: ex field match of old file(again high 3 bits)
         * 13-15: unused
         * 16: drive and high 3 bits us field match for old file
         * 17-24: file name match of old file
         * 25-27: file type match of old file
         * 28: ex field match of old file(again high 3 bits)
         * 29-31: unused
         *
         * obuf:
         * 0: status
         */
        if(size != 0x20)
            return 0;
        char filename_old[13];
        char filename_new[13];
        hx20ToUnixFilename(filename_old,ibuf+1);
        hx20ToUnixFilename(filename_new,ibuf+0x11);
        uint8_t drive_code = ibuf[0];
        uint8_t user = 0;
        uint8_t us_old = (ibuf[0] & 0xe0) | user;
        uint8_t extent_old = ibuf[0x0c];
        uint8_t us_new = ibuf[0x10];
        uint8_t extent_new = ibuf[0x1c];
        triggerActivityStatus(drive_code);
        printf("hx20 tries to rename file %s(extent %d) to %s(%d) on drive %d\n",
               filename_old,extent_old,filename_new,extent_new,drive_code);
        uint8_t obuf[1] = {0};
        try {
            if(!drive(drive_code).drive) {
                throw BDOSError(BDOS_READ_ERROR);
            }
            TF20DriveInterface *drive = this->drive(drive_code).drive.get();

            drive->file_rename(us_old, ibuf+1, extent_old,
                               us_new, ibuf+0x11, extent_new);
            obuf[0] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[0] = e.getBDOSError();
        }

        setCurrentFilename(drive_code, filename_new);

        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x1a: { //sets the BDOS buffer to memory address 0x80; no parameters
        /*
         * obuf:
         * 0: status
         */
        uint8_t obuf[0x1] = {0};
        obuf[0x0] = BDOS_OK;
        return conn->sendPacket(did, sid, fnc, 0x1, obuf);
    }
    case 0x1c: { //set write protect
        //this sets the current drive to be write protected until eiter
        //reset all(0xd) (or reset selective, if we find it)
        //no parameters.
        /*
         * obuf:
         * 0: status
         */
        uint8_t obuf[0x1] = {0};
        obuf[0x0] = BDOS_OK;
        return conn->sendPacket(did, sid, fnc, 0x1, obuf);
    }
    case 0x20: { //broken; set user num
        //tries to call BDOS function 64 instead of 32.
        //ibuf[0]: user number
        /*
         * ibuf:
         * 0: user number
         *
         * obuf:
         * 0: status
         */
        uint8_t obuf[0x1] = {0};
        obuf[0x0] = BDOS_OK;
        return conn->sendPacket(did, sid, fnc, 0x1, obuf);
    }
    case 0x21: { //read record from file
        /*
         * ibuf:
         * 0,1: hx-20 filehandle
         * 2-4: record number
         *
         * obuf:
         * 0: value of ex field of current directory entry
         * 1: record number, 0-0x80
         * 2-129: sector data
         * 130: status
         */
        if(size != 0x05)
            return 0;
        uint16_t hx20FcbAddress = (ibuf[0] << 8) | ibuf[1];
        uint32_t record = (ibuf[0x4] << 16) | (ibuf[0x3] << 8) | ibuf[0x2];
        printf("hx20 tries to read record %d to fcb at 0x%04x.\n",
               record,hx20FcbAddress);

        uint8_t obuf[0x83] = {0};

        try {
            if(fcbs.find(hx20FcbAddress) == fcbs.end()) {
                //the TFDOS does not handle this case.
                throw BDOSError(BDOS_FILE_NOT_FOUND);//fcb unknown
            }
            triggerActivityStatus(fcbs[hx20FcbAddress].drive_code);

            uint8_t cur_extent;
            uint8_t cur_record;

            fcbs[hx20FcbAddress].drive->file_read(fcbs[hx20FcbAddress].fcb,
                                                  record, cur_extent, cur_record,
                                                  obuf+2);
            obuf[0] = cur_extent;
            obuf[1] = cur_record;
            obuf[0x82] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[0x82] = e.getBDOSError();
        }
        setCurrentFilename(fcbs[hx20FcbAddress].drive_code,
                           fcbs[hx20FcbAddress].filename);

        return conn->sendPacket(did, sid, fnc, 0x83, obuf);
    }
    case 0x28: //write record to file, all zeros
    case 0x22: { //write record to file
        /* fnc 0x28 also takes a full cpm sector
         *
         * ibuf:
         * 0,1: hx-20 filehandle
         * 2-129: sector data
         * 130-132: record number
         *
         * obuf:
         * 0: value of ex field of current directory entry
         * 1: record number, 0-0x80
         * 2: status
         */
        if(size != 0x85)
            return 0;
        uint16_t hx20FcbAddress = (ibuf[0] << 8) | ibuf[1];
        uint32_t record = (ibuf[0x84] << 16) | (ibuf[0x83] << 8) | ibuf[0x82];
        printf("hx20 tries to write record %d to fcb at 0x%04x.\n",
               record,hx20FcbAddress);


        uint8_t obuf[3] = {0};

        try {
            if(fcbs.find(hx20FcbAddress) == fcbs.end()) {
                //the TFDOS does not handle this case.
                throw BDOSError(BDOS_FILE_NOT_FOUND);//fcb unknown
            }
            triggerActivityStatus(fcbs[hx20FcbAddress].drive_code);

            uint8_t cur_extent;
            uint8_t cur_record;
            if(fnc == 0x28) {
                //special zero write function
                uint8_t buf[128] = {0};
                fcbs[hx20FcbAddress].drive->file_write(fcbs[hx20FcbAddress].fcb,
                                                       buf, record, cur_extent, cur_record);
            } else {
                fcbs[hx20FcbAddress].drive->file_write(fcbs[hx20FcbAddress].fcb,
                                                       ibuf+2, record, cur_extent, cur_record);
            }
            obuf[0] = cur_extent;
            obuf[1] = cur_record;
            obuf[2] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[2] = e.getBDOSError();
        }

        setCurrentFilename(fcbs[hx20FcbAddress].drive_code,
                           fcbs[hx20FcbAddress].filename);

        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x23: { //file size calculation
        /*
         * ibuf:
         * 0,1: hx-20 filehandle
         *
         * obuf:
         * 0: contents of ex field
         * 1: contents of rc field
         * 2-4: record count
         * 5: status
         */
        if(size != 0x02)
            return 0;
        uint16_t hx20FcbAddress = (ibuf[0] << 8) | ibuf[1];//only usable as key into a fcb map, i guess
        printf("hx20 tries to calculate size of fcb at 0x%04x.\n",
               hx20FcbAddress);
        uint8_t obuf[6] = {0};
        try {
            if(fcbs.find(hx20FcbAddress) == fcbs.end()) {
                //the TFDOS does not handle this case.
                throw BDOSError(BDOS_FILE_NOT_FOUND);//fcb unknown
            }
            triggerActivityStatus(fcbs[hx20FcbAddress].drive_code);

            uint8_t cur_extent;
            uint8_t cur_record;
            uint32_t records;
            fcbs[hx20FcbAddress].drive->file_size(fcbs[hx20FcbAddress].fcb,
                                                  cur_extent, cur_record, records);

            setCurrentFilename(fcbs[hx20FcbAddress].drive_code,
                               fcbs[hx20FcbAddress].filename);

            obuf[0] = cur_extent;//extent number(unused?)
            obuf[1] = cur_record;//current record number(unused?)
            obuf[2] = records & 0xff;//R0 (low byte of record count)
            obuf[3] = (records & 0xff00) >> 8;//R1 (high byte of record count)
            obuf[4] = (records & 0xff0000) >> 16;//R2 (unused?)
            obuf[5] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[5] = e.getBDOSError();
        }

        return conn->sendPacket(did, sid, fnc, 6, obuf);
    }
    case 0x24: {//file tell
        /*
         * ibuf:
         * 0,1: hx-20 filehandle
         *
         * obuf:
         * 0-2: record count
         * 3: status
         */
        if(size != 0x02)
            return 0;
        uint16_t hx20FcbAddress = (ibuf[0] << 8) | ibuf[1];//only usable as key into a fcb map, i guess
        printf("hx20 tries to tell position of fcb at 0x%04x.\n",
               hx20FcbAddress);
        uint8_t obuf[4] = {0};
        try {
            if(fcbs.find(hx20FcbAddress) == fcbs.end()) {
                //the TFDOS does not handle this case.
                throw BDOSError(BDOS_FILE_NOT_FOUND);//fcb unknown
            }
            triggerActivityStatus(fcbs[hx20FcbAddress].drive_code);

            uint32_t records;
            fcbs[hx20FcbAddress].drive->file_tell(fcbs[hx20FcbAddress].fcb, records);

            obuf[0] = records & 0xff;//R0 (low byte of record count)
            obuf[1] = (records & 0xff00) >> 8;//R1 (high byte of record count)
            obuf[2] = (records & 0xff0000) >> 16;//R2 (unused?)
            obuf[3] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[3] = e.getBDOSError();
        }

        setCurrentFilename(fcbs[hx20FcbAddress].drive_code,
                           fcbs[hx20FcbAddress].filename);

        return conn->sendPacket(did, sid, fnc, 4, obuf);
    }
    case 0x27: { //clear all file handles
        /*
         * obuf:
         * 0: status
         */
        for(auto &fi : fcbs) {
            fi.second.drive->file_close(fi.second.fcb);
        }
        fcbs.clear();
        uint8_t obuf[0x1] = {0};
        obuf[0x0] = BDOS_OK;
        return conn->sendPacket(did, sid, fnc, 0x1, obuf);
    }
    case 0x79: {//flush HXBIOS sector cache
        /*
         * obuf:
         * 0: status
         */
        uint8_t obuf[0x1] = {0};
        obuf[0x0] = BDOS_OK;
        return conn->sendPacket(did, sid, fnc, 0x1, obuf);
    }
    case 0x7a: { // disk all copy
        /*
         * ibuf:
         * 0: drive code of source
         *
         * obuf:
         * 0,1: currently copied track, or 0xffff
         * 2: status
         */
        if(size != 0x1)
            return 0;
        uint8_t drive_code = ibuf[0];
        printf("hx20 tries to copy disk in drive %d\n",
               drive_code);
        uint8_t obuf[0x3] = {0};
        obuf[0x0] = 0xff;//msb of currently formatted track number
        obuf[0x1] = 0xff;//lsb of currently formatted track number
        triggerActivityStatus(1);
        triggerActivityStatus(2);

        if(drive_code == 0)//seems to assume code 0 is also 1 => 2 copy
            drive_code = 1;
        try {
            if(!drive(1).drive || !drive(2).drive) {
                throw BDOSError(BDOS_READ_ERROR);
            }
            TF20DriveInterface *drive_src = drive(drive_code).drive.get();
            TF20DriveInterface *drive_dst = drive(3-drive_code).drive.get();

            for(uint8_t track = 0; track < 40; track++) {
                for(uint8_t sector = 0; sector < 16*2*2; sector++) {
                    uint8_t track_buf[128];
                    drive_src->disk_read(track, sector, track_buf);
                    drive_dst->disk_write(track, sector, track_buf);
                    if(sector == 0) {
                        obuf[0x0] = 0;
                        obuf[0x1] = track;
                        obuf[0x2] = BDOS_OK;
                        int res = conn->sendPacket(did, sid, fnc, 3, obuf);
                        if(res != 0)
                            return res;
                    }
                }
            }

            obuf[0x0] = 0xff;//msb of currently formatted track number
            obuf[0x1] = 0xff;//lsb of currently formatted track number
            obuf[0x2] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[0x0] = 0xff;//msb of currently formatted track number
            obuf[0x1] = 0xff;//lsb of currently formatted track number
            obuf[0x2] = e.getBDOSError();
        }
        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x78: //direct write going through HXBIOS caches
    case 0x7b: { //direct write going through BDOS caches(that exclude HXBIOS caches)
        /*
         * ibuf:
         * 0: drive code
         * 1: track (0-39)
         * 2: cpm sector (1-64)
         * 3-130: sector data
         *
         * obuf:
         * 0: status
         */
        if(size != 0x83)
            return 0;
        uint8_t drive_code = ibuf[0];
        uint8_t track = ibuf[1];
        uint8_t sector = ibuf[2];//128 byte sectors
        printf("hx20 tries do direct read from drive %d, track %d, sector %d\n",
               drive_code,track,sector);
        uint8_t obuf[0x1] = {0};
        triggerActivityStatus(drive_code);

        try {
            if(!drive(drive_code).drive)
                throw BDOSError(BDOS_READ_ERROR);
            TF20DriveInterface *drive = this->drive(drive_code).drive.get();

            drive->disk_write(track, sector, ibuf+3);
            obuf[0x0] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[0x0] = e.getBDOSError();
        }
        return conn->sendPacket(did, sid, fnc, 0x1, obuf);
    }
    case 0x7c: { // disk formatting
        /*
         * ibuf:
         * 0: drive code
         *
         * obuf:
         * 0,1: currently formatted track, or 0xffff
         * 2: status
         */
        if(size != 0x1)
            return 0;
        uint8_t drive_code = ibuf[0];
        printf("hx20 tries to format disk in drive %d\n",
               drive_code);
        uint8_t obuf[0x3] = {0};
        triggerActivityStatus(drive_code);

        try {
            if(!drive(drive_code).drive) {
                throw BDOSError(BDOS_READ_ERROR);
            }
            TF20DriveInterface *drive = this->drive(drive_code).drive.get();

            for(uint8_t track = 0; track != 39; track++) {
                printf("format track %d\n", track);
                obuf[0x0] = 0;
                obuf[0x1] = track;
                obuf[0x2] = 0;
                int res = conn->sendPacket(did, sid, fnc, 3, obuf);
                if(res != 0)
                    return res;
                drive->disk_format(track);
            }

            obuf[0x0] = 0xff;//msb of currently formatted track number
            obuf[0x1] = 0xff;//lsb of currently formatted track number
            obuf[0x2] = 0;
        } catch(BDOSError const &e) {
            obuf[0x0] = 0xff;//msb of currently formatted track number
            obuf[0x1] = 0xff;//lsb of currently formatted track number
            obuf[0x2] = e.getBDOSError();
        }
        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x7d: { // new system generation
        /*
         * ibuf:
         * 0: ignored
         *
         * obuf:
         * 0,1: 0xffff
         * 2: status
         */
        if(size != 0x1)
            return 0;
        printf("hx20 tries to create a system disk in 2nd drive from the system in 1st drive\n");
        uint8_t obuf[0x3] = {0};
        triggerActivityStatus(1);
        triggerActivityStatus(2);

        try {
            if(!drive(1).drive || !drive(2).drive) {
                throw BDOSError(BDOS_READ_ERROR);
            }
            TF20DriveInterface *drive_src = drive(1).drive.get();
            TF20DriveInterface *drive_dst = drive(2).drive.get();

            //directly copy the first four tracks
            printf("Copying boot tracks\n");
            for(uint8_t track = 0; track < 4; track++) {
                for(uint8_t sector = 0; sector < 16*2*2; sector++) {
                    uint8_t track_buf[128];
                    drive_src->disk_read(track, sector, track_buf);
                    drive_dst->disk_write(track, sector, track_buf);
                    if(sector == 0) {
                        obuf[0x0] = 0;
                        obuf[0x1] = 0;
                        obuf[0x2] = 0;
                        int res = conn->sendPacket(did, sid, fnc, 3, obuf);
                        if(res != 0)
                            return res;
                    }
                }
            }

            printf("Copying system files\n");
            char unix_pattern[13] = "????????.SYS";
            uint8_t pattern[11];
            uint8_t dir_entry[32];
            std::string filename;
            unixToHx20Filename(pattern, unix_pattern);
            uint8_t search_res;
            try {
                drive_src->file_find_first(0, pattern, 0, dir_entry, filename);
                search_res = BDOS_OK;
            } catch(BDOSError const &e) {
                search_res = e.getBDOSError();
            }
            while(search_res == BDOS_OK) {
                char fn[14] = {0};
                hx20ToUnixFilename(fn, dir_entry+1);
                printf("%s...\n", fn);
                void *fcb_src = drive_src->file_open(0, dir_entry+1, 0);
                if(!fcb_src) {
                    throw BDOSError(BDOS_FILE_NOT_FOUND);
                }
                FileCloser srccloser(drive_src, fcb_src);
                drive_dst->file_remove(0, dir_entry+1, 0);
                void *fcb_dst;
                fcb_dst = drive_dst->file_create(0, dir_entry+1, 0);
                if(!fcb_dst) {
                    throw BDOSError(BDOS_WRITE_ERROR);
                }
                FileCloser dstcloser(drive_src, fcb_src);
                uint8_t extent;
                uint8_t record;
                uint32_t records;
                drive_src->file_size(fcb_src, extent, record, records);
                for(uint32_t r = 0; r < records; r++) {
                    uint8_t file_buf[128];
                    drive_src->file_read(fcb_src, r, extent, record, file_buf);
                    drive_dst->file_write(fcb_dst, file_buf, r, extent, record);
                    obuf[2] = BDOS_OK;
                }

                try {
                    drive_src->file_find_next(dir_entry, filename);
                    search_res = BDOS_OK;
                } catch(BDOSError const &e) {
                    search_res = e.getBDOSError();
                }
            }
            printf("Done\n");

            obuf[0x0] = 0xff;//
            obuf[0x1] = 0xff;//done, 0x0000 => not done
            obuf[0x2] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[0x0] = 0xff;//msb of currently formatted track number
            obuf[0x1] = 0xff;//lsb of currently formatted track number
            obuf[0x2] = e.getBDOSError();
        }
        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x7e: { //disk free size calculation
        /*
         * ibuf:
         * 0: drive code
         *
         * obuf:
         * 0: number of free clusters
         * 1: status
         */
        if(size != 0x01)
            return 0;
        uint8_t drive_code = ibuf[0];
        printf("hx20 tries to calculate free space of drive %d\n",
               drive_code);
        uint8_t obuf[2] = {0};
        triggerActivityStatus(drive_code);

        try {
            if(!drive(drive_code).drive) {
                throw BDOSError(BDOS_READ_ERROR);
            }
            TF20DriveInterface *drive = this->drive(drive_code).drive.get();

            uint8_t clusters;
            drive->disk_size(clusters);//bdos error or 0
            obuf[0] = clusters;//number of free 2k-sectors
            obuf[1] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[1] = e.getBDOSError();
        }
        return conn->sendPacket(did, sid, fnc, 2, obuf);
    }
    case 0x77: //direct read going through HXBIOS caches
    case 0x7f: { //direct read going through BDOS caches(that exclude HXBIOS caches)
        /*
         * ibuf:
         * 0: drive code
         * 1: track (0-39)
         * 2: cpm sector (1-64)
         *
         * obuf:
         * 0-127: sector data
         * 128: status
         */
        if(size != 0x03)
            return 0;
        uint8_t drive_code = ibuf[0];
        uint8_t track = ibuf[1];
        uint8_t sector = ibuf[2];//128 byte sectors
        printf("hx20 tries do direct read from drive %d, track %d, sector %d\n",
               drive_code,track,sector);
        uint8_t obuf[0x81] = {0};
        triggerActivityStatus(drive_code);

        try {
            if(!drive(drive_code).drive) {
                throw BDOSError(BDOS_READ_ERROR);
            }

            TF20DriveInterface *drive = this->drive(drive_code).drive.get();

            drive->disk_read(track, sector, obuf);
            obuf[0x80] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[0x80] = e.getBDOSError();
        }
        return conn->sendPacket(did, sid, fnc, 0x81, obuf);
    }
    case 0x80: { //disk boot
        /*
         * ibuf:
         * 0: application code, inserted into BOOT<hex>.SYS
         *
         * obuf:
         * 0: status
         * 1-255: data
         */
        if(size != 1)
            return 0;
        char unixfilename[13];
        snprintf(unixfilename,13,"BOOT%02X.SYS",ibuf[0]);
        drive(1).last_file->setText(unixfilename);
        printf("hx20 requested %s\n",unixfilename);
        triggerActivityStatus(1);
        uint8_t filename[11];
        unixToHx20Filename(filename, unixfilename);

        uint8_t obuf[256+1] = {0};//need one more byte space so the second record fits in full

        try {
            if(!drive(1).drive) {
                throw BDOSError(BDOS_READ_ERROR);
            }
            TF20DriveInterface *drive = this->drive(1).drive.get();

            void *fcb = drive->file_open(0, filename, 0);
            if(!fcb) {
                throw BDOSError(BDOS_FILE_NOT_FOUND);
            }
            FileCloser fcbcloser(drive, fcb);
            uint8_t cur_extent;
            uint8_t cur_record;
            drive->file_read(fcb, 0, cur_extent, cur_record, obuf+1);
            drive->file_read(fcb, 1, cur_extent, cur_record, obuf+1+128);
            obuf[0] = BDOS_OK;
        } catch(BDOSError const &e) {
            obuf[0] = e.getBDOSError();
        }
        return conn->sendPacket(did, sid, fnc, 256, obuf);
    }
    case 0x81: { //load open
        /* ibuf:
         * 0-10: filename
         * 11: reloc type
         * 12,13: reloc address
         *
         * obuf:
         * 0: status
         * 1,2: code size
         *
         * general format of firmware file:
         * 256 bytes header: {
         *  0:     unused
         *  1-2:   code size, little endian
         *  3-255: unused
         * }
         * code
         * relocation data: bit field determining which code bytes need
         *                          relocation, msb first
         */

        std::string filename = hx20ToUnixFilename(ibuf);
        drive(1).last_file->setText(QString::fromStdString(filename));
        printf("hx20 requested %s\n",
               filename.c_str());
        uint8_t reloc_type = ibuf[11];
        printf("relocation: %s\n",
               reloc_type==0?"none":
               reloc_type==1?"from starting address":
               reloc_type==2?"from ending address":
               "unknown");
        uint16_t reloc_address = ibuf[13] | (ibuf[12] << 8);
        printf("address: 0x%04x\n",
               reloc_address
              );

        uint8_t obuf[3] = {0};

        try {
            if(!drive(1).drive) {
                throw BDOSError(BDOS_READ_ERROR);
            }
            TF20DriveInterface *drive = this->drive(1).drive.get();
            triggerActivityStatus(1);

            void *fcb = drive->file_open(0, ibuf, 0);
            if(!fcb) {
                throw BDOSError(BDOS_FILE_NOT_FOUND);
            }
            FileCloser fcbcloser(drive, fcb);

            uint8_t extent;
            uint8_t record;
            uint32_t records;
            drive->file_size(fcb, extent, record, records);
            load_buffer.resize(records * 128);
            printf("drive reported %d records\n", records);
            for(uint32_t r = 0; r < records; r++) {
                drive->file_read(fcb, r, extent, record, load_buffer.data() + 128*r);
            }

            std::ifstream f((std::string("disk/")+filename).c_str());

            unsigned int code_size = (uint8_t)load_buffer[1] |
                                     ((uint8_t)load_buffer[2] << 8);

            if(reloc_type != 0) {
                //now we need to relocate
                uint8_t reloc_offset;
                if(reloc_type == 2)
                    reloc_offset =
                    (reloc_address - code_size) >> 8;
                else
                    reloc_offset =
                    reloc_address >> 8;
                reloc_offset -= 0x60;
                printf("relocation offset = 0x%02x\n",
                       reloc_offset);

                uint8_t *bp = load_buffer.data()+0x100;
                uint8_t *mp = bp+code_size;
                printf("buffer: %p, bp: %p, mp: %p, code size: 0x%x\n",
                       load_buffer.data(),bp,mp,code_size);
                uint8_t t = 0x80;
                for(;
                        bp < load_buffer.data()+0x100+code_size;
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

            obuf[0] = BDOS_OK;
            obuf[1] = code_size >> 8;
            obuf[2] = code_size;
            hexdump((char *)obuf,3);
        } catch(BDOSError const &e) {
            obuf[0] = e.getBDOSError();
        }
        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x83: { //read one block(goes with 0x81: load open)
        /*
         * ibuf:
         * 0-1: record number
         *
         * obuf:
         * 0-1: next record number
         * 2-129: data
         * 130: status
         */
        unsigned int record = (ibuf[0] << 8) | ibuf[1];
        printf("hx20 requested record 0x%02x\n",record);
        uint8_t obuf[2+128+1] = {0};
        triggerActivityStatus(1);
        try {
            if(load_buffer.empty()) {
                obuf[1] = ibuf[1]+1;
                obuf[0] = ibuf[0]+((ibuf[1] == 0xff)?1:0);
                throw BDOSError(BDOS_FILE_NOT_FOUND);
            }

            unsigned int code_size = (unsigned char)load_buffer[1] |
                                     ((unsigned char)load_buffer[2] << 8);

            obuf[1] = ibuf[1]+1;
            obuf[0] = ibuf[0]+((ibuf[1] == 0xff)?1:0);
            if(load_buffer.size() >= 0x100 + record*128 + 128) {
                memcpy(obuf+2,load_buffer.data() + 0x100 + record*128,128);
                if(record*128 > code_size)
                    throw BDOSError(BDOS_FILE_NOT_FOUND);
                obuf[2+128] = BDOS_OK;
            } else {
                throw BDOSError(BDOS_FILE_NOT_FOUND);
            }
        } catch(BDOSError const &e) {
            obuf[2+128] = e.getBDOSError();
        }
        hexdump((char *)obuf,128+3);
        return conn->sendPacket(did, sid, fnc, 2+128+1, obuf);
    }
    default: {
        printf("HX20DiskDevice: got packet: sid = 0x%04x, did = 0x%04x, "
               "fnc = 0x%02x, size = 0x%04x\n",
               sid,did,fnc,size);
        hexdump((char *)ibuf,size);
        //TFDOS returns 1 byte/BDOS_OK for anything it does not know
        uint8_t obuf[0x1] = {0};
        obuf[0x0] = BDOS_OK;
        return conn->sendPacket(did, sid, fnc, 0x1, obuf);
    }
    }
}

HX20DiskDevice::DriveInfo::DriveInfo(uint8_t drive_code, QLabel *status_icon, QLineEdit *last_file, std::unique_ptr<QTimer> &&status_timer) : drive_code(drive_code), status_icon(status_icon), last_file(last_file), status_timer(std::move(status_timer)), dock(nullptr) {
}

HX20DiskDevice::DriveInfo::~DriveInfo() =default;

HX20DiskDevice::HX20DiskDevice(int ddno)
    : ddno(ddno),
      drive_1(1, new QLabel(), new QLineEdit(), std::make_unique<QTimer>()),
      drive_2(2, new QLabel(), new QLineEdit(), std::make_unique<QTimer>()),
      settingsConfig(nullptr), settingsPresets(nullptr) {
    activeIcon.addFile(":/16/disk_active.png", QSize(16,16));
    activeIcon.addFile(":/32/disk_active.png", QSize(32,32));
    inactiveIcon.addFile(":/16/disk_inactive.png", QSize(16,16));
    inactiveIcon.addFile(":/32/disk_inactive.png", QSize(16,16));
    for(int i = 1; i <= 2; i++) {
        drive(i).status_icon->setPixmap(inactiveIcon.pixmap(
                                        drive(i).status_icon->style()->pixelMetric(QStyle::PM_ButtonIconSize)));
        drive(i).status_timer->setSingleShot(true);
        QObject::connect(drive(i).status_timer.get(), &QTimer::timeout,
        drive(i).status_icon, [this,i]() {
            drive(i).status_icon->setPixmap(inactiveIcon.pixmap(
                                            drive(i).status_icon->style()->pixelMetric(QStyle::PM_ButtonIconSize)));
        });
        drive(i).last_file->setReadOnly(true);
    }
}

HX20DiskDevice::~HX20DiskDevice() {
    for(auto &fi : fcbs) {
        fi.second.drive->file_close(fi.second.fcb);
    }
}

void HX20DiskDevice::installNewDrive(int drive_code, std::unique_ptr<TF20DriveInterface> &&new_drive,
                                     QString const &title) {
    TF20DriveInterface *drive = this->drive(drive_code).drive.get();

    for(auto it = fcbs.begin(); it != fcbs.end();) {
        if(it->second.drive == drive) {
            it->second.drive->file_close(it->second.fcb);
            it = fcbs.erase(it);
        } else {
            it++;
        }
    }
    this->drive(drive_code).drive = std::move(new_drive);
    this->drive(drive_code).title = title;
    if(this->drive(drive_code).dock)
        this->drive(drive_code).dock->setWindowTitle(QString("Disk %1 %2").
                arg(static_cast<char>(ddno*2+drive_code-1+'A')).
                arg(this->drive(drive_code).title));
}

void HX20DiskDevice::setDiskDirectory(int drive_code, std::string const &dir) {
    installNewDrive(drive_code, std::make_unique<TF20DriveDirectory>(dir),
                    tr("Directory %1").arg(QString::fromStdString(dir)));
    QString tgtval = QString("dir://%1").arg(QString::fromStdString(dir));
    if(settingsConfig->value(QString("disk_%1").arg(drive_code)) != tgtval) {
        settingsConfig->setValue
        (QString("disk_%1").arg(drive_code), tgtval);
    }
}

void HX20DiskDevice::setDiskFile(int drive_code, std::string const &file) {
    setDiskFile(drive_code, file, TF20DriveDiskImageFileType::Autodetect);
}

void HX20DiskDevice::setDiskFile(int drive_code, std::string const &file,
                                 TF20DriveDiskImageFileType filetype) {
    installNewDrive(drive_code,
                    std::make_unique<TF20DriveDiskImage>(file, filetype),
                    tr("Image %1").arg(QString::fromStdString(file)));
    QString tgtval = QString("file://%2").arg(QString::fromStdString(file));
    QString proto;
    switch(filetype) {
    case TF20DriveDiskImageFileType::Raw:
        tgtval = "raw" + tgtval;
        break;
    case TF20DriveDiskImageFileType::TeleDisk:
        tgtval = "teledisk" + tgtval;
        break;
    default:
        break;
    }
    if(settingsConfig->value(QString("disk_%1").arg(drive_code)) != tgtval) {
        settingsConfig->setValue
        (QString("disk_%1").arg(drive_code), tgtval);
    }
}

void HX20DiskDevice::ejectDisk(int drive_code) {
    installNewDrive(drive_code, std::unique_ptr<TF20DriveDiskImage>(),
                    tr("Empty"));
    QString tgtval = QString("empty://");
    if(settingsConfig->value(QString("disk_%1").arg(drive_code)) != tgtval) {
        settingsConfig->setValue
        (QString("disk_%1").arg(drive_code), tgtval);
    }
}

void HX20DiskDevice::addDocksToMainWindow(QMainWindow *window,
        QMenu *devices_menu) {
    for(int i = 1; i <= 2; i++) {
        drive(i).dock = new QDockWidget(window);
        drive(i).dock->setObjectName(QString("diskdock_%1_%2").arg(ddno).arg(i));
        drive(i).dock->setWindowTitle(tr("Disk %1 %2").
                                      arg(static_cast<char>(ddno*2+i-1+'A')).
                                      arg(drive(i).title));
        QWidget *w = new QWidget(drive(i).dock);
        drive(i).dock->setWidget(w);
        QHBoxLayout *l = new QHBoxLayout();
        w->setLayout(l);
        l->addWidget(drive(i).status_icon);
        l->addWidget(drive(i).last_file);

        DockWidgetTitleBar *title_bar = new DockWidgetTitleBar();
        QMenu *mnu = new QMenu(title_bar->configureButton());
        mnu->addAction(tr("Set disk &directory..."),
        [this,i]() {
            QString result = QFileDialog::getExistingDirectory(nullptr,
                             tr("Set disk directory"));
            if(!result.isEmpty()) {
                try {
                    this->setDiskDirectory(i, result.toStdString());
                } catch(std::exception &e) {
                    QMessageBox::critical(nullptr,
                                          tr("Failed to open"),
                                          tr("Failed to open %1 as a disk directory:\n%2").arg(result).arg(e.what()));
                }
            }
        });
        mnu->addAction(tr("Set disk &file..."),
        [this,i]() {
            QString selectedFilter;
            QString result = QFileDialog::getSaveFileName(nullptr,
                             tr("Set disk file"),
                             QString(),
                             tr("Teledisk (*.td0);;Raw image (*.img);;All files (*.*)"),
                             &selectedFilter,
                             QFileDialog::DontConfirmOverwrite);
            if(!result.isEmpty()) {
                TF20DriveDiskImageFileType ft = TF20DriveDiskImageFileType::Autodetect;
                if(selectedFilter == "Teledisk (*.td0)") {
                    ft = TF20DriveDiskImageFileType::TeleDisk;
                } else if(selectedFilter == "Raw image (*.img)") {
                    ft = TF20DriveDiskImageFileType::Raw;
                } else if(selectedFilter == "All files (*.*)") {
                    ft = TF20DriveDiskImageFileType::Autodetect;
                }
                try {
                    this->setDiskFile(i, result.toStdString(), ft);
                } catch(std::exception &e) {
                    QMessageBox::critical(nullptr,
                                          tr("Failed to open"),
                                          tr("Failed to open %1 as a disk file:\n%2").arg(result).arg(e.what()));
                }
            }
        });
        mnu->addAction(tr("Eject disk"),
        [this,i]() {
            this->ejectDisk(i);
        });
        title_bar->configureButton()->setMenu(mnu);
        title_bar->configureButton()->setPopupMode(QToolButton::ToolButtonPopupMode::InstantPopup);
        drive(i).dock->setTitleBarWidget(title_bar);

        window->addDockWidget(Qt::LeftDockWidgetArea, drive(i).dock);

        connect(devices_menu->addAction(
                tr("Disk %1").
                arg(static_cast<char>(ddno*2+i-1+'A'))),
                &QAction::triggered,
        this,[dock=drive(i).dock]() {
            dock->show();
        });
    }
}

void HX20DiskDevice::setSettings(Settings::Group *settingsConfig,
                                 Settings::Group *settingsPresets) {
    if(this->settingsConfig == settingsConfig &&
            this->settingsPresets == settingsPresets)
        return;
    if(this->settingsConfig) {
        disconnect(this->settingsConfig, &Settings::Group::changedChildKey,
                   this, &HX20DiskDevice::updateFromConfig);
    }
    if(this->settingsPresets) {
        disconnect(this->settingsPresets, &Settings::Group::changedChildKey,
                   this, &HX20DiskDevice::updateFromConfig);
    }
    this->settingsConfig = settingsConfig;
    this->settingsPresets = settingsPresets;


    if(this->settingsConfig) {
        connect(this->settingsConfig, &Settings::Group::changedChildKey,
                this, &HX20DiskDevice::updateFromConfig);
    }
    if(this->settingsPresets) {
        connect(this->settingsPresets, &Settings::Group::changedChildKey,
                this, &HX20DiskDevice::updateFromConfig);
    }
    updateFromConfig();
}

void HX20DiskDevice::setDiskUrl(int drive_code, QString const &url) {
    if(url.startsWith("empty://")) {
        ejectDisk(drive_code);
    } else if(url.startsWith("dir://")) {
        setDiskDirectory(drive_code, url.mid(6).toStdString());
    } else if(url.startsWith("file://")) {
        setDiskFile(drive_code, url.mid(7).toStdString(),
                    TF20DriveDiskImageFileType::Autodetect);
    } else if(url.startsWith("rawfile://")) {
        setDiskFile(drive_code, url.mid(10).toStdString(),
                    TF20DriveDiskImageFileType::Raw);
    } else if(url.startsWith("telediskfile://")) {
        setDiskFile(drive_code, url.mid(15).toStdString(),
                    TF20DriveDiskImageFileType::TeleDisk);
    }
}

void HX20DiskDevice::updateFromConfig() {
    if(!settingsConfig)
        return;
    for(int drive_code = 1; drive_code <= 2; drive_code++) {
        QString d1 = settingsConfig->value
                     (QString("disk_%1").arg(drive_code),
                      "empty://",true).toString();
        setDiskUrl(drive_code, d1);
    }
}
