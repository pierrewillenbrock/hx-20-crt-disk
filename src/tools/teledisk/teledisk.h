
#pragma once

struct FileHeader {
    char ID[2];//TD or td(for "advanced compression")
    unsigned char volSeq;//volume number of this file in the sequence
    unsigned char checkSig;//unique in multi file sequence
    unsigned char version;
    unsigned char sourceDensity;
    unsigned char driveType;
    unsigned char trackDensity;//bit 7 determines whether there is comment
    unsigned char DOSMode;
    unsigned char mediaSurfaces;
    unsigned short crc;
} __attribute__((packed));

struct CommentHeader {//optional, determined by bit 7 of trackDensity above
    unsigned short crc;
    unsigned short size;
    unsigned char year;
    unsigned char month;
    unsigned char day;
    unsigned char hour;
    unsigned char minute;
    unsigned char second;
} __attribute__((packed));

struct TrackHeader {
    unsigned char sectorCount;
    unsigned char physCylinder;
    unsigned char physSide;
    unsigned char crc;
} __attribute__((packed));

struct SectorHeader {
    unsigned char idCylinder;
    unsigned char idSide;
    unsigned char idSector;
    unsigned char idLengthCode;//size=128*2^idLengthCode
    unsigned char flags;//bit0: dupe, bit1: data crc error, bit2:deleted mark, bit4: dos sector missing, bit5: data missing, id missing
    unsigned char crc;
} __attribute__((packed));

