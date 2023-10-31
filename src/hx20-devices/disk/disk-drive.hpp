
#pragma once

#include <stdint.h>

struct CHS {
    unsigned int idCylinder;
    unsigned int idSide;
    unsigned int idSector;
    CHS() =default;
    CHS(CHS const &) =default;
    CHS &operator=(CHS const &) =default;
    bool operator==(CHS const &oth) const {
        return idCylinder == oth.idCylinder &&
               idSide == oth.idSide &&
               idSector == oth.idSector;
    }
    CHS(unsigned int idCylinder,
        unsigned int idSide,
        unsigned int idSector) :
        idCylinder(idCylinder), idSide(idSide), idSector(idSector) {}
    CHS advanceSHC(unsigned int sectors, CHS const &min, CHS const &max) const {
        CHS tmp = *this;
        tmp.idSector += sectors;
        tmp.idSide += (tmp.idSector-min.idSector) / (max.idSector - min.idSector+1);
        tmp.idSector = min.idSector + (tmp.idSector-min.idSector) % (max.idSector - min.idSector+1);
        tmp.idCylinder += (tmp.idSide-min.idSide) / (max.idSide - min.idSide+1);
        tmp.idSide = min.idSide + (tmp.idSide-min.idSide) % (max.idSide - min.idSide+1);
        return tmp;
    }
};

class DiskDriveInterface {
public:
    //sector_size_code: actual size: 128bytes << sector_size_code
    virtual ~DiskDriveInterface() =default;
    virtual void reset() =0;
    virtual bool write(CHS const &chs,
                       void const *buffer, uint8_t sector_size_code) =0;
    virtual bool format(uint8_t track, uint8_t head,
                        uint8_t num_sectors, uint8_t sector_size_code) =0;
    virtual bool size(CHS &chs) =0;
    virtual bool read(CHS const &chs, void *buffer,
                      uint8_t sector_size_code) =0;
    virtual bool size(CHS const &chs, uint8_t &sector_size_code) =0;
};

