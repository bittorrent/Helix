//
// server.hpp
// ~~~~~~~~~~
//
// Copyright (c) 2003-2007 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef __SERVER_HPP__
#define __SERVER_HPP__

#include <boost/asio.hpp>
#include <string>
#include <boost/noncopyable.hpp>
#include "connection.hpp"
#include "connection_manager.hpp"
#include "request_handler.hpp"

namespace http {
namespace server {

typedef boost::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor_ptr;

/// The top-level class of the HTTP server.
class server
  : private boost::noncopyable
{
public:
  /// Construct the server to listen on the specified TCP address(es) and port
  explicit server(const std::vector<std::string>& addresses, const std::string& port);

  /// Run the server's io_service loop.
  void run();

  /// Stop the server.
  void stop();

  /// Get the io_service associated with the object.
  boost::asio::io_service& io_service() { return io_service_; }

  connection_manager& get_connection_manager() { return connection_manager_; }

  void set_request_handler(request_handler* rh) { request_handler_ = rh; }
  request_handler* get_request_handler() { return request_handler_; }

private:

  /// Start an asynchronous accept operation.
  void start_accept(const acceptor_ptr acceptor, const connection_ptr pconnection);

  /// Handle completion of an asynchronous accept operation.
  void handle_accept(const boost::system::error_code& e,
                     const acceptor_ptr acceptor, const connection_ptr pconnection);

  /// Handle a request to stop the server.
  void handle_stop();

  /// The io_service used to perform asynchronous operations.
  boost::asio::io_service io_service_;

  /// Acceptor used to listen for incoming connections.
  std::vector<acceptor_ptr> acceptors_;

  /// The connection manager which owns all live connections.
  connection_manager connection_manager_;

  /// The handler for all incoming requests.
  request_handler* request_handler_;
};

} // namespace server
} // namespace http

#endif // __SERVER_HPP__
