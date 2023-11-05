/*
 * All rights reserved. Permission granted for non-commercial use.
 */
/**************************************************************
 lzhuf.c
 written by Haruyasu Yoshizaki 11/20/1988
 some minor changes 4/6/1989
 comments translated by Haruhiko Okumura 4/7/1989
 **************************************************************/

/*
 LZHUF.C (c)1989 by Haruyasu Yoshizaki, Haruhiko Okumura, and Kenji Rikitake.
 All rights reserved. Permission granted for non-commercial use.
 */
/*
 * * "Converted" to c++
 * * Moved everything into a namespace
 * * Converted the state to a context struct
 * * Changed internal functions and arrays to static
 * * Fixed a warning
 * * Removed the 32bit length header from the encoded result
 */

#include "lzh.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <deque>
#include <assert.h>
#include <algorithm>

namespace lzh {

/********** LZSS compression **********/

#define N           4096 /* buffer size */
#define F           60   /* lookahead buffer size */
#define THRESHOLD   2    /* maximum number of copyable bytes to instead store as single bytes */
#define NIL         N    /* leaf of tree */

/* Huffman coding */

#define N_CHAR   (256 + F - THRESHOLD) // = 314
/* number of kinds of characters (character code = 0..N_CHAR-1)
 * these are the plain char encodings and the possible copy length encodings */
#define T        (N_CHAR * 2 - 1)    /* size of table = 627 */
#define R        (T - 1)         /* position of root = 626 */
#define MAX_FREQ 0x8000      /* updates tree when the
                              * root frequency comes to this value. */

typedef unsigned char uchar;

struct HuffContext {
private:
    unsigned short freq[T + 1];
public:
    unsigned short prnt[T + N_CHAR];   /* pointers to parent nodes, except for the
                                        * elements [T..T + N_CHAR - 1] which are used to get
                                        * the positions of leaves corresponding to the codes. */

    unsigned short son[T];     /* pointers to child nodes (son[], son[] + 1) */

    void StartHuff();
    void reconst();
    void update(unsigned int c);
};

class BitWriterContext {
private:
    unsigned int putbuf = 0;
    uchar putlen = 0;
    unsigned char *output = nullptr;
    unsigned int output_len = 0;
    /** short term storage of a small amount of output data
     * Usually no more than 4 bytes. Used to be able to output
     * a complete "command" when there is not enough real output
     * available.
     */
    std::deque<unsigned char> saved_data;
public:

    /**
     * \param l Number of bits in c
     * \param c Bits; valid starting bit 15 down to bit 16-l
     */
    void Putcode(int l, unsigned int c);     /* output c bits of code */
    void EncodeEnd();
    bool empty();
    /**
     * @param output buffer to write to
     * @param output_len length of buffer
     * @return unused length remaining in original buffer before setOutput
     */
    unsigned int setOutput(unsigned char *output, unsigned int output_len);
};

struct TreeContext {
    //unsigned char text_buf[N];
    unsigned char text_buf[N + F - 1];
    unsigned short match_position, match_length, lson[N + 1], rson[N + 257], dad[N + 1];

    void InitTree();
    void InsertNode(int r);
    void DeleteNode(int p);
};

struct EncodeContext {
    TreeContext tree;

    BitWriterContext bitwriter;
    HuffContext huff;

    struct FunctionContext {
        int len, r, s;
        enum State {
            Data,
            Final,
            Finished
        } state;
    };
    FunctionContext f;

    void EncodeChar(unsigned c);
    void EncodePosition(unsigned c);
};

class BitReaderContext {
private:
    unsigned short getbuf = 0;
    uchar getlen = 0;
    unsigned char const *input = nullptr;
    unsigned int input_len = 0;
    /** short term storage of a small amount of input data
     * Usually no more than 4 bytes. Used to be able to eventually
     * input a complete "command" when there is not enough real input
     * available in any Decode call(for example if it is fed single bytes).
     */
    std::deque<unsigned char> saved_data;
public:

    int GetBit();    /* get one bit */
    int GetByte();   /* get one byte */
    /**
     * @param input buffer to read from
     * @param input_len length of buffer
     * @return data length remaining in original buffer before setInput
     */
    unsigned int setInput(unsigned char const *input, unsigned int input_len);
};

struct DecodeContext {
    BitReaderContext bitreader;
    HuffContext huff;

    struct FunctionContext {
        std::deque<unsigned char> saved_data;
        unsigned char text_buf[N];
        unsigned int r;
    };

    FunctionContext f;

    int DecodeChar();
    int DecodePosition();
};

/********** LZSS compression **********/

void TreeContext::InitTree()  /* initialize trees */
{
    int  i;

    for (i = N + 1; i <= N + 256; i++)
        rson[i] = NIL;          /* root */
    for (i = 0; i < N; i++)
        dad[i] = NIL;           /* node */
}

void TreeContext::InsertNode(int r)  /* insert to tree */
{
    int  i, p, cmp;
    unsigned int c;

    cmp = 1;
    p = N + 1 + text_buf[r & (N - 1)];
    rson[r] = lson[r] = NIL;
    match_length = 0;
    for ( ; ; ) {
        if (cmp >= 0) {
            if (rson[p] != NIL)
                p = rson[p];
            else {
                rson[p] = r;
                dad[r] = p;
                return;
            }
        } else {
            if (lson[p] != NIL)
                p = lson[p];
            else {
                lson[p] = r;
                dad[r] = p;
                return;
            }
        }
        for(i = 1; i < F; i++)
            if((cmp = text_buf[(r + i) & (N - 1)] -
                      text_buf[(p + i) & (N - 1)]) != 0)
                break;
        if (i > THRESHOLD) {
            if (i > match_length) {
                match_position = ((r - p) & (N - 1)) - 1;
                if ((match_length = i) >= F)
                    break;
            }
            if (i == match_length) {
                if ((c = ((r - p) & (N - 1)) - 1) < match_position) {
                    match_position = c;
                }
            }
        }
    }
    dad[r] = dad[p];
    lson[r] = lson[p];
    rson[r] = rson[p];
    dad[lson[p]] = r;
    dad[rson[p]] = r;
    if (rson[dad[p]] == p)
        rson[dad[p]] = r;
    else
        lson[dad[p]] = r;
    dad[p] = NIL;  /* remove p */
}

void TreeContext::DeleteNode(int p)  /* remove from tree */
{
    int  q;

    if (dad[p] == NIL)
        return;         /* not registered */
    if (rson[p] == NIL)
        q = lson[p];
    else
        if (lson[p] == NIL)
            q = rson[p];
        else {
            q = lson[p];
            if (rson[q] != NIL) {
                do {
                    q = rson[q];
                } while (rson[q] != NIL);
                rson[dad[q]] = lson[q];
                dad[lson[q]] = dad[q];
                lson[q] = lson[p];
                dad[lson[p]] = q;
            }
            rson[q] = rson[p];
            dad[rson[p]] = q;
        }
    dad[q] = dad[p];
    if (rson[dad[p]] == p)
        rson[dad[p]] = q;
    else
        lson[dad[p]] = q;
    dad[p] = NIL;
}

/* Huffman coding */

/* tables for encoding and decoding the upper 6 bits of position */

/* for encoding */
static uchar const p_len[64] = {
    0x03, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08
};

static uchar const p_code[64] = {
    0x00, 0x20, 0x30, 0x40, 0x50, 0x58, 0x60, 0x68,
    0x70, 0x78, 0x80, 0x88, 0x90, 0x94, 0x98, 0x9C,
    0xA0, 0xA4, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xBC,
    0xC0, 0xC2, 0xC4, 0xC6, 0xC8, 0xCA, 0xCC, 0xCE,
    0xD0, 0xD2, 0xD4, 0xD6, 0xD8, 0xDA, 0xDC, 0xDE,
    0xE0, 0xE2, 0xE4, 0xE6, 0xE8, 0xEA, 0xEC, 0xEE,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
    0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};

/* for decoding */
static uchar const d_code[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
    0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F,
    0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11,
    0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
    0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15,
    0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
    0x18, 0x18, 0x19, 0x19, 0x1A, 0x1A, 0x1B, 0x1B,
    0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1F, 0x1F,
    0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23,
    0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
    0x28, 0x28, 0x29, 0x29, 0x2A, 0x2A, 0x2B, 0x2B,
    0x2C, 0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2F, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
};

static uchar const d_len[256] = {
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
};

int BitReaderContext::GetBit()    /* get one bit */
{
    unsigned short i;

    while ((input_len > 0 && getlen <= 8) || getlen < 1) {
        if(saved_data.empty()) {
            if(input_len <= 0)
                return -1;
            i = *input++;
            input_len--;
        } else {
            i = saved_data.front();
            saved_data.pop_front();
        }
        getbuf |= i << (8 - getlen);
        getlen += 8;
    }
    i = getbuf;
    getbuf <<= 1;
    getlen--;
    return (i >> 15);
}

int BitReaderContext::GetByte()   /* get one byte */
{
    unsigned short i;

    while (getlen <= 8) {
        if(saved_data.empty()) {
            if(input_len <= 0)
                return -1;
            i = *input++;
            input_len--;
        } else {
            i = saved_data.front();
            saved_data.pop_front();
        }
        getbuf |= i << (8 - getlen);
        getlen += 8;
    }
    i = getbuf;
    getbuf <<= 8;
    getlen -= 8;
    return (i >> 8);
}

unsigned int BitReaderContext::setInput(unsigned char const *input, unsigned int input_len) {
    if(this->input) {
        while(this->input_len > 0 && saved_data.size() < 4) {
            saved_data.push_back(*this->input++);
            this->input_len--;
        }
    }
    unsigned int res = this->input_len;
    this->input = input;
    this->input_len = input_len;
    return res;
}

void BitWriterContext::Putcode(int l, unsigned int c)     /* output c bits of code */
{
    //c is 16 bits wide, bits are valid starting bit 15 down to bit 16-l.
    putbuf |= c >> putlen;
    putlen += l;
    if (putlen >= 8)
    {
        if(output_len <= 0) {
            saved_data.push_back(putbuf >> 8);
        } else {
            *output++ = putbuf >> 8;
            output_len--;
        }
        putlen -= 8;
        putbuf <<= 8;
        if (putlen >= 8)
        {
            if(output_len <= 0) {
                saved_data.push_back(putbuf >> 8);
            } else {
                *output++ = putbuf >> 8;
                output_len--;
            }
            putlen -= 8;
            putbuf = c << (l - putlen);
        }
    }
}

void BitWriterContext::EncodeEnd()
{
    if (putlen) {
        if(output_len <= 0) {
            saved_data.push_back(putbuf >> 8);
        } else {
            *output++ = putbuf >> 8;
            output_len--;
        }
    }
    putlen = 0;
}

bool BitWriterContext::empty() {
    return saved_data.empty();
}

unsigned int BitWriterContext::setOutput(unsigned char *output, unsigned int output_len) {
    unsigned int res = this->output_len;
    this->output = output;
    this->output_len = output_len;
    if(this->output && this->output_len) {
        while(!saved_data.empty() && this->output_len > 0) {
            *this->output++ = saved_data.front();
            this->output_len--;
            saved_data.pop_front();
        }
    }
    return res;
}

/* initialization of tree */

void HuffContext::StartHuff()
{
    unsigned short i, j;

    for (i = 0; i < N_CHAR; i++)
    {
        freq[i] = 1;
        son[i] = i + T;
        prnt[i + T] = i;
    }
    i = 0; j = N_CHAR;
    while (j <= R)
    {
        freq[j] = freq[i] + freq[i + 1];
        son[j] = i;
        prnt[i] = prnt[i + 1] = j;
        i += 2;
        j++;
    }
    freq[T] = 0xffff;
    prnt[R] = 0;
}


/* reconstruction of tree */

void HuffContext::reconst()
{
    int i, j, k;
    unsigned f, l;

    /* collect leaf nodes in the first half of the table */
    /* and replace the freq by (freq + 1) / 2. */
    j = 0;
    for (i = 0; i < T; i++)
    {
        if (son[i] >= T)
        {
            freq[j] = (freq[i] + 1) / 2;
            son[j] = son[i];
            j++;
        }
    }
    /* begin constructing tree by connecting sons */
    for (i = 0, j = N_CHAR; j < T; i += 2, j++)
    {
        k = i + 1;
        f = freq[j] = freq[i] + freq[k];
        for (k = j - 1; f < freq[k]; k--);
        k++;
        l = (j - k) * 2;
        memmove(&freq[k + 1], &freq[k], l);
        freq[k] = f;
        memmove(&son[k + 1], &son[k], l);
        son[k] = i;
    }
    /* connect prnt */
    for (i = 0; i < T; i++)
    {
        if ((k = son[i]) >= T)
            prnt[k] = i;
        else
            prnt[k] = prnt[k + 1] = i;
    }
}


/* increment frequency of given code by one, and update tree */
void HuffContext::update(unsigned int c)
{
    unsigned int i, j, k, l;

    if (freq[R] == MAX_FREQ)
        reconst();

    c = prnt[c + T];
    do {
        k = ++freq[c];

        /* if the order is disturbed, exchange nodes */
        if (k > freq[l = c + 1]) {
            while (k > freq[++l]);
            l--;
            freq[c] = freq[l];
            freq[l] = k;

            i = son[c];
            prnt[i] = l;
            if (i < T) prnt[i + 1] = l;

            j = son[l];
            son[l] = i;

            prnt[j] = c;
            if (j < T) prnt[j + 1] = c;
            son[c] = j;

            c = l;
        }
    } while ((c = prnt[c]) != 0);   /* repeat up to root */
}

void EncodeContext::EncodeChar(unsigned c)
{
    //needs up to 2 bytes in output
    unsigned i;
    int j, k;

    i = 0;
    j = 0;
    k = huff.prnt[c + T];

    /* travel from leaf to root */
    do {
        i >>= 1;

        /* if node's address is odd-numbered, choose bigger brother node */
        if (k & 1) i += 0x8000;

        j++;
    } while ((k = huff.prnt[k]) != R);
    bitwriter.Putcode(j, i);
    //code = i;
    //len = j;
    huff.update(c);
}

void EncodeContext::EncodePosition(unsigned c)
{
    //needs up to 2 bytes in output
    unsigned i;

    /* output upper 6 bits by table lookup */
    i = c >> 6;
    bitwriter.Putcode(p_len[i], (unsigned)p_code[i] << 8);

    /* output lower 6 bits verbatim */
    bitwriter.Putcode(6, (c & 0x3f) << 10);
}

int DecodeContext::DecodeChar()
{
    unsigned int c;

    c = huff.son[R];

    /* travel from root to leaf, */
    /* choosing the smaller child node (son[]) if the read bit is 0, */
    /* the bigger (son[]+1} if 1 */
    while (c < T) {
        int bit = bitreader.GetBit();
        if(bit == -1) {
            //caller must roll back bitreader for us.
            return -1;
        }
        c += bit;
        c = huff.son[c];
    }
    c -= T;

    return c;
}

int DecodeContext::DecodePosition()
{
    int i;
    unsigned int j, c;

    /* recover upper 6 bits from table */
    i = bitreader.GetByte();
    if(i == -1) {
        //caller must roll back bitreader for us.
        return -1;
    }
    c = (unsigned int)d_code[i] << 6;
    j = d_len[i];

    /* read lower 6 bits verbatim */
    j -= 2;
    while (j--)
    {
        int bit = bitreader.GetBit();
        if(bit == -1) {
            //caller must roll back bitreader for us.
            return -1;
        }
        i = (i << 1) + bit;
    }
    return c | (i & 0x3f);
}

/* compression */
EncodeContext *BeginEncode() {
    EncodeContext *context = new EncodeContext();
    int  i;

    context->huff.StartHuff();
    context->tree.InitTree();
    context->f.s = 0;
    context->f.r = N - F;
    for (i = 0; i < N - F; i++) {
        context->f.s = (context->f.s + 1) & (N - 1);
        context->f.r = (context->f.r + 1) & (N - 1);
        context->tree.text_buf[i] = ' ';
    }

    context->f.len = 0;

    context->tree.InsertNode(context->f.r - F);
    context->f.state = EncodeContext::FunctionContext::Data;

    return context;
}

void EndEncode(EncodeContext *context) {
    delete context;
}

bool EncodeFinal(unsigned char *out, unsigned int &outlen,
              EncodeContext *context) {
    context->bitwriter.setOutput(out, outlen);

    if(context->f.state == EncodeContext::FunctionContext::Data) {
        context->f.state = EncodeContext::FunctionContext::Final;
        for(int i = context->f.len; i < F; i++) {
            context->tree.DeleteNode(context->f.s);
            context->f.s = (context->f.s + 1) & (N - 1);
            context->f.r = (context->f.r + 1) & (N - 1);
            context->tree.InsertNode(context->f.r);
        }
    }

    //len is the number of valid bytes in the look-ahead area of text_buf.
    //We only run this if len is F or these are the final bits of data.
    while(context->f.len > 0 && context->bitwriter.empty()) {
        int last_match_length = context->tree.match_length;
        if (last_match_length > context->f.len)
            last_match_length = context->f.len;
        if (last_match_length <= THRESHOLD)
        {
            last_match_length = 1;
            context->EncodeChar(context->tree.text_buf[context->f.r]);
        }
        else
        {
            context->EncodeChar(255 - THRESHOLD + last_match_length);
            context->EncodePosition(context->tree.match_position);
        }
        context->f.len -= last_match_length;
        for(int i = 0; i < last_match_length; i++) {
            context->tree.DeleteNode(context->f.s);
            context->f.s = (context->f.s + 1) & (N - 1);
            context->f.r = (context->f.r + 1) & (N - 1);
            context->tree.InsertNode(context->f.r);
        }

        if(context->f.len == 0) {
            context->f.state = EncodeContext::FunctionContext::Finished;
            context->bitwriter.EncodeEnd();
        }

        break;
    }

    outlen -= context->bitwriter.setOutput(nullptr, 0);
    return context->f.len == 0 && context->bitwriter.empty();
}

void Encode(unsigned char const *in, unsigned int &inlen,
            unsigned char *out, unsigned int &outlen,
            EncodeContext *context) {
    unsigned int offset = 0;

    if (inlen == 0) {
        outlen = 0;
        return;
    }

    context->bitwriter.setOutput(out, outlen);

    if(!context->bitwriter.empty()) {
        inlen = offset;
        outlen -= context->bitwriter.setOutput(nullptr, 0);
        return;
    }

    for(; context->f.len < F && offset < inlen;) {
        context->tree.DeleteNode(context->f.s);
        context->tree.text_buf[context->f.s] = in[offset++];
        context->f.len++;
        context->f.s = (context->f.s + 1) & (N - 1);
        context->f.r = (context->f.r + 1) & (N - 1);
        context->tree.InsertNode(context->f.r);
    }

    //len is the number of valid bytes in the look-ahead area of text_buf.
    //We only run this if len is F or these are the final bits of data.
    while(context->f.len >= F && context->bitwriter.empty()) {
        int last_match_length = context->tree.match_length;
        if (last_match_length > context->f.len)
            last_match_length = context->f.len;
        if (last_match_length <= THRESHOLD)
        {
            last_match_length = 1;
            context->EncodeChar(context->tree.text_buf[context->f.r]);
        }
        else
        {
            context->EncodeChar(255 - THRESHOLD + last_match_length);
            context->EncodePosition(context->tree.match_position);
        }
        context->f.len -= last_match_length;
        for(; context->f.len < F && offset < inlen;) {
            context->tree.DeleteNode(context->f.s);
            int c = in[offset++];
            context->tree.text_buf[context->f.s] = c;
            context->f.s = (context->f.s + 1) & (N - 1);
            context->f.r = (context->f.r + 1) & (N - 1);
            context->tree.InsertNode(context->f.r);
            context->f.len++;
        }
    }

    inlen = offset;
    outlen -= context->bitwriter.setOutput(nullptr, 0);
    return;
}

unsigned int Encode(unsigned char const *in, unsigned int inlen,
                    unsigned char *out, unsigned int outlen)  /* compression */
{
    if (inlen == 0)
        return 0;

    EncodeContext *context = BeginEncode();

    unsigned int count = 0;
    for(unsigned int i = 0; i < inlen;) {
        unsigned int _inlen = inlen-i;
        unsigned int _outlen = outlen-count;
        Encode(in+i, _inlen, out+count, _outlen, context);
        i += _inlen;
        count += _outlen;
    }

    while(true) {
        unsigned int _outlen = outlen-count;
        bool result = EncodeFinal(out+count, _outlen, context);
        count += _outlen;
        if(result)
            break;
    }

    EndEncode(context);

    return count;
}

DecodeContext *BeginDecode() {
    unsigned int i;
    DecodeContext *context = new DecodeContext();

    context->huff.StartHuff();

    for (i = 0; i < N - F; i++)
        context->f.text_buf[i] = ' ';

    context->f.r = N-F;

    return context;
}

void EndDecode(DecodeContext *context) {
    delete context;
}

void Decode(unsigned char const *in, unsigned int &inlen,
                    unsigned char *out, unsigned int &outlen,
                    DecodeContext *context)
{
    int c;
    unsigned int count;
    unsigned int i, j, k;

    if (outlen == 0) {
        inlen = 0;
        return;
    }

    context->bitreader.setInput(in, inlen);

    count = 0;
    while(count < outlen && !context->f.saved_data.empty()) {
        out[count] = context->f.saved_data.front();
        context->f.saved_data.pop_front();
        count++;
    }

    for (; count < outlen; )
    {
        BitReaderContext old_bitreader = context->bitreader;
        c = context->DecodeChar();
        if(c == -1) {
            //out of input. save the bits we still have for later.
            context->bitreader = old_bitreader;
            break;
        }
        if (c < 256) {
            context->huff.update(c);
            out[count] = c;
            count++;
            context->f.text_buf[context->f.r++] = c;
            context->f.r &= (N - 1);
        }
        else
        {
            int pos = context->DecodePosition();
            if(pos == -1) {
                //out of input. save the bits we still have for later.
                context->bitreader = old_bitreader;
                break;
            }
            context->huff.update(c);
            i = (context->f.r - pos - 1) & (N - 1);
            j = c - 255 + THRESHOLD;
            for (k = 0; k < j; k++)
            {
                c = context->f.text_buf[(i + k) & (N - 1)];
                if(count < outlen) {
                    out[count] = c;
                    count++;
                } else {
                    context->f.saved_data.push_back(c);
                }
                context->f.text_buf[context->f.r++] = c;
                context->f.r &= (N - 1);
            }
        }
    }

    inlen -= context->bitreader.setInput(nullptr, 0);

    outlen = count;
}

unsigned int Decode(unsigned char const *in, unsigned int inlen, unsigned char *out, unsigned int outlen)
{
    if (outlen == 0)
        return 0;

    DecodeContext *context = BeginDecode();

    unsigned int count = 0;
    for(unsigned int i = 0; i < inlen && count < outlen;) {
        unsigned int _inlen = inlen-i;
        unsigned int _outlen = outlen-count;
        Decode(in+i, _inlen, out+count, _outlen, context);
        i += _inlen;
        count += _outlen;
    }

    while(count < outlen) {
        unsigned int _inlen = 0;
        unsigned int _outlen = outlen-count;
        Decode(nullptr, _inlen, out+count, _outlen, context);
        count += _outlen;
        if(_outlen == 0)
            break;
    }

    EndDecode(context);

    return count;
}

}
