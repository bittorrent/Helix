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

#ifndef __UTILS_HPP__
#define __UTILS_HPP__

#include "templates.h"

#include <iostream>
#include <boost/lexical_cast.hpp>

#include "boost_utils.hpp"

#ifdef _MSC_VER
#define conn_assert(expr)                                               \
    do {                                                                \
        if (!(expr)) {                                                  \
            std::string what(string_format("%s:%d: %s",              \
                    __FILE__, __LINE__, #expr));   \
            logger << "Assertion failed: " << what << std::endl;        \
            throw std::runtime_error(what.c_str());                     \
        }                                                               \
    } while(0)
#else
#define conn_assert(expr)                                               \
    do {                                                                \
        if (!(expr)) {                                                  \
            std::string what(string_format("%s:%d:%s: %s",              \
                    __FILE__, __LINE__, __PRETTY_FUNCTION__, #expr));   \
            logger << "Assertion failed: " << what << std::endl;        \
            throw std::runtime_error(what.c_str());                     \
        }                                                               \
    } while(0)
#endif

extern std::ostream *logger_p;
class logger_filter
{
public:
    logger_filter(std::ostream& os): m_os(os)
    {
        m_os << "[" << time_string() << "] ";
    }

    ~logger_filter()
    {
        m_os.flush();
// This could be done automatically, but currently every log message
// already terminates in endl, so skip it.
//        m_os << std::endl;
    }

    template<class T>
    std::ostream& operator<<(T v)
    {
        m_os << v;
        return m_os;
    }
private:
    std::string time_string(void);
    std::ostream& m_os;
};

#define logger (logger_filter(*logger_p))

str hash_to_hex(str hex, const size_t hex_limit, const byte *hash);
void ss_hash_to_hex(const std::string& hash, std::string& hex);
bool hex_to_hash(byte *hash, const char *hex);

/*
  strsep() usage:

  str sp = mystr;
  str s = strsep(&sp, ',');
  for (; s != NULL; s = strsep(&sp, ',')) {
      if (*s != 0) {
          printf("csv found: [%s]\n", *s);
      }
  }
*/
str strsep(str *string_p, char delim);

static inline bool
starts_with(const std::string &haystack, const std::string &needle)
{
    return haystack.compare(0, needle.size(), needle) == 0;
}

std::string format_ip(uint32 ip);
uint32 parse_ip(cstr ip, bool *valid);

template<typename Result, typename Source>
inline Result safe_lexical_cast(const Source &s, const Result def = 0)
{
    try
    {
        return boost::lexical_cast<Result>(s);
    }
    catch (const boost::bad_lexical_cast& e)
    {
        return def;
    }
}

#if !defined(_WIN32)

#include <sys/time.h>
bool daemonize(std::string username, std::string groupname, std::string pidfilename, std::string logfilename, std::string progname);
void reopen_logfile(const std::string &);

#else

#define _CRT_SECURE_NO_DEPRECATE
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <time.h>
#include <winsock.h> // for timeval

void GTOD_Initialize();
int gettimeofday(struct timeval *tv, void *tz);

#endif


class StopWatch
{
public:

    StopWatch() { restart(); }

    void restart()
    {
        gettimeofday(&tv, NULL);
    }

    double get_msec()
    {
        struct timeval tv2;
        gettimeofday(&tv2, NULL);
        return tv_to_ms(tv2) - tv_to_ms(tv);
    }

    int64 get_usec()
    {
        struct timeval tv2;
        gettimeofday(&tv2, 0);
        return tv_to_us(tv2) - tv_to_us(tv);
    }

    static double tv_to_ms(struct timeval& atv)
    {
        return (atv.tv_sec * 1000.0) + (atv.tv_usec / 1000.0);
    }

    static int64 tv_to_us(struct timeval &tv)
    {
        return ((int64)tv.tv_sec * 1000000) + tv.tv_usec;
    }

    struct timeval tv;
};

class perfmeter
{
    public:
        perfmeter(const std::string &desc = "<none>");
        ~perfmeter();
        void set_name(const std::string &newdesc)
        {
            desc_ = newdesc;
        }
        void add_sample(int elapsed_us);
        void clear_stats();
        void log_status_and_clear();
        std::string instance_stats();

    private:
        int num_reqs;
        int64 total_elapsed_us;
        int64 square_total_elapsed_us; // for stdev
        int max_elapsed_us;
        int min_elapsed_us;

        double saved_avg_latency, saved_stdev;
        int saved_min_latency, saved_max_latency;

        std::string desc_;
};

class scoped_perf_sampler
{
    public:
        scoped_perf_sampler(perfmeter &pm) :
            pm_(pm)
        {
        }

        ~scoped_perf_sampler()
        {
            pm_.add_sample(sw.get_usec());
        }

    private:
        StopWatch sw;
        perfmeter &pm_;
};

extern bool verbose_logging;

#endif //__UTILS_HPP__
