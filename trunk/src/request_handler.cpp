//
// request_handler.cpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2007 BitTorrent, Inc.
// Copyright (c) 2003-2007 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "request_handler.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <boost/lexical_cast.hpp>
#include "reply.hpp"
#include "request.hpp"
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/escape_string.hpp>
#include "utils.hpp"
#include "server.hpp"

namespace http {
namespace server {


request_handler::request_handler(boost::asio::io_service& io_service,
                                 const std::string& port)
{
    gethostname(_hostname, 256);
}

bool request_handler::url_parse(const std::string& url,
                                std::string& request_path,
                                hash_map< std::string, std::vector<std::string> >& query_params)
{
    std::string::size_type path_len;
    try
    {
        path_len = url.find_first_of("?");
    }
    catch (std::exception& e)
    {
        path_len = url.size();
    }

    try
    {
        request_path = libtorrent::unescape_string(url.substr(0, path_len));
    }
    catch (std::runtime_error& exc)
    {
        return false;
    }

    std::string query_string = url.substr(path_len + 1, url.size() - (path_len + 1));

    str query_copy = strdup(query_string.c_str());
    boost::shared_ptr<char> fancy(query_copy, free);
    str query = query_copy;
    str kvpairs = strsep(&query, '&');
    for (; kvpairs != NULL; kvpairs = strsep(&query, '&')) {
        if (*kvpairs == 0)
            continue;
        
        str kvpairs2 = strdup(kvpairs);
        boost::shared_ptr<char> fancy(kvpairs2, free);
        
        str skey = strsep(&kvpairs2, '=');
        if (skey == NULL || *skey == 0)
            continue;
        str sval = strsep(&kvpairs2, '=');
        if (sval == NULL || *sval == 0)
            continue;

        std::string key;
        std::string val;

        try
        {
            key = libtorrent::unescape_string(skey);
        }
        catch (std::runtime_error& exc)
        {
            return false;
        }

        try
        {
            val = libtorrent::unescape_string(sval);
        }
        catch (std::runtime_error& exc)
        {
            return false;
        }

        //logger << key << " = " << val << std::endl;
        if (query_params.count(key))
        {
            std::vector<std::string>& vals = query_params[key];
            vals.push_back(val);
        }
        else
        {
            std::vector<std::string> vals;
            vals.push_back(val);
            query_params[key] = vals;
        }
    }

    return true;
}


void request_handler::reply_bencoded(Result& res, libtorrent::entry::dictionary_type &dict)
{
    try
    {
        std::stringstream st;
        bencode(std::ostream_iterator<char>(st), dict);
        const std::string& str = st.str();

        reply rep;

        rep.status = reply::ok;
        rep.content.append(str);
        rep.headers.resize(4);
        rep.headers[0].name = "Content-Length";
        rep.headers[0].value = boost::lexical_cast<std::string>((unsigned int)str.length());
        rep.headers[1].name = "Content-Type";
        rep.headers[1].value = "text/plain";
        rep.headers[2].name = "X-Server";
        rep.headers[2].value = _hostname;
        rep.headers[3].name = "X-CPU";
        rep.headers[3].value = string_format("%.2f", _cpu_monitor.get_cpu_percent());

        res.finished(rep);
    }
    catch (std::exception& e)
    {
        logger << "Exception preparing response: " << e.what() << std::endl;
        res.finished(reply::stock_reply(reply::internal_server_error));
    }
    catch (...)
    {
        logger << "Critical exception preparing response!" << std::endl;
        res.finished(reply::stock_reply(reply::internal_server_error));
    }
}


void request_handler::handle_request(server& http_server,
                                     const boost::asio::ip::tcp::endpoint& endpoint, const request& req, Result& res)
{
    res.finished(reply::stock_reply(reply::not_found));
}


} // namespace server
} // namespace http
