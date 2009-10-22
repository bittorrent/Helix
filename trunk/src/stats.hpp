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

#ifndef __STATS_HPP__
#define __STATS_HPP__

#include <time.h>
#include "templates.h"
#include "boost_utils.hpp"
#include <libtorrent/entry.hpp>
#include <libtorrent/peer_id.hpp>

#include "control.hpp"

using namespace libtorrent;

enum event_e {
    EMPTY = 0,
    STARTED,
    COMPLETED,
    STOPPED,
    PAUSED,
};

// stats reported by peers on each request
struct stats_struct {
    event_e event;
    time_t t_checkin;
    uint64 left;
    uint64 w_downloaded;
    uint64 p_downloaded;
    uint64 p_uploaded;
    uint64 c_bytes;
    uint64 w_bad;
    size_t w_fail;
};

struct stats_log {
    size_t peers;
    size_t seeds;
    uint64 w_downloaded;
    uint64 p_downloaded;
    uint64 p_uploaded;
    uint64 c_bytes;
    uint64 w_bad;
    uint64 cumulative_w_bad;
    size_t w_fail;
    size_t starts;
    size_t completes;
    size_t stops;
    size_t timeouts;
};

class StatsLogger
{
public:
    StatsLogger();
    void update_peer_counts(int d_peers, int d_seeds);
    void log_request(stats_struct& s);
    void add_timeout();
    size_t get_num_peers();
    size_t get_num_seeds();
    size_t get_num_completes();
    size_t get_w_bad();
    size_t get_cumulative_w_bad();

private:
    void reset();

    stats_log _stats;
};

#endif //__STATS_HPP__

