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

#include "stats.hpp"
#include <boost/bind.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/peer_id.hpp>
#include "utils.hpp"
#include "control.hpp"

using namespace libtorrent;

StatsLogger::StatsLogger()
{
    memset(&_stats, 0, sizeof(_stats));
}

void StatsLogger::update_peer_counts(int d_peers, int d_seeds)
{
    _stats.peers += d_peers;
    _stats.seeds += d_seeds;
}

void StatsLogger::log_request(stats_struct& s)
{
    _stats.w_downloaded += s.w_downloaded;
    _stats.p_downloaded += s.p_downloaded;
    _stats.p_uploaded += s.p_uploaded;
    _stats.c_bytes += s.c_bytes;

    _stats.w_bad += s.w_bad;
    _stats.cumulative_w_bad += s.w_bad;
    _stats.w_fail += s.w_fail;

    switch (s.event) {
        case (STARTED): {
            _stats.starts += 1;
        }
        break;
        case (COMPLETED): {
            _stats.completes += 1;
        }
        break;
        case (STOPPED): {
            _stats.stops += 1;
        }
        break;
        default:
        break;
    }

    // TODO: more stats
}

void StatsLogger::add_timeout()
{
    _stats.timeouts += 1;
}

void StatsLogger::reset()
{
    size_t peers = _stats.peers;
    size_t seeds = _stats.seeds;
    size_t cumulative_w_bad = _stats.cumulative_w_bad;
    memset(&_stats, 0, sizeof(_stats));
    _stats.peers = peers;
    _stats.seeds = seeds;
    _stats.cumulative_w_bad = cumulative_w_bad;
}

size_t StatsLogger::get_num_peers()
{
    return _stats.peers;
}

size_t StatsLogger::get_num_seeds()
{
    return _stats.seeds;
}

size_t StatsLogger::get_num_completes()
{
    return _stats.completes;
}

size_t StatsLogger::get_w_bad()
{
    return _stats.w_bad;
}

size_t StatsLogger::get_cumulative_w_bad()
{
    return _stats.cumulative_w_bad;
}

