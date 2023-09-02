
#include "tf20-adapters.hpp"

#include <fstream>
#include <sstream>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include "hx20-ser-proto.hpp"
#include <unistd.h>
#include "disk-drive.hpp"
#include "disk-drive-adapters.hpp"

static void hx20ToUnixFilename(char *dst,uint8_t const *src) {
    char *fp = dst;
    memcpy(fp,src,8);
    for(fp += 7; *fp == ' ' && fp >= dst; fp--) {}
    fp++;
    *fp = '.';
    fp++;
    memcpy(fp,src+8,3);
    for(fp += 2; *fp == ' '; fp--) {}
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

static bool matchFilename(char const *name, char const *pattern) {
    //chars match exactly, '?' match any char, or '.', but don't eat '.'
    //'*' at the begin matches the component(file name or file type)
    //just before the . and the end of the name insert ' '
    if(*name == '.') //skip the initial '.' for hidden, i.E. system files
        name++;
    //match the filename part.
    while(*name && *pattern) {
        if(*name == '.') {
            if(*pattern == '.') {
                break;
            } else if(*pattern == '?' || *pattern == '*') {
                pattern++;
                continue;
            } else {
                return false;
            }
        } else if(*pattern == *name || *pattern == '?' || *pattern == '*') {
            name++;
            if(*pattern != '*')
                pattern++;
            continue;
        } else{
            return false;
        }
    }
    if(*name != '.' || *pattern != '.')
        return false;
    name++;
    pattern++;
    //match the type part.
    while(*name && *pattern) {
        if(*pattern == '.' || *name == '.') {
            return false;
        } else if(*pattern == *name || *pattern == '?' || *pattern == '*') {
            name++;
            if(*pattern != '*')
                pattern++;
            continue;
        } else{
            return false;
        }
    }
    return true;
}

static bool dirent_filename_compare(uint8_t const *n1, uint8_t const *n2) {
    return memcmp(n1,n2,8) == 0 &&
            (n1[8] & 0x7f) == (n2[8] & 0x7f) &&
            (n1[9] & 0x7f) == (n2[9] & 0x7f) &&
            (n1[10] & 0x7f) == (n2[10] & 0x7f);
}

static bool dirent_filename_match(uint8_t const *n, uint8_t const *p) {
    printf("matching name %.8s.%.3s against pattern %.8s.%.3s\n",
           n,n+8,p,p+8);
    if(p[0] != '*') {
        for(int i = 0; i < 8; i++) {
            if(p[i] != n[i] && p[i] != '?')
                return false;
        }
    }
    if(p[8] != '*') {
        for(int i = 0; i < 8; i++) {
            if((p[i] & 0x7f) != (n[i] & 0x7f) && p[i] != '?')
                return false;
        }
    }
    return true;
}

class DirSearch {
private:
    DIR *dir;
    std::string pattern;
public:
    char const *filename;
    DirSearch(char const *name, std::string const &pattern);
    ~DirSearch();
    bool good();
    struct dirent *readdir();
    uint8_t findNext(uint8_t *obuf);
};

DirSearch::DirSearch(char const *name, std::string const &pattern)
    : dir(opendir(name))
    , pattern(pattern) {
}

DirSearch::~DirSearch() {
    if(dir)
        closedir(dir);
}

bool DirSearch::good() {
    return dir;
}

struct dirent *DirSearch::readdir() {
    return ::readdir(dir);
}

uint8_t DirSearch::findNext(uint8_t *obuf) {
    uint8_t res;
    struct dirent *de;
    while((de = readdir())) {
        printf("seeing %s\n",de->d_name);
        if(matchFilename(de->d_name,pattern.c_str()) &&
                unixToHx20Filename(obuf+1,de->d_name)) {
            filename = de->d_name;
            struct stat statbuf;
            if(fstatat(dirfd(dir), de->d_name, &statbuf, 0) != 0) {
                throw IOError(errno, std::system_category(), "Could not stat file in dir");
            }
            bool canwrite = faccessat(dirfd(dir), de->d_name, W_OK, 0) == 0;
            printf("found %s\n",de->d_name);
            res = 0x00;
            obuf[0] = 0;
            //obuf[0]: 0
            //obuf[1-8]: filename
            //obuf[9-11]: type
            //filename already filled in. may wish to
            //modify r/o and sys flags.
            if(!canwrite)
                obuf[9] |= 0x80; //read only
            if(de->d_name[0] == '.')
                obuf[10] |= 0x80; //system file
            //obuf[11] |= 0x80; //unused
            int extents = (statbuf.st_size+32767) / 32768 -1;
            int records = ((statbuf.st_size % 32768) + 127) / 128;
            obuf[12] = extents << 1 | (records >= 0x80?1:0);//extents, msb of records
            obuf[13] = 0;
            obuf[14] = 0;
            obuf[15] = records - (records >= 0x80?0x80:0);//record count in the last extent
            memset(obuf+16,0,16);
            return res;
        }
    }

    return BDOS_FILE_NOT_FOUND;
}

class DirFCB {
private:
    std::fstream st;
public:
    DirFCB(char const *filename, bool create = false);
    bool good();
    uint32_t size();//in records
    uint8_t write(uint32_t record,uint8_t const *buf);
    uint8_t read(uint32_t record,uint8_t *buf);
    void close();
};

DirFCB::DirFCB(char const *filename, bool create) {
    if(create) {
        st.open(filename, std::ios::out);
        st.close();
    }
    st.open(filename, std::ios::in|std::ios::out);
}

bool DirFCB::good() {
    return st.good();
}

uint32_t DirFCB::size() {
    st.seekg(0,std::ios::end);
    uint64_t sz = st.tellg();
    if(sz > 0xffffff)
        sz = 0xffffff;
    return sz;
}

uint8_t DirFCB::write(uint32_t record,uint8_t const *buf) {
    st.seekp(record*128,std::ios::beg);
    st.write((char *)buf,128);
    return 0;//TODO: get the st state and make a bdos error if needed
}

uint8_t DirFCB::read(uint32_t record,uint8_t *buf) {
    st.seekg(record*128,std::ios::beg);
    st.read((char *)buf,128);
    return 0;//TODO: get the st state and make a bdos error if needed
}

void DirFCB::close() {
}

TF20DriveDirectory::TF20DriveDirectory(std::string const &base_dir)
    : base_dir(base_dir)
{
}

TF20DriveDirectory::~TF20DriveDirectory() =default;

void TF20DriveDirectory::reset() {
}

void *TF20DriveDirectory::file_open(uint8_t const *filename, uint8_t extent) {
    //ios::nocreate and ios::noreplace
    char unixfilename[13];
    hx20ToUnixFilename(unixfilename,filename);
    DirFCB *fcb = new DirFCB((base_dir + "/" + unixfilename).c_str(), false);;
    if(!fcb->good()) {
        delete fcb;
        fcb = new DirFCB((base_dir + "/." + unixfilename).c_str(), false);;
    }
    return fcb;
}

void TF20DriveDirectory::file_close(void *_fcb) {
    DirFCB *fcb = reinterpret_cast<DirFCB *>(_fcb);
    fcb->close();
    delete fcb;
}

uint8_t TF20DriveDirectory::file_find_first(uint8_t const *pattern, uint8_t extent, void *dir_entry, std::string &filename) {
    uint8_t *obuf = (uint8_t *)dir_entry;
    char unixpattern[13];
    hx20ToUnixFilename(unixpattern, pattern);
    dirSearch.reset(new DirSearch("disk", unixpattern));
    if(!dirSearch->good()) {
        return BDOS_READ_ERROR;
    }

    uint8_t res = dirSearch->findNext(obuf);
    if(res == 0)
        filename = dirSearch->filename;
    return res;
}

uint8_t TF20DriveDirectory::file_find_next(void *dir_entry, std::string &filename) {
    uint8_t *obuf = (uint8_t *)dir_entry;
    uint8_t res = dirSearch->findNext(obuf);
    if(res == 0)
        filename = dirSearch->filename;
    return res;
}

uint8_t TF20DriveDirectory::file_remove(uint8_t const *filename, uint8_t extent) {
    char unixfilename[0x13];
    hx20ToUnixFilename(unixfilename, filename);

    if(unlink((base_dir+ "/" +unixfilename).c_str()) == -1) {
        if(errno == ENOENT) {
            if(unlink((base_dir+ "/." +unixfilename).c_str()) == -1) {
                if(errno == ENOENT)
                    return BDOS_FILE_NOT_FOUND;
                else if(errno == EROFS)
                    return BDOS_WRITE_PROTECT_ERROR1;
                else
                    return BDOS_WRITE_ERROR;
            }
        } else if(errno == EROFS)
            return BDOS_WRITE_PROTECT_ERROR1;
        else
            return BDOS_WRITE_ERROR;
    }

    return 0x00;
}

void *TF20DriveDirectory::file_create(uint8_t const *filename, uint8_t extent) {
    //ios::nocreate and ios::noreplace
    char unixfilename[13];
    hx20ToUnixFilename(unixfilename,filename);
    DirFCB *fcb = new DirFCB((base_dir + "/" + unixfilename).c_str(), false);;
    if(!fcb->good()) {
        delete fcb;
        fcb = new DirFCB((base_dir + "/." + unixfilename).c_str(), false);;
    }
    if(!fcb->good()) {
        delete fcb;
        fcb = new DirFCB((base_dir + "/" + unixfilename).c_str(), true);
    }
    return fcb;
}

uint8_t TF20DriveDirectory::file_rename(uint8_t const *old_filename, uint8_t old_extent,
                 uint8_t const *new_filename, uint8_t new_extent) {
        char unixfilename_old[13];
        char unixfilename_new[13];
        hx20ToUnixFilename(unixfilename_old, old_filename);
        hx20ToUnixFilename(unixfilename_new, new_filename);

        //0xff if source file not existant, or the destination already
        //exists(need to check against basic docs of rename
        //command -- result: basic checks for existing destination)
        //if our unix-rename fails, we do a write error(0xfb)
        if(rename((base_dir+"/"+unixfilename_old).c_str(),
                  (base_dir+"/"+unixfilename_new).c_str()) == -1) {
            if(errno == ENOENT) {
            if(rename((base_dir+"/."+unixfilename_old).c_str(),
                      (base_dir+"/."+unixfilename_new).c_str()) == -1) {
                if(errno == ENOENT)
                    return BDOS_FILE_NOT_FOUND;
                else if(errno == EROFS)
                    return BDOS_WRITE_PROTECT_ERROR1;
                else
                    return BDOS_WRITE_ERROR;
            }
        } else if(errno == EROFS)
            return BDOS_WRITE_PROTECT_ERROR1;
            else
                return BDOS_WRITE_ERROR;
        }

        return 0x00;//rename done
}

uint8_t TF20DriveDirectory::file_read(void *_fcb, uint32_t record,
                  uint8_t &cur_extent, uint16_t &cur_record, void *buffer) {
    DirFCB *fcb = reinterpret_cast<DirFCB *>(_fcb);
    cur_extent = ((record >> 8) << 1) | (((record & 0xff) > 0x80)?1:0);
    cur_record = (record & 0xff) - (((record & 0xff) > 0x80)?0x80:0);
    return fcb->read(record, (uint8_t*)buffer);
}

uint8_t TF20DriveDirectory::file_write(void *_fcb, void const *buffer, uint32_t record,
                   uint8_t &cur_extent, uint16_t &cur_record) {
    DirFCB *fcb = reinterpret_cast<DirFCB *>(_fcb);
    cur_extent = ((record >> 8) << 1) | (((record & 0xff) > 0x80)?1:0);
    cur_record = (record & 0xff) - (((record & 0xff) > 0x80)?0x80:0);
    return fcb->write(record, (uint8_t const *)buffer);
}

uint8_t TF20DriveDirectory::file_size(void *_fcb, uint8_t &extent,
                  uint16_t &record, uint32_t &records) {
    DirFCB *fcb = reinterpret_cast<DirFCB *>(_fcb);
    uint32_t sz = fcb->size();
    records = (sz+127)/128;
    if(records > 0x7fff) {
        extent = 0x7f;
        record = 0x100;
    } else {
        extent = records >> 8;
        record = records & 0xff;
    }
    return 0;
}

uint8_t TF20DriveDirectory::disk_write(uint8_t track, uint8_t sector, void const *buffer) {
    if(track >= 4 || sector >= 64)
        return BDOS_WRITE_ERROR;
    //.boot is not a valid name for any file, they must have \.?[^.]{0,8}\.[^.]{0,3}
    std::fstream st((base_dir + "/.boot").c_str(), std::ios::out);
    if(!st.good()) {
        return BDOS_WRITE_ERROR;
    }
    st.seekp(track * 8192 + sector * 128);
    if(!st.good()) {
        return BDOS_WRITE_ERROR;
    }
    st.write((char const*)buffer, 128);
    if(!st.good()) {
        return BDOS_WRITE_ERROR;
    }
    return 0;
}

uint8_t TF20DriveDirectory::disk_format(uint8_t track) {
    return BDOS_WRITE_ERROR;
}

uint8_t TF20DriveDirectory::disk_size(uint8_t &clusters) {
    clusters = 255;
    return 0;
}

uint8_t TF20DriveDirectory::disk_read(uint8_t track, uint8_t sector, void *buffer)  {
    if(track >= 4 || sector >= 64)
        return BDOS_READ_ERROR;
    //.boot is not a valid name for any file, they must have \.?[^.]{0,8}\.[^.]{0,3}
    std::fstream st((base_dir + "/.boot").c_str(), std::ios::in);
    if(!st.good()) {
        return BDOS_READ_ERROR;
    }
    st.seekg(track * 8192 + sector * 128);
    if(!st.good()) {
        return BDOS_READ_ERROR;
    }
    st.read((char*)buffer, 128);
    if(!st.good()) {
        return BDOS_READ_ERROR;
    }
    return 0;
}

static bool read_dir_extent(DiskDriveInterface *drive, uint8_t extent, uint8_t *dir_ent) {
    uint8_t buf[256];
    if(!drive->read(CHS(4,0,(extent >> 3)+1), buf, 1))
        return false;
    memcpy(dir_ent, buf+(extent & 0x7)*32, 32);
    return true;
}

static bool write_dir_extent(DiskDriveInterface *drive, uint8_t extent, uint8_t const *dir_ent) {
    uint8_t buf[256];
    if(!drive->read(CHS(4,0,(extent >> 3)+1), buf, 1))
        return false;
    memcpy(buf+(extent & 0x7)*32, dir_ent, 32);
    if(!drive->write(CHS(4,0,(extent >> 3)+1), buf, 1))
        return false;
    return true;
}

static unsigned int get_records_in_dirent(uint8_t const *dir_ent) {
    return (dir_ent[12] & 1) * 128 + dir_ent[15];
}

static void set_records_in_dirent(uint8_t *dir_ent, uint16_t records) {
    dir_ent[12] = (dir_ent[12] & 0xfe) | ((records >= 0x80)?1:0);
    dir_ent[15] = records - ((records >= 0x80)?0x80:0);
}

static unsigned int get_records_in_file(uint8_t const *last_dir_ent) {
    return last_dir_ent[12] * 128 + last_dir_ent[15];
}

static unsigned int get_blocks_in_dirent(uint8_t const *dir_ent) {
    return (get_records_in_dirent(dir_ent) + 15)/16;
}

static uint8_t find_free_block_after(DiskDriveInterface *drive, uint8_t after_block = 0) {
    uint8_t dir_ent[32];
    uint8_t to_be_checked_block = after_block+1;
    while(to_be_checked_block < 144) { // 144 blocks in data area (40-4 tracks * 2 sides * 16 sectors / 8 sectors/block)
        bool is_free = true;
        for(int i = 0; i < 64; i++) { // 64 directory entries in 2kb directory block
            if(read_dir_extent(drive, i, dir_ent) != 0)
                return 0;
            if(dir_ent[0] != 0)
                continue;
            for(unsigned int j = 0; j < get_blocks_in_dirent(dir_ent); j++) {
                if(dir_ent[j+16] == to_be_checked_block) {
                    is_free = false;
                    break;
                }
            }
        }
        if(!is_free)
            to_be_checked_block++;
        else
            return to_be_checked_block;
    }
    return 0;
}

static int find_free_dirent_after(DiskDriveInterface *drive, int after_entry, uint8_t *dir_ent) {
    while(after_entry+1 < 64) {
        after_entry++;
        if(read_dir_extent(drive, after_entry, dir_ent) != 0)
            return -1;
        if(dir_ent[0] == 0xe5)
            return after_entry;
    }
    return -1;
}

class ImgSearch {
private:
    DiskDriveInterface *drive;
    uint8_t pattern[11];
    uint8_t ent;
public:
    uint8_t const *filename;
    ImgSearch(DiskDriveInterface *drive, uint8_t const *pattern);
    ~ImgSearch();
    uint8_t findNext(uint8_t *obuf);
};

ImgSearch::ImgSearch(DiskDriveInterface *drive, uint8_t const *pattern)
    : drive(drive), ent(0) {
        memcpy(this->pattern, pattern, 11);
}

ImgSearch::~ImgSearch() = default;

uint8_t ImgSearch::findNext(uint8_t *obuf) {
    while(ent < 64) {
        read_dir_extent(drive, ent, obuf);
        ent++;
        if(obuf[0] == 0 && dirent_filename_match(obuf+1, pattern)) {
            //TODO do we need to filter for this to be the last entry?
            return 0;
        }
    }
    return BDOS_FILE_NOT_FOUND;
}

class ImgFCB {
private:
    DiskDriveInterface *drive;
    uint8_t filename[11];
    int first_ent;
    int last_ent;
public:
    ImgFCB(DiskDriveInterface *drive, uint8_t const *filename, bool create = false);
    bool good();
    uint32_t size();//in records
    uint8_t write(uint32_t record,uint8_t const *buf);
    uint8_t read(uint32_t record,uint8_t *buf);
    void close();
};

ImgFCB::ImgFCB(DiskDriveInterface *drive, uint8_t const *filename, bool create)
    : drive(drive), first_ent(-1), last_ent(-1) {
    memcpy(this->filename, filename, 11);
    uint8_t dir_ent[32];
    for(int i = 0; i < 64; i++) {
        if(!read_dir_extent(drive, i, dir_ent)) {
            printf("Cannot read extent %d\n", i);
            return;
        }
        printf("filename %.8s %.3s\n", dir_ent+1, dir_ent+9);
        if(dir_ent[0] == 0 && dirent_filename_compare(filename, dir_ent+1)) {
            printf("Found file in extent %d\n", i);
            //found one.
            if(first_ent == -1) {
                memcpy(this->filename, dir_ent+1, 11);
                first_ent = i;
            }
            last_ent = i;
        }
    }
    if(create) {
        if(first_ent != 0) {
            first_ent = -1;
            last_ent = -1;
            return;
        }
        for(int i = 0; i < 64; i++) {
            if(dir_ent[0] != 0) {
                printf("Found empty extent %d\n", i);
                memset(dir_ent, 0, 32);
                memcpy(dir_ent+1, filename, 11);
                if(!write_dir_extent(drive, i, dir_ent))
                    return;
                first_ent = i;
                last_ent = i;
                return;
            }
        }
    }
}

bool ImgFCB::good() {
    //basically file found/created or not.
    return first_ent != -1;
}

uint32_t ImgFCB::size() {
    uint8_t dir_ent[32];
    if(!read_dir_extent(drive, last_ent, dir_ent))
        return BDOS_READ_ERROR;
    return get_records_in_file(dir_ent)*128;
}

uint8_t ImgFCB::write(uint32_t record,uint8_t const *buf) {
    //first, check if the file is already large enough
    uint8_t dir_ent[32];
    if(!read_dir_extent(drive, last_ent, dir_ent))
        return BDOS_READ_ERROR;
    unsigned int records_in_file = get_records_in_file(dir_ent);
    int ent;
    if(records_in_file <= record) {
        //no, it is not.
        //check if we need to add more blocks
        if((records_in_file+15)/16 < (record+1+15)/16) {
            //yes, complete this one.
            records_in_file = (records_in_file+15) & ~0xf;
            set_records_in_dirent(dir_ent, records_in_file % 32768);

            //now add blocks until we have enough.
            int current_add_block = 0;
            while(records_in_file/16 < (record+1+15)/16) {
                //if this ent is already full, get a new one.
                if(records_in_file % 256 == 0) {
                    uint8_t last_extent_bits = dir_ent[12] & 0xfe;
                    if(!write_dir_extent(drive, last_ent, dir_ent))
                        return BDOS_WRITE_ERROR;
                    int res = find_free_dirent_after(drive, last_ent, dir_ent);
                    if(res == -1)
                        return BDOS_WRITE_ERROR;
                    last_ent = res;
                    memset(dir_ent, 0, 32);
                    memcpy(dir_ent+1, filename, 11);
                    dir_ent[12] = last_extent_bits+2;
                }
                int res = find_free_block_after(drive, current_add_block);
                current_add_block = res;
                if(res == 0)
                    return BDOS_READ_ERROR;
                dir_ent[(records_in_file % 256) / 16] = res;
                records_in_file += 16;
            }
        }
        if((records_in_file+15)/16 >= (record+1+15)/16) {
            //ok, that is all blocks that are needed. fix the record count.
            records_in_file = record+1;
            set_records_in_dirent(dir_ent, records_in_file % 32768);
        }
        if(!write_dir_extent(drive, last_ent, dir_ent))
            return BDOS_WRITE_ERROR;
        ent = last_ent;
    } else if (records_in_file / 256 != record / 256) {
        //find the correct extent
        for(ent = first_ent; ent < last_ent; ent++) {
            if(!read_dir_extent(drive, ent, dir_ent))
                return BDOS_READ_ERROR;
            if(dir_ent[0] == 0 &&
                memcmp(dir_ent+1, filename, 11) == 0 &&
                (dir_ent[12] >> 1) == record / 256)
                break;
        }
    }

    if(dir_ent[16+(record % 256)/16] == 0) {
        int res = find_free_block_after(drive, 0);
        if(res == -1)
            return BDOS_READ_ERROR;
        dir_ent[16+(record % 256)/16] = res;
        if(!write_dir_extent(drive, ent, dir_ent))
            return BDOS_WRITE_ERROR;
    }
    int block = dir_ent[16+(record % 256)/16];
    //now get the sector for this
    uint8_t sector_data[256];
    CHS chs(4+(block >> 2),(block>>1)&1, ((block & 1) << 3 | ((record >> 1) & 7))+1);
    if(!drive->read(chs, sector_data, 1))
        return BDOS_READ_ERROR;
    memcpy(sector_data + 128*(record & 1), buf, 128);
    if(!drive->write(chs, sector_data, 1))
        return BDOS_WRITE_ERROR;

    return 0;
}

uint8_t ImgFCB::read(uint32_t record,uint8_t *buf) {
    //first, check if the file is already large enough
    uint8_t dir_ent[32];
    if(!read_dir_extent(drive, last_ent, dir_ent)) {
        printf("read: cannot find last extent %d\n", last_ent);
        return BDOS_READ_ERROR;
    }
    printf("read: found last extent %d\n", last_ent);
    unsigned int records_in_file = get_records_in_file(dir_ent);
    int ent;
    if(records_in_file <= record) {
        printf("read: requested record %d, but only %d in file\n", record, records_in_file);
        return BDOS_READ_ERROR;
    } else if (records_in_file / 256 != record / 256) {
        //find the correct extent
        for(ent = first_ent; ent < last_ent; ent++) {
            if(!read_dir_extent(drive, ent, dir_ent)) {
                printf("read: cannot find extent %d\n", ent);
                return BDOS_READ_ERROR;
            }
            if(dir_ent[0] == 0 &&
                memcmp(dir_ent+1, filename, 11) == 0 &&
                (dir_ent[12] >> 1) == record / 256)
                break;
        }
        printf("read: found extent %d\n", ent);
    }

    if(dir_ent[16+(record % 256)/16] == 0) {
        printf("read: block %d is not active\n", 16+(record % 256)/16);
        return BDOS_READ_ERROR;
    }
    int block = dir_ent[16+(record % 256)/16];
    printf("Reading record %d in block %d: %d\n",
           record, 16+(record % 256)/16, block);
    //now get the sector for this
    uint8_t sector_data[256];
    CHS chs(4+(block >> 2),(block>>1)&1, ((block & 1) << 3 | ((record >> 1) & 7))+1);
    if(!drive->read(chs, sector_data, 1)) {
        printf("read: failed to read disk %d %d %d\n", chs.idCylinder, chs.idSide, chs.idSector);
        return BDOS_READ_ERROR;
    }
    memcpy(buf, sector_data + 128*(record & 1), 128);

    return 0;
}

void ImgFCB::close() {
}

TF20DriveDiskImage::TF20DriveDiskImage(std::string const &file) {
    //TODO we probably want to be able to create files as well.
    std::stringstream errors;
    errors << "Failed to open file \"" << file << "\".\n";
    if(!drive) {
        try {
            drive = std::make_unique<TelediskImageDrive>(file);
        } catch(std::exception &e) {
            errors << "TeleDisk: " << e.what();
        }
    }
    if(!drive) {
        throw std::runtime_error(errors.str());
    }
}

TF20DriveDiskImage::~TF20DriveDiskImage() =default;

void TF20DriveDiskImage::reset() {
}

void *TF20DriveDiskImage::file_open(uint8_t const *filename, uint8_t extent) {
    ImgFCB *fcb = new ImgFCB(drive.get(), filename, false);
    if(!fcb->good()) {
        delete fcb;
        return nullptr;
    }
    return fcb;
}

void TF20DriveDiskImage::file_close(void *_fcb) {
    ImgFCB *fcb = reinterpret_cast<ImgFCB *>(_fcb);
    fcb->close();
    delete fcb;
}

uint8_t TF20DriveDiskImage::file_find_first(uint8_t const *pattern, uint8_t extent, void *dir_entry, std::string &filename) {
    dirSearch = std::make_unique<ImgSearch>(drive.get(), pattern);

    uint8_t *obuf = (uint8_t*)dir_entry;
    uint8_t res = dirSearch->findNext(obuf);
    char f[13];
    hx20ToUnixFilename(f, obuf+1);
    filename = f;
    return res;
}

uint8_t TF20DriveDiskImage::file_find_next(void *dir_entry, std::string &filename) {
    uint8_t *obuf = (uint8_t *)dir_entry;
    uint8_t res = dirSearch->findNext(obuf);
    char f[13];
    hx20ToUnixFilename(f, obuf+1);
    filename = f;
    return res;
}

uint8_t TF20DriveDiskImage::file_remove(uint8_t const *filename, uint8_t extent) {
    uint8_t dir_ent[32];
    for(int i = 0; i < 64; i++) {
        if(!read_dir_extent(drive.get(), i, dir_ent))
            return BDOS_READ_ERROR;
        if(dir_ent[0] != 0)
            continue;
        if(dirent_filename_compare(dir_ent+1,filename)) {
            dir_ent[0] = 0xea;
            if(!write_dir_extent(drive.get(), i, dir_ent))
                return BDOS_WRITE_ERROR;
        }
    }
    return 0;
}

void *TF20DriveDiskImage::file_create(uint8_t const *filename, uint8_t extent) {
    ImgFCB *fcb = new ImgFCB(drive.get(), filename, true);
    return fcb;
}

uint8_t TF20DriveDiskImage::file_rename(uint8_t const *old_filename, uint8_t old_extent,
                 uint8_t const *new_filename, uint8_t new_extent) {
    uint8_t dir_ent[32];
    for(int i = 0; i < 64; i++) {
        if(!read_dir_extent(drive.get(), i, dir_ent))
            return BDOS_READ_ERROR;
        if(dir_ent[0] != 0)
            continue;
        if(dirent_filename_compare(dir_ent+1, old_filename)) {
            //TODO figure out if this should be able to change file attributes
            memcpy(dir_ent+1, new_filename,8);
            dir_ent[9] = (dir_ent[9] & 0x80) | (new_filename[8] & 0x7f);
            dir_ent[10] = (dir_ent[10] & 0x80) | (new_filename[9] & 0x7f);
            dir_ent[11] = (dir_ent[11] & 0x80) | (new_filename[10] & 0x7f);
            if(!write_dir_extent(drive.get(), i, dir_ent))
                return BDOS_WRITE_ERROR;
        }
    }
    return 0;
}

uint8_t TF20DriveDiskImage::file_read(void *_fcb, uint32_t record,
                  uint8_t &cur_extent, uint16_t &cur_record, void *buffer) {
    ImgFCB *fcb = reinterpret_cast<ImgFCB *>(_fcb);
    cur_extent = ((record >> 8) << 1) | (((record & 0xff) > 0x80)?1:0);
    cur_record = (record & 0xff) - (((record & 0xff) > 0x80)?0x80:0);
    return fcb->read(record, (uint8_t*)buffer);
}

uint8_t TF20DriveDiskImage::file_write(void *_fcb, void const *buffer, uint32_t record,
                   uint8_t &cur_extent, uint16_t &cur_record) {
    ImgFCB *fcb = reinterpret_cast<ImgFCB *>(_fcb);
    cur_extent = ((record >> 8) << 1) | (((record & 0xff) > 0x80)?1:0);
    cur_record = (record & 0xff) - (((record & 0xff) > 0x80)?0x80:0);
    return fcb->write(record, (uint8_t const *)buffer);
}

uint8_t TF20DriveDiskImage::file_size(void *_fcb, uint8_t &extent,
                  uint16_t &record, uint32_t &records) {
    ImgFCB *fcb = reinterpret_cast<ImgFCB *>(_fcb);
    uint32_t sz = fcb->size();
    records = (sz+127)/128;
    if(records > 0x7fff) {
        extent = 0x7f;
        record = 0x100;
    } else {
        extent = records >> 8;
        record = records & 0xff;
    }
    return 0;
}

uint8_t TF20DriveDiskImage::disk_write(uint8_t track, uint8_t sector, void const *buffer) {
    uint8_t buf[256];
    if(!drive->read(CHS(track, sector >> 5, ((sector & 0x1e) >> 1)+1), buf, 1))
        return BDOS_READ_ERROR;
    memcpy(buf+128*(sector & 1), buffer, 128);
    if(!drive->write(CHS(track, sector >> 5, ((sector & 0x1e) >> 1)+1), buf, 1))
        return BDOS_WRITE_ERROR;
    return 0;
}

uint8_t TF20DriveDiskImage::disk_format(uint8_t track) {
    if(drive->format(track, 0, 16, 1))
        return 0;
    return BDOS_WRITE_ERROR;
}

uint8_t TF20DriveDiskImage::disk_size(uint8_t &clusters) {
    CHS chs;
    if(!drive->size(chs))
        return BDOS_READ_ERROR;
    clusters = (chs.idCylinder-4)*(chs.idSide)*(chs.idSector) / 4 - 1;
    uint8_t dir_ent[32];
    for(int i = 0; i < 64; i++) {
        if(!read_dir_extent(drive.get(), i, dir_ent))
            return BDOS_READ_ERROR;
        if(dir_ent[0] != 0)
            continue;
        clusters -=((dir_ent[15] + (dir_ent[12] & 0x1)*128)+15)/16;
    }
    return 0;
}

uint8_t TF20DriveDiskImage::disk_read(uint8_t track, uint8_t sector, void *buffer)  {
    uint8_t buf[256];
    if(!drive->read(CHS(track, sector >> 5, ((sector & 0x1e) >> 1)+1), buf, 1))
        return BDOS_READ_ERROR;
    memcpy(buffer, buf+128*(sector & 1), 128);
    return 0;
}

