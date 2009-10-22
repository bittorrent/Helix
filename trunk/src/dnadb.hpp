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

#ifndef __DNADB_HPP__
#define __DNADB_HPP__

#include "boost_utils.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"
#include "mysql++.h"
#include "templates.h"
#include "xplat_hash_map.hpp"
#include "libtorrent/peer_id.hpp"

USING_NAMESPACE_EXT

class ControlAPI;

class DBAuthorizer
{
public:
    typedef ConnectionPool<mysqlpp::Connection*> mysqlPool;

    DBAuthorizer(size_t max_connections = 10);
    ~DBAuthorizer()
    {
        _thread_pool.stop();
    }

    bool is_allowed(std::string binary_infohash);

    void start(void);
    void setup_controls(ControlAPI &controls);

private:
    void update_value(std::string binary_infohash);
    bool update_interest(std::string binary_infohash);
    void periodic_set_interest();
    void periodic_update();

    mysqlpp::Connection* make_connection();

    boost::mutex _cache_mutex;
    hash_set<libtorrent::sha1_hash> _blacklist;

    mysqlPool _connection_pool;
    ThreadPool _thread_pool;
    boost::asio::io_service& _io_service;

    LoopingCall _periodic_update;

    boost::posix_time::ptime _last_time;
    int _max_connections;

    std::string _mysql_db;
    std::string _mysql_host;
    std::string _mysql_user;
    std::string _mysql_password;
    int _mysql_port;
};

#endif //__DNADB_HPP__
