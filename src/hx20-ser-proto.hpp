
#pragma once

#include <stdint.h>
#include <unordered_map>
#include <unordered_set>
#include <system_error>
#include <vector>

struct pollfd;
class HX20SerialConnection;

class IOError : public std::system_error {
public:
    IOError(std::error_code ec) : system_error(ec) {}
    IOError(std::error_code ec, const std::string &what_arg) : system_error(ec, what_arg) {}
    IOError(std::error_code ec, const char *what_arg) : system_error(ec, what_arg) {}
    IOError(int ev, const std::error_category& ecat) : system_error(ev, ecat) {}
    IOError(int ev, const std::error_category& ecat, const std::string &what_arg) : system_error(ev, ecat, what_arg) {}
    IOError(int ev, const std::error_category& ecat, const char *what_arg) : system_error(ev, ecat, what_arg) {}
};

class HX20SerialDevice {
protected:
    virtual int getDeviceID() const = 0;
    virtual ~HX20SerialDevice() = 0;
    virtual
    __attribute__((warn_unused_result))
    int gotPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                          uint16_t size, uint8_t *buf,
                          HX20SerialConnection *conn) = 0;

    friend class HX20SerialConnection;
};

class HX20SerialMonitor {
public:
    enum InputPacketState {
        GotUnassociated,
        GotPacketHeaderRequest,
        GotPacketTextRequest,
        GotSelectRequest,
        GotReverseDirection,
        GotPacketHeaderResponse,
        GotPacketTextResponse
    };
    enum OutputPacketState {
        SentPacketHeaderRequest,
        SentPacketTextRequest,
        SentPacketHeaderResponse,
        SentPacketTextResponse,
        SentSelectResponse,
        SentReverseDirection
    };
protected:
    virtual ~HX20SerialMonitor();

    virtual void monitorInput(InputPacketState state,
                              std::vector<uint8_t> const &bytes) {}
    virtual void monitorOutput(OutputPacketState state,
                               std::vector<uint8_t> const &bytes) {}

    friend class HX20SerialConnection;
};

class HX20SerialConnection {
private:
    enum State {
        NoHeader,
        Select,
        Header,
        HaveHeader,
        Text
    };
    int fd;

    std::unordered_map<int, HX20SerialDevice *> devices;
    std::unordered_multiset<HX20SerialMonitor *> monitors;

    enum State state;

    std::vector<uint8_t> buf;
    void addByteToBuf(uint8_t b);
    uint8_t checkSumBuf();

    uint16_t selectedSlaveID, selectedMasterID;

    uint16_t did;
    uint16_t sid;
    uint8_t fnc;
    uint16_t siz;//size-1

    __attribute__((warn_unused_result))
    int receiveByte(uint8_t b);
public:
    HX20SerialConnection(char const *device);
    ~HX20SerialConnection();
    __attribute__((warn_unused_result))
    int poll();
    void loop();
    int getNfds() const;
    void fillPollFd(struct pollfd *pfd) const;
    __attribute__((warn_unused_result))
    int handleEvents(struct pollfd const *pfd, int nfds);

    void registerDevice(HX20SerialDevice *dev);
    void unregisterDevice(HX20SerialDevice *dev);
    void registerMonitor(HX20SerialMonitor* mon);
    void unregisterMonitor(HX20SerialMonitor* mon);

    __attribute__((warn_unused_result))
    int sendPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                   uint16_t size, uint8_t *buf);
};

