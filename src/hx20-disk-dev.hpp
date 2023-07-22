
#pragma once

#include <map>
#include <stdint.h>

#include "hx20-ser-proto.hpp"

class DirSearch;
class FCB;

class HX20DiskDevice : public HX20SerialDevice {
private:
    char *load_buffer;
    DirSearch *dirSearch;
    std::map<uint16_t,FCB *> fcbs;
protected:
    virtual int getDeviceID() const override;
    virtual int gotPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                          uint16_t size, uint8_t *buf,
                          HX20SerialConnection *conn) override;
public:
    HX20DiskDevice();
};


