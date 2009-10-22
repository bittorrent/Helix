//
// request_handler.hpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2007 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef HTTP_REQUEST_HANDLER_HPP
#define HTTP_REQUEST_HANDLER_HPP

#include <string>
#include "xplat_hash_map.hpp"
#include <boost/noncopyable.hpp>
#include <boost/asio.hpp>
#include <libtorrent/entry.hpp>
#include "cpu_monitor.hpp"

namespace http {
namespace server {

USING_NAMESPACE_EXT

class server;

struct reply;
class request;

class Result;

/// The common handler for all incoming requests.
class request_handler
  : private boost::noncopyable
{
public:
  explicit request_handler(boost::asio::io_service& io_service, const std::string& port);

  virtual ~request_handler() {};

  /// Handle a request and produce a reply.
  virtual void handle_request(server& http_server,
                              const boost::asio::ip::tcp::endpoint& endpoint, const request& req, Result& res);

protected:

  void reply_bencoded(Result& res, libtorrent::entry::dictionary_type &dict);

  /// Pull the path and query parameters out of a URL.
  static bool url_parse(const std::string& url,
                        std::string& request_path,
                        hash_map< std::string, std::vector<std::string> >& query_params);

  char _hostname[256];
  CpuMonitorThread _cpu_monitor;
};

} // namespace server
} // namespace http

#endif // HTTP_REQUEST_HANDLER_HPP
