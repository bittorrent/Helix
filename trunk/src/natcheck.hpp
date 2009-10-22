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

#ifndef __NATCHECK_HPP__
#define __NATCHECK_HPP__

#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include "callback_handler.hpp"
#include "boost_utils.hpp"
#include "templates.h"

using boost::asio::ip::tcp;

static const char MAGIC_PEERID[] = "MAGICMAGICMAGICMAGIC";

#define SIZE_OF_PEER_ID 20
struct PeerConnHeader {
    char ident[20];
    byte reserved[8];
    byte info[20];
    byte peer_id[SIZE_OF_PEER_ID];
};

void StartNatCheck(boost::asio::io_service& io_service, tcp::endpoint const& endpoint,
                   const byte* binary_infohash, const byte* peer_id,
                   CallbackHandler<int> handler);

class NatCheck :
    public boost::enable_shared_from_this<NatCheck>,
    private boost::noncopyable
{
public:
    NatCheck(boost::asio::io_service& io_service, tcp::endpoint const& endpoint,
             const byte* binary_infohash, const byte* peer_id,
             CallbackHandler<int> handler);

    ~NatCheck();

    void start();

    double age(struct timeval *now = 0);

    static std::string class_stats();
private:

    static long natcheck_started;
    static long natcheck_created;
    static long natcheck_deleted;
    static long natcheck_success;
    static double natcheck_success_sum;
    static long natcheck_fail;
    static double natcheck_fail_sum;
    static long natcheck_timeout;
    static double natcheck_timeout_sum;

    void handle_connect(const boost::system::error_code& err);

    void handle_write_handshake(const boost::system::error_code& err);
    void handle_handshake(const boost::system::error_code& err);
    void handle_timeout(const char* msg, const boost::system::error_code& err);

    void result(int r);
    void result(const boost::system::error_code& err);
    void result(const std::exception& exc);


    tcp::socket socket_;
    bool socket_closed;

    boost::asio::streambuf response_;
    Timer timer_;
    CallbackHandler<int> handler_;
    struct timeval birthtime_;
    tcp::endpoint endpoint_;
    byte binary_infohash_[20];
    byte peer_id_[20];
    boost::scoped_ptr<PeerConnHeader> hdr_;
};

#endif //__NATCHECK_HPP__
