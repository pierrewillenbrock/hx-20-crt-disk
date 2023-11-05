
#include "tf20drivediskimage.hpp"

#include "disk-drive-adapters.hpp"

#include <sstream>
#include <string.h>

static bool dirent_compare(uint8_t const *n1, uint8_t const *n2) {
    if(memcmp(n1,n2,12) != 0)
        return false;
    if(((n1[12] ^ n2[12]) & 0xfe) != 0)
        return false;
    if(n1[13] != n2[13])
        return false;
    if(n1[14] != n2[14])
        return false;
    return true;
}

static bool dirent_compare_ignore_position(uint8_t const *n1, uint8_t const *n2) {
    if(memcmp(n1,n2,12) != 0)
        return false;
    if(((n1[12] ^ n2[12]) & 0xe0) != 0)
        return false;
    if(n1[13] != n2[13])
        return false;
    if(((n1[14] ^ n2[14]) & 0xf0) != 0)
        return false;
    return true;
}

static bool dirent_compare(uint8_t const *ent, uint8_t p_us,
                           uint8_t const *pattern, uint8_t p_ext) {
    if(p_us != '?' && ent[0] != p_us) {
        printf("\n");
        return false;
    }
    for(int i = 0; i < 11; i++) {
        if((pattern[i] & 0x7f) != (ent[i+1] & 0x7f) &&
                pattern[i] != '?') {
            printf("\n");
            return false;
        }
    }
    if(p_ext != '?' && (ent[12] & 0xfe) != (p_ext & 0xfe)) {
        printf("\n");
        return false;
    }
    printf(" match\n");
    return true;
}

static bool dirent_match(uint8_t const *ent,
                         uint8_t p_us, uint8_t const *pattern, uint8_t p_ext) {
    printf("matching dirent %d %s %d against pattern %d %s %d...",
           int(ent[0]), hx20ToUnixFilename(ent+1).c_str(), int(ent[12]),
           int(p_us), hx20ToUnixFilename(pattern).c_str(), int(p_ext));
    if(p_us != '?' && ent[0] != p_us) {
        printf("\n");
        return false;
    }
    for(int i = 0; i < 11; i++) {
        if((pattern[i] & 0x7f) != (ent[i+1] & 0x7f) &&
                pattern[i] != '?') {
            printf("\n");
            return false;
        }
    }
    if(p_ext != '?' && (ent[12] & 0xfe) != (p_ext & 0xfe)) {
        printf("\n");
        return false;
    }
    printf(" match\n");
    return true;
}

static bool read_dir_extent(DiskDriveInterface *drive, uint8_t extent, uint8_t *dir_ent) {
    uint8_t buf[256];
    printf("Read dirent, sector %d,%d,%d, offset +0x%x\n", 4, 0, (extent >> 3)+1, (extent & 0x7)*32);
    CHS chs(4,0,(extent >> 3)+1);
    if(!drive->read(chs, buf, 1)) {
        memset(buf, 0xe5, 256);
    }
    memcpy(dir_ent, buf+(extent & 0x7)*32, 32);
    return true;
}

static bool write_dir_extent(DiskDriveInterface *drive, uint8_t extent, uint8_t const *dir_ent) {
    uint8_t buf[256];
    printf("Read/write dirent, sector %d,%d,%d, offset +0x%x\n", 4, 0, (extent >> 3)+1, (extent & 0x7)*32);
    CHS chs(4,0,(extent >> 3)+1);
    if(!drive->read(chs, buf, 1)) {
        memset(buf, 0xe5, 256);
    }
    memcpy(buf+(extent & 0x7)*32, dir_ent, 32);
    if(!drive->write(chs, buf, 1))
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
    return (last_dir_ent[14] & 0x0f)*4096 + (last_dir_ent[12] & 0x1f) * 128 + last_dir_ent[15];
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
            if(!read_dir_extent(drive, i, dir_ent)) {
                printf("Could not read dir ent %d\n", i);
                return 0;
            }
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

static int find_free_dirent(DiskDriveInterface *drive, uint8_t *dir_ent) {
    for(uint8_t index = 0; index < 64; index++) {
        if(!read_dir_extent(drive, index, dir_ent))
            return -1;
        if(dir_ent[0] == 0xe5)
            return index;
    }
    return -1;
}

class ImgSearch {
private:
    DiskDriveInterface *drive;
    uint8_t pattern_us;
    uint8_t pattern_filenametype[11];
    uint8_t pattern_extent;
    uint8_t ent;
public:
    uint8_t const *filename;
    ImgSearch(DiskDriveInterface *drive, uint8_t pattern_us,
              uint8_t const *pattern, uint8_t pattern_extent);
    ~ImgSearch();
    uint8_t findNext(uint8_t *obuf);
};

ImgSearch::ImgSearch(DiskDriveInterface *drive, uint8_t pattern_us,
                     uint8_t const *pattern, uint8_t pattern_extent)
    : drive(drive), pattern_us(pattern_us), pattern_extent(pattern_extent), ent(0) {
        memcpy(this->pattern_filenametype, pattern, 11);
}

ImgSearch::~ImgSearch() = default;

uint8_t ImgSearch::findNext(uint8_t *obuf) {
    while(ent < 64) {
        read_dir_extent(drive, ent, obuf);
        ent++;
        if(dirent_match(obuf, pattern_us, pattern_filenametype, pattern_extent)) {
            //no need to filter any entries.
            return 0;
        }
    }
    return BDOS_FILE_NOT_FOUND;
}

class ImgFCB {
private:
    DiskDriveInterface *drive;
    uint8_t dirent[15];
    int last_ent;
    int position_records;
public:
    ImgFCB(DiskDriveInterface *drive, uint8_t us, uint8_t const *filename,
           uint8_t extent, bool create);
    bool good();
    uint64_t size();
    uint64_t tell();
    uint8_t write(uint32_t record,uint8_t const *buf);
    uint8_t read(uint32_t record,uint8_t *buf);
    void close();
};

ImgFCB::ImgFCB(DiskDriveInterface *drive, uint8_t us, uint8_t const *filename,
               uint8_t extent, bool create)
    : drive(drive), last_ent(-1), position_records(0) {
    unsigned int max_recs = 0;
    for(int i = 0; i < 64; i++) {
        uint8_t dir_ent[32];
        if(!read_dir_extent(drive, i, dir_ent)) {
            printf("Cannot read extent %d\n", i);
            return;
        }
        if(dir_ent[0] != 0)
            continue;
        printf("filename %s\n", hx20ToUnixFilename(dir_ent+1).c_str());
        if(dirent_compare(dir_ent, us, filename, extent)) {
            printf("Found file in extent %d\n", i);
            //found one.
            if((dir_ent[0xc] & 0x1e) == 0 && dir_ent[0xe] == 0) {
                memcpy(this->dirent, dir_ent, 15);
            }
            unsigned int recs = get_records_in_file(dir_ent);
            if(recs > max_recs) {
                last_ent = i;
                max_recs = recs;
            }
        }
    }
    if(create) {
        if(last_ent != -1) {
            last_ent = -1;
            printf("Create: file exists\n");
            return;
        }
        uint8_t dir_ent[32];
        last_ent = find_free_dirent(drive, dir_ent);
        if(last_ent != -1) {
            printf("Found empty extent %d\n", last_ent);
            memset(dir_ent, 0, 32);
            dir_ent[0] = us;
            memcpy(dir_ent+1, filename, 11);
            dir_ent[12] = extent & 0xf0;//the actual extent group number and extent number is 0.
            memcpy(this->dirent, dir_ent, 15);
            if(!write_dir_extent(drive, last_ent, dir_ent))
                return;
        }
        printf("Create: cannot find empty extent\n");
    }
}

bool ImgFCB::good() {
    //basically file found/created or not.
    return last_ent != -1;
}

uint64_t ImgFCB::size() {
    uint8_t dir_ent[32];
    if(!read_dir_extent(drive, last_ent, dir_ent)) {
        printf("Cannot read extent %d\n", last_ent);
        return BDOS_READ_ERROR;
    }
    return get_records_in_file(dir_ent)*128;
}

uint64_t ImgFCB::tell() {
    return position_records*128;
}

uint8_t ImgFCB::write(uint32_t record,uint8_t const *buf) {
    //first, check if the file is already large enough
    uint8_t dir_ent[32];
    if(!read_dir_extent(drive, last_ent, dir_ent)) {
        printf("Extent %d not found\n", last_ent);
        return BDOS_READ_ERROR;
    }
    unsigned int records_in_file = get_records_in_file(dir_ent);
    int ent;
    printf("Found %d records in file\n", records_in_file);
    if(records_in_file <= record) {
        //no, it is not.
        //check if we need to add more blocks
        if((records_in_file+15)/16 < (record+1+15)/16) {
            printf("For record %d, need to add new block\n", record);
            //yes, complete this one.
            records_in_file = (records_in_file+15) & ~0xf;
            set_records_in_dirent(dir_ent, records_in_file % 32768);

            //now add blocks until we have enough.
            int current_add_block = 0;
            while(records_in_file/16 < (record+1+15)/16) {
                //if this ent is already full, get a new one.
                if(records_in_file % 256 == 0 && records_in_file != 0) {
                    uint8_t last_extent_bits = dir_ent[12] & 0xfe;
                    if(!write_dir_extent(drive, last_ent, dir_ent)) {
                        printf("Could not write current dirent\n");
                        return BDOS_WRITE_ERROR;
                    }
                    int res = find_free_dirent(drive, dir_ent);
                    if(res == -1) {
                        printf("Could not find new free dirent\n");
                        return BDOS_WRITE_ERROR;
                    }
                    last_ent = res;
                    memset(dir_ent, 0, 32);
                    memcpy(dir_ent, this->dirent, 15);
                    dir_ent[12] = last_extent_bits+2;
                }
                int res = find_free_block_after(drive, current_add_block);
                current_add_block = res;
                if(res == 0) {
                    printf("Could not find new free block after %d\n", current_add_block);
                    return BDOS_READ_ERROR;
                }
                dir_ent[16+(records_in_file % 256) / 16] = res;
                records_in_file += 16;
            }
        }
        if((records_in_file+15)/16 >= (record+1+15)/16) {
            //ok, that is all blocks that are needed. fix the record count.
            records_in_file = record+1;
            set_records_in_dirent(dir_ent, records_in_file % 32768);
        }
        if(!write_dir_extent(drive, last_ent, dir_ent)) {
            printf("Could not write updated dir ent\n");
            return BDOS_WRITE_ERROR;
        }
        ent = last_ent;
    } else if (records_in_file / 256 != record / 256) {
        //find the correct extent
        this->dirent[12] = (this->dirent[12] & 0xe0) | ((record / 256) << 1);
        this->dirent[14] = (this->dirent[14] & 0xf0) | ((record / 4096));
        for(ent = 0; ent < 64; ent++) {
            if(!read_dir_extent(drive, ent, dir_ent)) {
                printf("Could not read dir ent\n");
                return BDOS_READ_ERROR;
            }
            if(dirent_compare(this->dirent, dir_ent))
                break;
        }
        if(ent == 64) {
            printf("Could not find dir ent\n");
            return BDOS_READ_ERROR;
        }
    }

    if(dir_ent[16+(record % 256)/16] == 0) {
        int res = find_free_block_after(drive, 0);
        if(res == -1) {
            printf("Could not find free block\n");
            return BDOS_READ_ERROR;
        }
        dir_ent[16+(record % 256)/16] = res;
        if(!write_dir_extent(drive, ent, dir_ent)) {
            printf("Could not write dir ent\n");
            return BDOS_WRITE_ERROR;
        }
    }
    int block = dir_ent[16+(record % 256)/16];
    //now get the sector for this
    uint8_t sector_data[256];
    CHS chs(4+(block >> 2),(block>>1)&1, ((block & 1) << 3 | ((record >> 1) & 7))+1);
    if(!drive->read(chs, sector_data, 1)) {
        printf("Could not read sector %d,%d,%d\n",chs.idCylinder,chs.idSide,chs.idSector);
        return BDOS_READ_ERROR;
    }
    memcpy(sector_data + 128*(record & 1), buf, 128);
    if(!drive->write(chs, sector_data, 1)) {
        printf("Could not write updated sector %d,%d,%d\n",chs.idCylinder,chs.idSide,chs.idSector);
        return BDOS_WRITE_ERROR;
    }
    position_records = record+1;

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
        this->dirent[12] = (this->dirent[12] & 0xe0) | ((record / 256) << 1);
        this->dirent[14] = (this->dirent[14] & 0xf0) | ((record / 4096));
        for(ent = 0; ent < 64; ent++) {
            if(!read_dir_extent(drive, ent, dir_ent)) {
                printf("Could not read dir ent\n");
                return BDOS_READ_ERROR;
            }
            if(dirent_compare(this->dirent, dir_ent))
                break;
        }
        if(ent == 64) {
            printf("Could not find dir ent\n");
            return BDOS_READ_ERROR;
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

    position_records = record+1;

    return 0;
}

void ImgFCB::close() {
}

TF20DriveDiskImage::TF20DriveDiskImage(std::string const &file) {
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

void *TF20DriveDiskImage::file_open(uint8_t us, uint8_t const *filename, uint8_t extent) {
    ImgFCB *fcb = new ImgFCB(drive.get(), us, filename, extent, false);
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

uint8_t TF20DriveDiskImage::file_find_first(uint8_t us, uint8_t const *pattern, uint8_t extent, void *dir_entry, std::string &filename) {
    dirSearch = std::make_unique<ImgSearch>(drive.get(), us, pattern, extent);

    uint8_t *obuf = (uint8_t*)dir_entry;
    uint8_t res = dirSearch->findNext(obuf);
    filename = hx20ToUnixFilename(obuf+1);
    return res;
}

uint8_t TF20DriveDiskImage::file_find_next(void *dir_entry, std::string &filename) {
    uint8_t *obuf = (uint8_t *)dir_entry;
    uint8_t res = dirSearch->findNext(obuf);
    filename = hx20ToUnixFilename(obuf+1);
    return res;
}

uint8_t TF20DriveDiskImage::file_remove(uint8_t us, uint8_t const *filename, uint8_t extent) {
    uint8_t dir_ent[32];
    uint8_t pattern[15] = {0};
    pattern[0] = us;
    memcpy(pattern+1, filename, 11);
    pattern[12] = extent;
    for(int i = 0; i < 64; i++) {
        if(!read_dir_extent(drive.get(), i, dir_ent))
            return BDOS_READ_ERROR;
        if(dir_ent[0] != 0)
            continue;
        if(dirent_compare_ignore_position(dir_ent, pattern)) {
            printf("Deleting file in entry %d\n", i);
            dir_ent[0] = 0xe5;
            if(!write_dir_extent(drive.get(), i, dir_ent))
                return BDOS_WRITE_ERROR;
        }
    }
    return 0;
}

void *TF20DriveDiskImage::file_create(uint8_t us, uint8_t const *filename, uint8_t extent) {
    ImgFCB *fcb = new ImgFCB(drive.get(), us, filename, extent, true);
    return fcb;
}

uint8_t TF20DriveDiskImage::file_rename(uint8_t old_us, uint8_t const *old_filename, uint8_t old_extent,
                 uint8_t new_us, uint8_t const *new_filename, uint8_t new_extent) {
    uint8_t dir_ent[32];
    uint8_t pattern[15] = {0};
    pattern[0] = old_us;
    memcpy(pattern+1, old_filename, 11);
    pattern[12] = old_extent;
    for(int i = 0; i < 64; i++) {
        if(!read_dir_extent(drive.get(), i, dir_ent))
            return BDOS_READ_ERROR;
        if(dir_ent[0] != 0)
            continue;
        if(dirent_compare_ignore_position(dir_ent, pattern)) {
            dir_ent[0] = new_us;
            memcpy(dir_ent+1, new_filename, 11);
            dir_ent[12] = (dir_ent[12] & 0x1f) | (new_extent & 0xe0);
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
    uint64_t sz = fcb->size();
    records = (sz+127)/128;
    if(records > 0xfff) {
        extent = 0x1f;
        record = 0x80;
    } else {
        extent = (records >> 7) & 0x1f;
        record = records & 0x7f;
    }
    return 0;
}

uint8_t TF20DriveDiskImage::file_tell(void *_fcb, uint32_t &records) {
    ImgFCB *fcb = reinterpret_cast<ImgFCB *>(_fcb);
    uint64_t sz = fcb->tell();
    records = (sz+127)/128;
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
    if(!drive->format(track, 0, 16, 1))
        return BDOS_WRITE_ERROR;
    if(!drive->format(track, 1, 16, 1))
        return BDOS_WRITE_ERROR;
    return 0;
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


