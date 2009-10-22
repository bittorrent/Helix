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

#include "utils.hpp"

#include <sstream>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdarg.h>

#if !defined(_WIN32)

#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#endif

#ifdef WIN32
    #define snprintf _snprintf
#endif

#define IS_CHAR_INSIDE(v,mi,ma) ( (unsigned char)((v) - (mi)) < ((ma) - (mi)))

std::ostream *logger_p = &std::cout;
std::ostream *default_logger_output = logger_p;

std::string logger_filter::time_string()
{
    struct tm tm;
    struct timeval tv;

    gettimeofday(&tv, 0);

    localtime_r(&tv.tv_sec, &tm);

    char buf[sizeof("2007-10-01 12:34:56")];

    strftime(buf, sizeof(buf), "%F %T", &tm);

    std::string ret(buf);

    // milliseconds.
    snprintf(buf, sizeof(buf), ".%02d", (int)(tv.tv_usec / (1000000 / 100)));
    ret.append(buf);

    strftime(buf, sizeof(buf), " %Z", &tm);
    ret.append(buf);

    return ret;
}

perfmeter::perfmeter(const std::string &desc) :
    num_reqs(0),
    total_elapsed_us(0),
    square_total_elapsed_us(0),
    max_elapsed_us(0),
    min_elapsed_us(INT_MAX),
    saved_avg_latency(0),
    saved_stdev(0),
    saved_min_latency(0),
    saved_max_latency(0),
    desc_(desc)
{
}

void perfmeter::add_sample(int elapsed_us)
{
    num_reqs++;
    total_elapsed_us += elapsed_us;
    square_total_elapsed_us += elapsed_us * elapsed_us;
    if(elapsed_us > max_elapsed_us)
    {
        max_elapsed_us = elapsed_us;
    }
    if(elapsed_us < min_elapsed_us)
    {
        min_elapsed_us = elapsed_us;
    }
}

void perfmeter::clear_stats()
{
    num_reqs = 0;
    total_elapsed_us = 0;
    square_total_elapsed_us = 0;
    max_elapsed_us = 0;
    min_elapsed_us = INT_MAX;
}

void perfmeter::log_status_and_clear()
{
    std::string ostring;

    if (num_reqs)
    {
        saved_avg_latency = (double)total_elapsed_us / num_reqs;
        double variance = (square_total_elapsed_us
                - 2.0 * saved_avg_latency * total_elapsed_us) / num_reqs
            + saved_avg_latency * saved_avg_latency;
        saved_stdev = sqrt(variance);

        saved_min_latency = min_elapsed_us;
        saved_max_latency = max_elapsed_us;

        ostring = string_format(
                "%d requests handled, latency min/avg/max/stdev = "
                "%ld/%.2f/%ld/%.2f us",
                num_reqs, min_elapsed_us, saved_avg_latency,
                max_elapsed_us, saved_stdev);
    }
    else
    {
        ostring = "0 requests handled.";
    }

    logger << ostring << std::endl;
    clear_stats();
}

std::string perfmeter::instance_stats()
{
    std::stringstream os;

    os << desc_ << " minimum latency: " << saved_min_latency << std::endl;
    os << desc_ << " average latency: " << saved_avg_latency << std::endl;
    os << desc_ << " maximum latency: " << saved_max_latency << std::endl;
    os << desc_ << " standard deviation: " << saved_stdev << std::endl;

    return os.str();
}

perfmeter::~perfmeter()
{
}

uint32 ReadBE32(const void *p)
{
        byte *pp = (byte*)p;
        return (pp[0] << 24) | (pp[1] << 16) | (pp[2] << 8) | (pp[3] << 0);
}

// 20*2+1 characters with NULL
str hash_to_hex(str hex, const size_t hex_limit, const byte *hash)
{
        snprintf(hex, hex_limit, "%.8X%.8X%.8X%.8X%.8X",
                 ReadBE32(hash),
                 ReadBE32(hash+4),
                 ReadBE32(hash+8),
                 ReadBE32(hash+12),
                 ReadBE32(hash+16));
        return hex;
}

void ss_hash_to_hex(const std::string& hash, std::string& hex)
{
    char buf[1024];
    hash_to_hex(buf, 1024, (const byte*)hash.c_str());
    hex.assign(buf, strlen(buf));
}

bool hex_to_hash(byte *hash, const char *hex)
{
        for(int i=0; i<40; i++) {
                byte c = hex[i];
                if (IS_CHAR_INSIDE(c, '0', '9'+1)) c -= '0';
                else if (IS_CHAR_INSIDE(c|=32, 'a', 'z'+1)) c -= 'a' - 10;
                else return false;
                if (!(i&1))
                        *hash = c << 4;
                else
                        *hash++ |= c;
        }
        return (hex[40] == 0);
}

str strsep(str *string_p, char delim) {
        str s = *string_p;
        str start = s;
        char c;

        if (s == NULL)
                return NULL;

        do {
                c = *s++;
                if (c == delim) {
                        s[-1] = 0;
                        *string_p = s;
                        return start;
                }
        } while (c != 0);
        *string_p = NULL;
        return start;
}

std::string format_ip(uint32 ip)
{
    std::stringstream s;
    s << ((ip >> 24) & 0xFF) << "."
      << ((ip >> 16) & 0xFF) << "."
      << ((ip >> 8) & 0xFF) << "."
      << ((ip >> 0) & 0xFF);

    return s.str();
}

uint32 parse_ip(cstr ip, bool *valid)
{
    uint32 r = 0;
    uint n;
    str end;

    if (valid) *valid=false;

    if (ip == NULL)
        return (uint32)-1;

    for(size_t i=0; i!=4; i++) {
        n = strtol(ip, &end, 10);
        if (n>255) return (uint32)-1;
        ip = end;
        if (*ip++ != (i==3 ? 0 : '.')) return (uint32)-1;
        r = (r<<8) + n;
    }

    if (valid) *valid=true;
    return r;
}

#if !defined(_WIN32)

uid_t get_user_uid(std::string username)
{
        struct passwd *pw = getpwnam(username.c_str());
        return pw ? pw->pw_uid : -1;
}

gid_t get_user_gid(std::string username)
{
        struct passwd *pw = getpwnam(username.c_str());
        return pw ? pw->pw_gid : -1;
}

gid_t get_group_gid(std::string groupname)
{
        struct group *gr = getgrnam(groupname.c_str());
        return gr ? gr->gr_gid : -1;
}

static void discard_fd(int ofd)
{
    int fd;

    close(ofd);
    fd = open("/dev/null", O_RDWR);
    if (fd == -1)
    {
        int err = errno;
        logger << "open(\"/dev/null\"): " << strerror(err) << std::endl;
    }
    else if (fd != ofd)
    {
        dup2(fd, ofd);
        close(fd);
    }
}

static std::string persistent_logfile;

static bool open_logfile(const std::string &logfilename, const std::string &serverdesc)
{
    std::ostream *logtmp = new std::ofstream(logfilename.c_str(),
            std::ios_base::app);

    if(!*logtmp)
    {
        logger << "Failed to open " << logfilename << "\n";
        return false;
    }

    if (logger_p != default_logger_output)
    {
        delete(logger_p);
    }
    logger_p = logtmp;

    persistent_logfile = logfilename;

    logger << "Logfile opened by helix " << serverdesc << std::endl;
    return true;
}

void reopen_logfile(const std::string &serverdesc)
{
    if (persistent_logfile.empty())
    {
        return;
    }

    logger << "Reopening logfile " << persistent_logfile << std::endl;

    open_logfile(persistent_logfile, serverdesc);
}

// when this function returns, you are a daemon
bool daemonize(std::string username, std::string groupname, std::string pidfilename, std::string logfilename, std::string serverdesc)
{
        bool ret = true;
        if ((getuid() == 0) && (username == ""))
        {
                logger << "If you start with root privileges you should really "
                        "provide a username argument so that daemon() can shed those "
                        "privileges before returning." << std::endl;
        }

        pid_t fpid, sid;
        fpid = fork();
        if (fpid < 0)
        {
                int err = errno;
                logger << "Unable to fork: " << strerror(err) << std::endl;
                return false;
        }

        if (fpid > 0)
        {
                // This is the parent.  Having spawned our daemon child, we can exit.
                exit(0);
        }

        sid = setsid();
        if (sid < 0)
        {
                int err = errno;
                logger << "unable to setsid: " << strerror(err) << std::endl;
                ret = false;
        }


        // I should now be a daemon.

        if (pidfilename == "")
        {
                pidfilename = "/var/run/helix/helix.pid";
        }

        if (logfilename == "")
        {
                logfilename = "/var/log/helix/helix.log";
        }

        pid_t pid = getpid();

        std::ofstream pidfile(pidfilename.c_str());
        pidfile << pid << "\n";
        pidfile.close();
        chmod(pidfilename.c_str(), S_IRUSR|S_IWUSR|S_IROTH|S_IRGRP);

        if(open_logfile(logfilename, serverdesc))
        {
            discard_fd(STDIN_FILENO);
            discard_fd(STDOUT_FILENO);
            discard_fd(STDERR_FILENO);
        }
        else
        {
            ret = false;
        }

        char *explanation =
                "When specifying a uid or gid other than your own "
                "you must be running as root for setuid/setgid to work.";
        gid_t newgid;

        if (groupname != "")
                newgid = get_group_gid(groupname);
        else if (username != "")
        {
                newgid = get_user_gid(username);
                if (newgid == (gid_t)-1)
                {
                        int err = errno;
                        logger << "Unable to fetch gid for user " << username
                                << ": " << strerror(err) << std::endl;
                        ret = false;
                }
        }
        else
                newgid = -1;

        if (newgid != (gid_t)-1)
        {
                if (setgid(newgid) == -1)
                {
                        int err = errno;
                        logger << "setgid(" << newgid << ") failed: " <<
                                strerror(err) << ".  " << explanation <<
                                std::endl;
                        ret = false;
                }
                logger << "set gid to " << groupname << "(" << newgid << ")\n";
        }

        if (username != "")
        {
                uid_t newuid = get_user_uid(username);
                if (setuid(newuid) == -1)
                {
                        int err = errno;
                        logger << "setuid(" << newuid << ") failed: " <<
                                strerror(err) << ".  " << explanation <<
                                "Your uid is " << getuid() << ".\n";
                        ret = false;
                }
                logger << "set uid to " << username << "(" << newuid << ")\n";
        }

        return ret;
}

#else

static CRITICAL_SECTION _gtod_sect;
static double counterPerMicrosecond = -1.0;
static unsigned __int64 frequency = 0;
static unsigned __int64 timeSecOffset = 0;
static unsigned __int64 startPerformanceCounter = 0;

void GTOD_Initialize()
{
        InitializeCriticalSection(&_gtod_sect);
}

/*
 * gettimeofday for windows
 *
 * counterPerMicrosecond is the number of counts per microsecond.
 * Double is required if we have less than 1 counter per microsecond.  This has not been tested.
 * On a PIII 700, it got about 3.579545.  This is (was?) guaranteed not to change while the processor is running.
 * We really don't need to check for loop detection.  On that machine it would take about 59645564 days to loop.
 * (2^64) / frequency / 60 / 60 / 24.
 *
 */
int gettimeofday(struct timeval *tv, void *ignored)
{
        unsigned __int64 counter;

        QueryPerformanceCounter((LARGE_INTEGER *) &counter);

        if (counter < startPerformanceCounter || counterPerMicrosecond == -1.0)
        {
                time_t t;
                EnterCriticalSection(&_gtod_sect);

                QueryPerformanceFrequency((LARGE_INTEGER *) &frequency);

                counterPerMicrosecond = (double)frequency / 1000000.0f;

                time(&t);
                QueryPerformanceCounter((LARGE_INTEGER *) &counter);
                startPerformanceCounter = counter;

                counter /= frequency;

                timeSecOffset = t - counter;

                LeaveCriticalSection(&_gtod_sect);
                QueryPerformanceCounter((LARGE_INTEGER *) &counter);
        }

        tv->tv_sec = (long)((counter / frequency) + timeSecOffset);
        tv->tv_usec = (long)((__int64)(counter / counterPerMicrosecond) % 1000000);

        return 0;
}
#endif
