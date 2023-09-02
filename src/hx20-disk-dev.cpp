
/* protocol documentation in tms_10-11.pdf
   DIP4 on the HX-20 must be on for basic to look for the TF-20

   TODO find where function 0x13: file remove is documented
 */

#include <stdlib.h>
#include <string.h>
#include <fstream>
#include "hx20-disk-dev.hpp"
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
#include "dockwidgettitlebar.hpp"
#include <QMenu>
#include <QFileDialog>
#include <QMessageBox>

/* TODO
 * * Make a separate interface for the actual "disk"-io so it can be swapped
 *   between directory-on-disk and file-with-disk-image
 * * Make the used data source configurable(file or directory picker)
 * * General: Add a way to add/remove disk drives
 * * General: Add a way to store configurations of all the things
 * * General: Add a inspection tool for the communication that then also
 * *          decodes the information and also our answers.
 */

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

static void hx20ToUnixFilename(char *dst,uint8_t const *src) {
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

HX20DiskDevice::DriveInfo &HX20DiskDevice::drive(uint8_t drive_code) {
    if(drive_code == 1)
        return drive_1;
    else if(drive_code == 2)
        return drive_2;
    else
        throw std::runtime_error("Drive code invalid");
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

int HX20DiskDevice::gotPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                               uint16_t size, uint8_t *buf,
                               HX20SerialConnection *conn) {
    switch(fnc) {
    case 0x0e: { //reset
        triggerActivityStatus(1);
        triggerActivityStatus(2);
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
        triggerActivityStatus(drive_code);
        printf("hx20 tries to open %s(extent %d) on drive %d for fcb at 0x%04x.\n",
               filename,extent,drive_code,hx20FcbAddress);
        uint8_t obuf[1];
        if(drive_code < 1 || drive_code > 2) {
            obuf[0] = BDOS_SELECT_ERROR;
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }

        if(!drive(drive_code).drive) {
            obuf[0] = BDOS_READ_ERROR;
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        TF20DriveInterface *drive = this->drive(drive_code).drive.get();
        void *fcb;
        if(fnc == 0x16) {
            fcb = drive->file_create(buf+3, extent);
        } else {
            fcb = drive->file_open(buf+3, extent);
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
        if(fcbs.find(hx20FcbAddress) == fcbs.end()) {
            obuf[0] = BDOS_FILE_NOT_FOUND;//fcb unknown
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        triggerActivityStatus(fcbs[hx20FcbAddress].drive_code);
        setCurrentFilename(fcbs[hx20FcbAddress].drive_code, fcbs[hx20FcbAddress].filename);
        fcbs[hx20FcbAddress].drive->file_close(fcbs[hx20FcbAddress].fcb);
        fcbs.erase(hx20FcbAddress);
        obuf[0] = 0x00;//file closed
        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x11: { //findfirst
        if(size != 0x0d)
            return 0;
        char pattern[13];
        hx20ToUnixFilename(pattern,buf+1);
        uint8_t drive_code = buf[0];
        uint8_t extent = buf[1+8+3];
        printf("hx20 requested first file matching %s\n",
               pattern);
        printf("extent number %d\n",extent);
        printf("from drive %d\n",drive_code);

        triggerActivityStatus(drive_code);

        uint8_t obuf[0x21];
        if(drive_code < 1 || drive_code > 2) {
            obuf[0] = BDOS_SELECT_ERROR;
            return conn->sendPacket(did, sid, fnc, 33, obuf);
        }

        if(!drive(drive_code).drive) {
            obuf[0] = BDOS_READ_ERROR;
            return conn->sendPacket(did, sid, fnc, 33, obuf);
        }
        TF20DriveInterface *drive = this->drive(drive_code).drive.get();

        std::string filename;
        obuf[0] = drive->file_find_first(buf+1, extent, obuf+1, filename);
        dirSearch.drive = drive;
        dirSearch.drive_code = drive_code;
        if(obuf[0] == 0) {
            setCurrentFilename(drive_code, filename);
        }

        return conn->sendPacket(did, sid, fnc, 33, obuf);
    }
    case 0x12: { //find next
        if(size != 0x01)
            return 0;
        //would the single byte be the driveCode? protocol says to ignore.
        printf("hx20 requested next file\n");

        uint8_t obuf[0x21];

        std::string filename;
        obuf[0] = dirSearch.drive->file_find_next(obuf+1, filename);

        if(obuf[0] == 0) {
            setCurrentFilename(dirSearch.drive_code, filename);
        }

        triggerActivityStatus(dirSearch.drive_code);

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
        triggerActivityStatus(drive_code);

        uint8_t obuf[0x1];

        if(!drive(drive_code).drive) {
            obuf[0] = BDOS_READ_ERROR;
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        TF20DriveInterface *drive = this->drive(drive_code).drive.get();

        obuf[0] = drive->file_remove(buf+1, extent);

        setCurrentFilename(dirSearch.drive_code, filename);

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
        triggerActivityStatus(drive_code);
        printf("hx20 tries to rename file %s(extent %d) to %s(%d) on drive %d\n",
               filename_old,extent_old,filename_new,extent_new,drive_code);
        uint8_t obuf[1];
        if(drive_code < 1 || drive_code > 2) {
            obuf[0] = BDOS_SELECT_ERROR;
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        if(!drive(drive_code).drive) {
            obuf[0] = BDOS_READ_ERROR;
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        TF20DriveInterface *drive = this->drive(drive_code).drive.get();

        obuf[0] = drive->file_rename(buf+1, extent_old, buf+0x11, extent_new);

        setCurrentFilename(drive_code, filename_new);

        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x21: { //read record from file
        if(size != 0x05)
            return 0;
        uint16_t hx20FcbAddress = (buf[0] << 8) | buf[1];
        uint32_t record = (buf[0x4] << 16) | (buf[0x3] << 8) | buf[0x2];
        printf("hx20 tries to read record %d to fcb at 0x%04x.\n",
               record,hx20FcbAddress);

        uint8_t obuf[0x83];

        if(fcbs.find(hx20FcbAddress) == fcbs.end()) {
            obuf[2] = BDOS_FILE_NOT_FOUND;//fcb unknown
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }
        triggerActivityStatus(fcbs[hx20FcbAddress].drive_code);

        uint8_t cur_extent;
        uint16_t cur_record;
        obuf[0x82] = fcbs[hx20FcbAddress].drive->file_read(fcbs[hx20FcbAddress].fcb,
                                                           record, cur_extent, cur_record,
                                                           obuf+2);
        obuf[0] = (cur_extent << 1) | (cur_record >> 8);
        obuf[1] = cur_record;
        setCurrentFilename(fcbs[hx20FcbAddress].drive_code,
                           fcbs[hx20FcbAddress].filename);

        return conn->sendPacket(did, sid, fnc, 0x83, obuf);
    }
    case 0x22: { //write record to file
        if(size != 0x85)
            return 0;
        uint16_t hx20FcbAddress = (buf[0] << 8) | buf[1];
        uint32_t record = (buf[0x84] << 16) | (buf[0x83] << 8) | buf[0x82];
        printf("hx20 tries to write record %d to fcb at 0x%04x.\n",
               record,hx20FcbAddress);


        uint8_t obuf[3];

        if(fcbs.find(hx20FcbAddress) == fcbs.end()) {
            obuf[2] = BDOS_FILE_NOT_FOUND;//fcb unknown
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }
        triggerActivityStatus(fcbs[hx20FcbAddress].drive_code);

        uint8_t cur_extent;
        uint16_t cur_record;
        obuf[2] = fcbs[hx20FcbAddress].drive->file_write(fcbs[hx20FcbAddress].fcb,
                                                         buf+2, record, cur_extent, cur_record);
        obuf[0] = (cur_extent << 1) | (cur_record >> 8);
        obuf[1] = cur_record;
        setCurrentFilename(fcbs[hx20FcbAddress].drive_code,
                           fcbs[hx20FcbAddress].filename);

        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x23: { //file size calculation
        if(size != 0x02)
            return 0;
        uint16_t hx20FcbAddress = (buf[0] << 8) | buf[1];//only usable as key into a fcb map, i guess
        printf("hx20 tries to calculate size of fcb at 0x%04x.\n",
               hx20FcbAddress);
        uint8_t obuf[6];
        if(fcbs.find(hx20FcbAddress) == fcbs.end()) {
            obuf[5] = BDOS_FILE_NOT_FOUND;//fcb unknown
            return conn->sendPacket(did, sid, fnc, 6, obuf);
        }
        triggerActivityStatus(fcbs[hx20FcbAddress].drive_code);

        uint8_t cur_extent;
        uint16_t cur_record;
        uint32_t records;
        obuf[5] = fcbs[hx20FcbAddress].drive->file_size(fcbs[hx20FcbAddress].fcb,
                                                         cur_extent, cur_record, records);

        setCurrentFilename(fcbs[hx20FcbAddress].drive_code,
                           fcbs[hx20FcbAddress].filename);

        obuf[0] = (cur_extent << 1) | ((cur_record > 0x80)?1:0);//extent number(unused?)
        obuf[1] = (cur_record & 0xff) - ((cur_record > 0x80)?0x80:0);//current record number(unused?)
        obuf[2] = records & 0xff;//R0 (low byte of record count)
        obuf[3] = (records & 0xff00) >> 8;//R1 (high byte of record count)
        obuf[4] = (records & 0xff0000) >> 16;//R2 (unused?)
        return conn->sendPacket(did, sid, fnc, 6, obuf);
    }
    case 0x7a: { // disk all copy
        if(size != 0x1)
            return 0;
        uint8_t drive_code = buf[0];
        printf("hx20 tries to copy disk in drive %d\n",
               drive_code);
        uint8_t obuf[0x3];
        obuf[0x0] = 0xff;//msb of currently formatted track number
        obuf[0x1] = 0xff;//lsb of currently formatted track number
        triggerActivityStatus(1);
        triggerActivityStatus(2);

        if(drive_code < 1 || drive_code > 2) {
            obuf[0x2] = BDOS_SELECT_ERROR;
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }

        if(!drive(1).drive || !drive(2).drive) {
            obuf[2] = BDOS_READ_ERROR;
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }
        TF20DriveInterface *drive_src = drive(drive_code).drive.get();
        TF20DriveInterface *drive_dst = drive(3-drive_code).drive.get();

        for(uint8_t track = 0; track < 40; track++) {
            for(uint8_t sector = 0; sector < 16*2*2; sector++) {
                uint8_t track_buf[128];
                obuf[0x2] = drive_src->disk_read(track, sector, track_buf);
                if(obuf[0x2] != 0) {
                    obuf[0x0] = 0xff;//msb of currently formatted track number
                    obuf[0x1] = 0xff;//lsb of currently formatted track number
                    return conn->sendPacket(did, sid, fnc, 3, obuf);
                }
                obuf[0x2] = drive_dst->disk_write(track, sector, track_buf);
                if(obuf[0x2] != 0) {
                    obuf[0x0] = 0xff;//msb of currently formatted track number
                    obuf[0x1] = 0xff;//lsb of currently formatted track number
                    return conn->sendPacket(did, sid, fnc, 3, obuf);
                }
                if(sector == 0) {
                    obuf[0x0] = 0;
                    obuf[0x1] = track;
                    obuf[0x2] = 0;
                    int res = conn->sendPacket(did, sid, fnc, 3, obuf);
                    if(res < 0)
                        return res;
                }
            }
        }

        obuf[0x0] = 0xff;//msb of currently formatted track number
        obuf[0x1] = 0xff;//lsb of currently formatted track number
        obuf[0x2] = 0;
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
        triggerActivityStatus(drive_code);

        if(drive_code < 1 || drive_code > 2) {
            obuf[0x0] = BDOS_SELECT_ERROR;
            return conn->sendPacket(did, sid, fnc, 0x1, obuf);
        }
        if(!drive(drive_code).drive) {
            obuf[0] = BDOS_READ_ERROR;
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        TF20DriveInterface *drive = this->drive(drive_code).drive.get();

        obuf[0x0] = drive->disk_write(track, sector, buf+3);
        return conn->sendPacket(did, sid, fnc, 0x1, obuf);
    }
    case 0x7c: { // disk formatting
        if(size != 0x1)
            return 0;
        uint8_t drive_code = buf[0];
        printf("hx20 tries to format disk in drive %d\n",
               drive_code);
        uint8_t obuf[0x3];
        triggerActivityStatus(drive_code);

        if(drive_code < 1 || drive_code > 2) {
            obuf[0x2] = BDOS_SELECT_ERROR;
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }
        if(!drive(drive_code).drive) {
            obuf[0] = BDOS_READ_ERROR;
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        TF20DriveInterface *drive = this->drive(drive_code).drive.get();

        for(uint8_t track = 0; track != 39; track++) {
            obuf[0x0] = 0;
            obuf[0x1] = track;
            obuf[0x2] = 0;
            int res = conn->sendPacket(did, sid, fnc, 3, obuf);
            if(res < 0)
                return res;
            uint8_t res_code = drive->disk_format(track);
            if(res_code != 0) {
                obuf[0x0] = 0xff;//msb of currently formatted track number
                obuf[0x1] = 0xff;//lsb of currently formatted track number
                obuf[0x2] = res_code;
                return conn->sendPacket(did, sid, fnc, 3, obuf);
            }
        }

        obuf[0x0] = 0xff;//msb of currently formatted track number
        obuf[0x1] = 0xff;//lsb of currently formatted track number
        obuf[0x2] = 0;
        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x7d: { // new system generation
        if(size != 0x1)
            return 0;
        printf("hx20 tries to create a system disk in 2nd drive from the system in 1st drive\n");
        uint8_t obuf[0x3];
        triggerActivityStatus(1);
        triggerActivityStatus(2);

        if(!drive(1).drive || !drive(2).drive) {
            obuf[2] = BDOS_READ_ERROR;
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }
        TF20DriveInterface *drive_src = drive(1).drive.get();
        TF20DriveInterface *drive_dst = drive(2).drive.get();

        //directly copy the first four tracks
        for(uint8_t track = 0; track < 4; track++) {
            for(uint8_t sector = 0; sector < 16*2*2; sector++) {
                uint8_t track_buf[128];
                obuf[0x2] = drive_src->disk_read(track, sector, track_buf);
                if(obuf[0x2] != 0) {
                    obuf[0x0] = 0xff;//msb of currently formatted track number
                    obuf[0x1] = 0xff;//lsb of currently formatted track number
                    return conn->sendPacket(did, sid, fnc, 3, obuf);
                }
                obuf[0x2] = drive_dst->disk_write(track, sector, track_buf);
                if(obuf[0x2] != 0) {
                    obuf[0x0] = 0xff;//msb of currently formatted track number
                    obuf[0x1] = 0xff;//lsb of currently formatted track number
                    return conn->sendPacket(did, sid, fnc, 3, obuf);
                }
                if(sector == 0) {
                    obuf[0x0] = 0;
                    obuf[0x1] = 0;
                    obuf[0x2] = 0;
                    int res = conn->sendPacket(did, sid, fnc, 3, obuf);
                    if(res < 0)
                        return res;
                }
            }
        }

        char unix_pattern[13] = "*.SYS";
        uint8_t pattern[11];
        uint8_t dir_entry[32];
        std::string filename;
        unixToHx20Filename(pattern, unix_pattern);
        uint8_t search_res = drive_src->file_find_first(pattern, 0, dir_entry, filename);
        while(search_res == 0) {
            void *fcb_src = drive_src->file_open(dir_entry, 0);
            if(!fcb_src) {
                obuf[0x0] = 0xff;//
                obuf[0x1] = 0xff;//done, 0x0000 => not done
                obuf[0x2] = BDOS_FILE_NOT_FOUND;
                return conn->sendPacket(did, sid, fnc, 3, obuf);
            }
            void *fcb_dst = drive_dst->file_create(dir_entry, 0);
            if(!fcb_dst) {
                drive_src->file_close(fcb_src);
                obuf[0x0] = 0xff;//
                obuf[0x1] = 0xff;//done, 0x0000 => not done
                obuf[0x2] = BDOS_WRITE_ERROR;
                return conn->sendPacket(did, sid, fnc, 3, obuf);
            }
            uint8_t extent;
            uint16_t record;
            uint32_t records;
            drive_src->file_size(fcb_src, extent, record, records);
            for(uint32_t r = 0; r < records; r++) {
                uint8_t file_buf[128];
                obuf[2] = drive_src->file_read(fcb_src, r, extent, record, file_buf);
                if(obuf[2] != 0) {
                    drive_src->file_close(fcb_src);
                    drive_dst->file_close(fcb_dst);
                    obuf[0x0] = 0xff;//
                    obuf[0x1] = 0xff;//done, 0x0000 => not done
                    return conn->sendPacket(did, sid, fnc, 3, obuf);
                }
                obuf[2] = drive_dst->file_write(fcb_dst, file_buf, r, extent, record);
                if(obuf[2] != 0) {
                    drive_src->file_close(fcb_src);
                    drive_dst->file_close(fcb_dst);
                    obuf[0x0] = 0xff;//
                    obuf[0x1] = 0xff;//done, 0x0000 => not done
                    return conn->sendPacket(did, sid, fnc, 3, obuf);
                }
            }
            drive_src->file_close(fcb_src);
            drive_dst->file_close(fcb_dst);

            search_res = drive_src->file_find_next(dir_entry, filename);
        }

        obuf[0x0] = 0xff;//
        obuf[0x1] = 0xff;//done, 0x0000 => not done
        obuf[0x2] = 0;
        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x7e: { //disk free size calculation
        if(size != 0x01)
            return 0;
        uint8_t drive_code = buf[0];
        printf("hx20 tries to calculate free space of drive %d\n",
               drive_code);
        uint8_t obuf[2];
        triggerActivityStatus(drive_code);

        if(drive_code < 1 || drive_code > 2) {
            obuf[1] = BDOS_SELECT_ERROR;
            return conn->sendPacket(did, sid, fnc, 2, obuf);
        }
        if(!drive(drive_code).drive) {
            obuf[1] = BDOS_READ_ERROR;
            return conn->sendPacket(did, sid, fnc, 2, obuf);
        }
        TF20DriveInterface *drive = this->drive(drive_code).drive.get();

        uint8_t clusters;
        obuf[1] = drive->disk_size(clusters);//bdos error or 0
        obuf[0] = clusters;//number of free 2k-sectors
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
        triggerActivityStatus(drive_code);

        if(drive_code < 1 || drive_code > 2) {
            obuf[0x80] = BDOS_SELECT_ERROR;
            return conn->sendPacket(did, sid, fnc, 0x81, obuf);
        }

        if(!drive(drive_code).drive) {
            obuf[0] = BDOS_READ_ERROR;
            return conn->sendPacket(did, sid, fnc, 1, obuf);
        }
        TF20DriveInterface *drive = this->drive(drive_code).drive.get();

        obuf[0x80] = drive->disk_read(track, sector, obuf);
        return conn->sendPacket(did, sid, fnc, 0x81, obuf);
    }
    case 0x80: { //disk boot
        if(size != 1)
            return 0;
        char unixfilename[13];
        snprintf(unixfilename,13,"BOOT%02X.SYS",buf[0]);
        drive(1).last_file->setText(unixfilename);
        printf("hx20 requested %s\n",unixfilename);
        triggerActivityStatus(1);
        uint8_t filename[11];
        unixToHx20Filename(filename, unixfilename);

        uint8_t obuf[256+1];//need one more byte space so the second record fits in full

        if(!drive(1).drive) {
            obuf[0] = BDOS_READ_ERROR;
            return conn->sendPacket(did, sid, fnc, 256, obuf);
        }
        TF20DriveInterface *drive = this->drive(1).drive.get();

        void *fcb = drive->file_open(filename, 0);
        if(!fcb) {
            obuf[0] = BDOS_FILE_NOT_FOUND;
            return conn->sendPacket(did, sid, fnc, 256, obuf);
        }
        uint8_t cur_extent;
        uint16_t cur_record;
        obuf[0] = drive->file_read(fcb, 0, cur_extent, cur_record, obuf+1);
        if(obuf[0] != 0) {
            obuf[0] = BDOS_FILE_NOT_FOUND;
            drive->file_close(fcb);
            return conn->sendPacket(did, sid, fnc, 256, obuf);
        }
        obuf[0] = drive->file_read(fcb, 1, cur_extent, cur_record, obuf+1+128);
        if(obuf[0] != 0) {
            obuf[0] = BDOS_FILE_NOT_FOUND;
        }
        drive->file_close(fcb);
        return conn->sendPacket(did, sid, fnc, 256, obuf);
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
        drive(1).last_file->setText(filename);
        printf("hx20 requested %s\n",
               filename);
        uint8_t reloc_type = buf[11];
        printf("relocation: %s\n",
               reloc_type==0?"none":
               reloc_type==1?"from starting address":
               reloc_type==2?"from ending address":
               "unknown");
        uint16_t reloc_address = buf[13] | (buf[12] << 8);
        printf("address: 0x%04x\n",
               reloc_address
              );

        uint8_t obuf[3];

        if(!drive(1).drive) {
            obuf[0] = BDOS_READ_ERROR;
            return conn->sendPacket(did, sid, fnc, 3, obuf);
        }
        TF20DriveInterface *drive = this->drive(1).drive.get();
        triggerActivityStatus(1);

        void *fcb = drive->file_open(buf, 0);
        if(!fcb) {
            obuf[0] = BDOS_FILE_NOT_FOUND;
            return conn->sendPacket(did, sid, fnc, 257, obuf);
        }
        uint8_t cur_extent;
        uint16_t cur_record;
        obuf[0] = drive->file_read(fcb, 0, cur_extent, cur_record, obuf+1);
        if(obuf[0] != 0) {
            drive->file_close(fcb);
            return conn->sendPacket(did, sid, fnc, 257, obuf);
        }
        obuf[0] = drive->file_read(fcb, 1, cur_extent, cur_record, obuf+1+128);

        uint8_t extent;
        uint16_t record;
        uint32_t records;
        drive->file_size(fcb, extent, record, records);
        load_buffer.resize(records * 128);
        for(uint32_t r = 0; r < records; r++) {
            obuf[0] = drive->file_read(fcb, r, extent, record, load_buffer.data() + 128*r);
            if(obuf[0] != 0) {
                drive->file_close(fcb);
                return conn->sendPacket(did, sid, fnc, 3, obuf);
            }
        }
        drive->file_close(fcb);

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

        obuf[0] = 0x00;
        obuf[1] = code_size >> 8;
        obuf[2] = code_size;
        hexdump((char *)obuf,3);
        return conn->sendPacket(did, sid, fnc, 3, obuf);
    }
    case 0x82: { //load close
        load_buffer.clear();
        uint8_t obuf[1];
        obuf[0] = 0x00;
        return conn->sendPacket(did, sid, fnc, 1, obuf);
    }
    case 0x83: { //read one block(goes with 0x81: load open)
        unsigned int record = (buf[0] << 8) | buf[1];
        printf("hx20 requested record 0x%02x\n",record);
        uint8_t obuf[2+128+1];
        triggerActivityStatus(1);
        if(load_buffer.empty()) {
            obuf[1] = buf[1]+1;
            obuf[0] = buf[0]+((buf[1] == 0xff)?1:0);
            obuf[2+128] = BDOS_FILE_NOT_FOUND;

            hexdump((char *)obuf,128+3);
            return conn->sendPacket(did, sid, fnc, 2+128+1, obuf);
        }

        unsigned int code_size = (unsigned char)load_buffer[1] |
                                 ((unsigned char)load_buffer[2] << 8);

        obuf[1] = buf[1]+1;
        obuf[0] = buf[0]+((buf[1] == 0xff)?1:0);
        memcpy(obuf+2,load_buffer.data() + 0x100 + record*128,128);

        obuf[2+128] = record*128 > code_size?0xff:0x00;

        hexdump((char *)obuf,128+3);
        return conn->sendPacket(did, sid, fnc, 2+128+1, obuf);
    }
    default: {
        printf("HX20DiskDevice: got packet: sid = 0x%04x, did = 0x%04x, "
               "fnc = 0x%02x, size = 0x%04x\n",
               sid,did,fnc,size);
        hexdump((char *)buf,size);
        return 0;
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
    settingsConfig(nullptr), settingsPresets(nullptr)
{
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
    installNewDrive(drive_code, std::make_unique<TF20DriveDiskImage>(file),
                    tr("Image %1").arg(QString::fromStdString(file)));
    QString tgtval = QString("file://%1").arg(QString::fromStdString(file));
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
        QMenu *mnu = new QMenu();
        mnu->addAction(tr("Set disk &directory..."),
        [this,i]() {
            QString result = QFileDialog::getExistingDirectory(nullptr,
                             tr("Set disk directory"));
            if(!result.isEmpty()) {
                try {
                    this->setDiskDirectory(i, result.toStdString());
                } catch (std::exception &e) {
                    QMessageBox::critical(nullptr,
                                          tr("Failed to open"),
                                          tr("Failed to open %1 as a disk directory:\n%2").arg(result).arg(e.what()));
                }
            }
        });
        mnu->addAction(tr("Set disk &file..."),
        [this,i]() {
            QString result = QFileDialog::getOpenFileName(nullptr,
                             tr("Set disk file"),
                             QString(),
                             tr("Teledisk (*.td0)"));
            if(!result.isEmpty()) {
                try {
                    this->setDiskFile(i, result.toStdString());
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

void HX20DiskDevice::updateFromConfig() {
    for(int drive_code = 1; drive_code <= 2; drive_code++) {
        QString d1 = settingsConfig->value
                     (QString("disk_%1").arg(drive_code),
                      "empty://",true).toString();
        if(d1.startsWith("empty://")) {
            ejectDisk(drive_code);
        } else if(d1.startsWith("dir://")) {
            setDiskDirectory(drive_code, d1.mid(6).toStdString());
        } else if(d1.startsWith("file://")) {
            setDiskFile(drive_code, d1.mid(7).toStdString());
        }
    }
}