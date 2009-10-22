/*
The MIT License

Copyright (c) 2009 BitTorrent Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#ifndef __TEMPLATES_H__
#define __TEMPLATES_H__

#include <stdlib.h>
#include <assert.h>
#include <boost/cstdint.hpp>
#include <string>

#if defined(_WIN32)
#    define inline __forceinline
#else
#    define _cdecl
#endif

#if !defined(_WIN32)
#    define strnicmp strncasecmp
#    define stricmp strcasecmp
#endif

// standard types
typedef boost::uint8_t byte;
typedef boost::uint8_t uint8;
typedef boost::int8_t int8;
typedef boost::uint16_t uint16;
typedef boost::int16_t int16;
typedef boost::uint16_t uint16;
typedef boost::uint32_t uint32;
typedef boost::int32_t int32;
typedef boost::uint64_t uint64;
typedef boost::int64_t int64;

typedef boost::uint32_t uint;

// always ANSI
typedef const char * cstr;
typedef char * str;


// Compiler workaround
// blerg. libtorrent redefines this
/*
#ifndef for
#define for if(0);else for
#endif
*/

#define lenof(x) (sizeof(x)/sizeof(x[0]))

// Common defines
#define INLINE _inline
#define IS_INT_INSIDE(v,mi,ma) ( (unsigned int)((v) - (mi)) < ((ma) - (mi)))
#define IS_CHAR_INSIDE(v,mi,ma) ( (unsigned char)((v) - (mi)) < ((ma) - (mi)))

#define IS_INT_INSIDE_SIZE(v, mi, n) ( (unsigned int)((v) - (mi)) < ((n)))

template <typename T>
T* alloc(size_t size)
{
    return (T*)malloc(sizeof(T) * size);
}

static inline uint32 SWAP32(uint32 x) {
        return (x >> 24) | (x << 24) | ((x >> 8) & 0xff00) | ((x << 8) & 0xff0000);
}

static inline uint16 SWAP16(uint16 x) {
        return (uint16)((x >> 8) | (x << 8));
}

#define htons_inline SWAP16
#define htonl_inline SWAP32



// Utility templates
#undef min
#undef max

template <typename T> static inline T min(T a, T b) { if (a < b) return a; return b; }
template <typename T> static inline T max(T a, T b) { if (a > b) return a; return b; }

template <typename T> static inline T min(T a, T b, T c) { return min(min(a,b),c); }
template <typename T> static inline T max(T a, T b, T c) { return max(max(a,b),c); }
template <typename T> static inline T clamp(T v, T mi, T ma)
{
        if (v > ma) v = ma;
        if (v < mi) v = mi;
        return v;
}

#endif //__TEMPLATES_H__
