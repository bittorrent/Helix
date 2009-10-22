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

#ifndef __HELIX_HANDLER_HPP__
#define __HELIX_HANDLER_HPP__

#include <libtorrent/entry.hpp>
#include "control.hpp"

#ifndef DISABLE_DNADB
#include "dnadb.hpp"
#endif

namespace http {
namespace server {

class Swarm;

class helix_handler: public request_handler
{
public:

    explicit helix_handler(boost::asio::io_service& io_service, const std::string& port);

    void handle_request(server& http_server,
                        const boost::asio::ip::tcp::endpoint& endpoint, const request& req, Result& res);

    ControlAPI controls;
    static std::string handler_name(void);

    void start();
private:

    bool set_torrents_enabled(bool enabled, std::vector< std::string > &);
    std::string get_torrent_blacklist();

    void periodic();
    void reply_bencoded(Result& res, libtorrent::entry::dictionary_type &dict, Swarm* s = NULL, reply::status_type status = reply::ok);
    void reply_text(Result &res, const std::string &content);
    void do_helix_statistics(void);
    static std::string class_stats();
    bool endpoint_ok_for_control_set(const boost::asio::ip::tcp::endpoint &);

    static std::string get_swarm_flags(const std::string &);
    static bool set_swarm_flags(const std::string &,
            const hash_map< std::string, std::vector<std::string> >&);

    boost::asio::io_service& _io_service;
    char _myid[6*2 + 1];
    LoopingCall _periodic;

    perfmeter pm_;

#ifndef DISABLE_DNADB
    DBAuthorizer dba;
#endif

    // the time when the last snapshot of all
    // the swarms was saved to disk
    int _last_checkpoint;
    bool control_only_from_localhost_;
    bool enforce_db_blacklist_;
    bool enforce_auth_token_;
    std::string secret_auth_token_;
};

} // namespace server
} // namespace http

#endif // __HELIX_HANDLER_HPP__
