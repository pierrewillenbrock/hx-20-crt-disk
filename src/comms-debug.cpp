
#include "comms-debug.hpp"

#include <QTreeView>
#include <QVBoxLayout>
#include <QSplitter>
#include <unordered_map>

#define SOH 0x1
#define STX 0x2
#define ETX 0x3
#define EOT 0x4
#define ENQ 0x5
#define ACK 0x6
#define DLE 0x10
#define NAK 0x15
#define WAK DLE
#define PS 0x31

class Decoder {
public:
    typedef std::unordered_map<QString, QVariant> MetaData;
    virtual ~Decoder() =default;
    virtual void decodePacket(RawDecodePacketInfo &basePacket, DecodeLocation const &loc, MetaData const &md = MetaData()) {}
};

class DecoderFactory {
public:
    virtual ~DecoderFactory() =default;
    virtual Decoder *create() =0;
};

static bool decodeUint8(DecodeLocation &locptr, DecodeResult &dr, QString const &name, int offset = 0) {
    if(locptr.size() >= 1) {
        DecodeResult dr2;
        dr2.location = locptr;
        dr2.location.end = dr2.location.begin+1;
        dr2.name = name;
        dr2.value = QString("%1").arg(locptr.at(0) + offset);
        dr.subdecodes.push_back(dr2);
        locptr.begin ++;
        return true;
    }
    locptr.begin = locptr.end;
    return false;
}

static bool decodeUint8Hex(DecodeLocation &locptr, DecodeResult &dr, QString const &name, int offset = 0) {
    if(locptr.size() >= 1) {
        DecodeResult dr2;
        dr2.location = locptr;
        dr2.location.end = dr2.location.begin+1;
        dr2.name = name;
        dr2.value = QString("0x%1").arg(locptr.at(0) + offset,2,16,QChar('0'));
        dr.subdecodes.push_back(dr2);
        locptr.begin ++;
        return true;
    }
    locptr.begin = locptr.end;
    return false;
}

static bool decodeUint8Indexed(DecodeLocation &locptr, DecodeResult &dr, QString const &name, std::unordered_map<uint8_t, QString> const &map) {
    if(locptr.size() >= 1) {
        DecodeResult dr2;
        dr2.location = locptr;
        dr2.location.end = dr2.location.begin+1;
        dr2.name = name;
        auto it = map.find(locptr.at(0));
        if(it == map.end())
            dr2.value = QString("(Unknown 0x%1)").arg(locptr.at(0),2,16,QChar('0'));
        else
            dr2.value = QString("%1 (0x%2)").arg(it->second).
                        arg(locptr.at(0),2,16,QChar('0'));
        dr.subdecodes.push_back(dr2);
        locptr.begin ++;
        return true;
    }
    locptr.begin = locptr.end;
    return false;
}

static bool decodeUint16(DecodeLocation &locptr, DecodeResult &dr, QString const &name, int offset = 0) {
    if(locptr.size() >= 2) {
        DecodeResult dr2;
        dr2.location = locptr;
        dr2.location.end = dr2.location.begin+2;
        dr2.name = name;
        dr2.value = QString("%1").arg(((locptr.at(0) << 8) | locptr.at(1)) + offset);
        dr.subdecodes.push_back(dr2);
        locptr.begin += 2;
        return true;
    }
    locptr.begin = locptr.end;
    return false;
}

static bool decodeUint16Hex(DecodeLocation &locptr, DecodeResult &dr, QString const &name, int offset = 0) {
    if(locptr.size() >= 2) {
        DecodeResult dr2;
        dr2.location = locptr;
        dr2.location.end = dr2.location.begin+2;
        dr2.name = name;
        dr2.value = QString("0x%1").arg(((locptr.at(0) << 8) | locptr.at(1)) + offset,4,16,QChar('0'));
        dr.subdecodes.push_back(dr2);
        locptr.begin += 2;
        return true;
    }
    locptr.begin = locptr.end;
    return false;
}

static bool decodeAsciiArray(DecodeLocation &locptr, DecodeResult &dr, QString const &name, size_t size, uint8_t mask = 0xff) {
    if(locptr.size() >= size) {
        DecodeResult dr2;
        dr2.location = locptr;
        dr2.location.end = dr2.location.begin+size;
        dr2.name = name;
        dr2.value = "";
        for(unsigned int i = 0; i < size; i++) {
            dr2.value += QChar(locptr.at(i) & mask);
        }
        dr.subdecodes.push_back(dr2);
        locptr.begin += size;
        return true;
    }
    locptr.begin = locptr.end;
    return false;
}

static std::unordered_map<uint8_t, QString> bdos_result_map({
    {0x00, "BDOS_OK"},
    {0xfa, "BDOS_READ_ERROR"},
    {0xfb, "BDOS_WRITE_ERROR"},
    {0xfc, "BDOS_SELECT_ERROR"},
    {0xfd, "BDOS_WRITE_PROTECT_ERROR"},
    {0xfe, "BDOS_WRITE_PROTECT_ERROR"},
    {0xff, "BDOS_FILE_NOT_FOUND"}});

class DiskDecoder : public Decoder {
private:
public:
    DiskDecoder() {}
    virtual void decodePacket(RawDecodePacketInfo &basePacket,
                              DecodeLocation const &loc,
                              MetaData const &md) override {
        if(md.find("fnc") == md.end())
            return;
        if(md.find("did") == md.end())
            return;
        bool good;
        int fnc = md.find("fnc")->second.toInt(&good);
        if(!good)
            return;
        int did = md.find("did")->second.toInt(&good);
        if(!good)
            return;
        DecodeResult dr;
        dr.name = QString("Disk Device %1").arg(did-0x31+1);
        dr.location = loc;
        auto locptr = loc;
        switch(fnc) {
        case 0x0e: { //reset
            dr.value = "Reset";
            if(basePacket.dir == RawDecodePacketInfo::SlaveToMaster) {
                if(!decodeUint8Hex(locptr, dr, "Result")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": Reset";
            break;
        }
        case 0x0f: //file open
        case 0x16: { //file create (those two are very similar on unix)
            if(fnc == 0x0f)
                dr.value = "File Open";
            else
                dr.value = "File Create";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(locptr.size() >= 3)
                    dr.name = QString("Disk Drive %1:").arg(QChar('A' + 2*(did-0x31) + locptr.at(2)-1));

                decodeUint16Hex(locptr, dr, "Master FCB Address");
                decodeUint8(locptr, dr, "Drive Code");
                decodeAsciiArray(locptr, dr, "File Name", 8);
                decodeAsciiArray(locptr, dr, "File Type", 3, 0x7f);

                if(!decodeUint8(locptr, dr, "Extent")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                if(!decodeUint8Indexed(locptr, dr, "Result",bdos_result_map)) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x10: {
            dr.value = "File Close";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(!decodeUint16Hex(locptr, dr, "Master FCB Address")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                if(!decodeUint8Indexed(locptr, dr, "Result",bdos_result_map)) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x11: { //findfirst
            dr.value = "File Find First";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(locptr.size() >= 1)
                    dr.name = QString("Disk Drive %1:").arg(QChar('A' + 2*(did-0x31) + locptr.at(0)-1));

                decodeUint8(locptr, dr, "Drive Code");
                decodeAsciiArray(locptr, dr, "File Name Pattern", 8);
                decodeAsciiArray(locptr, dr, "File Type Pattern", 3, 0x7f);

                if(!decodeUint8(locptr, dr, "Extent")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                decodeUint8Indexed(locptr, dr, "Result",bdos_result_map);

                DecodeResult dr2;
                dr2.location = locptr;
                dr2.name = "FCB";
                if(dr2.location.end > dr2.location.begin + 32)
                    dr2.location.end = dr2.location.begin + 32;
                decodeUint8Hex(locptr, dr2, "Valid");
                decodeAsciiArray(locptr, dr2, "File Name", 8);
                if(locptr.size() >= 1) {
                    DecodeResult dr3;
                    dr3.location = locptr;
                    dr3.location.begin = dr3.location.begin+0;
                    dr3.location.end = dr3.location.begin+1;
                    dr3.name = "Read Only";
                    dr3.value = QString("%1... ...").
                                arg((locptr.at(0) >> 7) & 1);
                    dr2.subdecodes.push_back(dr3);
                }
                if(locptr.size() >= 2) {
                    DecodeResult dr3;
                    dr3.location = locptr;
                    dr3.location.begin = dr3.location.begin+1;
                    dr3.location.end = dr3.location.begin+1;
                    dr3.name = "System";
                    dr3.value = QString("%1... ...").
                                arg((locptr.at(1) >> 7) & 1);
                    dr2.subdecodes.push_back(dr3);
                }
                if(locptr.size() >= 3) {
                    DecodeResult dr3;
                    dr3.location = locptr;
                    dr3.location.begin = dr3.location.begin+2;
                    dr3.location.end = dr3.location.begin+1;
                    dr3.name = "Unused";
                    dr3.value = QString("%1... ...").
                                arg((locptr.at(2) >> 7) & 1);
                    dr2.subdecodes.push_back(dr3);
                }
                decodeAsciiArray(locptr, dr2, "File Type", 3, 0x7f);
                uint8_t extent_code = 0;
                if(locptr.size() >= 1) {
                    DecodeResult dr3;
                    dr3.location = locptr;
                    dr3.location.end = dr3.location.begin+1;
                    dr3.name = "Extent";
                    extent_code = locptr.at(0);
                    dr3.value = QString("0x%1").
                                arg((locptr.at(0) >> 1) & 0x7f, 2, 16, QChar('0'));
                    dr2.subdecodes.push_back(dr3);
                    locptr.begin++;
                }
                decodeUint8Hex(locptr, dr2, "Unused");
                decodeUint8Hex(locptr, dr2, "Unused");
                if(locptr.size() >= 1) {
                    DecodeResult dr3;
                    dr3.location = locptr;
                    dr3.location.end = dr3.location.begin+1;
                    dr3.name = "Records";
                    extent_code = locptr.at(0);
                    dr3.value = QString("%1").
                                arg(locptr.at(0) + 0x80 * (extent_code & 1));
                    dr2.subdecodes.push_back(dr3);
                    locptr.begin++;
                }
                bool incomplete = false;
                if(locptr.size() >= 1) {
                    DecodeResult dr3;
                    dr3.name = "Blocks";
                    dr3.location = locptr;
                    if(dr3.location.end > dr3.location.begin+16)
                        dr3.location.end = dr3.location.begin+16;
                    for(int i = 0; i < 16; i++) {
                        if(!decodeUint8Hex(locptr, dr3, "Block"))
                            incomplete = true;
                    }
                    dr2.subdecodes.push_back(dr3);
                }
                dr.subdecodes.push_back(dr2);
                if(incomplete) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x12: { //find next
            dr.value = "File Find Next";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(!decodeUint8(locptr, dr, "Unused")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                decodeUint8Indexed(locptr, dr, "Result",bdos_result_map);

                DecodeResult dr2;
                dr2.location = locptr;
                dr2.name = "FCB";
                if(dr2.location.end > dr2.location.begin + 32)
                    dr2.location.end = dr2.location.begin + 32;
                decodeUint8Hex(locptr, dr2, "Valid");
                decodeAsciiArray(locptr, dr2, "File Name", 8);
                if(locptr.size() >= 1) {
                    DecodeResult dr3;
                    dr3.location = locptr;
                    dr3.location.begin = dr3.location.begin+0;
                    dr3.location.end = dr3.location.begin+1;
                    dr3.name = "Read Only";
                    dr3.value = QString("%1... ...").
                                arg((locptr.at(0) >> 7) & 1);
                    dr2.subdecodes.push_back(dr3);
                }
                if(locptr.size() >= 2) {
                    DecodeResult dr3;
                    dr3.location = locptr;
                    dr3.location.begin = dr3.location.begin+1;
                    dr3.location.end = dr3.location.begin+1;
                    dr3.name = "System";
                    dr3.value = QString("%1... ...").
                                arg((locptr.at(1) >> 7) & 1);
                    dr2.subdecodes.push_back(dr3);
                }
                if(locptr.size() >= 3) {
                    DecodeResult dr3;
                    dr3.location = locptr;
                    dr3.location.begin = dr3.location.begin+2;
                    dr3.location.end = dr3.location.begin+1;
                    dr3.name = "Unused";
                    dr3.value = QString("%1... ...").
                                arg((locptr.at(2) >> 7) & 1);
                    dr2.subdecodes.push_back(dr3);
                }
                decodeAsciiArray(locptr, dr2, "File Type", 3, 0x7f);
                uint8_t extent_code = 0;
                if(locptr.size() >= 1) {
                    DecodeResult dr3;
                    dr3.location = locptr;
                    dr3.location.end = dr3.location.begin+1;
                    dr3.name = "Extent";
                    extent_code = locptr.at(0);
                    dr3.value = QString("0x%1").
                                arg((locptr.at(0) >> 1) & 0x7f, 2, 16, QChar('0'));
                    dr2.subdecodes.push_back(dr3);
                    locptr.begin++;
                }
                decodeUint8Hex(locptr, dr2, "Unused");
                decodeUint8Hex(locptr, dr2, "Unused");
                if(locptr.size() >= 1) {
                    DecodeResult dr3;
                    dr3.location = locptr;
                    dr3.location.end = dr3.location.begin+1;
                    dr3.name = "Records";
                    extent_code = locptr.at(0);
                    dr3.value = QString("%1").
                                arg(locptr.at(0) + 0x80 * (extent_code & 1));
                    dr2.subdecodes.push_back(dr3);
                    locptr.begin++;
                }
                bool incomplete = false;
                if(locptr.size() >= 1) {
                    DecodeResult dr3;
                    dr3.name = "Blocks";
                    dr3.location = locptr;
                    if(dr3.location.end > dr3.location.begin+16)
                        dr3.location.end = dr3.location.begin+16;
                    for(int i = 0; i < 16; i++) {
                        if(!decodeUint8Hex(locptr, dr3, "Block"))
                            incomplete = true;
                    }
                    dr2.subdecodes.push_back(dr3);
                }
                dr.subdecodes.push_back(dr2);
                if(incomplete) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x13: { //remove a file
            dr.value = "File Remove";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(locptr.size() >= 1)
                    dr.name = QString("Disk Drive %1:").arg(QChar('A' + 2*(did-0x31) + locptr.at(0)-1));

                decodeUint8(locptr, dr, "Drive Code");
                decodeAsciiArray(locptr, dr, "File Name", 8);
                decodeAsciiArray(locptr, dr, "File Type", 3, 0x7f);

                if(!decodeUint8(locptr, dr, "Extent")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                if(!decodeUint8Indexed(locptr, dr, "Result", bdos_result_map)) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x17: { //file rename
            dr.value = "File Rename";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(locptr.size() >= 1)
                    dr.name = QString("Disk Drive %1:").arg(QChar('A' + 2*(did-0x31) + locptr.at(0)-1));

                decodeUint8(locptr, dr, "Drive Code"); //0x00
                decodeAsciiArray(locptr, dr, "Old File Name", 8); // 0x01-0x08
                decodeAsciiArray(locptr, dr, "Old File Type", 3, 0x7f); //0x09-0x0b
                decodeUint8(locptr, dr, "Old Extent"); //0x0c
                decodeUint8Hex(locptr, dr, "Unused"); //0x0d
                decodeUint8Hex(locptr, dr, "Unused"); //0x0e
                decodeUint8Hex(locptr, dr, "Unused"); //0x0f
                decodeUint8Hex(locptr, dr, "Drive Code"); //0x10
                decodeAsciiArray(locptr, dr, "New File Name", 8); // 0x11-0x18
                decodeAsciiArray(locptr, dr, "New File Type", 3, 0x7f); //0x19-0x1b
                decodeUint8(locptr, dr, "New Extent"); //0x1c
                decodeUint8Hex(locptr, dr, "Unused"); //0x1d
                decodeUint8Hex(locptr, dr, "Unused"); //0x1e

                if(!decodeUint8Hex(locptr, dr, "Unused")) { //0x1f
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                if(!decodeUint8Indexed(locptr, dr, "Result", bdos_result_map)) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x21: {
            dr.value = "File Read Record";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                decodeUint16Hex(locptr, dr, "Master FCB Address");
                if(locptr.size() >= 3) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+3;
                    dr2.name = "Record";
                    dr2.value = QString("%1").arg((locptr.at(2) << 16) | (locptr.at(1) << 8) | locptr.at(0));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin += 3;
                } else {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                if(locptr.size() >= 1) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Extent";
                    dr2.value = QString("0x%1").
                                arg(locptr.at(0), 2, 16, QChar('0'));
                    DecodeResult dr3;
                    dr3.location = dr2.location;
                    dr3.name = "Extent match bits";
                    dr3.value = QString("0x%1").
                                arg((locptr.at(0) & 0xe0) >> 5, 1, 16, QChar('0'));
                    dr2.subdecodes.push_back(dr3);
                    dr3.name = "Extent group number";
                    dr3.value = QString("0x%1").
                                arg((locptr.at(0) & 0x1e) >> 1, 1, 16, QChar('0'));
                    dr2.subdecodes.push_back(dr3);
                    dr3.name = "Extent number(counts 8 blocks)";
                    dr3.value = QString("0x%1").
                                arg((locptr.at(0) & 0x01), 1, 16, QChar('0'));
                    dr2.subdecodes.push_back(dr3);
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                }
                if(locptr.size() >= 1) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Records";
                    dr2.value = QString("%1").
                                arg(locptr.at(0));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                }
                if(locptr.size() > 0) { //0x02
                    DecodeResult dr2;
                    dr2.location = locptr;
                    if(dr2.location.end > dr2.location.begin+128)
                        dr2.location.end = dr2.location.begin+128;
                    dr2.name = "Data";
                    dr.subdecodes.push_back(dr2);
                    if(locptr.end > locptr.begin + 128) {
                        locptr.begin += 128;
                    } else  {
                        locptr.begin = locptr.end;
                        DecodeResult dr2;
                        dr2.location = loc;
                        dr2.name = "Incomplete";
                        dr.subdecodes.push_back(dr2);
                    }
                } else  {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
                decodeUint8Indexed(locptr, dr, "Result", bdos_result_map); //0x82
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x22: { //write record to file
            dr.value = "File Write Record";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                decodeUint16Hex(locptr, dr, "Master FCB Address");
                if(locptr.size() >= 3) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+3;
                    dr2.name = "Record";
                    dr2.value = QString("%1").arg((locptr.at(2) << 16) | (locptr.at(1) << 8) | locptr.at(0));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin += 3;
                } else {
                    locptr.begin = locptr.end;
                }
                if(locptr.size() > 0) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    if(dr2.location.end > dr2.location.begin+128)
                        dr2.location.end = dr2.location.begin+128;
                    dr2.name = "Data";
                    dr.subdecodes.push_back(dr2);
                    if(locptr.end > locptr.begin + 128) {
                        locptr.begin += 128;
                    } else  {
                        locptr.begin = locptr.end;
                        DecodeResult dr2;
                        dr2.location = loc;
                        dr2.name = "Incomplete";
                        dr.subdecodes.push_back(dr2);
                    }
                } else  {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                if(locptr.size() >= 1) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Extent";
                    dr2.value = QString("0x%1").
                                arg(locptr.at(0), 2, 16, QChar('0'));
                    DecodeResult dr3;
                    dr3.location = dr2.location;
                    dr3.name = "Extent match bits";
                    dr3.value = QString("0x%1").
                                arg((locptr.at(0) & 0xe0) >> 5, 1, 16, QChar('0'));
                    dr2.subdecodes.push_back(dr3);
                    dr3.name = "Extent group number";
                    dr3.value = QString("0x%1").
                                arg((locptr.at(0) & 0x1e) >> 1, 1, 16, QChar('0'));
                    dr2.subdecodes.push_back(dr3);
                    dr3.name = "Extent number(counts 8 blocks)";
                    dr3.value = QString("0x%1").
                                arg((locptr.at(0) & 0x01), 1, 16, QChar('0'));
                    dr2.subdecodes.push_back(dr3);
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                }
                if(locptr.size() >= 1) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Records";
                    dr2.value = QString("%1").
                                arg(locptr.at(0));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                }
                if(!decodeUint8Indexed(locptr, dr, "Result", bdos_result_map)) { //0x02
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x23: { //file size calculation
            dr.value = "File Size";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(!decodeUint16Hex(locptr, dr, "Master FCB Address")) {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                uint8_t extent_code = 0;
                if(locptr.size() >= 1) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Extent";
                    extent_code = locptr.at(0);
                    dr2.value = QString("0x%1").
                                arg((locptr.at(0) >> 1) & 0x7f, 2, 16, QChar('0'));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                }
                if(locptr.size() >= 1) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Records";
                    extent_code = locptr.at(0);
                    dr2.value = QString("%1").
                                arg(locptr.at(0) + 0x80 * (extent_code & 1));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                }
                if(locptr.size() >= 3) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+3;
                    dr2.name = "Records";
                    extent_code = locptr.at(0);
                    dr2.value = QString("%1").
                                arg((locptr.at(2) << 16) | (locptr.at(1) << 8) | locptr.at(0));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin += 3;
                } else {
                    locptr.begin = locptr.end;
                }
                if(!decodeUint8Indexed(locptr, dr, "Result", bdos_result_map)) {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x7a: { // disk all copy
            dr.value = "Disk Copy";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(locptr.size() >= 1) {
                    dr.name = QString("Disk Drive %1:").arg(QChar('A' + 2*(did-0x31) + locptr.at(0)-1));
                    dr.value = QString("Disk Copy to %1").arg(QChar('A' + 2*(did-0x31) + (2-locptr.at(0))));
                }
                if(!decodeUint8(locptr, dr, "Drive code")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                decodeUint16(locptr, dr, "Current Track");
                if(!decodeUint8Indexed(locptr, dr, "Result", bdos_result_map)) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": Disk copy";
            break;
        }
        case 0x7b: { //direct write
            dr.value = "Disk Write";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(locptr.size() >= 1) {
                    dr.name = QString("Disk Drive %1:").arg(QChar('A' + 2*(did-0x31) + locptr.at(0)-1));
                }
                decodeUint8(locptr, dr, "Drive code");
                decodeUint8(locptr, dr, "Track");
                decodeUint8(locptr, dr, "Sector");
                if(locptr.size() > 0) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    if(dr2.location.end > dr2.location.begin+128)
                        dr2.location.end = dr2.location.begin+128;
                    dr2.name = "Data";
                    dr.subdecodes.push_back(dr2);
                    if(locptr.end > locptr.begin + 128) {
                        locptr.begin += 128;
                    } else  {
                        locptr.begin = locptr.end;
                        DecodeResult dr2;
                        dr2.location = loc;
                        dr2.name = "Incomplete";
                        dr.subdecodes.push_back(dr2);
                    }
                } else  {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                if(!decodeUint8Indexed(locptr, dr, "Result", bdos_result_map)) { //0x02
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x7c: { // disk formatting
            dr.value = "Disk Format";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(locptr.size() >= 1)
                    dr.name = QString("Disk Drive %1:").arg(QChar('A' + 2*(did-0x31) + locptr.at(0)-1));
                if(!decodeUint8(locptr, dr, "Drive code")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                decodeUint16(locptr, dr, "Current Track");
                if(!decodeUint8Indexed(locptr, dr, "Result", bdos_result_map)) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x7d: { // new system generation
            dr.value = "System generation";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(!decodeUint8(locptr, dr, "Unused")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                decodeUint16(locptr, dr, "Completion marker");
                if(!decodeUint8Indexed(locptr, dr, "Result", bdos_result_map)) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x7e: { //disk free size calculation
            dr.value = "Disk Size";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(locptr.size() >= 1)
                    dr.name = QString("Disk Drive %1:").arg(QChar('A' + 2*(did-0x31) + locptr.at(0)-1));
                if(!decodeUint8(locptr, dr, "Drive code")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                decodeUint8(locptr, dr, "Clusters");
                if(!decodeUint8Indexed(locptr, dr, "Result", bdos_result_map)) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x7f: { //direct read
            dr.value = "Disk Read";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(locptr.size() >= 1)
                    dr.name = QString("Disk Drive %1:").arg(QChar('A' + 2*(did-0x31) + locptr.at(0)-1));
                decodeUint8(locptr, dr, "Drive code");
                decodeUint8(locptr, dr, "Track");
                if(!decodeUint8(locptr, dr, "Sector")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                if(locptr.size() > 0) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    if(dr2.location.end > dr2.location.begin+128)
                        dr2.location.end = dr2.location.begin+128;
                    dr2.name = "Data";
                    dr.subdecodes.push_back(dr2);
                    if(locptr.end > locptr.begin + 128) {
                        locptr.begin += 128;
                    } else  {
                        locptr.begin = locptr.end;
                        DecodeResult dr2;
                        dr2.location = loc;
                        dr2.name = "Incomplete";
                        dr.subdecodes.push_back(dr2);
                    }
                }
                if(!decodeUint8Indexed(locptr, dr, "Result", bdos_result_map)) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x80: { //disk boot
            dr.value = "Disk Boot";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(!decodeUint8(locptr, dr, "Application Code")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                decodeUint8Indexed(locptr, dr, "Result", bdos_result_map);
                DecodeResult dr2;
                dr2.location = locptr;
                dr2.name = "Data";
                dr.subdecodes.push_back(dr2);
                locptr.begin = locptr.end;
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x81: { //load open
            dr.value = "Load Open";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                decodeAsciiArray(locptr, dr, "File Name", 8);
                decodeAsciiArray(locptr, dr, "File Type", 3, 0x7f);

                decodeUint8Indexed(locptr, dr, "Relocation Type", {
                    {0, "None"},
                    {1,"From Start Address"},
                    {2,"From End Address"}
                });
                if(!decodeUint16Hex(locptr, dr, "Relocation Address")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                decodeUint8Indexed(locptr, dr, "Result", bdos_result_map);
                if(!decodeUint16Hex(locptr, dr, "Code Size")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x82: { //load close
            dr.value = "Load Close";
            if(basePacket.dir == RawDecodePacketInfo::SlaveToMaster) {
                if(!decodeUint8Indexed(locptr, dr, "Result", bdos_result_map)) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        case 0x83: { //read one block(goes with 0x81: load open)
            dr.value = "Load Read";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(!decodeUint16(locptr, dr, "Record")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                decodeUint16(locptr, dr, "Next Record");
                if(locptr.size() > 0) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    if(dr2.location.end > dr2.location.begin+128)
                        dr2.location.end = dr2.location.begin+128;
                    dr2.name = "Data";
                    dr.subdecodes.push_back(dr2);
                    if(locptr.end > locptr.begin + 128) {
                        locptr.begin += 128;
                    } else  {
                        locptr.begin = locptr.end;
                        DecodeResult dr2;
                        dr2.location = loc;
                        dr2.name = "Incomplete";
                        dr.subdecodes.push_back(dr2);
                    }
                }
                if(!decodeUint8Indexed(locptr, dr, "Result", bdos_result_map)) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = dr.name + ": " + dr.value;
            break;
        }
        default:
            dr.value = QString("Unknown(0x%1)").arg(fnc, 2, 16, QChar('0'));
            basePacket.info = dr.name + ": "+dr.value;
            break;
        }
        if(locptr.size() > 0) {
            DecodeResult dr2;
            dr2.location = locptr;
            dr2.name = "Extra data";
            dr.subdecodes.push_back(dr2);
            dr.location.end = locptr.begin;
        }
        basePacket.decoded.push_back(dr);
    }
};

class DisplayDecoder : public Decoder {
private:
public:
    DisplayDecoder() {}
    virtual void decodePacket(RawDecodePacketInfo &basePacket,
                              DecodeLocation const &loc,
                              MetaData const &md) override {
        if(md.find("fnc") == md.end())
            return;
        bool good;
        int fnc = md.find("fnc")->second.toInt(&good);
        if(!good)
            return;
        DecodeResult dr;
        dr.name = "Display";
        dr.location = loc;
        auto locptr = loc;
        switch(fnc) {
        case 0x84: {
            dr.value = "Screen Device Select";
            if(basePacket.dir == RawDecodePacketInfo::SlaveToMaster) {
                if(!decodeUint8Hex(locptr, dr, "Result")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Screen Device Select";
            break;
        }
        case 0x85: {
            dr.value = "Init Controller";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(!decodeUint8Hex(locptr, dr, "Unused")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                if(!decodeUint8Hex(locptr, dr, "Result")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Init Controller";
            break;
        }
        case 0x87: {
            dr.value = "Set Virtual Screen Size";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                decodeUint8(locptr, dr, "Virtual Width", 1);
                decodeUint8(locptr, dr, "Virtual Height", 1);
                if(!decodeUint16Hex(locptr, dr, "Base Address")) {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                if(!decodeUint8Hex(locptr, dr, "Result")) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Set Virtual Screen Size";
            break;
        }
        case 0x88: {
            dr.value = "Get Virtual Screen Size";
            if(basePacket.dir == RawDecodePacketInfo::SlaveToMaster) {
                decodeUint8(locptr, dr, "Virtual Width", 1);
                if(!decodeUint8(locptr, dr, "Virtual Height", 1)) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Get Virtual Screen Size";
            break;
        }
        case 0x89: {
            dr.value = "Get Window Size";
            if(basePacket.dir == RawDecodePacketInfo::SlaveToMaster) {
                decodeUint8(locptr, dr, "Window Width", 1);
                if(!decodeUint8(locptr, dr, "Window Height", 1)) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Get Window Size";
            break;
        }
        case 0x8a: {
            dr.value = "Get Window Position";
            if(basePacket.dir == RawDecodePacketInfo::SlaveToMaster) {
                decodeUint8(locptr, dr, "Window Column");
                if(!decodeUint8(locptr, dr, "Window Row")) {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Get Window Position";
            break;
        }
        case 0x8c: {
            dr.value = "Get Cursor Position";
            if(basePacket.dir == RawDecodePacketInfo::SlaveToMaster) {
                decodeUint8(locptr, dr, "Cursor Column");
                if(!decodeUint8(locptr, dr, "Cursor Row")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Get Cursor Position";
            break;
        }
        case 0x8d: {
            dr.value = "Get Cursor Margin";
            if(basePacket.dir == RawDecodePacketInfo::SlaveToMaster) {
                if(!decodeUint8(locptr, dr, "Cursor Margin")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Get Cursor Margin";
            break;
        }
        case 0x8e: {
            dr.value = "Get Scroll Steps";
            if(basePacket.dir == RawDecodePacketInfo::SlaveToMaster) {
                decodeUint8(locptr, dr, "Horizontal Scroll Step");
                if(!decodeUint8(locptr, dr, "Vertical Scroll Step")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Get Scroll Steps";
            break;
        }
        case 0x8f: {
            dr.value = "Get Pixel";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                decodeUint16(locptr, dr, "X");
                if(!decodeUint16(locptr, dr, "Y")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                if(!decodeUint8Hex(locptr, dr, "Colorcode")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Get Pixel";
            break;
        }
        case 0x91: {
            dr.value = "Get Logical Line Range";
            if(basePacket.dir == RawDecodePacketInfo::SlaveToMaster) {
                decodeUint8(locptr, dr, "First Column");
                decodeUint8(locptr, dr, "First Row");
                decodeUint8(locptr, dr, "Last Column");
                if(!decodeUint8(locptr, dr, "Last Row")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Get Logical Line Range";
            break;
        }
        case 0x92: {
            dr.value = "Display Character";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(locptr.size() >= 1) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Character";
                    if(locptr.at(0) >= 0x20 && locptr.at(0) < 0x7f)
                        dr2.value = QString("%1(0x%2)").
                                    arg(QChar(locptr.at(0))).
                                    arg(locptr.at(0), 2, 16, QChar('0'));
                    else
                        dr2.value = QString("0x%1").
                                    arg(locptr.at(0), 2, 16, QChar('0'));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                } else  {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                decodeUint8(locptr, dr, "Cursor Column");
                if(!decodeUint8(locptr, dr, "Cursor Row")) {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Display Character";
            break;
        }
        case 0x93: {
            dr.value = "Set Display Mode";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(locptr.size() >= 1) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Text mode";
                    if(locptr.at(0) == 0)
                        dr2.value = QString("Off(0)");
                    else if(locptr.at(0) == 1)
                        dr2.value = QString("On(1)");
                    else
                        dr2.value = QString("Unknown(0x%1)").
                                    arg(locptr.at(0), 2, 16, QChar('0'));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                }
                if(locptr.size() >= 1) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Graphics mode";
                    if(locptr.at(0) == 0)
                        dr2.value = QString("Off(0)");
                    else if(locptr.at(0) == 1)
                        dr2.value = QString("Color(1)");
                    else if(locptr.at(0) == 2)
                        dr2.value = QString("Monochromatic(2)");
                    else
                        dr2.value = QString("Unknown(0x%1)").
                                    arg(locptr.at(0), 2, 16, QChar('0'));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                }
                if(!decodeUint8Hex(locptr, dr, "Background Color")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                if(!decodeUint8Hex(locptr, dr, "Result")) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Set Display Mode";
            break;
        }
        case 0x95: {
            dr.value = "Get Character";
            if(basePacket.dir == RawDecodePacketInfo::SlaveToMaster) {
                if(locptr.size() >= 1) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Character";
                    if(locptr.at(0) >= 0x20 && locptr.at(0) < 0x7f)
                        dr2.value = QString("%1(0x%2)").
                                    arg(QChar(locptr.at(0))).
                                    arg(locptr.at(0), 2, 16, QChar('0'));
                    else
                        dr2.value = QString("0x%1").
                                    arg(locptr.at(0), 2, 16, QChar('0'));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                }
                if(!decodeUint8Hex(locptr, dr, "Background Color")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Get Character";
            break;
        }
        case 0x97: {
            dr.value = "Get Character Content";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                decodeUint8(locptr, dr, "X");
                if(!decodeUint8(locptr, dr, "Y")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                DecodeResult dr2;
                dr2.location = locptr;
                dr2.name = "Character Data";
                dr2.value = QString("(%1 characters)").arg(dr2.location.size());
                dr.subdecodes.push_back(dr2);
                locptr.begin = locptr.end;
            }
            basePacket.info = "Display: Get Character Content";
            break;
        }
        case 0x98: {
            dr.value = "Display Character";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(locptr.size() >= 1) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Character";
                    if(locptr.at(0) >= 0x20 && locptr.at(0) < 0x7f)
                        dr2.value = QString("%1(0x%2)").
                                    arg(QChar(locptr.at(0))).
                                    arg(locptr.at(0), 2, 16, QChar('0'));
                    else
                        dr2.value = QString("0x%1").
                                    arg(locptr.at(0), 2, 16, QChar('0'));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                } else  {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            } else {
                decodeUint8(locptr, dr, "Cursor Column");
                decodeUint8(locptr, dr, "Cursor Row");
                decodeUint8(locptr, dr, "First Logical Line Row");
                if(!decodeUint8(locptr, dr, "Last Logical Line Row")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Display Character";
            break;
        }
        //hx20 does not want answer if bit 6 is set
        case 0xc0: {
            dr.value = "Set Window Position";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                decodeUint8(locptr, dr, "Window Column");
                if(!decodeUint8(locptr, dr, "Window Row")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Set Window Position";
            break;
        }
        case 0xc2: {
            dr.value = "Set Cursor Position";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                decodeUint8(locptr, dr, "Cursor Column");
                if(!decodeUint8(locptr, dr, "Cursor Row")) {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Set Cursor Position";
            break;
        }
        case 0xc3: {
            dr.value = "Set Cursor Margin";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(!decodeUint8(locptr, dr, "Cursor Margin")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Set Cursor Margin";
            break;
        }
        case 0xc4: {
            dr.value = "Set Scroll Steps";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                decodeUint8(locptr, dr, "Horizontal Step");
                if(!decodeUint8(locptr, dr, "Vertical Step")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Set Scroll Steps";
            break;
        }
        case 0xc5: {
            dr.value = "Enable List Flag";
            basePacket.info = "Display: Enable List Flag";
            break;
        }
        case 0xc6: {
            dr.value = "Disable List Flag";
            basePacket.info = "Display: Disable List Flag";
            break;
        }
        case 0xc7: {
            dr.value = "Set Pixel";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                decodeUint16(locptr, dr, "X");
                decodeUint16(locptr, dr, "Y");
                if(!decodeUint8Hex(locptr, dr, "Colorcode")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Set Pixel";
            break;
        }
        case 0xc8: {
            dr.value = "Draw Line";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                decodeUint16(locptr, dr, "Start X");
                decodeUint16(locptr, dr, "Start Y");
                decodeUint16(locptr, dr, "End X");
                decodeUint16(locptr, dr, "End Y");
                if(!decodeUint8Hex(locptr, dr, "Colorcode")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Draw Line";
            break;
        }
        case 0xc9:
            dr.value = "Terminate Logical Line";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(!decodeUint8(locptr, dr, "Row")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Terminate Logical Line";
            break;
        case 0xca: {
            dr.value = "Clear Graphics";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(!decodeUint8Hex(locptr, dr, "Colorcode")) {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Clear Graphics";
            break;
        }
        case 0xcb: {
            dr.value = "Set Scroll Speed";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(!decodeUint8(locptr, dr, "Scroll Speed")) {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Set Scroll Speed";
            break;
        }
        case 0xcd: {
            dr.value = "Set Character";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(locptr.size() >= 1) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Character";
                    if(locptr.at(0) >= 0x20 && locptr.at(0) < 0x7f)
                        dr2.value = QString("%1(0x%2)").
                                    arg(QChar(locptr.at(0))).
                                    arg(locptr.at(0), 2, 16, QChar('0'));
                    else
                        dr2.value = QString("0x%1").
                                    arg(locptr.at(0), 2, 16, QChar('0'));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin ++;
                } else {
                    locptr.begin = locptr.end;
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Set Character";
            break;
        }
        case 0xce: {
            dr.value = "Set Access Pointer";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                decodeUint8(locptr, dr, "Access X");
                if(!decodeUint8(locptr, dr, "Access Y")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Set Access Pointer";
            break;
        }
        case 0xcf:
            dr.value = "Set Color Set";
            if(basePacket.dir == RawDecodePacketInfo::MasterToSlave) {
                if(!decodeUint8(locptr, dr, "Color Set")) {
                    DecodeResult dr2;
                    dr2.location = loc;
                    dr2.name = "Incomplete";
                    dr.subdecodes.push_back(dr2);
                }
            }
            basePacket.info = "Display: Set Color Set";
            break;
        case 0xd4://screen new?
            dr.value = "Screen New?";
            basePacket.info = "Display: Screen New?";
            break;
        default:
            dr.value = QString("Unknown(0x%1)").arg(fnc, 2, 16, QChar('0'));
            basePacket.info = "Display: "+dr.value;
            break;
        }
        if(locptr.size() > 0) {
            DecodeResult dr2;
            dr2.location = locptr;
            dr2.name = "Extra data";
            dr.subdecodes.push_back(dr2);
            dr.location.end = locptr.begin;
        }
        basePacket.decoded.push_back(dr);
    }
};

class PacketDecoder : public Decoder {
private:
    DiskDecoder disk;
    DisplayDecoder dpy;
public:
    PacketDecoder() {}
    virtual void decodePacket(RawDecodePacketInfo &basePacket,
                              DecodeLocation const &loc,
                              MetaData const &md) override {
        if(md.find("did") == md.end())
            return;
        bool good;
        int did = md.find("did")->second.toInt(&good);
        if(!good)
            return;

        switch(did) {
        case 0x30:
            dpy.decodePacket(basePacket, loc, md);
            return;
        case 0x31:
        case 0x32:
            disk.decodePacket(basePacket, loc, md);
            return;
        }
    }
};

static bool checkChecksum(DecodeLocation const &loc) {
    uint8_t sum = 0;
    for(unsigned int i = 0; i < loc.size(); i++) {
        sum += loc.at(i);
    }
    return sum == 0;
}

class RawDecoder : public Decoder {
private:
    uint8_t sel_master;
    uint8_t sel_slave;
    bool isSelected;
    uint16_t sid;
    uint16_t did;
    uint16_t fnc;
    uint16_t size;
    bool haveHeader;
    PacketDecoder pktDecoder;
public:
    RawDecoder(): isSelected(false), haveHeader(false) {}
    virtual void decodePacket(RawDecodePacketInfo &basePacket,
                              DecodeLocation const &loc,
                              MetaData const &md) override {
        //This will fully decode packets, selects, reversion of direction
        //metadata produced:(into a copy of md):
        // int sid, int did, int fnc, bool headergood, bool master-to-slave

        //we are exploiting here that we are going to get complete packets
        if(loc.at(0) == PS) {
            basePacket.title = "Select";
            DecodeResult dr;
            dr.name = "EPSP";
            dr.value = "Select";
            dr.location = loc;
            auto locptr = loc;
            if(dr.location.end > dr.location.begin+4)
                dr.location.end = dr.location.begin+4;
            QString text;
            locptr.begin++;
            if(locptr.size() >= 1) {
                sel_slave = locptr.at(0);
                text += QString("Slave: %1 ").arg(sel_slave, 2, 16, QChar('0'));
                DecodeResult dr2;
                dr2.location = locptr;
                dr2.location.end = dr2.location.begin+1;
                dr2.name = "Slave";
                dr2.value = QString("%1").arg(sel_slave, 2, 16, QChar('0'));
                dr.subdecodes.push_back(dr2);
                locptr.begin++;
            }
            if(locptr.size() >= 1) {
                sel_master = locptr.at(0);
                text += QString("Master: %1 ").arg(sel_master, 2, 16, QChar('0'));
                DecodeResult dr2;
                dr2.location = locptr;
                dr2.location.end = dr2.location.begin+1;
                dr2.name = "Master";
                dr2.value = QString("%1").arg(sel_master, 2, 16, QChar('0'));
                dr.subdecodes.push_back(dr2);
                locptr.begin++;
            }
            if(locptr.size() < 1) {
                text += "Incomplete";
                DecodeResult dr2;
                dr2.location = loc;
                dr2.location.begin+=1;
                dr2.location.end = dr2.location.begin+1;
                dr2.name = "Incomplete";
                dr.subdecodes.push_back(dr2);
                isSelected = false;
            } else if(locptr.at(0) != ENQ) {
                text += "Invalid: should end in ENQ";
                DecodeResult dr2;
                dr2.location = locptr;
                dr2.location.begin+=3;
                dr2.location.end = dr2.location.begin+1;
                dr2.name = "Incorrect byte";
                dr2.value = QString("%1").arg(locptr.at(0), 2, 16, QChar('0'));
                dr.subdecodes.push_back(dr2);
                isSelected = false;
            } else {
                isSelected = true;
            }
            if(locptr.size() > 0) {
                DecodeResult dr2;
                dr2.location = locptr;
                dr2.name = "Extra data";
                dr.subdecodes.push_back(dr2);
            }

            basePacket.decoded.push_back(dr);
            basePacket.info = text;
            //no further decodes.
        } else if (loc.at(0) == SOH) {
            basePacket.title = "Header";
            DecodeResult dr;
            dr.name = "EPSP";
            dr.value = "Header";
            dr.location = loc;
            auto locptr = loc;
            QString text;
            uint8_t fmt = 0;
            locptr.begin++;
            if(locptr.size() >= 1) {
                fmt = locptr.at(0);
                DecodeResult dr2;
                dr2.location = locptr;
                dr2.location.end = dr2.location.begin+1;
                dr2.name = "Format";
                dr2.value = QString("%1").arg(locptr.at(0), 8, 2, QChar('0'));
                DecodeResult dr3 = dr2;
                dr3.name = "Slave To Master";
                dr3.value = QString(".... ...%1").arg(locptr.at(0) & 1);
                dr2.subdecodes.push_back(dr3);
                dr3.name = "Size 16 bit";
                dr3.value = QString(".... ..%1.").arg((locptr.at(0) >> 1) & 1);
                dr2.subdecodes.push_back(dr3);
                dr3.name = "IDs 16 bit";
                dr3.value = QString(".... .%1..").arg((locptr.at(0) >> 2) & 1);
                dr2.subdecodes.push_back(dr3);
                dr.subdecodes.push_back(dr2);
                locptr.begin++;
            }
            if(fmt & 0x4) {
                if(locptr.size() >= 2) {
                    did = (locptr.at(0) << 8) | locptr.at(1);
                    text += QString("DID %1 ").arg(did, 4, 16, QChar('0'));
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+2;
                    dr2.name = "DID";
                    dr2.value = QString("%1").arg(sid, 4, 16, QChar('0'));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin += 2;
                } else {
                    locptr.begin = locptr.end;
                }
                if(locptr.size() >= 2) {
                    sid = (locptr.at(0) << 8) | locptr.at(1);
                    text += QString("SID %1 ").arg(sid, 2, 16, QChar('0'));
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+2;
                    dr2.name = "SID";
                    dr2.value = QString("%1").arg(sid, 4, 16, QChar('0'));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin += 2;
                } else {
                    locptr.begin = locptr.end;
                }
            } else {
                if(locptr.size() >= 1) {
                    did = locptr.at(0);
                    text += QString("DID %1 ").arg(did, 2, 16, QChar('0'));
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "DID";
                    dr2.value = QString("%1").arg(did, 2, 16, QChar('0'));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                }
                if(locptr.size() >= 1) {
                    sid = locptr.at(0);
                    text += QString("SID %1 ").arg(sid, 2, 16, QChar('0'));
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "SID";
                    dr2.value = QString("%1").arg(sid, 2, 16, QChar('0'));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                }
            }
            if(locptr.size() >= 1) {
                fnc = locptr.at(0);
                text += QString("Function %1 ").arg(fnc, 2, 16, QChar('0'));
                DecodeResult dr2;
                dr2.location = locptr;
                dr2.location.end = dr2.location.begin+1;
                dr2.name = "Function";
                dr2.value = QString("%1").arg(fnc, 2, 16, QChar('0'));
                dr.subdecodes.push_back(dr2);
                locptr.begin++;
            }
            if(fmt & 0x2) {
                if(locptr.size() >= 2) {
                    size = ((locptr.at(0) << 8) | locptr.at(1)) + 1;
                    text += QString("Size %1 ").arg(size, 4, 16, QChar('0'));
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+2;
                    dr2.name = "Size";
                    dr2.value = QString("%1").arg(this->size, 4, 16, QChar('0'));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin += 2;
                } else {
                    locptr.begin = locptr.end;
                }
            } else {
                if(locptr.size() >= 1) {
                    size = (unsigned)locptr.at(0) + 1;
                    text += QString("Size %1 ").arg(size, 2, 16, QChar('0'));
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Size";
                    dr2.value = QString("%1").arg(this->size, 2, 16, QChar('0'));
                    dr.subdecodes.push_back(dr2);
                    locptr.begin++;
                }
            }
            if(locptr.size() <= 0) {
                text += "Incomplete";
                DecodeResult dr2;
                dr2.location = loc;
                dr2.location.begin+=1;
                dr2.location.end = dr2.location.begin+1;
                dr2.name = "Incomplete";
                dr.subdecodes.push_back(dr2);
                haveHeader = false;
            } else {
                auto hdrloc = loc;
                hdrloc.end = locptr.begin+1;
                DecodeResult dr2;
                dr2.location = locptr;
                dr2.location.end = dr2.location.begin+1;
                dr2.value = QString("%1").arg(locptr.at(0), 2, 16, QChar('0'));
                if(checkChecksum(hdrloc)) {
                    haveHeader = true;
                    dr2.name = "Checksum";
                } else {
                    haveHeader = false;
                    text += "Incorrect checksum";
                    dr2.name = "Incorrect checksum";
                }
                dr.subdecodes.push_back(dr2);
                locptr.begin++;
            }
            if(locptr.size() > 0) {
                DecodeResult dr2;
                dr2.location = locptr;
                dr2.name = "Extra data";
                dr.subdecodes.push_back(dr2);
                dr.location.end = locptr.begin;
            }

            basePacket.decoded.push_back(dr);
            basePacket.info = text;
            //no further decodes.
        } else if (loc.at(0) == STX) {
            basePacket.title = "Data";
            DecodeResult dr;
            dr.name = "EPSP";
            dr.value = "Data";
            dr.location = loc;
            bool dataok = true;
            auto locptr = loc;
            auto dataloc = loc;
            QString text;
            locptr.begin++;
            if(haveHeader) {
                if(locptr.size() >= size) {
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+size;
                    dataloc = dr2.location;
                    dr2.name = "Data";
                    dr2.value = QString("%1 bytes").arg(dr2.location.size());
                    dr.subdecodes.push_back(dr2);
                    locptr.begin += size;
                } else {
                    //"Incomplete" text will be added when looking for the checksum
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dataloc = dr2.location;
                    dr2.name = "Incomplete data";
                    dr2.value = QString("%1 bytes").arg(dr2.location.size());
                    dr.subdecodes.push_back(dr2);
                    locptr.begin = locptr.end;
                    dataok = false;
                }
            } else {
                DecodeResult dr2;
                dr2.location = locptr;
                dr2.location.end-=2;
                dataloc = dr2.location;
                dr2.name = "Unknown Data";
                dr2.value = QString("%1 bytes").arg(dr2.location.size());
                dr.subdecodes.push_back(dr2);
                locptr.begin = locptr.end-2;
                dataok = false;
            }
            if(locptr.size() >= 1) {
                if(locptr.at(0) != ETX) {
                    text += "Incorrect ETX ";
                    DecodeResult dr2;
                    dr2.location = locptr;
                    dr2.location.end = dr2.location.begin+1;
                    dr2.name = "Incorrect End of Data";
                    dr.subdecodes.push_back(dr2);
                    dataok = false;
                }
                locptr.begin++;
            }
            if(locptr.size() <= 0) {
                text += "Incomplete";
                DecodeResult dr2;
                dr2.location = loc;
                dr2.location.begin+=1;
                dr2.location.end = dr2.location.begin+1;
                dr2.name = "Incomplete";
                dr.subdecodes.push_back(dr2);
                haveHeader = false;
                dataok = false;
            } else {
                auto hdrloc = loc;
                hdrloc.end = locptr.begin+1;
                DecodeResult dr2;
                dr2.location = locptr;
                dr2.location.end = dr2.location.begin+1;
                dr2.value = QString("%1").arg(locptr.at(0), 2, 16, QChar('0'));
                if(checkChecksum(hdrloc)) {
                    dr2.name = "Checksum";
                } else {
                    dr2.name = "Incorrect checksum";
                    text += "Incorrect checksum";
                    dataok = false;
                }
                dr.subdecodes.push_back(dr2);
                locptr.begin++;
            }
            if(locptr.size() > 0) {
                DecodeResult dr2;
                dr2.location = locptr;
                dr2.name = "Extra data";
                dr.subdecodes.push_back(dr2);
                dr.location.end = locptr.begin;
            }

            basePacket.decoded.push_back(dr);
            basePacket.info = text;

            if(dataok && haveHeader) {
                auto md2 = md;
                md2["did"] = QVariant(did);
                md2["sid"] = QVariant(sid);
                md2["fnc"] = QVariant(fnc);
                pktDecoder.decodePacket(basePacket,
                                        dataloc,
                                        md2);
            }
        } else if (loc.at(0) == EOT) {
            basePacket.title = "End of Transmission/Reverse direction";
            DecodeResult dr;
            dr.name = "EPSP";
            dr.value = "End of Transmission/Reverse direction";
            dr.location = loc;
            if(dr.location.end > dr.location.begin+1)
                dr.location.end = dr.location.begin+1;
            QString text;
            if(loc.size() > 1) {
                DecodeResult dr2;
                dr2.location = loc;
                dr2.location.begin+=1;
                dr2.name = "Extra data";
                dr.subdecodes.push_back(dr2);
            }

            basePacket.decoded.push_back(dr);
            basePacket.info = text;
            //no further decodes.
        } else if (loc.at(0) == ACK) {
            basePacket.title = "Acknowledge";
            DecodeResult dr;
            dr.name = "EPSP";
            dr.value = "Acknowledge";
            dr.location = loc;
            if(dr.location.end > dr.location.begin+1)
                dr.location.end = dr.location.begin+1;
            QString text;
            if(loc.size() > 1) {
                DecodeResult dr2;
                dr2.location = loc;
                dr2.location.begin+=1;
                dr2.name = "Extra data";
                dr.subdecodes.push_back(dr2);
            }

            basePacket.decoded.push_back(dr);
            basePacket.info = text;
            //no further decodes.
        } else if (loc.at(0) == NAK) {
            basePacket.title = "Negative Acknowledge";
            DecodeResult dr;
            dr.name = "EPSP";
            dr.value = "Negative Acknowledge";
            dr.location = loc;
            if(dr.location.end > dr.location.begin+1)
                dr.location.end = dr.location.begin+1;
            QString text;
            if(loc.size() > 1) {
                DecodeResult dr2;
                dr2.location = loc;
                dr2.location.begin+=1;
                dr2.name = "Extra data";
                dr.subdecodes.push_back(dr2);
            }

            basePacket.decoded.push_back(dr);
            basePacket.info = text;
            //no further decodes.
        } else if (loc.at(0) == WAK) {
            basePacket.title = "Wait Acknowledge";
            DecodeResult dr;
            dr.name = "EPSP";
            dr.value = "Wait Acknowledge";
            dr.location = loc;
            if(dr.location.end > dr.location.begin+1)
                dr.location.end = dr.location.begin+1;
            QString text;
            if(loc.size() > 1) {
                DecodeResult dr2;
                dr2.location = loc;
                dr2.location.begin+=1;
                dr2.name = "Extra data";
                dr.subdecodes.push_back(dr2);
            }

            basePacket.decoded.push_back(dr);
            basePacket.info = text;
            //no further decodes.
        } else {
            basePacket.title = "Extra Data";
            DecodeResult dr;
            dr.location = loc;
            dr.name = "Extra data";

            basePacket.decoded.push_back(dr);
            //no further decodes.
        }
    }
};

PacketListModel::PacketListModel(std::deque<RawDecodePacketInfo> &packets) : packets(packets) {
}

void PacketListModel::beforeAddPacket() {
    beginInsertRows(QModelIndex(), packets.size(), packets.size());
}

void PacketListModel::afterAddPacket() {
    endInsertRows();
}

QVariant PacketListModel::data(const QModelIndex &index, int role) const {
    switch(role) {
    case Qt::ItemDataRole::DisplayRole:
        if(index.column() == 0) {
            if(!index.parent().isValid()) {
                if(index.row() < 0 || (unsigned)index.row() >= packets.size())
                    return QVariant();
                auto &pi = packets[index.row()];
                return pi.title;
            }
        }
        if(index.column() == 1) {
            if(!index.parent().isValid()) {
                if(index.row() < 0 || (unsigned)index.row() >= packets.size())
                    return QVariant();
                auto &pi = packets[index.row()];
                return pi.info;
            }
        }
        return QString();
    case Qt::ItemDataRole::TextAlignmentRole:
        if(index.column() == 0) {
            if(!index.parent().isValid()) {
                if(index.row() < 0 || (unsigned)index.row() >= packets.size())
                    return QVariant();
                auto &pi = packets[index.row()];
                if(pi.dir == RawDecodePacketInfo::MasterToSlave)
                    return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
                else
                    return QVariant(Qt::AlignRight | Qt::AlignVCenter);
            }
        }
        return QVariant();
    default:
        return QVariant();
    }
}

QVariant PacketListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    switch(role) {
    case Qt::ItemDataRole::DisplayRole:
        switch(section) {
        case 0:
            return QString("Packet");
        case 1:
            return QString("Info");
        }
    default:
        return QVariant();
    }
}

QModelIndex PacketListModel::index(int row, int column, const QModelIndex &parent) const {
    if(!parent.isValid())
        return createIndex(row, column, quintptr(0xffffffff));
    return QModelIndex();
}

QModelIndex PacketListModel::parent(const QModelIndex &child) const {
    return QModelIndex();
}

int PacketListModel::rowCount(const QModelIndex &parent) const {
    if(!parent.isValid())
        return packets.size();
    return 0;
}

int PacketListModel::columnCount(const QModelIndex &parent) const {
    return 2;
}

PacketDecodeModel::PacketDecodeModel(std::deque<RawDecodePacketInfo> &packets) : packets(packets), pktidx(-1) {
    treeInfo.resize(1);
    treeInfo[0].indexInParent = 0;
    treeInfo[0].parent = 0;
}

DecodeResult const &PacketDecodeModel::findDecodeResult(QModelIndex const &idx)
const {
    std::vector<TreeInfo const *> ti;
    //there is no DecodeResult for the root node.
    assert(idx.isValid());
    unsigned int pos = idx.internalId();
    while(pos != 0) {
        ti.push_back(&treeInfo[pos]);
        assert(pos >= 0 && pos < treeInfo.size());
        pos = treeInfo[pos].parent;
    }
    assert(!ti.empty());
    assert(pktidx >= 0 && (unsigned)pktidx < packets.size());
    assert(ti.back()->indexInParent >= 0 &&
           ti.back()->indexInParent < packets[pktidx].decoded.size());
    DecodeResult const *res =
    &packets[pktidx].decoded[ti.back()->indexInParent];
    ti.pop_back();
    while(!ti.empty()) {
        assert(ti.back()->indexInParent >= 0 &&
               ti.back()->indexInParent < res->subdecodes.size());
        res = &res->subdecodes[ti.back()->indexInParent];
        ti.pop_back();
    }
    return *res;
}

void PacketDecodeModel::setPacketIndex(int pktidx) {
    beginResetModel();
    this->pktidx = pktidx;
    endResetModel();
}

QVariant PacketDecodeModel::data(const QModelIndex &index, int role) const {
    switch(role) {
    case Qt::ItemDataRole::DisplayRole: {
        if(!index.isValid())
            return QVariant();
        DecodeResult const &res = findDecodeResult(index);
        if(index.column() == 0) {
            return res.name;
        } else if(index.column() == 1) {
            return res.value;
        }
        return QVariant();
        break;
    }
    default:
        return QVariant();
    }
}

QVariant PacketDecodeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    switch(role) {
    case Qt::ItemDataRole::DisplayRole:
        switch(section) {
        case 0:
            return QString("Name");
        case 1:
            return QString("Value");
        }
    default:
        return QVariant();
    }
}

QModelIndex PacketDecodeModel::index(int row, int column, const QModelIndex &parent) const {
    unsigned ppos = parent.isValid()?parent.internalId():0;
    assert(ppos >= 0 && ppos < treeInfo.size());
    if(row < 0)
        return QModelIndex();
    while(treeInfo[ppos].children.size() <= (unsigned)row) {
        TreeInfo ti;
        ti.indexInParent = treeInfo[ppos].children.size();
        ti.parent = ppos;
        treeInfo[ppos].children.push_back(treeInfo.size());
        treeInfo.push_back(ti);
    }

    QModelIndex child = createIndex(row, column, treeInfo[ppos].children[row]);
    assert(child.internalId() > 0 && child.internalId() < treeInfo.size());
    return child;
}

QModelIndex PacketDecodeModel::parent(const QModelIndex &child) const {
    if(!child.isValid())
        return QModelIndex();
    //0 is reserved for the root element and is never used as internalId;
    //0 uses an invalid QModelIndex.
    assert(child.internalId() > 0 && child.internalId() < treeInfo.size());
    if(treeInfo[child.internalId()].parent == 0)
        return QModelIndex();
    return createIndex(treeInfo[child.internalId()].indexInParent,
                       0,
                       quintptr(treeInfo[child.internalId()].parent));
}

int PacketDecodeModel::rowCount(const QModelIndex &parent) const {
    if(pktidx < 0 || (unsigned)pktidx >= packets.size())
        return 0;
    if(!parent.isValid())
        return packets[pktidx].decoded.size();
    else
        return findDecodeResult(parent).subdecodes.size();
}

int PacketDecodeModel::columnCount(const QModelIndex &parent) const {
    return 2;
}

RawDecodeModel::RawDecodeModel(std::deque<RawDecodePacketInfo> &packets) :
    packets(packets), pktidx(-1), selbegin(-1), selend(-1) {
}

void RawDecodeModel::setPacketIndex(int pktidx) {
    beginResetModel();
    this->pktidx = pktidx;
    endResetModel();
}

void RawDecodeModel::selectBytes(int begin, int end) {
    beginResetModel();
    this->selbegin = begin;
    this->selend = end;
    endResetModel();
}


QVariant RawDecodeModel::data(const QModelIndex &index, int role) const {
    switch(role) {
    case Qt::ItemDataRole::DisplayRole: {
        if(!index.isValid())
            return QVariant();
        if(pktidx < 0 || (unsigned)pktidx >= packets.size())
            return QVariant();
        size_t ofs = index.row() * 16;
        if(index.column() == 0) {
            return QString("%1").arg(ofs, 4, 16, QChar('0'));
        } else if(index.column() == 17) {
            auto &raw = packets[pktidx].raw;
            QString t;
            for(unsigned i = ofs; i < raw.size() && i < ofs+16; i++) {
                t = t + ((raw[i] >= 0x20 && raw[i] <= 0x7e)?QChar(raw[i]):QChar('.'));
            }
            return t;
        } else {
            auto &raw = packets[pktidx].raw;
            if(index.column()-1 + ofs >= raw.size())
                return QString();
            else
                return QString("%1").arg(raw[ofs+index.column()-1], 2, 16, QChar('0'));
        }
    }
    default:
        return QVariant();
    }
}

QVariant RawDecodeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    switch(role) {
    default:
        return QVariant();
    }
}

QModelIndex RawDecodeModel::index(int row, int column, const QModelIndex &parent) const {
    if(!parent.isValid())
        return createIndex(row, column, quintptr(0xffffffff));
    else if (parent.internalId() == 0xffffffff)
        return createIndex(row, column, quintptr(parent.row()));
    else
        return QModelIndex();
}

QModelIndex RawDecodeModel::parent(const QModelIndex &child) const {
    if(!child.isValid())
        return QModelIndex();
    if(child.internalId() == 0xffffffff)
        return QModelIndex();
    return createIndex(child.internalId(), 0, 0xffffffff);
}

int RawDecodeModel::rowCount(const QModelIndex &parent) const {
    if(pktidx < 0 || (unsigned)pktidx >= packets.size())
        return 0;
    if(!parent.isValid())
        return (packets[pktidx].raw.size()+15)/16;
    return 0;
}

int RawDecodeModel::columnCount(const QModelIndex &parent) const {
    return 18;
}


CommsDebugWindow::CommsDebugWindow(QWidget *parent, Qt::WindowFlags f)
    : QDockWidget(parent, f), conn(nullptr), scrollToNewest(true),
      packetlist(new QTreeView(this)), packetdecode(new QTreeView(this)),
      rawdecode(new QTreeView(this)), rawDecoder(std::make_unique<RawDecoder>())
{
    QSplitter *w = new QSplitter(this);
    w->setOrientation(Qt::Orientation::Vertical);
    w->addWidget(packetlist);
    w->addWidget(packetdecode);
    w->addWidget(rawdecode);

    QFont textfont = packetlist->font();
    textfont.setFamilies({"Courier", "Mono"});
    textfont.setFixedPitch(true);
    packetlist->setFont(textfont);
    packetdecode->setFont(textfont);
    rawdecode->setFont(textfont);

    setWidget(w);
    setWindowTitle(tr("Communications log"));

    packetlistmodel = new PacketListModel(packets);
    packetlistmodel->setParent(this);
    packetlist->setModel(packetlistmodel);
    packetlist->setItemsExpandable(false);

    packetlist->setColumnWidth
    (0, packetlist->fontMetrics().averageCharWidth() * 50);

    connect(packetlistmodel, &QAbstractItemModel::rowsInserted,
            this, [this]
    (const QModelIndex &parent, int first, int last) {
        packetlist->expandRecursively(parent);
        if(scrollToNewest)
            packetlist->scrollTo(
            packetlistmodel->index(packetlistmodel->rowCount(parent)-1, 0,
                                   parent));
    });

    packetdecodemodel = new PacketDecodeModel(packets);
    packetdecodemodel->setParent(this);
    packetdecode->setModel(packetdecodemodel);
    packetdecode->setColumnWidth
    (0, packetdecode->fontMetrics().averageCharWidth() * 30);

    rawdecodemodel = new RawDecodeModel(packets);
    rawdecodemodel->setParent(this);
    rawdecode->setModel(rawdecodemodel);
    rawdecode->setHeaderHidden(true);
    rawdecode->setItemsExpandable(false);

    rawdecode->setColumnWidth
    (0, rawdecode->fontMetrics().averageCharWidth() * 11.5);
    for(int i = 0; i < 16; i++) {
        rawdecode->setColumnWidth
        (i+1, rawdecode->fontMetrics().averageCharWidth() * 3);
    }
    for(int i = 3; i < 16; i+=4) {
        rawdecode->setColumnWidth
        (i+1, rawdecode->fontMetrics().averageCharWidth() * 4);
    }
    rawdecode->setColumnWidth
    (17, rawdecode->fontMetrics().averageCharWidth() * 19);
    rawdecode->setSelectionBehavior(QAbstractItemView::SelectItems);

    connect(rawdecodemodel, &QAbstractItemModel::rowsInserted,
    this, [this](const QModelIndex &parent, int first, int last) {
        rawdecode->expandRecursively(parent);
    });
    connect(rawdecodemodel, &QAbstractItemModel::modelReset,
    this, [this]() {
        rawdecode->expandRecursively(QModelIndex());
        for(int i = 0; i < rawdecodemodel->rowCount(QModelIndex()); i++) {
            rawdecode->setFirstColumnSpanned(i, QModelIndex(), false);
        }
    });

    connect(packetlist->selectionModel(), &QItemSelectionModel::currentChanged,
    this, [this](const QModelIndex &item, const QModelIndex &prev) {
        if(!item.isValid())
            return;
        packetdecodemodel->setPacketIndex(item.row());
        rawdecodemodel->setPacketIndex(item.row());
    });

    connect(packetdecode->selectionModel(), &QItemSelectionModel::currentChanged,
    this, [this](const QModelIndex &item, const QModelIndex &prev) {
        if(!item.isValid())
            return;
        DecodeResult const &res = packetdecodemodel->findDecodeResult(item);
        if(res.location.base) {
            rawdecode->selectionModel()->clearSelection();
            QItemSelection sel;
            int row_first = res.location.begin / 16;
            int col_first = (res.location.begin % 16) + 1;
            int row_last = (res.location.end - 1) / 16;
            int col_last = ((res.location.end - 1) % 16) + 1;
            if(row_first == row_last) {
                sel.select(rawdecodemodel->index(row_first, col_first, QModelIndex()),
                           rawdecodemodel->index(row_first, col_last, QModelIndex()));
            } else {
                sel.select(rawdecodemodel->index(row_first, col_first, QModelIndex()),
                           rawdecodemodel->index(row_first, 17, QModelIndex()));
                for(int row = row_first + 1; row < row_last; row++) {
                    sel.select(rawdecodemodel->index(row, 0, QModelIndex()),
                               rawdecodemodel->index(row, 17, QModelIndex()));
                }
                sel.select(rawdecodemodel->index(row_last, 0, QModelIndex()),
                           rawdecodemodel->index(row_last, col_last, QModelIndex()));
            }
            rawdecode->selectionModel()->select(sel,
                                                QItemSelectionModel::SelectionFlag::ClearAndSelect);
        }
    });

}

CommsDebugWindow::~CommsDebugWindow() {
    if(conn)
        conn->unregisterMonitor(this);
}

void CommsDebugWindow::showEvent(QShowEvent *event) {
    if(conn)
        conn->registerMonitor(this);
}

void CommsDebugWindow::hideEvent(QHideEvent *event) {
    if(conn)
        conn->unregisterMonitor(this);
}

void CommsDebugWindow::setConnection(HX20SerialConnection *conn) {
    if(this->conn && isVisible())
        this->conn->unregisterMonitor(this);
    this->conn = conn;
    if(conn && isVisible())
        conn->registerMonitor(this);
}

void CommsDebugWindow::monitorInput(InputPacketState state,
                                    std::vector<uint8_t> const &bytes) {
    RawDecodePacketInfo pi;
    pi.dir = RawDecodePacketInfo::MasterToSlave;
    pi.raw = bytes;

    DecodeLocation loc;
    loc.base = pi.raw.data();
    loc.begin = 0;
    loc.end = pi.raw.size();
    rawDecoder->decodePacket(pi, loc);

    packetlistmodel->beforeAddPacket();
    packets.push_back(pi);
    packetlistmodel->afterAddPacket();
}

void CommsDebugWindow::monitorOutput(OutputPacketState state,
                                     std::vector<uint8_t> const &bytes) {
    RawDecodePacketInfo pi;
    pi.dir = RawDecodePacketInfo::SlaveToMaster;
    pi.raw = bytes;

    DecodeLocation loc;
    loc.base = pi.raw.data();
    loc.begin = 0;
    loc.end = pi.raw.size();
    rawDecoder->decodePacket(pi, loc);

    packetlistmodel->beforeAddPacket();
    packets.push_back(pi);
    packetlistmodel->afterAddPacket();
}


