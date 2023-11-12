
#include "tf20-adapters.hpp"

#include <string.h>

std::string hx20ToUnixFilename(uint8_t const *src) {
    char dst[13];
    char *fp = dst;
    memcpy(fp,src,8);
    for(fp += 7; *fp == ' ' && fp >= dst; fp--) {}
    fp++;
    *fp = '.';
    fp++;
    memcpy(fp,src+8,3);
    for(fp += 2; *fp == ' '; fp--) {}
    fp++;
    *fp = 0;
    for(fp = dst; *fp; fp++) {
        *fp &= 0x7f;
    }
    return dst;
}

