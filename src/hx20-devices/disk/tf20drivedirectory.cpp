
#include "tf20drivedirectory.hpp"

#include "../../../../src/hx20-ser-proto.hpp"

#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <system_error>
#include <fstream>
#include <unistd.h>

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
    if(*name == '.') //skip the initial '.' for hidden, i.E. system files
        name++;
    //match the filename part.
    while(*name && *pattern) {
        if(*name == '.') {
            if(*pattern == '.') {
                break;
            } else if(*pattern == '?') {
                pattern++;
                continue;
            } else {
                return false;
            }
        } else if(*pattern == *name || *pattern == '?') {
            name++;
            if(*pattern != '*')
                pattern++;
            continue;
        } else {
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
        } else if(*pattern == *name || *pattern == '?') {
            name++;
            if(*pattern != '*')
                pattern++;
            continue;
        } else {
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
    uint64_t size();//in bytes
    uint64_t tell();//in bytes
    uint8_t write(uint32_t record,uint8_t const *buf);
    uint8_t read(uint32_t record,uint8_t *buf);
};

DirFCB::DirFCB(char const *filename, bool create) {
    if(create) {
        st.open(filename, std::ios::out);
        st.close();
    }
    st.open(filename, std::ios::in|std::ios::out);
    if(!st.good()) {
        if(create)
            throw BDOSError(BDOS_WRITE_ERROR);
        else
            throw BDOSError(BDOS_FILE_NOT_FOUND);
    }
}

uint64_t DirFCB::size() {
    uint64_t pos = st.tellg();
    st.seekg(0,std::ios::end);
    uint64_t sz = st.tellg();
    if(sz > 0xffffff*128)
        sz = 0xffffff*128;
    st.seekg(pos,std::ios::beg);
    return sz;
}

uint64_t DirFCB::tell() {
    uint64_t pos = st.tellg();
    if(pos > 0xffffff*128)
        pos = 0xffffff*128;
    return pos;
}

uint8_t DirFCB::write(uint32_t record,uint8_t const *buf) {
    st.seekp(record*128,std::ios::beg);
    st.write((char *)buf,128);
    if(!st.good())
        return BDOS_WRITE_ERROR;
    return BDOS_OK;
}

uint8_t DirFCB::read(uint32_t record,uint8_t *buf) {
    st.seekg(record*128,std::ios::beg);
    st.read((char *)buf,128);
    if(!st.good())
        return BDOS_READ_ERROR;
    return BDOS_OK;
}

TF20DriveDirectory::TF20DriveDirectory(std::string const &base_dir)
    : base_dir(base_dir) {
}

TF20DriveDirectory::~TF20DriveDirectory() =default;

void TF20DriveDirectory::reset() {
}

void *TF20DriveDirectory::file_open(uint8_t us, uint8_t const *filename, uint8_t extent) {
    //ios::nocreate and ios::noreplace
    std::string unixfilename = hx20ToUnixFilename(filename);
    try {
        return new DirFCB((base_dir + "/" + unixfilename).c_str(), false);;
    } catch(BDOSError const &e) {
    }
    return new DirFCB((base_dir + "/." + unixfilename).c_str(), false);;
}

void TF20DriveDirectory::file_close(void *_fcb) {
    DirFCB *fcb = reinterpret_cast<DirFCB *>(_fcb);
    delete fcb;
}

void TF20DriveDirectory::file_find_first(uint8_t us, uint8_t const *pattern, uint8_t extent, void *dir_entry, std::string &filename) {
    uint8_t *obuf = (uint8_t *)dir_entry;
    std::string unixpattern = hx20ToUnixFilename(pattern);
    dirSearch.reset(new DirSearch("disk", unixpattern));
    if(!dirSearch->good()) {
        throw BDOSError(BDOS_READ_ERROR);
    }

    uint8_t res = dirSearch->findNext(obuf);
    if(res == BDOS_OK)
        filename = dirSearch->filename;
    else
        throw BDOSError(res);
}

void TF20DriveDirectory::file_find_next(void *dir_entry, std::string &filename) {
    uint8_t *obuf = (uint8_t *)dir_entry;
    uint8_t res = dirSearch->findNext(obuf);
    if(res == BDOS_OK)
        filename = dirSearch->filename;
    else
        throw BDOSError(res);
}

void TF20DriveDirectory::file_remove(uint8_t us, uint8_t const *filename, uint8_t extent) {
    std::string unixfilename = hx20ToUnixFilename(filename);

    if(unlink((base_dir+ "/" +unixfilename).c_str()) == -1) {
        if(errno == ENOENT) {
            if(unlink((base_dir+ "/." +unixfilename).c_str()) == -1) {
                if(errno == ENOENT)
                    throw BDOSError(BDOS_FILE_NOT_FOUND);
                else if(errno == EROFS)
                    throw BDOSError(BDOS_DISK_WRITE_PROTECT_ERROR);
                else
                    throw BDOSError(BDOS_WRITE_ERROR);
            }
        } else if(errno == EROFS)
            throw BDOSError(BDOS_DISK_WRITE_PROTECT_ERROR);
        else
            throw BDOSError(BDOS_WRITE_ERROR);
    }
}

void *TF20DriveDirectory::file_create(uint8_t us, uint8_t const *filename, uint8_t extent) {
    //ios::nocreate and ios::noreplace
    std::string unixfilename = hx20ToUnixFilename(filename);

    try {
        return new DirFCB((base_dir + "/" + unixfilename).c_str(), false);
    } catch(BDOSError const &e) {
    }
    try {
        return new DirFCB((base_dir + "/." + unixfilename).c_str(), false);;
    } catch(BDOSError const &e) {
    }
    return new DirFCB((base_dir + "/" + unixfilename).c_str(), true);
}

void TF20DriveDirectory::file_rename(uint8_t old_us, uint8_t const *old_filename, uint8_t old_extent,
                                     uint8_t new_us, uint8_t const *new_filename, uint8_t new_extent) {
    std::string unixfilename_old = hx20ToUnixFilename(old_filename);
    std::string unixfilename_new = hx20ToUnixFilename(new_filename);

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
                    throw BDOSError(BDOS_FILE_NOT_FOUND);
                else if(errno == EROFS)
                    throw BDOSError(BDOS_DISK_WRITE_PROTECT_ERROR);
                else
                    throw BDOSError(BDOS_WRITE_ERROR);
            }
        } else if(errno == EROFS)
            throw BDOSError(BDOS_DISK_WRITE_PROTECT_ERROR);
        else
            throw BDOSError(BDOS_WRITE_ERROR);
    }
}

void TF20DriveDirectory::file_read(void *_fcb, uint32_t record,
                                   uint8_t &cur_extent, uint8_t &cur_record, void *buffer) {
    DirFCB *fcb = reinterpret_cast<DirFCB *>(_fcb);
    cur_extent = (record >> 7) & 0x1f;
    cur_record = record & 0x7f;
    uint8_t res = fcb->read(record, (uint8_t *)buffer);
    if(res != BDOS_OK)
        throw BDOSError(res);
}

void TF20DriveDirectory::file_write(void *_fcb, void const *buffer, uint32_t record,
                                    uint8_t &cur_extent, uint8_t &cur_record) {
    DirFCB *fcb = reinterpret_cast<DirFCB *>(_fcb);
    cur_extent = (record >> 7) & 0x1f;
    cur_record = record & 0x7f;
    uint8_t res = fcb->write(record, (uint8_t const *)buffer);
    if(res != BDOS_OK)
        throw BDOSError(res);
}

void TF20DriveDirectory::file_size(void *_fcb, uint8_t &extent,
                                   uint8_t &record, uint32_t &records) {
    DirFCB *fcb = reinterpret_cast<DirFCB *>(_fcb);
    uint64_t sz = fcb->size();
    records = (sz+127)/128;
    if(records > 0xfff) {
        extent = 0x1f;
        record = 0x80;
    } else {
        extent = (records >> 7) & 0x1f;
        record = records & 0x7f;
    }
}

void TF20DriveDirectory::file_tell(void *_fcb, uint32_t &records) {
    DirFCB *fcb = reinterpret_cast<DirFCB *>(_fcb);
    uint64_t pos = fcb->tell();
    records = (pos+127)/128;
}

void TF20DriveDirectory::disk_write(uint8_t track, uint8_t sector, void const *buffer) {
    if(track >= 4 || sector >= 64)
        throw BDOSError(BDOS_WRITE_ERROR);
    //.boot is not a valid name for any file, they must have \.?[^.]{0,8}\.[^.]{0,3}
    std::fstream st((base_dir + "/.boot").c_str(), std::ios::out);
    if(!st.good()) {
        throw BDOSError(BDOS_WRITE_ERROR);
    }
    st.seekp(track * 8192 + sector * 128);
    if(!st.good()) {
        throw BDOSError(BDOS_WRITE_ERROR);
    }
    st.write((char const *)buffer, 128);
    if(!st.good()) {
        throw BDOSError(BDOS_WRITE_ERROR);
    }
}

void TF20DriveDirectory::disk_format(uint8_t track) {
    throw BDOSError(BDOS_WRITE_ERROR);
}

void TF20DriveDirectory::disk_size(uint8_t &clusters) {
    clusters = 255;
}

void TF20DriveDirectory::disk_read(uint8_t track, uint8_t sector, void *buffer)  {
    if(track >= 4 || sector >= 64)
        throw BDOSError(BDOS_READ_ERROR);
    //.boot is not a valid name for any file, they must have \.?[^.]{0,8}\.[^.]{0,3}
    std::fstream st((base_dir + "/.boot").c_str(), std::ios::in);
    if(!st.good()) {
        throw BDOSError(BDOS_READ_ERROR);
    }
    st.seekg(track * 8192 + sector * 128);
    if(!st.good()) {
        throw BDOSError(BDOS_READ_ERROR);
    }
    st.read((char *)buffer, 128);
    if(!st.good()) {
        throw BDOSError(BDOS_READ_ERROR);
    }
}

