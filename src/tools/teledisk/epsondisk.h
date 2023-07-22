
#pragma once

#include <stdint.h>

struct epsonDirEnt {
    uint8_t us;//0: valid, 0xe5(or anything else, really) invalid
    char file[8];
    char type[3];//if msb of type[0] set, file is r/o. if msb of type[1]
    //set, file is system file
    uint8_t ex;//number of extents, 16 blocks each(128 records) of file. 0-17.
    uint8_t unused[2];
    uint8_t rc;//number of records in file(0-128)
    uint8_t block[16];//addresses 2k-blocks, starting at 1 after the
    //directory. 0=> not used, 1-139 are valid block numbers
};

