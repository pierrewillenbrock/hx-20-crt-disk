
#pragma once

#include <iostream>
#include <vector>

namespace TeleDiskParser {

class Disk;

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

class Sector {
public:
    enum SectorFlags {
        DuplicateInSameTrack = 1,
        CRCerror = 2,
        DeletedMark = 4,
        DOSNotAllocated = 0x10,//no data available
        DataMissing = 0x20,//no data availabe
        IDMissing = 0x40,

        NoDataMask = DataMissing | DOSNotAllocated
    };
    CHS chs;
    unsigned int idLengthCode;
    unsigned int size;//2^idLengthCode * 128
    SectorFlags flags;
    char *data;
public:
    Sector(std::basic_istream<char> &s);
    Sector(Sector const &s);
    ~Sector();
};

class Track {
private:
    unsigned int sectorCount;
    friend class Disk;
public:
    unsigned int physCylinder;
    unsigned int physSide;
    std::vector<Sector> sectors;
public:
    Track(std::basic_istream<char> &s);
};

class Comment {
public:
    struct Timestamp {
        int year, month, day, hour, minute, second;
    };
    Timestamp timestamp;
    std::string comment;
public:
    Comment(std::basic_istream<char> &s);
};

class Disk {
public:
    enum Density {
        D250Kbps = 0,
        D300Kbps = 1,
        D500Kbps = 2,
        D250KbpsFM = 128,
        D300KbpsFM = 129,
        D500KbpsFM = 130,
    };
    enum DriveType {
        D360K = 1,
        D1_2M = 2,
        D720K = 3,
        D1_44M = 4,
    };
    enum TrackDensity {
        Single = 0,
        Double = 1,
        Quad = 2
    };

    Density sourceDensity;
    DriveType driveType;
    TrackDensity trackDensity;
    Comment *comment;
    bool dosMode;
    unsigned int mediaSurfaces;

    std::vector<Track> tracks;

    CHS min;
    CHS max;
private:
    void readDisk(std::basic_istream<char> &s);
public:
    Disk(const char *filename);
    Disk(std::basic_istream<char> &s);
    Sector *findSector(CHS const &chs);
};
}

