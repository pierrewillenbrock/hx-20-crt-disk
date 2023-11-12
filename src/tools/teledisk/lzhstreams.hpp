
#pragma once

#include <ostream>
#include <istream>
#include <fstream>
#include <stdio.h>
#include "lzh.hpp"

#define LZHSTREAM_BUFFER_SIZE 0

template<typename OStream>
class olzhstreambuf : public std::basic_streambuf<char> {
private:
    OStream &output;
    lzh::EncodeContext *ec;
protected:
#if LZHSTREAM_BUFFER_SIZE == 0
    virtual int_type overflow(int_type c = std::char_traits<char>::eof()) override {
        //pbase, pptr, epptr, pbump
        //we could probably just take c (if not eof) and push that into the compressor
        if(c == std::char_traits<char>::eof())
            return c;
        unsigned char ich = c;
        unsigned char och[4];
        unsigned int _inlen = 1;
        unsigned int _outlen = 4;
        lzh::Encode(&ich, _inlen,
                    och, _outlen, ec);
        if(_outlen > 0)
            output.write((char const *)och,_outlen);
        return c;
    }
#else
#error Implement
#endif
public:
    olzhstreambuf(OStream &output) : output(output) {
        ec = lzh::BeginEncode();
    }
    virtual ~olzhstreambuf() override {
#if LZHSTREAM_BUFFER_SIZE == 0
        while(true) {
            char ch[4];
            unsigned int _outlen = 4;
            bool result = lzh::EncodeFinal((unsigned char *)ch, _outlen, ec);
            if(_outlen > 0)
                output.write(ch,_outlen);
            if(result)
                break;
        }
#else
#error Implement
#endif
        lzh::EndEncode(ec);
    }
};

template<typename IStream>
class ilzhstreambuf : public std::basic_streambuf<char> {
private:
    IStream &input;
    lzh::DecodeContext *dc;
protected:
#if LZHSTREAM_BUFFER_SIZE == 0
    virtual int_type uflow() override {
        //eback, gptr, egptr, gbump, setg
        //we could actually just pull one single byte and return it.
        unsigned char och = 0;
        unsigned char ich = 0;
        //first, try to pull a char out without putting one in.
        unsigned int _inlen = 0;
        unsigned int _outlen = 1;
        lzh::Decode(&ich, _inlen,
                    &och, _outlen, dc);
        if(_outlen == 1)
            return och;
        //else put data in until it gives us something or the source is eof
        while(input.good()) {
            input.read((char *)&ich, 1);
            _inlen = 1;
            _outlen = 1;
            lzh::Decode(&ich, _inlen,
                        &och, _outlen, dc);
            if(_outlen == 1)
                return och;
        }
        return std::char_traits<char>::eof();
    }
#else
#error Implement
#endif
public:
    ilzhstreambuf(IStream &input) : input(input) {
        dc = lzh::BeginDecode();
    }
    virtual ~ilzhstreambuf() override {
        lzh::EndDecode(dc);
    }
};

template<typename OStream>
class olzhstream : public std::basic_ostream<char> {
private:
    olzhstreambuf<OStream> buf;
public:
    olzhstream(OStream &output) : std::basic_ostream<char>(&buf), buf(output) {}
};

template<typename IStream>
class ilzhstream : public std::basic_istream<char> {
private:
    ilzhstreambuf<IStream> buf;
public:
    ilzhstream(IStream &input) :
        std::basic_istream<char>(&buf), buf(input) {}
};

