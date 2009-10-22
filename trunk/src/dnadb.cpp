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

#ifndef DISABLE_DNADB

#include "dnadb.hpp"
#include "utils.hpp"
#include "control.hpp"
#include <fstream>

#define AUTH_INTERVAL (5 * 60)

DBAuthorizer::DBAuthorizer(size_t max_connections) :
    _connection_pool(max_connections,
                     boost::bind(&DBAuthorizer::make_connection, this)),
    _io_service(_thread_pool.io_service()),
    _periodic_update(_io_service),
    _last_time(boost::posix_time::min_date_time),
    _max_connections(max_connections)
{}

void DBAuthorizer::setup_controls(ControlAPI &controls)
{
    controls.add_variable("mysql_db",
            boost::bind(&ControlAPI::set_string, &_mysql_db, _1),
            boost::bind(&ControlAPI::get_string, &_mysql_db));
    controls.add_variable("mysql_host",
            boost::bind(&ControlAPI::set_string, &_mysql_host, _1),
            boost::bind(&ControlAPI::get_string, &_mysql_host));
    controls.add_variable("mysql_user",
            boost::bind(&ControlAPI::set_string, &_mysql_user, _1),
            boost::bind(&ControlAPI::get_string, &_mysql_user));
    controls.add_variable("mysql_password",
            boost::bind(&ControlAPI::set_string, &_mysql_password, _1),
            boost::bind(&ControlAPI::get_string, &_mysql_password));
    controls.add_variable("mysql_port",
            boost::bind(&ControlAPI::set_int, &_mysql_port, _1),
            boost::bind(&ControlAPI::get_int, &_mysql_port));
}

void DBAuthorizer::start(void)
{
    _thread_pool.start(_max_connections);

    _periodic_update.start(boost::posix_time::seconds(AUTH_INTERVAL),
                           boost::bind(&DBAuthorizer::periodic_update, this));
    _periodic_update.post_one();
}

bool DBAuthorizer::is_allowed(std::string binary_infohash)
{
    boost::mutex::scoped_lock lock(_cache_mutex);

    return (_blacklist.count(binary_infohash) == 0);
}

void DBAuthorizer::periodic_update()
{
    mysqlpp::Connection *con = _connection_pool.get_connection();

    // we might have a NULL which means a connection attempt failed.
    if (!con) {
        return;
    }

    mysqlpp::Query query = con->query();

    boost::posix_time::ptime p = boost::posix_time::second_clock::universal_time();

    std::string last_update;
    last_update += "'";
    last_update += boost::gregorian::to_iso_extended_string_type<char>(_last_time.date());
    last_update += " ";
    last_update += boost::posix_time::to_simple_string(_last_time.time_of_day());
    last_update += "'";
    query << "SELECT torrents.tid, torrents.enabled, domains.suspended, companies.suspended "
       "FROM torrents, domains, companies WHERE "
        "domains.did = torrents.did AND "
        "companies.cid = torrents.cid AND "
        "(torrents.modified >= " << last_update << " OR "
        "domains.modified >= " << last_update << ")";
    // optimization for when we have an empty black-list. Only look
    // for torrents that are disabled/suspended
    if (_blacklist.empty())
        query << " AND (torrents.enabled = false OR "
        "domains.suspended = true OR "
        "companies.suspended = true)";

#if MYSQLPP_HEADER_VERSION >= MYSQLPP_VERSION(3, 0, 0)
    mysqlpp::StoreQueryResult res = query.store();
#else
    mysqlpp::Result res = query.store();
#endif

    if (!res) {
        logger << "Failed to get update: " << query.error() << std::endl;
        delete con;
        _connection_pool.lost_connection();
        return;
    }

    _connection_pool.put_connection(con);
    _last_time = p;

    mysqlpp::Row::size_type i;
    int num_rows = res.num_rows();
    logger << "updating " << num_rows << " torrents" << std::endl;
    int num_removed = 0;
    int num_added = 0;
    boost::mutex::scoped_lock lock(_cache_mutex);
    for (i = 0; i < num_rows; ++i) {
        mysqlpp::Row row = res.at(i);
          libtorrent::sha1_hash tid;
          std::stringstream stream(std::string(row.at(0).data(), row.at(0).length()));
        stream >> tid;
        // column 2 and 3 are 'suspended', not enabled, so they are
        // inverted from column 1, which is 'enabled'
        bool enabled = bool(row.at(1)) && bool(!row.at(2)) && bool(!row.at(3));
        if (enabled)
        {
            if (_blacklist.erase(tid))
                ++num_removed;
        }
        else
        {
            if (_blacklist.insert(tid).second)
                ++num_added;
        }
    }
    lock.unlock();

    logger << "   added   " << num_added << " torrents to blacklist" << std::endl;
    logger << "   removed " << num_removed << " torrents from blacklist" << std::endl;

    return;
}

mysqlpp::Connection* DBAuthorizer::make_connection()
{
    mysqlpp::Connection *con = new mysqlpp::Connection(false);
    con->connect(_mysql_db.c_str(), _mysql_host.c_str(), _mysql_user.c_str(), _mysql_password.c_str(), _mysql_port);
    if (!(*con)) {
        logger << "MYSQL connection failed: " << con->error() << "\n";
        logger << "  " << _mysql_db << " " << _mysql_host << " " << _mysql_user << " " << _mysql_port << "\n";
        delete con;
        return NULL;
    }
    return con;
}

#endif

