//
// server.cpp
// ~~~~~~~~~~
//
// Copyright (c) 2003-2007 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Modified heavily by Greg Hazel, 2007

#include "server.hpp"
#include <boost/bind.hpp>
#include "utils.hpp"

struct v6only
{
   v6only(bool enable): m_value(enable) {}
   template<class Protocol>
   int level(Protocol const&) const { return IPPROTO_IPV6; }
   template<class Protocol>
   int name(Protocol const&) const { return IPV6_V6ONLY; }
   template<class Protocol>
   int const* data(Protocol const&) const { return &m_value; }
   template<class Protocol>
   size_t size(Protocol const&) const { return sizeof(m_value); }
   int m_value;
};
  
namespace http {
namespace server {

server::server(const std::vector<std::string>& addresses, const std::string& port)
  : io_service_(),
    connection_manager_(),
    request_handler_(NULL)
{
    // Open the acceptor with the option to reuse the address (i.e. SO_REUSEADDR).
    boost::asio::ip::tcp::resolver resolver(io_service_);
    for (size_t i = 0; i < addresses.size(); i++)
    {
        try
        {
            boost::asio::ip::tcp::resolver::query query(addresses[i], port);
            boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve(query);
            boost::asio::ip::tcp::acceptor *acceptor = new boost::asio::ip::tcp::acceptor(io_service_);
            acceptor_ptr pacceptor(acceptor);
            acceptor->open(endpoint.protocol());
            if (endpoint.protocol() == boost::asio::ip::tcp::v6())
                acceptor->set_option(v6only(true));
            acceptor->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            acceptor->bind(endpoint);
            acceptor->listen(2000);
            connection_ptr new_connection(new connection(*this));
            start_accept(pacceptor, new_connection);
            acceptors_.push_back(pacceptor);
        }
        catch (const std::exception& e)
        {
            logger << "Unable to listen on '" << addresses[i] << "': " << e.what() << std::endl;
        }
    }
}

void server::run()
{
    // The io_service::run() call will block until all asynchronous operations
    // have finished. While the server is running, there is always at least one
    // asynchronous operation outstanding: the asynchronous accept call waiting
    // for new incoming connections.
    io_service_.run();
}

void server::stop()
{
    // Post a call to the stop function so that server::stop() is safe to call
    // from any thread.
    io_service_.post(boost::bind(&server::handle_stop, this));
}

void server::start_accept(const acceptor_ptr acceptor, const connection_ptr pconnection)
{
    acceptor->async_accept(pconnection->socket(), pconnection->peer_endpoint(),
                           boost::bind(&server::handle_accept, this,
                                       boost::asio::placeholders::error, acceptor, pconnection));
}

void server::handle_accept(const boost::system::error_code& e,
                           const acceptor_ptr acceptor, const connection_ptr pconnection)
{
    if (!e)
    {
        connection_manager_.start(pconnection);
        connection_ptr new_connection(new connection(*this));
        start_accept(acceptor, new_connection);
    }
}

void server::handle_stop()
{
    // The server is stopped by cancelling all outstanding asynchronous
    // operations. Once all operations have finished the io_service::run() call
    // will exit.
    while (acceptors_.size() != 0)
    {
        acceptor_ptr a = acceptors_.back();
        acceptors_.pop_back(); // ah, relief.
        a->close();
    }
    connection_manager_.stop_all();
    // BUG: forceful shutdown
    io_service_.stop();
}

} // namespace server
} // namespace http
