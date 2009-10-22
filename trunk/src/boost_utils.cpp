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

#include "boost_utils.hpp"

std::string _format_arg_list(const char *fmt, va_list args)
{
    if (!fmt) return "";
    int result = -1, length = 256;
    char *buffer = 0;
    while (result == -1)
    {
	    if (buffer) delete [] buffer;
	    buffer = new char [length + 1];
	    memset(buffer, 0, length + 1);
	    result = vsnprintf(buffer, length, fmt, args);
	    length *= 2;
    }
    
    std::string s(buffer);
    delete [] buffer;
    return s;
}

std::string string_format(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::string s = _format_arg_list(fmt, args);
    va_end(args);
    return s;
}
