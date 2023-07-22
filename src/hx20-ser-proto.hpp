
#pragma once

#include <stdint.h>
#include <map>
#include <system_error>

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
    virtual
    __attribute__((warn_unused_result))
    int gotPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                          uint16_t size, uint8_t *buf,
                          HX20SerialConnection *conn) = 0;

    friend class HX20SerialConnection;
};

class HX20SerialConnection {
private:
    enum State {
        Disconnected,
        Select,
        NoHeader,
        Header,
        HaveHeader,
        Text
    };
    int fd;

    std::map<int, HX20SerialDevice *> devices;

    enum State state;

    uint8_t *buf;
    int buf_size;
    int buf_pos;
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

    __attribute__((warn_unused_result))
    int sendPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                   uint16_t size, uint8_t *buf);
};

