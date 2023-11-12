
#pragma once

#include <iostream>
#include <vector>
#include <set>
#include <memory>

namespace TeleDiskParser {

class Disk;

class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string &__arg) : std::runtime_error(__arg) {}
    explicit FormatError(const char *__arg) : std::runtime_error(__arg) {}
    FormatError(FormatError &&oth) noexcept : std::runtime_error(oth) {}
    FormatError(const FormatError &) noexcept = default;
};

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
    SectorFlags flags;
    std::vector<char> data;
public:
    Sector(std::basic_istream<char> &s);
    Sector(Sector const &s);
    Sector();
    void write(std::basic_ostream<char> &s, bool no_compress = false);
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
    Track();
    void write(std::basic_ostream<char> &s, bool no_compress_sectors = false);
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
    Comment();
    void write(std::basic_ostream<char> &s);
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
    std::unique_ptr<Comment> comment;
    bool dosMode;
    unsigned int mediaSurfaces;

    std::vector<Track> tracks;

    CHS min;
    CHS max;
    std::set<int> cylinder_ids;
    std::set<int> side_ids;
    std::set<int> sector_ids;
    std::set<int> length_ids;
    bool advancedCompression;
    bool no_compress_sectors;
private:
    void readDiskMain(bool have_comment, std::basic_istream<char> &s);
    void writeDiskMain(std::basic_ostream<char> &s);
    void readDisk(std::basic_istream<char> &s);
    void writeDisk(std::basic_ostream<char> &s);
public:
    Disk(const char *filename);
    Disk(std::basic_istream<char> &s);
    Disk();
    void write(const char *filename);
    void write(std::basic_ostream<char> &s);
    Sector *findSector(CHS const &chs);
    Track *findTrack(unsigned int physCylinder, unsigned int physSide, bool create=false);
};
}

