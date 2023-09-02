
#define _DEFAULT_SOURCE

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

#define SOH 0x1
#define STX 0x2
#define ETX 0x3
#define EOT 0x4
#define ENQ 0x5
#define ACK 0x6
#define DLE 0x10
#define NAK 0x15
#define WAK DLE

int fd;

#define READb(v) do { uint8_t __b; if(read(fd,&__b,1) != 1) return -1; (v) = __b; } while(0)
#define READSUMb(v) do { uint8_t __b; if(read(fd,&__b,1) != 1) return -1; sum += __b; (v) = __b; } while(0)
#define WRITEb(v) do { uint8_t __b = (v); if(write(fd,&__b,1) != 1) return -1; } while(0)
#define WRITESUMb(v) do { uint8_t __b = (v); if(write(fd,&__b,1) != 1) return -1; sum += __b; } while(0)

int send_packet(uint16_t sid, uint16_t did, uint8_t fnc, uint16_t size,
                 uint8_t *buf) __attribute__((warn_unused_result));
int send_packet(uint16_t sid, uint16_t did, uint8_t fnc, uint16_t size,
                 uint8_t *buf) {
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
    printf("eot = %02x\n", b);


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
        printf("answer: %02x\n",b);
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
        printf("answer: %02x\n",b);
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

#define PHYS_WIDTH 32
#define PHYS_HEIGHT 16
uint8_t width = 80;
uint8_t height = 25;
uint8_t cur_x = 0;
uint8_t cur_y = 0;

int got_packet(uint16_t sid, uint16_t did, uint8_t fnc, uint16_t size,
                uint8_t *inbuf) __attribute__((warn_unused_result));
int got_packet(uint16_t sid, uint16_t did, uint8_t fnc, uint16_t size,
                uint8_t *inbuf) {
    uint8_t b;
    switch(fnc) {
    case 0x80:
        b = 0xff;
        if(send_packet(did, sid, fnc, 1, &b) < 0) return -1;
        break;
    case 0x93:
        b = 0;
        if(send_packet(did, sid, fnc, 1, &b) < 0) return -1;
        break;
    case 0x88: {
        uint8_t buf[2];
        buf[0] = width-1;
        buf[1] = height-1;
        if(send_packet(did, sid, fnc, 2, buf) < 0) return -1;
        break;
    }
    case 0x89: {
        uint8_t buf[2];
        buf[0] = PHYS_WIDTH-1;
        buf[1] = PHYS_HEIGHT-1;
        if(send_packet(did, sid, fnc, 2, buf) < 0) return -1;
        break;
    }
    case 0x8c: {
        uint8_t buf[2];
        buf[0] = cur_x;
        buf[1] = cur_y;
        if(send_packet(did, sid, fnc, 2, buf) < 0) return -1;
        break;
    }
    case 0x8f: {
        uint8_t b;
        printf("sending pixel at %d,%d\n",
               (inbuf[0] << 8) | inbuf[1], (inbuf[2] << 8) | inbuf[3]);
        b = 0;
        if(send_packet(did, sid, fnc, 1, &b) < 0) return -1;
        break;
    }
    case 0x92: {
        printf("writing %c(%02x) at %d,%d\n",
               inbuf[0], inbuf[0],
               cur_x, cur_y);
        uint8_t buf[2];
        buf[0] = cur_x;
        buf[1] = cur_y;
        if(send_packet(did, sid, fnc, 2, buf) < 0) return -1;
        break;
    }
    case 0x98: {
        printf("writing %c(%02x) at %d,%d\n",
               inbuf[0], inbuf[0],
               cur_x, cur_y);
        uint8_t buf[4];
        buf[0] = cur_x;
        buf[1] = cur_y;
        buf[2] = 0;
        buf[3] = 0;
        if(send_packet(did, sid, fnc, 4, buf) < 0) return -1;
        break;
    }
    case 0x91: {
        uint8_t buf[4];
        buf[0] = 0;
        buf[1] = 0;
        buf[2] = cur_x;
        buf[3] = cur_y;
        if(send_packet(did, sid, fnc, 4, buf) < 0) return -1;
        break;
    }
    case 0x97: {
        printf("sending %d char(s) from %d,%d\n",
               inbuf[2], inbuf[0], inbuf[1]);
        uint8_t *b = (uint8_t *)malloc(inbuf[2]);
        memset(b,0,inbuf[2]);
        if(send_packet(did, sid, fnc, inbuf[2], b) < 0) return -1;
        break;
    }
    //hx20 does not want an answer if bit 6 is set
    case 0xc2:
        printf("setting cursor_position to %d,%d\n",
               inbuf[0],inbuf[1]);
        cur_x = inbuf[0];
        cur_y = inbuf[1];
        break;
    case 0xc5:
        break;
    case 0xc6:
        break;
    case 0xc7:
        printf("set point at %d,%d to %d\n",
               (inbuf[0] << 8) | inbuf[1], (inbuf[2] << 8) | inbuf[3], inbuf[4]);
        break;
    case 0xc8:
        printf("set line from %d,%d to %d,%d to %d\n",
               (inbuf[0] << 8) | inbuf[1], (inbuf[2] << 8) | inbuf[3],
               (inbuf[4] << 8) | inbuf[5], (inbuf[6] << 8) | inbuf[7],
               inbuf[8]);
        break;
    case 0xc9:
        break;
    case 0xcf:
        printf("select color set %d\n",inbuf[0]);
        break;
    case 0xd4://screen new?
        break;
    default:
        printf("unknown function %02x\n", fnc);
        exit(1);
    }
    return 0;
}

int enquiry() __attribute__((warn_unused_result));
int enquiry() {
    uint8_t did, sid, enq;

    READb(did);
    READb(sid);
    READb(enq);

    printf("got enquiry did=%02x, sid=%02x, enq=%02x\n",
           did, sid, enq);
    WRITEb(ACK);
    return 0;
}

int have_header = 0;
uint8_t fmt;
uint8_t did_l, did_h;
uint8_t sid_l, sid_h;
uint8_t fnc;
uint8_t siz_l, siz_h;

int header() __attribute__((warn_unused_result));
int header() {
    did_h = 0;
    sid_h = 0;
    siz_h = 0;
    uint8_t hcs;

    uint8_t sum = SOH;

    READSUMb(fmt);
    if(fmt & 4)
        READSUMb(did_h);
    READSUMb(did_l);
    if(fmt & 4)
        READSUMb(sid_h);
    READSUMb(sid_l);
    READSUMb(fnc);
    if(fmt & 2)
        READSUMb(siz_h);
    READSUMb(siz_l);
    READSUMb(hcs);

    printf("got header %02x %04x %04x %02x %04x %02x (%02x)\n",
           fmt,
           did_l | (did_h << 8),
           sid_l | (sid_h << 8),
           fnc,
           siz_l | (siz_h << 8),
           hcs, sum);

    if(sum != 0)
        WRITEb(NAK);
    else
        WRITEb(ACK);
    have_header = sum == 0;
    return 0;
}

int text() __attribute__((warn_unused_result));
int text() {
    uint8_t *buf = (uint8_t *)malloc(siz_l | (siz_h << 8));
    uint8_t etx;
    uint8_t cks;
    uint8_t sum = STX;
    int i;
    for(i = 0; i < (siz_l | (siz_h << 8))+1; i++) {
        READSUMb(buf[i]);
    }
    READSUMb(etx);
    READSUMb(cks);

    printf("received data:");
    for(i = 0; i < (siz_l | (siz_h << 8))+1; i++) {
        if((i & 0xf) == 0)
            printf("\n%04x\t", i);
        if((i & 0x7) == 0)
            printf(" ");
        printf("%02x ",buf[i]);
    }
    printf("\netx=%02x cks=%02x (sum = %02x)\n",
           etx, cks, sum);
    if(sum != 0)
        WRITEb(NAK);
    else
        WRITEb(ACK);

    if(got_packet(sid_l | (sid_h << 8),
                  did_l | (did_h << 8),
                  fnc,
                  (siz_l | (siz_h << 8))+1,
                  buf) < 0)
    {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

int read_packet() __attribute__((warn_unused_result));
int read_packet() {
    while(1) {
        uint8_t b;
        READb(b);

        if(b == 0)
            continue;

        if(b == 0x31) {
            if(enquiry() < 0)
                return -1;
            continue;
        }

        if(b == SOH) {
            if(header() < 0)
                return -1;
            continue;
        }

        if(b == STX) {
            if(have_header) {
                if(text() < 0)
                    return -1;
            } else {
                printf("Bad STX\n");
            }
            continue;
        }

        if(b == EOT) {
            printf("EOT\n");
            continue;
        }

        printf("?? %02x\n",b);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct termios termios_d;

    char const *device = "/dev/ttyUSB0";
    if(argc > 1)
        device = argv[1];

    fd = open(device, O_RDWR);
    if(fd < 0) {
        fprintf(stderr,"Could not open tty device\n");
        return 1;
    }

    tcgetattr(fd, &termios_d);
    cfmakeraw(&termios_d);

    termios_d.c_cflag &= ~CRTSCTS;

    cfsetospeed(&termios_d,B38400);
    cfsetispeed(&termios_d,0);
    tcsetattr(fd, TCSANOW, &termios_d);
    tcflush(fd, TCIOFLUSH);

    while(1) {
        if(read_packet() < 0) {
            fprintf(stderr, "Failed to read packet\n");
            return 1;
        }
    }
}

