
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <termios.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <sys/ioctl.h>

#include "hx20-ser-proto.hpp"

/* the tf-20 expects this sequence:
  EOT
  {
    EOT
    (less than 21 timeout loops)
    anything
  |
    (less than 21 timeout loops)
    0x31
  }
  Device ID
  anything
  ENQ
  (sends ACK)
  {
    EOT
    (restarting after first EOT)
  |
    SOT
    fmt
    did
    sid
    fnc
    siz
    hcs
    (sends ACK)
    [
      ENQ
      (sends ACK)
    ]
    {
      EOT
      (restarting after first EOT)
    |
      STX
      data
      ETX
      checksum over all of the above
      (sends ACK)
      [
        ENQ
        (sends ACK)
      ]
      EOT
    }
  }
 */

#define SOH 0x1
#define STX 0x2
#define ETX 0x3
#define EOT 0x4
#define ENQ 0x5
#define ACK 0x6
#define DLE 0x10
#define NAK 0x15
#define WAK DLE

#define READb(v) do { uint8_t __b; if(read(fd,&__b,1) != 1) return -1; (v) = __b; } while(0)
#define READSUMb(v) do { uint8_t __b; if(read(fd,&__b,1) != 1) return -1; sum += __b; (v) = __b; } while(0)
#define WRITEb(v) do { uint8_t __b(v); if(write(fd,&__b,1) != 1) return -1; } while(0)
#define WRITESUMb(v) do { uint8_t __b(v); if(write(fd,&__b,1) != 1) return -1; sum += __b; } while(0)

int HX20SerialConnection::sendPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                                      uint16_t size, uint8_t *buf) {
    uint8_t sum;
    uint8_t fmt = 1;
    uint8_t b;
    uint16_t siz = size-1;
    int i;
    if((did & 0xff00) || (sid & 0xff00))
        fmt |= 0x04;
    if(siz & 0xff00)
        fmt |= 0x02;

    READb(b);
//    printf("eot = %02x\n", b);


    while(1) {
        sum = 0;
        WRITESUMb(SOH);
        WRITESUMb(fmt);

        if(fmt & 0x4)
            WRITESUMb(did >> 8);
        WRITESUMb(did & 0xff);

        if(fmt & 0x4)
            WRITESUMb(sid >> 8);
        WRITESUMb(sid & 0xff);

        WRITESUMb(fnc);

        if(fmt & 0x4)
            WRITESUMb(siz >> 8);
        WRITESUMb(siz & 0xff);

        WRITEb(-sum);

        READb(b);
//        printf("answer: %02x\n",b);
        if(b == ACK)
            break;
        else if(b == (WAK >> 8)) {
            READb(b);
            if(b != (WAK & 0xff))
                return -1;
            usleep(100000);
        } else
            return -1;
    }

    while(1) {
        sum = 0;
        WRITESUMb(STX);
        for(i = 0; i < size; i++) {
            WRITESUMb(buf[i]);
        }
        WRITESUMb(ETX);
        WRITEb(-sum);

        READb(b);
//        printf("answer: %02x\n",b);
        if(b == ACK)
            break;
        else if(b == (WAK >> 8)) {
            READb(b);
            if(b != (WAK & 0xff))
                return -1;
            usleep(100000);
        } else
            return -1;
    }

    WRITEb(EOT);
    return 0;
}

void HX20SerialConnection::addByteToBuf(uint8_t b) {
    if(buf_pos >= buf_size) {
        buf_size += 256;
        buf = (uint8_t *)realloc(buf,buf_size);
    }
    buf[buf_pos++] = b;
}

uint8_t HX20SerialConnection::checkSumBuf() {
    int i;
    uint8_t sum = 0;
    for(i = 0; i < buf_pos; i++)
        sum += buf[i];
    return sum;
}

int HX20SerialConnection::receiveByte(uint8_t b) {
    printf("Got %02x in ",b);
    switch(state) {
    case Disconnected:
        printf("Disconnected\n");
        if(b == 0x31) {
            state = Select;
            addByteToBuf(b);
        }
        return 0;
    case Select: {
        printf("Select\n");
        addByteToBuf(b);
        if(buf_pos < 3)
            return 0;
        selectedSlaveID = buf[1];
        selectedMasterID = buf[2];
        printf("Selected 0x%04x => 0x%04x\n",
               selectedMasterID,
               selectedSlaveID);
        buf_pos = 0;
        state = NoHeader;
        return 0;
    }
    case HaveHeader:
        printf("HaveHeader");
        if(b == STX) {
            printf("\n");
            addByteToBuf(b);
            state = Text;
            return 0;
        }
        printf("->");
    case NoHeader:
        printf("NoHeader\n");
        if(b == ENQ) {
            HX20SerialDevice *dev = devices[selectedSlaveID];
            if(dev) {
                WRITEb(ACK);
            }
        } else if(b == SOH) {
            addByteToBuf(b);
            state = Header;
        }
        return 0;
    case Header: {
        printf("Header\n");
        addByteToBuf(b);
        if(buf_pos < 2)
            return 0;
        uint8_t fmt = buf[1];
        int size = 1+1+1+1+1+1+1;
        //soh+fmt+did+sid+fnc+siz+hcs
        if(fmt & 0x2)
            size++;
        if(fmt & 0x4)
            size += 2;
        if(buf_pos < size)
            return 0;
        if(checkSumBuf() != 0) {
            WRITEb(NAK);
            buf_pos = 0;
            state = NoHeader;
            return 0;
        }

        buf_pos = 2;//soh+fmt
        if(fmt & 4) {
            did = buf[buf_pos++] << 8;
            did |= buf[buf_pos++];
            sid = buf[buf_pos++] << 8;
            sid |= buf[buf_pos++];
        } else {
            did = buf[buf_pos++];
            sid = buf[buf_pos++];
        }
        fnc = buf[buf_pos++];
        if(fmt & 2) {
            siz = buf[buf_pos++] << 8;
            siz |= buf[buf_pos++];
        } else {
            siz = buf[buf_pos++];
        }

        WRITEb(ACK);
        buf_pos = 0;
        state = HaveHeader;
        return 0;
    }
    case Text: {
        printf("Text\n");
        addByteToBuf(b);
        if(buf_pos < siz+1+1+1+1)
            return 0;
        if(checkSumBuf() != 0) {
            WRITEb(NAK);
            buf_pos = 0;
            state = HaveHeader;
            return 0;
        }

        WRITEb(ACK);
        state = HaveHeader;

        HX20SerialDevice *dev = devices[selectedSlaveID];

        if(dev) {
            return dev->gotPacket(
                   did, sid, fnc, siz+1, buf+1,
                   this);
        }
        return 0;
    }
    default:
        printf("Unknown??\n");
        break;
    }
    return 0;
}

HX20SerialConnection::HX20SerialConnection(char const *device) :
    state(Disconnected), buf(NULL), buf_size(0), buf_pos(0) {
    struct termios termios_d;

    fd = open(device, O_RDWR);
    if(fd == -1)
        throw IOError(errno, std::system_category(), "Could not open device");

    if(tcgetattr(fd, &termios_d) == -1)
        throw IOError(errno, std::system_category(), "Get terminal attributes failed");
    cfmakeraw(&termios_d);

    termios_d.c_cflag &= ~CRTSCTS;

    cfsetospeed(&termios_d,B38400);
    cfsetispeed(&termios_d,0);
    if(tcsetattr(fd, TCSANOW, &termios_d) == -1)
        throw IOError(errno, std::system_category(), "Set terminal attributes failed");
    if(tcflush(fd, TCIOFLUSH) == -1)
        throw IOError(errno, std::system_category(), "Flush terminal failed");

    int i = TIOCM_DTR; // Pin DTR wird deaktiviert (-12V)
    // interessiert aber iirc niemanden?
    if(ioctl(fd, TIOCMBIC, &i) == -1)
        throw IOError(errno, std::system_category(), "Setting DTR failed");
}

HX20SerialConnection::~HX20SerialConnection() {
    close(fd);
}

int HX20SerialConnection::poll() {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    int res;
    while((res = ::poll(&pfd,1,0)) > 0) {
        if(pfd.revents & POLLIN) {
            uint8_t b;
            READb(b);
            if(receiveByte(b) < 0)
                return -1;
        }
    }
    if(res < 0)
        return -1;
    return 0;
}

void HX20SerialConnection::loop() {
    while(1) {
        if(poll() < 0)
            return;
    }
}

int HX20SerialConnection::getNfds() const {
    return 1;
}

void HX20SerialConnection::fillPollFd(struct pollfd *pfd) const {
    pfd->fd = fd;
    pfd->events = POLLIN;
}

int HX20SerialConnection::handleEvents(struct pollfd const *pfd, int nfds) {
    if(nfds < 1)
        return 0;
    if(pfd->revents & POLLIN) {
        return poll();
    }
    return 0;
}

void HX20SerialConnection::registerDevice(HX20SerialDevice *dev) {
    devices[dev->getDeviceID()] = dev;
}

void HX20SerialConnection::unregisterDevice(HX20SerialDevice *dev) {
    devices.erase(dev->getDeviceID());
}

