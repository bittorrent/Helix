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

#include "authorizer.hpp"
#include "sha.h"
#include "utils.hpp"

const char* _sekret = "omgasekretdonttellanyone";

bool SaltyAuthorizer::is_allowed(const byte* binary_infohash, const byte* salted)
{
    bool valid;
    byte hash[20];
    size_t length = 20 + strlen(_sekret) + 1;
    char* buf = alloc<char>(length);

    memcpy(buf, binary_infohash, 20);
    memcpy(&buf[20], _sekret, strlen(_sekret) + 1);

    SHA1::Hash(buf, length - 1, hash);

    /*
    char hex[20*2 + 1];
    hash_to_hex(hex, 20*2+1, hash);
    printf("%s\n", hex);
    */

    valid = memcmp(hash, salted, 20) == 0;

    free(buf);

    return valid;
}

