//
// connection.hpp
// ~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2007 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef HTTP_CONNECTION_HPP
#define HTTP_CONNECTION_HPP

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/shared_array.hpp>
#include <boost/enable_shared_from_this.hpp>
#include "reply.hpp"
#include "request.hpp"
#include "request_handler.hpp"
#include "request_parser.hpp"
#include "templates.h"

namespace http {
namespace server {

class server;

class connection_manager;

class Result;

/// Represents a single connection from a client.
class connection
  : public boost::enable_shared_from_this<connection>,
    private boost::noncopyable
{
public:
  /// Construct a connection with the given io_service.
  explicit connection(server& http_server);

  /// Get the socket associated with the connection.
  boost::asio::ip::tcp::socket& socket() { return _socket; }

  /// Get the remove peer address associated with the connection.
  boost::asio::ip::tcp::endpoint& peer_endpoint() { return _endpoint; }

  /// Start the first asynchronous operation for the connection.
  void start();

  /// Stop all asynchronous operations associated with the connection.
  void stop();

  /// Submits pending result if one is not in progess.
  void write();

  /// Begins a read operation.
  void read();

  /// Processes one request in the incoming buffer
  void process_buffer();

private:
  /// Handle completion of a read operation.
  void handle_read(const boost::system::error_code& e,
      std::size_t bytes_transferred);

  /// Handle completion of a write operation.
  void handle_write(const boost::system::error_code& e);

  /// Socket for the connection.
  boost::asio::ip::tcp::socket _socket;

  /// Remote peer's endpoint
  boost::asio::ip::tcp::endpoint _endpoint;

  /// The manager for this connection.
  connection_manager& _connection_manager;

  /// The handler used to process the incoming request.
  request_handler* _request_handler;

  /// Buffer for incoming data.
  boost::shared_array<char> _buffer;

  /// Current read position.
  size_t _recv_pos;

  /// The incoming request.
  request _request;

  /// The parser for the incoming request.
  request_parser _request_parser;

  /// Whether we are writing a result currently.
  bool _writing;

  /// Whether to close the connection when the write completes.
  bool _should_keepalive;

  /// List of pending results.
  std::list<Result> _results;

  /// The reply to be sent back to the client.
  reply _reply;

  server& _http_server;
};

typedef boost::shared_ptr<connection> connection_ptr;

class Result {
public:
    Result(connection_ptr c);
    void finished(const reply& r);

    bool complete;
    reply _reply;
    connection_ptr _connection;
};


} // namespace server
} // namespace http

#endif // HTTP_CONNECTION_HPP
