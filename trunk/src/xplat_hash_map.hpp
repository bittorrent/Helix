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

#ifndef __XPLAT_HASH_MAP_HPP__
#define __XPLAT_HASH_MAP_HPP__

#include "xplat_ext.hpp"
#include "libtorrent/peer_id.hpp"

#ifdef WIN32
#include <hash_map>

template <class _Kty,
          class _Ty,
          class _Hash = _STD less<_Kty>, // WRONG! but unused
          class _Cmp = _STD less<_Kty> >
class hash_map : public stdext::hash_map<_Kty, _Ty, stdext::hash_compare<_Kty, _Cmp> >
{
};

template <class _Kty,
          class _Pr = _STD less<_Kty> >
class hash : public stdext::hash_compare<_Kty, _Pr>
{
};

#else

#include <ext/hash_map>
#include <ext/hash_set>

namespace __gnu_cxx
{
        template<> struct hash<libtorrent::sha1_hash>
        {
                size_t operator()( const libtorrent::sha1_hash& x ) const
                {
                        size_t ret = 0;
                        for (int i = 0; i < 20; ++i) ret += x[i];
                        return ret;
                }
        };

        template<> struct hash< std::string >
        {
                size_t operator()( const std::string& x ) const
                {
                        return hash< const char* >()( x.c_str() );
                }
        };
}
#endif

#endif //__XPLAT_HASH_MAP_HPP__

