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

#include "cpu_monitor.hpp"

#ifndef WIN32

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/times.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

void cpu_usage_struct::periodic()
{
    pid_t p = getpid();
    std::stringstream s;

#ifdef __APPLE__
	 // apple/BSD ps has different commandline options
	 return;
#endif

    // "/proc/%d/stat", 3rd from the last is the CPU index
    // or:
    s << "ps -p " << p << " -o psr --no-heading";

    FILE* f = popen(s.str().c_str(), "r");
    if (!f)
    {
        std::cerr << "Error opening '" << s.str() << "': " << strerror(errno) << "!\n";
        return;
    }
    char buf[16];
    fread(buf, sizeof(buf), 1, f);
    pclose(f);
    buf[sizeof(buf) - 1] = 0;
    size_t which_cpu = (size_t)atoi(buf);


    size_t cur_cpu = 0;
    std::ifstream inf("/proc/stat");

    while (!inf.eof())
    {
        std::string tag;
        std::getline(inf, tag, ' ');

        if (tag == "cpu")
        {
            // throw away the extra up to the newline
            std::getline(inf, tag);
            continue;
        }

        if (tag.compare(0, 3, "cpu") != 0)
            break;

        cpu_info_struct c;


        assert(cpu_data.size() >= cur_cpu);
        if (cpu_data.size() == cur_cpu)
        {
            c.user = 0;
            c.nice = 0;
            c.sys = 0;
            c.idle = 0;
            cpu_data.push_back(c);
        }

        inf >> c.user;
        inf >> c.nice;
        inf >> c.sys;
        inf >> c.idle;

        cpu_info_struct& old_c = cpu_data[cur_cpu];

        cpu_info_struct temp_c;
        temp_c.user = c.user - old_c.user;
        temp_c.nice = c.nice - old_c.nice;
        temp_c.sys = c.sys - old_c.sys;
        temp_c.idle = c.idle - old_c.idle;

        double total = temp_c.user + temp_c.nice + temp_c.sys + temp_c.idle;
        double percent = (temp_c.user + temp_c.nice + temp_c.sys) / total;

        if (cur_cpu == which_cpu)
        {
            _add_sample(percent * 100.0);
            /*
            printf("cpu%Zu %.2f %.2f\n", cur_cpu, percent * 100.0,
                   current_cpu_percent);
            */
        }

        cpu_data[cur_cpu] = c;

        // throw away the extra up to the newline
        std::getline(inf, tag);

        cur_cpu++;
    }
}

#else

void cpu_usage_struct::periodic()
{
}

#endif


cpu_usage_struct::cpu_usage_struct()
{
    current_cpu_percent = 0;
    weight_constant = 0;
    window_pos = 0;
    for (size_t i = 0; i < WINDOW_SIZE; i++)
    {
        sliding_window[i] = 0;
        weight_constant += _weight(i);
    }
}

void cpu_usage_struct::_add_sample(double s)
{
    sliding_window[window_pos] = s;

    // calc average with weighting
    double total = 0;
    for (size_t i = 0; i < WINDOW_SIZE; i++)
    {
        size_t p = _index(i + window_pos + 1);
        total += sliding_window[p] * (_weight(p) / weight_constant);
    }
    current_cpu_percent = total;

    window_pos = _index(window_pos + 1);
}

