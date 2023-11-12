
#pragma once

#include <stdint.h>
#include <string>
#include <memory>
#include <stdexcept>

class DiskDriveInterface;
class DirSearch;
class ImgSearch;

/* "bdos" error codes:
   fa: read error
   fb: write error
   fc: drive select error
   fd/fe: write protect error
*/

#define BDOS_OK 0x00
#define BDOS_READ_ERROR 0xfa
#define BDOS_WRITE_ERROR 0xfb
#define BDOS_SELECT_ERROR 0xfc
#define BDOS_DISK_WRITE_PROTECT_ERROR 0xfd //used when the directory could not be read
#define BDOS_FILE_WRITE_PROTECT_ERROR 0xfe //used when a file is read-only
#define BDOS_FILE_NOT_FOUND 0xff

class BDOSError : public std::runtime_error {
private:
    uint8_t bdos_error;
public:
    BDOSError(uint8_t bdos_error) : std::runtime_error("BDOS Error"), bdos_error(bdos_error) {}
    BDOSError(uint8_t bdos_error, const std::string& what_arg ) : std::runtime_error(what_arg), bdos_error(bdos_error) {}
    BDOSError(uint8_t bdos_error, const char *what_arg) : std::runtime_error(what_arg), bdos_error(bdos_error) {}
    BDOSError(const BDOSError &other) noexcept = default;
    uint8_t getBDOSError() const { return bdos_error; }
};

class TF20DriveInterface {
public:
    virtual ~TF20DriveInterface() =default;
    //0x0e
    virtual void reset() =0;
    //0x0f
    virtual void *file_open(uint8_t us, uint8_t const *filename, uint8_t extent) =0;
    //0x10
    virtual void file_close(void *fcb) =0;
    //0x11
    virtual void file_find_first(uint8_t us, uint8_t const *pattern, uint8_t extent, void *dir_entry, std::string &filename) =0;
    //0x12
    virtual void file_find_next(void *dir_entry, std::string &filename) =0;
    //0x13
    virtual void file_remove(uint8_t us, uint8_t const *filename, uint8_t extent) =0;
    //0x16
    virtual void *file_create(uint8_t us, uint8_t const *filename, uint8_t extent) =0;
    //0x17 //this may support "rename" between drives!
    virtual void file_rename(uint8_t old_us, uint8_t const *old_filename, uint8_t old_extent,
                             uint8_t new_us, uint8_t const *new_filename, uint8_t new_extent) =0;
    //0x21
    virtual void file_read(void *fcb, uint32_t record,
                           uint8_t &cur_extent, uint8_t &cur_record, void *buffer) =0;
    //0x22
    virtual void file_write(void *fcb, void const *buffer, uint32_t record,
                            uint8_t &cur_extent, uint8_t &cur_record) =0;
    //0x23
    virtual void file_size(void *fcb, uint8_t &extent,
                              uint8_t &record, uint32_t &records) =0;
    //0x24
    virtual void file_tell(void *fcb, uint32_t &records) =0;
    //0x7b
    virtual void disk_write(uint8_t track, uint8_t sector, void const *buffer) =0;
    //0x7c
    virtual void disk_format(uint8_t track) =0;
    //0x7e
    virtual void disk_size(uint8_t &clusters) =0;
    //0x7f
    virtual void disk_read(uint8_t track, uint8_t sector, void *buffer) =0;
};

std::string hx20ToUnixFilename(uint8_t const *src);
