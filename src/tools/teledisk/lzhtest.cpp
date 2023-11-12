
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

    int count = 0;
    int in_block_size = 1;
    int out_block_size = 1;
    for(unsigned int i = 0; i < inlen;) {
        unsigned int _inlen = std::min((int)(inlen-i), in_block_size);
        unsigned int _outlen = std::min((int)(outlen-count), out_block_size);
        lzh::Encode(in+i, _inlen, out+count, _outlen, context);
        i += _inlen;
        count += _outlen;
    }

    while(true) {
        unsigned int _outlen = std::min((int)(outlen-count), out_block_size);
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
    if(argc < 2) {
        fprintf(stderr, "No input file argument\n");
        return 1;
    }
    std::ifstream f(argv[1]);
    std::vector<char> src;
    while(true) {
        int pos = src.size();
        src.resize(pos+65536);
        if(!f.read(src.data()+pos, 65536)) {
            src.resize(pos+f.gcount());
            break;
        }
    }
    std::vector<char> compr;
    compr.resize(src.size()*2);
    int comprsize = TestEncode((unsigned char *)src.data(), src.size(), (unsigned char *)compr.data(), compr.size());
    compr.resize(comprsize);
    std::vector<char> uncompr;
    uncompr.resize(src.size());
    fprintf(stderr,"Compressed to %zu bytes\n", compr.size());
    TestDecode((unsigned char *)compr.data(), compr.size(), (unsigned char *)uncompr.data(), uncompr.size());
    if(memcmp(uncompr.data(), src.data(), src.size()) != 0) {
        for(unsigned int i = 0; i < src.size(); i++) {
            if(uncompr[i] != src[i]) {
                fprintf(stderr, "Original and uncompressed mismatch at position %d\n", i);
                break;
            }
        }
        return 1;
    }
    fprintf(stderr,"Successfully uncompressed back to %zu\n",uncompr.size());
    return 0;
}
