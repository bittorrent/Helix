//
// connection.cpp
// ~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2007 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "connection.hpp"
#include <vector>
#include <boost/pool/pool.hpp>
#include <boost/bind.hpp>
#include "connection_manager.hpp"
#include "request_handler.hpp"
#include "server.hpp"
#include "utils.hpp"

namespace http {
namespace server {

#define SERVER_BUFFER_SIZE 65535

boost::pool<> _connection_buffer(SERVER_BUFFER_SIZE);

void free_buffer(char* mem)
{
	_connection_buffer.free(mem);
}

int pending = 0;

class GlobalCounter
{
public:
    GlobalCounter(std::string label)
    {
        val = 0;
        name = label;
        t = time(NULL);
    }

    void add()
    {
        val++;
        time_t cur = time(NULL);
        time_t runtime = cur - t;
        if (runtime > 15)
        {
            logger << "+++ " << val / runtime << " " << name << " +++" << std::endl;
            val = 0;
            t = cur;
        }
    }

    size_t val;
    time_t t;
    std::string name;
};

GlobalCounter c_starts("starts/s");
GlobalCounter c_stops("stops/s");

connection::connection(server& http_server)
  : _socket(http_server.io_service()),
    _connection_manager(http_server.get_connection_manager()),
    _request_handler(NULL),
    _http_server(http_server)
{
    _writing = false;
    _should_keepalive = false;
    pending++;
    //logger << "new connection! " << pending << " pending" << std::endl;
}

void connection::start()
{
    c_starts.add();

    _request_handler = _http_server.get_request_handler();

    _recv_pos = 0;
	_buffer.reset((char*)_connection_buffer.malloc(), &free_buffer);
    _socket.set_option(boost::asio::ip::tcp::no_delay(true));
    read();
}

void connection::stop()
{
    c_stops.add();

    boost::system::error_code ec;
    if (_socket.is_open())
    {
        // ignore errors
        _socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
    }

    // ignore errors
    _socket.close(ec);

    pending--;
    //logger << "closed connection! " << pending << " pending" << std::endl;
}

void connection::read()
{
    conn_assert(_recv_pos < SERVER_BUFFER_SIZE);
    _socket.async_read_some(boost::asio::buffer(&_buffer[_recv_pos], SERVER_BUFFER_SIZE - _recv_pos),
                            boost::bind(&connection::handle_read, shared_from_this(),
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::bytes_transferred));
}

void connection::handle_read(const boost::system::error_code& e,
    std::size_t bytes_transferred)
{
    if (e)
    {
        if (e != boost::asio::error::operation_aborted)
        {
            _connection_manager.stop(shared_from_this());
        }
        return;
    }

    _recv_pos += bytes_transferred;
    conn_assert(_recv_pos <= SERVER_BUFFER_SIZE);

    process_buffer();
}

void connection::process_buffer()
{
    while (true)
    {
        boost::tribool result;
        char const *it;
        boost::tie(result, it) = _request_parser.parse(
            _request, &_buffer[0], &_buffer[0] + _recv_pos);

        size_t bytes_used = it - &_buffer[0];
        conn_assert(bytes_used <= _recv_pos);
        memmove(&_buffer[0], it, _recv_pos - bytes_used);
        _recv_pos -= bytes_used;

        if (boost::indeterminate(result))
        {
            if (bytes_used >= SERVER_BUFFER_SIZE)
            {
                logger << "Request exceeded buffer size (" << bytes_used << "/" << SERVER_BUFFER_SIZE << ")" << std::endl;
                _connection_manager.stop(shared_from_this());
                return;
            }
            read();
            return;
        }

        _request_parser.reset();

        _results.push_back(Result(shared_from_this()));
        Result& nr = _results.back();

        // errors and all HTTP versions except 1.1 (see below) default to false
        _should_keepalive = false;

        if (!result)
        {
            nr.finished(reply::stock_reply(reply::bad_request));
            return;
        }

        std::vector<header>& headers = _request.headers;

        if (_request.http_version_major == 1)
        {
            if (_request.http_version_minor == 1)
            {
                // HTTP 1.1 defaults to true
                _should_keepalive = true;
            }

            for (size_t i = 0; i < headers.size(); i++)
            {
                if (stricmp(headers[i].name.c_str(), "connection") == 0)
                {
                    if (stricmp(headers[i].value.c_str(), "keep-alive") == 0)
                    {
                        _should_keepalive = true;
                    }
                    else if (stricmp(headers[i].value.c_str(), "close") == 0)
                    {
                        _should_keepalive = false;
                    }
                    break;
                }
            }

            /*
            if (!_should_keepalive)
            {
                logger << "shouldn't keepalive! HTTP 1." <<  _request.http_version_minor << std::endl;
            }
            */
        }

        _request_handler->handle_request(_http_server, peer_endpoint(), _request, nr);
        _request.reset();

        if (!_should_keepalive)
        {
            // the write completion handler will do the disconnect
            break;
        }
    }
}

void connection::write()
{
    if (_writing)
        return;
    if (_results.size() == 0)
        return;

    Result& r = _results.front();

    if (!r.complete)
        return;

    header h;
    h.name = "Connection";
    if (_should_keepalive)
    {
        h.value = "Keep-Alive";
    } else {
        h.value = "close";
    }
    r._reply.headers.push_back(h);

    _writing = true;
    boost::asio::async_write(_socket, r._reply.to_buffers(),
                             boost::bind(&connection::handle_write, shared_from_this(),
                                         boost::asio::placeholders::error));
}

void connection::handle_write(const boost::system::error_code& e)
{
    _writing = false;

    _results.pop_front();

    if (e)
    {
        if (e != boost::asio::error::operation_aborted)
        {
            _connection_manager.stop(shared_from_this());
        }
        return;
    }

    if (!_should_keepalive)
    {
        _connection_manager.stop(shared_from_this());
        return;
    }

    write();
}

Result::Result(connection_ptr c)
{
    complete = false;
    _connection = c;
}

void Result::finished(const reply& r)
{
    _reply = r;
    complete = true;
    _connection->write();
}

} // namespace server
} // namespace http
