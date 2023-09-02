
#include "disk-drive-adapters.hpp"
#include "tools/teledisk/parser.hpp"
#include <string.h>

TelediskImageDrive::TelediskImageDrive(std::string const &filename)
: filename(filename) {
    diskimage = std::make_unique<TeleDiskParser::Disk>(filename.c_str());
}

TelediskImageDrive::~TelediskImageDrive() =default;

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

