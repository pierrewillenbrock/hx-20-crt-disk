
#include "disk-drive-adapters.hpp"
#include "../../tools/teledisk/parser.hpp"
#include <string.h>
#include <QFileInfo>

TelediskImageDrive::TelediskImageDrive(std::string const &filename)
    : filename(filename) {
    QFileInfo fi(filename.c_str());
    if(fi.exists()) {
        diskimage = std::make_unique<TeleDiskParser::Disk>(filename.c_str());
    } else {
        diskimage = std::make_unique<TeleDiskParser::Disk>();
        diskimage->advancedCompression = true;
        diskimage->write(filename.c_str());
    }
}

TelediskImageDrive::~TelediskImageDrive() {
}

void TelediskImageDrive::reset() {
}

bool TelediskImageDrive::write(CHS const &chs,
                               void const *buffer, uint8_t sector_size_code) {
    TeleDiskParser::CHS tdchs(chs.idCylinder, chs.idSide, chs.idSector);
    auto sector = diskimage->findSector(tdchs);
    if(!sector)
        return false;
    if(sector->idLengthCode > sector_size_code)
        return false;
    if(sector->data.size() > 128U << sector_size_code)
        memcpy(sector->data.data(), buffer, 128 << sector_size_code);
    else
        memcpy(sector->data.data(), buffer, sector->data.size());
    diskimage->write(filename.c_str());
    return true;
}

bool TelediskImageDrive::format(uint8_t track, uint8_t head,
                                uint8_t num_sectors, uint8_t sector_size_code) {
    //clear out all sectors belonging to this head
    TeleDiskParser::Track *t = diskimage->findTrack(track, head, true);
    if(!t)
        return false;
    for(int i = 0; i < num_sectors; i++) {
        TeleDiskParser::Sector s;
        s.chs.idCylinder = track;
        s.chs.idSide = head;
        s.chs.idSector = i+1;
        s.idLengthCode = sector_size_code;
        s.flags = TeleDiskParser::Sector::SectorFlags(0);
        s.data.resize(128 << sector_size_code);
        memset(s.data.data(), 0xe5, s.data.size());
        t->sectors.push_back(s);
    }
    diskimage->write(filename.c_str());
    return true;
}

bool TelediskImageDrive::size(CHS &chs) {
    auto tdmax = diskimage->max;
    auto tdmin = diskimage->min;
    chs.idCylinder = tdmax.idCylinder-tdmin.idCylinder+1;
    chs.idSector = tdmax.idSector-tdmin.idSector+1;
    chs.idSide = tdmax.idSide-tdmin.idSide+1;
    return true;
}

bool TelediskImageDrive::read(CHS const &chs, void *buffer,
                              uint8_t sector_size_code) {
    TeleDiskParser::CHS tdchs(chs.idCylinder, chs.idSide, chs.idSector);
    auto sector = diskimage->findSector(tdchs);
    if(!sector)
        return false;
    if(sector->idLengthCode < sector_size_code)
        return false;
    if(sector->data.size() > 128U << sector_size_code)
        memcpy(buffer, sector->data.data(), 128 << sector_size_code);
    else
        memcpy(buffer, sector->data.data(), sector->data.size());
    return true;
}

bool TelediskImageDrive::size(CHS const &chs, uint8_t &sector_size_code) {
    TeleDiskParser::CHS tdchs(chs.idCylinder, chs.idSide, chs.idSector);
    auto sector = diskimage->findSector(tdchs);
    if(!sector)
        return false;
    sector_size_code = sector->idLengthCode;
    return true;
}

RawImageDrive::RawImageDrive(std::string const &filename)
    : filename(filename) {
    QFileInfo fi(filename.c_str());
    if(!fi.exists()) {
        file.open(filename, std::ios_base::binary | std::ios_base::in |
                  std::ios_base::out | std::ios_base::trunc);
    } else {
        file.open(filename, std::ios_base::binary | std::ios_base::in |
                  std::ios_base::out);
    }
    file.seekp(0, std::ios_base::seekdir::_S_end);
    file_size = file.tellp();
}

RawImageDrive::~RawImageDrive() {
}

void RawImageDrive::reset() {
}

bool RawImageDrive::write(CHS const &chs,
                          void const *buffer, uint8_t sector_size_code) {
    ssize_t pos = 256*(chs.idSector-1 + 16*(chs.idSide + 2*chs.idCylinder));
    while(file_size < pos) {
        file.seekp(file_size);
        std::vector<char> buf;
        buf.resize(256);
        memset(buf.data(), 0xe5, buf.size());
        file.write(buf.data(), buf.size());
        file_size += buf.size();
    }
    file.seekp(pos);
    file.write(static_cast<char const *>(buffer), 256);
    if(file_size < file.tellp())
        file_size = file.tellp();
    return true;
}

bool RawImageDrive::format(uint8_t track, uint8_t head,
                           uint8_t num_sectors, uint8_t sector_size_code) {
    ssize_t pos = 256*(16*(head + 2*track));
    std::vector<char> buf;
    while(file_size < pos) {
        file.seekp(file_size);
        buf.resize(256);
        memset(buf.data(), 0xe5, buf.size());
        file.write(buf.data(), buf.size());
        file_size += buf.size();
    }
    file.seekp(pos);
    buf.resize(256*16);
    memset(buf.data(), 0xe5, buf.size());
    file.write(buf.data(), buf.size());
    if(file_size < file.tellp())
        file_size = file.tellp();
    return true;
}

bool RawImageDrive::size(CHS &chs) {
    chs.idCylinder = 40;
    chs.idSector = 16;
    chs.idSide = 2;
    return true;
}

bool RawImageDrive::read(CHS const &chs, void *buffer,
                         uint8_t sector_size_code) {
    ssize_t pos = 256*(chs.idSector-1 + 16*(chs.idSide + 2*chs.idCylinder));
    while(file_size < pos+256) {
        memset(buffer, 0xe5, 256);
        return true;
    }
    file.seekg(pos);
    file.read(static_cast<char *>(buffer), 256);
    return true;
}

bool RawImageDrive::size(CHS const &chs, uint8_t &sector_size_code) {
    sector_size_code = 1;
    return true;
}

