
#pragma once

#include <stdint.h>

struct epsonDirEnt {
    uint8_t us;//0: valid, 0xe5(or anything else, really) invalid
    char file[8];
    char type[3];//if msb of type[0] set, file is r/o. if msb of type[1]
    //set, file is system file
    uint8_t ex;//bits1-7: index number of this extent, starts at 0.
               //bit0: records in this extent, multiples of 128.
               //      add to rc to get the actual number of records.
    uint8_t unused[2];
    uint8_t rc;//number of records in this extent, up to 128 (0-128)
               //      add to (ex & 1)*128 to get the actual number of records.
               //not sure if we get ex.0=1,rc=0 or ex.0=0,rc=128 for 16384 bytes
               //empty files are ex.0=0,rc=0
    uint8_t block[16];//addresses 2k-blocks, starting at 1 after the
    //directory. 0=> not used, 1-139 are valid block numbers
};

