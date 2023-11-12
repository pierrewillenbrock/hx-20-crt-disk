
#include "lzh.hpp"
#include <fstream>
#include <vector>
#include <string.h>
#include <stdio.h>

unsigned int TestEncode(unsigned char *in, unsigned int inlen,
                        unsigned char *out, unsigned int outlen) { /* compression */
    if(inlen == 0)
        return 0;

    lzh::EncodeContext *context = lzh::BeginEncode();

    unsigned int count = 0;
    unsigned int in_block_size = 1;
    unsigned int out_block_size = 1;
    for(unsigned int i = 0; i < inlen;) {
        unsigned int _inlen = std::min(inlen-i, in_block_size);
        unsigned int _outlen = std::min(outlen-count, out_block_size);
        lzh::Encode(in+i, _inlen, out+count, _outlen, context);
        i += _inlen;
        count += _outlen;
    }

    while(true) {
        unsigned int _outlen = std::min(outlen-count, out_block_size);
        bool result = lzh::EncodeFinal(out+count, _outlen, context);
        count += _outlen;
        if(result)
            break;
    }

    lzh::EndEncode(context);

    return count;
}

unsigned int TestDecode(unsigned char *in, unsigned int inlen, unsigned char *out, unsigned int outlen) {
    if(outlen == 0)
        return 0;

    lzh::DecodeContext *context = lzh::BeginDecode();

    unsigned int count = 0;
    unsigned int in_block_size = 1;
    unsigned int out_block_size = 1;
    for(unsigned int i = 0; i < inlen;) {
        unsigned int _inlen = std::min(inlen-i, in_block_size);
        unsigned int _outlen = std::min(outlen-count, out_block_size);
        lzh::Decode(in+i, _inlen, out+count, _outlen, context);
        i += _inlen;
        count += _outlen;
    }

    while(count < outlen) {
        unsigned int _inlen = 0;
        unsigned int _outlen = std::min(outlen-count, out_block_size);
        lzh::Decode(nullptr, _inlen, out+count, _outlen, context);
        count += _outlen;
    }

    lzh::EndDecode(context);

    return count;
}

int main(int argc, char **argv) {
    if(argc < 3) {
        fprintf(stderr, "No input and output file argument\n");
        return 1;
    }
    std::ifstream f(argv[1]);
    std::vector<char> compr;
    while(true) {
        int pos = compr.size();
        compr.resize(pos+65536);
        if(!f.read(compr.data()+pos, 65536)) {
            compr.resize(pos+f.gcount());
            break;
        }
    }
    std::vector<char> uncompr;
    uncompr.resize(compr.size());
    while(true) {
        unsigned int res = lzh::Decode((unsigned char *)compr.data(), compr.size(), (unsigned char *)uncompr.data(), uncompr.size());
        if(res == uncompr.size())
            uncompr.resize(uncompr.size()*2);
        else {
            uncompr.resize(res);
            break;
        }
    }
    std::ofstream of(argv[2]);
    of.write(uncompr.data(), uncompr.size());
    return 0;
}
