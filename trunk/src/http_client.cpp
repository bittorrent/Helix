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

#include "http_client.hpp"

#include <stdarg.h>
#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <boost/bind.hpp>
#include <boost/random.hpp>
#include "templates.h"
#include "utils.hpp"

using boost::asio::ip::tcp;

/*
    TODO:

    Buffers:
    Not so fond of the buffer management here. It involves a memmove to shift back, and
    a full copy on result. Scatter-gather would reduce the effect of the memmove, and
    boost::asio makes that relatively easy. Another option is a std::deque to reduce the
    effect of cut_receive_buffer without the scatter-gather change, but several pieces
    of the code rely on a contiguous chunk of memory.
*/

#define EP_TO_STR(e) (e).address().to_string() << ":" << (e).port()

void HTTPClient::init()
{
    _connecting = false;
    _writing = false;
    _reading = false;
    _waiting = false;
    _port = 0;
    m_recv_pos = 0;
    m_recv_buffer.resize(BUFFER_SIZE * 2);

    _maxDelay = 3600;
    _initialDelay = 1.0;
    // Note: These highly sensitive factors have been precisely measured by
    // the National Institute of Science and Technology.  Take extreme care
    // in altering them, or you may damage your Internet!
    _factor = 2.7182818284590451f; // (math.e)
    // Phi = 1.6180339887498948; // (Phi is acceptable for use as a
    // factor if e is too large for your application.)
    _jitter = 0.11962656492f; // molar Planck constant times c, Jule meter/mole

    _delay = _initialDelay;

    _maxRetries = 0;
    _retries = 0;
}

HTTPClient::request_struct& HTTPClient::add_request()
{
    request_struct nr;
    _pending_requests.push_back(nr);
    return _pending_requests.back();
}

void HTTPClient::get(const std::string& path,
                     std::vector<header>& headers,
                     callback_t handler)
{
    request_struct &r = add_request();
    r.path = path;
    r.headers = headers;
    r.handler = handler;
    submit_next_request();
}

void HTTPClient::post(const std::string& path, const std::string& body,
                      callback_t handler)
{
    request_struct &r = add_request();
    r.path = path;
    r.body = body;
    r.handler = handler;
    submit_next_request();
}

void HTTPClient::submit_next_request() try
{
    if (_pending_requests.empty())
    {
        return;
    }

    if (_connecting || _writing || _waiting)
    {
        return;
    }

    if (!_socket.is_open())
    {
        start_connection();
        return;
    }

    request_struct request = _pending_requests.front();
    _pending_requests.pop_front();

    conn_assert(m_send_buffer.empty());

    std::ostringstream request_stream;

    if (request.body.size() > 0)
    {
        request_stream << "POST";
    } else {
        request_stream << "GET";
    }

    request_stream << " " << request.path << " HTTP/1.1\r\n";

    //logger << "Submitting: " << request_stream.str() << std::endl;

    request_stream << "Host: " << _host << ":" << _port << "\r\n";
    request_stream << "User-Agent: HelixClient/1.0\r\n";
    request_stream << "Accept: */*\r\n";
    request_stream << "Connection: Keep-Alive\r\n";

    for (uint i = 0; i < request.headers.size(); i++)
    {
        header& h = request.headers[i];
        request_stream << h.name << ": " << h.value << "\r\n";
    }

    if (request.body.size() > 0)
    {
        // ok ok, so posting > 4GB will fail. who cares.
        request_stream << "Content-Length: " << (unsigned int)request.body.length() << "\r\n";
        request_stream << "Content-Type: application/octet-stream\r\n";
    }

    request_stream << "\r\n";

    if (request.body.size() > 0)
    {
        request_stream << request.body;
    }

    _active_requests.push_back(request);

    m_send_buffer = request_stream.str();

    _writing = true;
    boost::asio::async_write(_socket, boost::asio::buffer(m_send_buffer),
                       boost::bind(&HTTPClient::handle_write_request, this,
                                   boost::asio::placeholders::error));
}
catch (std::exception& e)
{
    request_failed(e);
};

void HTTPClient::start_connection()
{
    // Start an asynchronous resolve to translate the server and service names
    // into a list of endpoints.
    tcp::resolver::query query(_host, "http");

    _connecting = true;
    _resolver.async_resolve(query,
                            boost::bind(&HTTPClient::handle_resolve, this,
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::iterator));

    _timer.expires_from_now(boost::posix_time::seconds(HTTP_TIMEOUT));
    _timer.async_wait(boost::bind(&HTTPClient::handle_timeout, this,
                                  boost::asio::placeholders::error));
}

void HTTPClient::handle_timeout(const boost::system::error_code& err)
{
    if (err)
    {
        // the timer was reset. no big deal.
        return;
    }
    // timeouts are non-critical, I assume. retry.
    abort_request();
}

void HTTPClient::handle_resolve(const boost::system::error_code& err,
                                tcp::resolver::iterator endpoint_iterator)
{
    if (err)
    {
        _connecting = false;
        request_failed(err);
        return;
    }

    // Attempt a connection to the first endpoint in the list. Each endpoint
    // will be tried until we successfully establish a connection.
    next_endpoint(endpoint_iterator);
}

#define EP_TO_STR(e) (e).address().to_string() << ":" << (e).port()

void HTTPClient::next_endpoint(tcp::resolver::iterator endpoint_iterator)
{
    tcp::endpoint endpoint = *endpoint_iterator;
    endpoint.port(_port);
    //logger << EP_TO_STR(endpoint) << "\n";
    _socket.async_connect(endpoint,
                          boost::bind(&HTTPClient::handle_connect, this,
                                      boost::asio::placeholders::error,
                                      ++endpoint_iterator));

    _timer.expires_from_now(boost::posix_time::seconds(HTTP_TIMEOUT));
    _timer.async_wait(boost::bind(&HTTPClient::handle_timeout, this,
                                  boost::asio::placeholders::error));
}

void HTTPClient::handle_connect(const boost::system::error_code& err,
                                tcp::resolver::iterator endpoint_iterator)
{
    if (!err)
    {
        _connecting = false;
        _retries = 0;
        _delay = _initialDelay;
        // The connection was successful. Send the request.
        submit_next_request();
    }
    else if (endpoint_iterator != tcp::resolver::iterator())
    {
        // The connection failed. Try the next endpoint in the list.
        _socket.close();
        next_endpoint(endpoint_iterator);
    }
    else
    {
        _connecting = false;
        request_failed(err);
    }
}

void HTTPClient::handle_write_request(const boost::system::error_code& err) try
{
    _writing = false;
    m_send_buffer.clear();
    if (err)
    {
        request_failed(err);
        return;
    }

    // TODO: have some maximum pending limit.
    // TODO: also, don't pipeline requests until the first one comes
    //       back, and we know pipelining is allowed.
    submit_next_request();

    start_reading();
}
catch (std::exception& e)
{
    request_failed(err);
}


void HTTPClient::result(const std::string& res)
{
    _timer.cancel();
    conn_assert(_active_requests.size());
    request_struct r = _active_requests.front();
    _active_requests.pop_front();
    r.handler(res);
    submit_next_request();
}

// select random number generator
boost::mt19937 rng;

float normalvariate(float mean, float sigma)
{
    static bool seeded = false;

    if (!seeded)
    {
        // seed generator with #seconds since 1970
        rng.seed(static_cast<unsigned> (std::time(0)));
        seeded = true;
    }

    // select desired probability distribution
    boost::normal_distribution<float> norm_dist(mean, sigma);

    // bind random number generator to distribution, forming a function
    boost::variate_generator<boost::mt19937&, boost::normal_distribution<float> >  normal_sampler(rng, norm_dist);

    // sample from the distribution
    return normal_sampler();
}


void HTTPClient::abort_request()
{
    logger << "HTTPClient Request aborted." << std::endl;
    _socket.close();
    _timer.cancel();

    m_recv_pos = 0;
    _pending_requests.splice(_pending_requests.begin(),
                             _active_requests);

    if (_pending_requests.empty())
    {
        return;
    }

    _retries += 1;
    if (_maxRetries > 0 && _retries > _maxRetries)
    {
        logger << "HTTPClient Abandoning after " << _retries << " retries.\n" << std::endl;
        return;
    }

    _delay = min<float>(_delay * _factor, _maxDelay);
    if (_jitter)
    {
        _delay = normalvariate(_delay, _delay * _jitter);
    }

    _waiting = true;

    _timer.expires_from_now(boost::posix_time::milliseconds((long)(_delay * 1000)));
    _timer.async_wait(boost::bind(&HTTPClient::handle_wait, this,
                                  boost::asio::placeholders::error));
}

void HTTPClient::handle_wait(const boost::system::error_code& err)
{
    if (err)
    {
        // the timer was reset. no big deal.
        return;
    }
    logger << "HTTPClient retrying." << std::endl;
    _waiting = false;
    submit_next_request();
}

void HTTPClient::abort_request(const std::exception& exc)
{
    if (_active_requests.size() == 0)
        return;
    request_struct r = _active_requests.front();
    _active_requests.pop_front();

    abort_request();

    r.handler(exc);
}

void HTTPClient::request_failed(const boost::system::error_code& err)
{
    logger << "HTTPClient Request failed: " << err.message() << ".  Retrying." << std::endl;
    abort_request();
}

void HTTPClient::request_failed(const std::exception& exc)
{
    _timer.cancel();

    logger << "HTTPClient Request failed: " << exc.what() << std::endl;
    if (_active_requests.size() == 0)
        return;
    request_struct r = _active_requests.front();
    _active_requests.pop_front();
    r.handler(exc);
    submit_next_request();
}

void HTTPClient::start_reading()
{
    if (_reading || _active_requests.empty())
        return;

    //logger << m_recv_pos << ":" << m_recv_pos + BUFFER_SIZE << " of " << m_recv_buffer.size() << std::endl;
    conn_assert(m_recv_pos <= BUFFER_SIZE);
    conn_assert((m_recv_buffer.size() - m_recv_pos) >= BUFFER_SIZE);
    _reading = true;
    _socket.async_read_some(boost::asio::buffer(&m_recv_buffer[m_recv_pos], BUFFER_SIZE),
                            boost::bind(&HTTPClient::on_receive, this, _1, _2));

    _timer.expires_from_now(boost::posix_time::seconds(HTTP_TIMEOUT));
    _timer.async_wait(boost::bind(&HTTPClient::handle_timeout, this,
                                  boost::asio::placeholders::error));
}


void HTTPClient::cut_receive_buffer(int size)
{
    conn_assert((int)m_recv_buffer.size() >= size);
    conn_assert((int)m_recv_pos >= size);
    memmove(&m_recv_buffer[0], &m_recv_buffer[0] + size, m_recv_pos - size);

    m_recv_pos -= size;
    conn_assert(m_recv_pos <= m_recv_buffer.size());

    if ((BUFFER_SIZE * 2) >= m_recv_pos) m_recv_buffer.resize(BUFFER_SIZE * 2);
}

void HTTPClient::on_receive(const boost::system::error_code& err,
                            std::size_t bytes_transferred) try
{
    _reading = false;
    if (err && err != boost::asio::error::eof)
    {
        request_failed(err);
        return;
    }

    m_recv_pos += bytes_transferred;
    if (m_recv_pos >= BUFFER_SIZE)
    {
        request_failed(err);
        return;
    }

    conn_assert(m_recv_pos <= m_recv_buffer.size());

    while ((m_recv_pos > 0) && (_active_requests.size()))
    {
        //logger << "parsing " << m_recv_pos << " of " << m_recv_buffer.size() << std::endl;

        buffer::const_interval recv_buffer = receive_buffer();
        m_parser.incoming(recv_buffer);

        //logger << "[" << std::string(recv_buffer.begin, recv_buffer.end - recv_buffer.begin) << "]" << std::endl;

        // this means the entire status line hasn't been received yet
        if (m_parser.status_code() == -1)
        {
            break;
        }

        if (!m_parser.header_finished())
        {
            break;
        }

        std::string trasnfer_encoding = m_parser.header<std::string>("transfer-encoding");

        if (trasnfer_encoding == "chunked")
        {
            // unfortunately we can't just pass this on to the caller, since we can't
            // tell how long it is
            abort_request(std::runtime_error("HTTPClient does not support Transfer-Encoding: chunked"));
            return;
        }

        if (m_parser.content_length() == -1)
        {
            std::string connection = m_parser.header<std::string>("connection");
            if (connection != "close" && m_parser.protocol() == "HTTP/1.1")
            {
                // we have to abort, or read for who-knows how long
                abort_request(std::runtime_error("no content-length in HTTP response"));
                return;
            }
        }

        if (!m_parser.finished())
        {
            break;
        }

        // TODO: handle redirects?
#if 1
        if ((m_parser.status_code() != 200) &&
            (m_parser.status_code() != 206))
        {
            logger << "failing request with status " << m_parser.status_code() << std::endl;
            std::string error_msg = boost::lexical_cast<std::string>(m_parser.status_code())
                + " " + m_parser.message();
            request_failed(std::runtime_error(error_msg));

            // TODO: clean this junk
            buffer::const_interval http_body = m_parser.get_body();
            m_parser.reset();
            cut_receive_buffer(http_body.end - recv_buffer.begin);
            continue;
        }
#endif

        buffer::const_interval http_body = m_parser.get_body();

        // bleh, copy.
        std::string body(http_body.begin, http_body.end - http_body.begin);
        result(body);

        m_parser.reset();

        cut_receive_buffer(http_body.end - recv_buffer.begin);
    }

    conn_assert(m_recv_pos <= BUFFER_SIZE);

    if (err == boost::asio::error::eof)
    {
        if (!_active_requests.empty())
        {
            logger << "HTTPClient closed with pending requests!" << std::endl;
        }
        abort_request();
        return;
    }

    start_reading();
}
catch (std::exception& err)
{
    request_failed(err);
}
