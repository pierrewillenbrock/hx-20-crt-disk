
/* protocol documentation in tms_04.pdf, starting at 4.3, pg 4-3
 */

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
#include <cassert>

#include "hx20-ser-proto.hpp"

/* the tf-20 expects this sequence:
 * EOT
 * {
 *   EOT
 *   (less than 21 timeout loops)
 *   anything
 * |
 *   (less than 21 timeout loops)
 *   0x31
 * }
 * Device ID
 * anything
 * ENQ
 * (sends ACK)|(sends NAK, restart where?)
 * {
 *   EOT
 *   (restarting(where?) after first EOT)
 * |
 *   SOT
 *   fmt
 *   did
 *   sid
 *   fnc
 *   siz
 *   hcs
 *   (sends ACK)|(sends NAK, restart where?)
 *   [
 *     ENQ
 *     (sends ACK)
 *   ]
 *   {
 *     EOT
 *     (restarting(where?) after first EOT)
 *   |
 *     STX
 *     data
 *     ETX
 *     checksum over all of the above
 *     (sends ACK)|(sends NAK, restart where?)
 *     [
 *       ENQ
 *       (sends ACK)
 *     ]
 *     EOT
 *   }
 * }
 *
 * When the TF-20 sends a packet, it does like this:
 * up to 4 restarts over all. Cancels the command if number of restarts greater than 4
 * start:
 *   (send SOH, fmt, did, sid, fnc, siz, hcs)
 * header_recv:
 *   no data or NAK => restart at header_recv |
 *   ACK => continue |
 *   any other data: restart at start
 *
 *   reset restart counter to 4
 * start_buffer:
 *   (send STX, buffer, ETX, hcs)
 * buffer_recv:
 *   no data or NAK => restart at buffer_recv |
 *   ACK => continue |
 *   any other data: restart at start_buffer
 *
 *   (send EOT)
 */

#if 0
#define EPSP_DEBUG(fmt, ...) EPSP_DEBUG(fmt, ## __VA_ARGS__)
#else
#define EPSP_DEBUG(fmt, ...)
#endif

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

HX20SerialDevice::~HX20SerialDevice() =default;

HX20SerialMonitor::~HX20SerialMonitor() =default;

static int readTimeout(int fd, void *buf, size_t nbytes, int timeout) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    int res;
    res = ::poll(&pfd,1,timeout);
    if(res <= 0) {
        return 0;
    }

    return read(fd, buf, nbytes);
}

#define READb(v) do { uint8_t __b; if(read(fd,&__b,1) != 1) return -1; (v) = __b; } while(0)
#define READSUMb(v) do { uint8_t __b; if(read(fd,&__b,1) != 1) return -1; sum += __b; (v) = __b; } while(0)
#define WRITEb(v) do { uint8_t __b(v); if(write(fd,&__b,1) != 1) return -1; } while(0)
#define WRITESUMb(v) do { uint8_t __b(v); if(write(fd,&__b,1) != 1) return -1; sum += __b; } while(0)

int HX20SerialConnection::sendPacket(uint16_t sid, uint16_t did, uint8_t fnc,
                                     uint16_t size, uint8_t *buf) {
    /*
     * When the TF-20 sends a packet, it does like this:
     * up to 4 restarts over all. Cancels the command if number of restarts greater than 4
     * start:
     *   (send SOH, fmt, did, sid, fnc, siz, hcs)
     * header_recv:
     *   no data or NAK => restart at header_recv |
     *   ACK => continue |
     *   any other data: restart at start
     *
     *   reset restart counter to 4
     * start_buffer:
     *   (send STX, buffer, ETX, hcs)
     * buffer_recv:
     *   no data or NAK => restart at buffer_recv |
     *   ACK => continue |
     *   any other data: restart at start_buffer
     *
     *   (send EOT)
     */
    uint8_t sum;
    uint8_t fmt = 1;//slave sending a block to master
    uint8_t b;
    uint16_t siz = size-1;
    int i;

    //The disk device would have at most one byte stored, so just flush
    //all the chatter from the hx-20 out.
    while(true) {
        int res = readTimeout(fd, &b, 1, 0);
        if(res < 0)
            return -1;
        if(res == 0) {
            break;
        }
        for(auto &m : monitors)
            m->monitorInput(HX20SerialMonitor::GotUnassociated,
                            std::vector<uint8_t>(1, b));
    }

    if((did & 0xff00) || (sid & 0xff00))
        fmt |= 0x04;
    if(siz & 0xff00)
        fmt |= 0x02;

    int retries = 4;

    while(1) {
        sum = 0;
        std::vector<uint8_t> data;
        WRITESUMb(SOH);
        data.push_back(SOH);
        WRITESUMb(fmt);
        data.push_back(fmt);

        if(fmt & 0x4) {
            data.push_back(did >> 8);
            WRITESUMb(did >> 8);
        }
        data.push_back(did & 0xff);
        WRITESUMb(did & 0xff);

        if(fmt & 0x4) {
            data.push_back(sid >> 8);
            WRITESUMb(sid >> 8);
        }
        data.push_back(sid & 0xff);
        WRITESUMb(sid & 0xff);

        data.push_back(fnc);
        WRITESUMb(fnc);

        if(fmt & 0x2) {
            data.push_back(siz >> 8);
            WRITESUMb(siz >> 8);
        }
        data.push_back(siz & 0xff);
        WRITESUMb(siz & 0xff);

        data.push_back(-sum);
        WRITEb(-sum);

        for(auto &m : monitors)
            m->monitorOutput(HX20SerialMonitor::SentPacketHeaderRequest, data);

        while(true) {
            int res = readTimeout(fd, &b, 1, 800);
            if(res < 0)
                return -1;
            if(res == 0) {
                b = 0;
                break;
            }
//            EPSP_DEBUG("answer: %02x\n",b);
            for(auto &m : monitors)
                m->monitorInput(HX20SerialMonitor::GotPacketHeaderResponse,
                                std::vector<uint8_t>(1, b));
            if(b == ACK) {
                break;
            } else if(b == NAK || b == EOT) {
                continue;
            } else {
                //WAK handling would be:
                //1) WAK received; wait 1s, send ENQ. if WAK again, stay at 1)
                //2) ACK received, continue.
                EPSP_DEBUG("error: hx20 replied %02x\n",b);
                fflush(stdout);
                break;
            }
        }
        if(b == ACK)
            break;
        retries--;
        if(retries == 0)
            return 1;
    }

    retries = 4;
    while(1) {
        sum = 0;
        std::vector<uint8_t> data;
        WRITESUMb(STX);
        data.push_back(STX);
        for(i = 0; i < size; i++) {
            WRITESUMb(buf[i]);
            data.push_back(buf[i]);
        }
        data.push_back(ETX);
        WRITESUMb(ETX);
        data.push_back(-sum);
        WRITEb(-sum);

        for(auto &m : monitors)
            m->monitorOutput(HX20SerialMonitor::SentPacketTextRequest, data);

        while(true) {
            int res = readTimeout(fd, &b, 1, 800);
            if(res < 0)
                return -1;
            if(res == 0) {
                b = 0;
                break;
            }
//            EPSP_DEBUG("answer: %02x\n",b);
            for(auto &m : monitors)
                m->monitorInput(HX20SerialMonitor::GotPacketTextResponse,
                                std::vector<uint8_t>(1, b));
            if(b == ACK) {
                break;
            } else if(b == NAK || b == EOT) {
                continue;
            } else {
                //WAK handling would be:
                //1) WAK received; wait 1s, send ENQ. if WAK again, stay at 1)
                //2) ACK received, continue.
                EPSP_DEBUG("error: hx20 replied %02x\n",b);
                fflush(stdout);
                break;//restart at sending the header
            }
        }
        if(b == ACK)
            break;
        retries--;
        if(retries == 0)
            return 1;
    }

    WRITEb(EOT);
    for(auto &m : monitors)
        m->monitorOutput(HX20SerialMonitor::SentReverseDirection,
                         std::vector<uint8_t>(1, EOT));
    return 0;
}

void HX20SerialConnection::addByteToBuf(uint8_t b) {
    buf.push_back(b);
}

uint8_t HX20SerialConnection::checkSumBuf() {
    uint8_t sum = 0;
    for(unsigned int i = 0; i < buf.size(); i++)
        sum += buf[i];
    return sum;
}

int HX20SerialConnection::receiveByte(uint8_t b) {
    EPSP_DEBUG("Got %02x in ",b);
    switch(state) {
    case Select: {
        //according to docs, this is: PS <sid> <did> ENQ, which can
        //be followed up with ACK by the <did> device
        EPSP_DEBUG("Select\n");
        addByteToBuf(b);
        if(buf.size() < 4) {
            fflush(stdout);
            return 0;
        }
        selectedSlaveID = buf[1];
        selectedMasterID = buf[2];
        EPSP_DEBUG("Selected 0x%04x => 0x%04x\n",
                   selectedMasterID,
                   selectedSlaveID);
        for(auto &m : monitors)
            m->monitorInput(HX20SerialMonitor::GotSelectRequest, buf);
        buf.clear();
        HX20SerialDevice *dev = devices[selectedSlaveID];
        if(dev) {
            for(auto &m : monitors)
                m->monitorOutput(HX20SerialMonitor::SentSelectResponse,
                                 std::vector<uint8_t>(1, ACK));
            WRITEb(ACK);
        }
        EPSP_DEBUG("\n");
        state = NoHeader;
        fflush(stdout);
        return 0;
    }
    case HaveHeader:
        assert(buf.empty());
        EPSP_DEBUG("HaveHeader");
        if(b == STX) {
            EPSP_DEBUG("\n");
            addByteToBuf(b);
            state = Text;
            fflush(stdout);
            return 0;
        }
        EPSP_DEBUG("->");
    case NoHeader:
        assert(buf.empty());
        EPSP_DEBUG("NoHeader");
        state = NoHeader;
        if(b == SOH) {
            addByteToBuf(b);
            state = Header;
            EPSP_DEBUG("->Header\n");
        } else if(b == PS) {
            state = Select;
            addByteToBuf(b);
        } else {
            for(auto &m : monitors)
                m->monitorInput(HX20SerialMonitor::GotUnassociated, std::vector<uint8_t>(1, b));
            EPSP_DEBUG("\n");
        }
        fflush(stdout);
        return 0;
    case Header: {
        EPSP_DEBUG("Header");
        addByteToBuf(b);
        if(buf.size() < 2) {
            EPSP_DEBUG("\n");
            fflush(stdout);
            return 0;
        }
        uint8_t fmt = buf[1];
        unsigned int size = 1+1+1+1+1+1+1;
        //soh+fmt+did+sid+fnc+siz+hcs
        if(fmt & 0x1) { //should be 0 for MasterToSlave. We ignore it.
        }
        if(fmt & 0x2) //Size is 16 bit
            size++;
        if(fmt & 0x4) //IDs are 16 bit
            size += 2;
        if(buf.size() < size) {
            EPSP_DEBUG("\n");
            fflush(stdout);
            return 0;
        }
        if(checkSumBuf() != 0) {
            for(auto &m : monitors)
                m->monitorInput(HX20SerialMonitor::GotPacketHeaderRequest, buf);
            WRITEb(NAK);
            for(auto &m : monitors)
                m->monitorOutput(HX20SerialMonitor::SentPacketHeaderResponse,
                                 std::vector<uint8_t>(1, NAK));
            buf.clear();
            EPSP_DEBUG("->NoHeader\n");
            state = NoHeader;
            fflush(stdout);
            return 0;
        }

        int pos = 2;//soh+fmt
        if(fmt & 4) {
            did = buf[pos++] << 8;
            did |= buf[pos++];
            sid = buf[pos++] << 8;
            sid |= buf[pos++];
        } else {
            did = buf[pos++];
            sid = buf[pos++];
        }
        fnc = buf[pos++];
        if(fmt & 2) {
            siz = buf[pos++] << 8;
            siz |= buf[pos++];
        } else {
            siz = buf[pos++];
        }

        for(auto &m : monitors)
            m->monitorInput(HX20SerialMonitor::GotPacketHeaderRequest, buf);
        WRITEb(ACK);
        for(auto &m : monitors)
            m->monitorOutput(HX20SerialMonitor::SentPacketHeaderResponse,
                             std::vector<uint8_t>(1, ACK));
        buf.clear();
        EPSP_DEBUG("->HaveHeader\n");
        state = HaveHeader;
        fflush(stdout);
        return 0;
    }
    case Text: {
        EPSP_DEBUG("Text\n");
        addByteToBuf(b);
        if(buf.size() < (unsigned)siz+1+1+1+1) {
            fflush(stdout);
            return 0;
        }
        if(checkSumBuf() != 0) {
            for(auto &m : monitors)
                m->monitorInput(HX20SerialMonitor::GotPacketTextRequest, buf);
            WRITEb(NAK);
            for(auto &m : monitors)
                m->monitorOutput(HX20SerialMonitor::SentPacketTextResponse,
                                 std::vector<uint8_t>(1, NAK));

            buf.clear();
            state = HaveHeader;
            EPSP_DEBUG("->HaveHeader\n");
            fflush(stdout);
            return 0;
        }

        for(auto &m : monitors)
            m->monitorInput(HX20SerialMonitor::GotPacketTextRequest, buf);
        WRITEb(ACK);
        for(auto &m : monitors)
            m->monitorOutput(HX20SerialMonitor::SentPacketTextResponse,
                             std::vector<uint8_t>(1, ACK));
        state = EndOfText;
        EPSP_DEBUG("->EndOfText\n");
        fflush(stdout);
        break;
    }
    case EndOfText: {
        EPSP_DEBUG("EndOfText");
        for(auto &m : monitors)
            m->monitorInput(HX20SerialMonitor::GotPacketTextEnd,
                            std::vector<uint8_t>(1, b));
        if(b == ENQ) {
            WRITEb(ACK);
            for(auto &m : monitors)
                m->monitorOutput(HX20SerialMonitor::SentPacketTextResponse,
                                 std::vector<uint8_t>(1, ACK));
            break;
        } else if(b != EOT) {
            break;
        }
        EPSP_DEBUG("->HaveHeader\n");
        fflush(stdout);
        state = HaveHeader;

        HX20SerialDevice *dev = devices[did];

        if(dev) {
            fflush(stdout);
            int res = dev->gotPacket(
                      did, sid, fnc, siz+1, buf.data()+1,
                      this);
            buf.clear();
            return res;
        }
        buf.clear();
        fflush(stdout);
        return 0;
    }
    default:
        EPSP_DEBUG("Unknown??\n");
        fflush(stdout);
        break;
    }
    return 0;
}

HX20SerialConnection::HX20SerialConnection(char const *device) :
    state(NoHeader) {
    struct termios termios_d;

    fd = open(device, O_RDWR);
    if(fd == -1)
        throw IOError(errno, std::system_category(), "Could not open device");

    if(tcgetattr(fd, &termios_d) == -1)
        throw IOError(errno, std::system_category(), "Get terminal attributes failed");
    cfmakeraw(&termios_d);

    termios_d.c_cflag &= ~CRTSCTS;

    cfsetospeed(&termios_d,B38400);
    cfsetispeed(&termios_d,B38400);
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
    EPSP_DEBUG("Registering 0x%02x for %s\n",
               dev->getDeviceID(), typeid(dev).name());
    fflush(stdout);
    if(devices.find(dev->getDeviceID()) != devices.end())
        throw std::runtime_error("There already is a device with the same ID");
    devices[dev->getDeviceID()] = dev;
}

void HX20SerialConnection::unregisterDevice(HX20SerialDevice *dev) {
    devices.erase(dev->getDeviceID());
}

void HX20SerialConnection::registerMonitor(HX20SerialMonitor *mon) {
    monitors.insert(mon);
}

void HX20SerialConnection::unregisterMonitor(HX20SerialMonitor *mon) {
    monitors.erase(mon);
}


