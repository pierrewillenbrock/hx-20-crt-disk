#pragma once

namespace lzh {
struct EncodeContext;
EncodeContext *BeginEncode();
void EndEncode(EncodeContext *context);
bool EncodeFinal(unsigned char *out, unsigned int &outlen,
                 EncodeContext *context);
void Encode(unsigned char const *in, unsigned int &inlen,
            unsigned char *out, unsigned int &outlen,
            EncodeContext *context);
unsigned int Encode(unsigned char const *in, unsigned int inlen,
                    unsigned char *out, unsigned int outlen);

struct DecodeContext;
DecodeContext *BeginDecode();
void EndDecode(DecodeContext *context);
void Decode(unsigned char const *in, unsigned int &inlen,
            unsigned char *out, unsigned int &outlen,
            DecodeContext *context);
unsigned int Decode(unsigned char const *in, unsigned int inlen,
                    unsigned char *out, unsigned int outlen);
}
