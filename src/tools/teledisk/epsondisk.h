
#pragma once

#include <stdint.h>

/* This is basically the CP/M 2.2 directory element, as stored on disk.
 *
 * It is parametrized over a few things from the disk parameter block:
 *
 * CP/M 2.2 Disk parameter block
 * =============================
 *
 * SPT:    the total number of sectors per track
 * BSH:    the data allocation block shift factor, determined by the data block
 *         allocation size
 * BLM:    the data allocation block mask (2^(BSH-1))
 * EXM:    the extent mask, determined by the data block allocation size and
 *         the number of disk blocks
 * DSM:    determines the total storage capacity of the disk drive
 *         (the maximum index of any block, including directory blocks)
 * DRM:    determines the total number of directory entries that can be
 *         stored on this drive(the maximum index of any entry)
 * AL[01]: determine reserved directory blocks
 * CKS:    the size of the directory check vector
 * OFF:    the number of reserved tracks at the beginning of the (logical) disk
 *
 *
 * BSH/BLM depend on the data allocation size(block size; BLS)
 * BSH is the number of bits needed to store a position of
 * a CP/M sector(128 bytes) in a block:
 *
 *    BLS   number of sectors  BSH  BLM
 *    1024       8              3    7
 *    2048      16              4   15
 *    4096      32              5   31
 *    8192      64              6   63
 *   16384     128              7  127
 *
 * EXM depends on DSM and BLS, and is a mask for the bits needed to specify a
 *   CP/M sector count on top of the 128 that can be put into the record count
 *
 *     BLS  DSM>255  number of sectors per directory entry   EXM
 *    1024    N         16*  8 = 128                          0
 *    1024    Y          8*  8 =  64                         not allowed; 0 would probably work
 *    2048    N         16* 16 = 256                          1
 *    2048    Y          8* 16 = 128                          0
 *    4096    N         16* 32 = 512                          3
 *    4096    Y          8* 32 = 256                          1
 *    8192    N         16* 64 =1024                          7
 *    8192    Y          8* 64 = 512                          3
 *   16384    N         16*128 =2048                         15
 *   16384    Y          8*128 =1024                          7
 *
 * AL0,AL1 are a bitmask with a 1 bit set for every block required to host
 * the directory. Each directory entry is 32 byte. So, given DRM and BLS, the
 * number of blocks required for the directory is ⌈BLS/(DRM+1)*32⌉. Bits are
 * allocated starting at the most significant bit of AL0.
 *
 * CP/M 2.2 Directory Elements
 * ===========================
 * This attempts to document the on disk format as parametrized by the
 * disk parameter block.
 *
 * US      0xE5 for invalid. High 3 bits are matched against file control
 *         block contents, low 5 bits are the user number(0-31).
 * F1-F8   8 bytes of file name; ASCII, bit7 always 0
 * T1      bit0-6: file type character, ASCII; bit7: read/only
 * T2      bit0-6: file type character, ASCII; bit7: system file
 * T3      bit0-6: file type character, ASCII; bit7 always 0
 * EX      EXM masks the bits for describing the extra 128 sector chunks fit
 *         into this directory element.
 *         The next higher bits up to bit 4 are the index of this extent.
 *         When looking for a directory entry, the bits masked by EXM are
 *         ignored.
 * S1      Unused
 * S2      Extra 4 bits for the extent index. Highest bit is used by CP/M to
 *         mark the file as written to. This bit does not reach the disk.
 *         The assembler source calls this "modnum".
 *         Last field compared when looking for directory entries for the same
 *         file.
 * RC      Numbers of sectors in this directory element, max 128. This is
 *         never 128 unless the directory element is filled.
 *         Never used in directory entry searches.
 * D0-DN   depending on DSM>255, either 16 uint8_t for block numbers, or
 *         8 uint16_t for block numbers
 *
 * Between S2, EX, RC, a file could in theory span up to 65536 sectors.
 * (Which is why CP/M 2.2 has a record number of 2+1 bytes, where
 *  the first two can be 0-65535 and the other byte be 0, or the first
 *  two are 0 and the other byte is 1)
 * EXM is the result of CP/M 2.2 trying to stay backwards compatible with
 * CP/M 1.4, where the EX field was exclusively the directory element index,
 * but being flexible in the number of blocks stored in the directory element.
 * They also call directory elements "extent groups", where all bits of the
 * virtual directory elements have the same index modulo the EXM.
 */
struct epsonDirEnt {
    uint8_t us;//0: valid, 0xe5(or anything else, really) invalid
    char file[8];//bit7 always is 0
    char type[3];//if msb of type[0] set, file is r/o. if msb of type[1]
    //set, file is system file
    uint8_t ex;//bits1-7: index number of this extent, starts at 0.
               //bit0: records in this extent, multiples of 128.
               //      add to rc to get the actual number of records.
    uint8_t s1;
    uint8_t s2;
    uint8_t rc;//number of records in this extent, up to 128 (0-128)
               //      add to (ex & 1)*128 to get the actual number of records.
               //not sure if we get ex.0=1,rc=0 or ex.0=0,rc=128 for 16384 bytes
               //empty files are ex.0=0,rc=0
    uint8_t block[16];//addresses 2k-blocks, starting at 1 after the
    //directory. 0=> not used, 1-139 are valid block numbers
};

