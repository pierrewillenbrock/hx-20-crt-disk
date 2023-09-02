
#pragma once

#include "disk-drive.hpp"
#include <string>
#include <memory>

namespace TeleDiskParser {
class Disk;
}

class TelediskImageDrive : public DiskDriveInterface {
private:
    std::unique_ptr<TeleDiskParser::Disk> diskimage;
    std::string filename;
public:
    TelediskImageDrive(std::string const &filename);
    virtual ~TelediskImageDrive() override;
    virtual void reset() override;
    virtual bool write(CHS const &chs,
                       void const *buffer, uint8_t sector_size_code) override;
    virtual bool format(uint8_t track, uint8_t head,
                        uint8_t num_sectors, uint8_t sector_size_code) override;
    virtual bool size(CHS &chs) override;
    virtual bool read(CHS const &chs, void *buffer,
                      uint8_t sector_size_code) override;
    virtual bool size(CHS const &chs, uint8_t &sector_size_code) override;
};
