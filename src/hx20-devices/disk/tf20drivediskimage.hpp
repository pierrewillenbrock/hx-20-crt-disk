
#pragma once

#include "tf20-adapters.hpp"

class TF20DriveDiskImage : public TF20DriveInterface {
private:
    std::unique_ptr<ImgSearch> dirSearch;
    std::unique_ptr<DiskDriveInterface> drive;
public:
    TF20DriveDiskImage(std::string const &file);
    virtual ~TF20DriveDiskImage() override;
    //0x0e
    virtual void reset() override;
    //0x0f
    virtual void *file_open(uint8_t us, uint8_t const *filename, uint8_t extent) override;
    //0x10
    virtual void file_close(void *fcb) override;
    //0x11
    virtual uint8_t file_find_first(uint8_t us, uint8_t const *pattern, uint8_t extent, void *dir_entry, std::string &filename) override;
    //0x12
    virtual uint8_t file_find_next(void *dir_entry, std::string &filename) override;
    //0x13
    virtual uint8_t file_remove(uint8_t us, uint8_t const *filename, uint8_t extent) override;
    //0x16
    virtual void *file_create(uint8_t us, uint8_t const *filename, uint8_t extent) override;
    //0x17 //this may support "rename" between drives!
    virtual uint8_t file_rename(uint8_t old_us, uint8_t const *old_filename, uint8_t old_extent,
                             uint8_t new_us, uint8_t const *new_filename, uint8_t new_extent) override;
    //0x21
    virtual uint8_t file_read(void *fcb, uint32_t record,
                              uint8_t &cur_extent, uint16_t &cur_record, void *buffer) override;
    //0x22
    virtual uint8_t file_write(void *fcb, void const *buffer, uint32_t record,
                               uint8_t &cur_extent, uint16_t &cur_record) override;
    //0x23
    virtual uint8_t file_size(void *fcb, uint8_t &extent,
                              uint16_t &record, uint32_t &records) override;
    //0x24
    virtual uint8_t file_tell(void *fcb, uint32_t &records) override;
    //0x7b
    virtual uint8_t disk_write(uint8_t track, uint8_t sector, void const *buffer) override;
    //0x7c
    virtual uint8_t disk_format(uint8_t track) override;
    //0x7e
    virtual uint8_t disk_size(uint8_t &clusters) override;
    //0x7f
    virtual uint8_t disk_read(uint8_t track, uint8_t sector, void *buffer)  override;
};

