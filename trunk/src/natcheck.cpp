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

#include "natcheck.hpp"
#include "utils.hpp"

#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <boost/bind.hpp>
#include <deque>

#define NC_TIMEOUT 15
#define NC_MAX_CHECKING 256

using boost::asio::ip::tcp;

static const char _ident_string[] = "\x13" "BitTorrent protocol";
static const char _peer_id[] = "DNA1000-000000000000";

int num_checking = 0;
typedef std::deque< boost::shared_ptr< NatCheck > > nc_queue_type;
nc_queue_type nc_queue;

long NatCheck::natcheck_started;
long NatCheck::natcheck_created;
long NatCheck::natcheck_deleted;
long NatCheck::natcheck_success;
double NatCheck::natcheck_success_sum;
long NatCheck::natcheck_fail;
double NatCheck::natcheck_fail_sum;
long NatCheck::natcheck_timeout;
double NatCheck::natcheck_timeout_sum;

void BuildLoginPacket(PeerConnHeader &hdr, const byte *binary_infohash)
{
    memcpy(hdr.ident, _ident_string, 20);

    ((uint32*)hdr.reserved)[0] = 0;
    ((uint32*)hdr.reserved)[1] = 0;

    memcpy(hdr.info, binary_infohash, 20);
    memcpy(hdr.peer_id, _peer_id, SIZE_OF_PEER_ID);
}

void ParseLoginPacket(PeerConnHeader *hdr, const byte *binary_infohash, const byte *peer_id)
{
    if (memcmp(hdr->ident, _ident_string, 20) != 0) {
        throw std::logic_error("Incorrect protocol header");
    }
    if (memcmp(hdr->info, binary_infohash, 20) != 0) {
        throw std::logic_error("Incorrect infohash");
    }
    if (memcmp(hdr->peer_id, peer_id, SIZE_OF_PEER_ID) != 0) {
        // exclude magic id, for load tester
        if (memcmp(hdr->peer_id, MAGIC_PEERID, SIZE_OF_PEER_ID) != 0) {
            throw std::logic_error("Incorrect peer id");
        }
    }
}


void start_from_queue()
{
    if ((num_checking < NC_MAX_CHECKING) && !nc_queue.empty())
    {
        //logger << "starting a natcheck, " << nc_queue.size() << " queued" << std::endl;
        boost::shared_ptr<NatCheck> n = nc_queue.back();
        nc_queue.pop_back();
        n->start();
    }
}

double nc_queue_average_age()
{
    nc_queue_type::iterator it;
    double total = 0;
    struct timeval now;
    int n = nc_queue.size();

    if (n == 0) return 0;

    gettimeofday(&now, 0);

    for (it = nc_queue.begin(); it != nc_queue.end(); it++)
    {
        total += (*it)->age(&now);
    }

    return total / n;
}

void StartNatCheck(boost::asio::io_service& io_service, tcp::endpoint const& endpoint,
                   const byte* binary_infohash, const byte* peer_id,
                   CallbackHandler<int> handler)
{
    boost::shared_ptr<NatCheck> n(new NatCheck(io_service, endpoint, binary_infohash, peer_id, handler));
    nc_queue.push_back(n);
    start_from_queue();
}

NatCheck::NatCheck(boost::asio::io_service& io_service,
                   tcp::endpoint const& endpoint,
                   const byte* binary_infohash, const byte* peer_id,
                   CallbackHandler<int> handler)
    : socket_(io_service), timer_(io_service), endpoint_(endpoint)
{
    assert(handler);
    handler_ = handler;
    socket_closed = true;
    memcpy(binary_infohash_, binary_infohash, 20);
    memcpy(peer_id_, peer_id, SIZE_OF_PEER_ID);
    gettimeofday(&birthtime_, 0);
    natcheck_created++;
}

NatCheck::~NatCheck()
{
    natcheck_deleted++;
}

void NatCheck::start()
{
    natcheck_started++;
    num_checking++;
    socket_closed = false;
#ifdef DISABLE_NATCHECK
    result(1); // if natchecks are disabled, consider this natcheck passed right away
    return;
#endif
    socket_.async_connect(endpoint_,
                          boost::bind(&NatCheck::handle_connect, shared_from_this(),
                                      boost::asio::placeholders::error));

    timer_.expires_from_now(boost::posix_time::seconds(NC_TIMEOUT));
    timer_.async_wait(boost::bind(&NatCheck::handle_timeout, shared_from_this(),
                                  "Timeout while connecting",
                                  boost::asio::placeholders::error));
}

static double tv_delta(const struct timeval &tv1, const struct timeval &tv2)
{
    return tv2.tv_sec - tv1.tv_sec + 1e-6 * (tv2.tv_usec - tv1.tv_usec);
}

double NatCheck::age(struct timeval *now)
{
    struct timeval tv;

    if (!now)
    {
        now = &tv;
        gettimeofday(now, 0);
    }

    return tv_delta(birthtime_, *now);
}

std::string NatCheck::class_stats()
{
    std::stringstream st;

    st << "NatCheck created: " << natcheck_created << std::endl;
    st << "NatCheck deleted: " << natcheck_deleted << std::endl;
    st << "NatCheck started: " << natcheck_started << std::endl;
    st << "NatCheck success: " << natcheck_success << std::endl;
    st << "NatCheck success time: " << natcheck_success_sum << std::endl;
    st << "NatCheck fail: " << natcheck_fail << std::endl;
    st << "NatCheck fail time: " << natcheck_fail_sum << std::endl;
    st << "NatCheck timeout: " << natcheck_timeout << std::endl;
    st << "NatCheck timeout time: " << natcheck_timeout_sum << std::endl;
    st << "NatCheck queue length: " << nc_queue.size() << std::endl;
    st << "NatCheck queue average age: " << nc_queue_average_age() << std::endl;
    st << "NatCheck num_checking: " << num_checking << std::endl;

    return st.str();
}

void NatCheck::result(int r)
{
    CallbackHandler<int> h = handler_;
    handler_ = NULL;
    timer_.cancel();
    h(r);
    num_checking--;
    natcheck_success++;
    natcheck_success_sum += age();
    start_from_queue();
}

void NatCheck::result(const boost::system::error_code& err)
{
    result(boost::system::system_error(err));
}

void NatCheck::result(const std::exception& exc)
{
    timer_.cancel();
    socket_closed = true;
    socket_.close();
    assert(handler_);
    CallbackHandler<int> h = handler_;
    assert(h);
    handler_ = NULL;
    assert(h);
    h(exc);
    assert(h);
    natcheck_fail++;
    natcheck_fail_sum += age();
    num_checking--;
    start_from_queue();
}

void NatCheck::handle_connect(const boost::system::error_code& err)
{
    if (socket_closed)
    {
        return;
    }

    if (err)
    {
        result(err);
        return;
    }

    hdr_.reset(new PeerConnHeader);
    BuildLoginPacket(*hdr_, binary_infohash_);

    boost::asio::async_write(socket_,
                             boost::asio::buffer((const void*)hdr_.get(), sizeof(PeerConnHeader)),
                             boost::bind(&NatCheck::handle_write_handshake,
                                         shared_from_this(),
                                         boost::asio::placeholders::error));

    timer_.expires_from_now(boost::posix_time::seconds(NC_TIMEOUT));
    timer_.async_wait(boost::bind(&NatCheck::handle_timeout, shared_from_this(),
                                  "Timeout while writing",
                                  boost::asio::placeholders::error));
}

void NatCheck::handle_write_handshake(const boost::system::error_code& err)
{
    assert(hdr_);
    hdr_.reset();

    if (socket_closed)
    {
        return;
    }

    if (err)
    {
        result(err);
        return;
    }

    boost::asio::async_read(socket_, response_,
                            boost::asio::transfer_at_least(sizeof(PeerConnHeader)),
                            boost::bind(&NatCheck::handle_handshake, shared_from_this(),
                                        boost::asio::placeholders::error));

    timer_.expires_from_now(boost::posix_time::seconds(NC_TIMEOUT));
    timer_.async_wait(boost::bind(&NatCheck::handle_timeout, shared_from_this(),
                                  "Timeout while reading",
                                  boost::asio::placeholders::error));
}



void NatCheck::handle_timeout(const char* msg, const boost::system::error_code& err)
{
    if (socket_closed)
    {
        return;
    }

    if (err)
    {
        // the timer was reset. no big deal.
        return;
    }
    natcheck_timeout++;
    natcheck_timeout_sum += age();
    result(std::logic_error(msg));
    handler_ = CallbackHandler<int>(swallow_success<int>, swallow_error);
    assert(handler_);
    socket_closed = true;
    socket_.close();
}

void NatCheck::handle_handshake(const boost::system::error_code& err)
{
    if (socket_closed)
    {
        return;
    }

    if (err) // EOF is an error.
    {
        result(err);
        return;
    }

    std::stringstream s;
    s << &response_;
    socket_closed = true;
    socket_.close();
    assert(s.str().size() >= sizeof(PeerConnHeader));
    try
    {
        ParseLoginPacket((PeerConnHeader*)s.str().c_str(), binary_infohash_, peer_id_);
        result(1);
    }
    catch (std::exception& e)
    {
        result(e);
    }
}
