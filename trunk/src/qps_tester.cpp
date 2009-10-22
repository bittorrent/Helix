//
// qps_tester.cpp
// ~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2007 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

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

#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <time.h>
#include <boost/asio.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind.hpp>

using boost::asio::ip::tcp;

typedef boost::scoped_ptr<tcp::socket> socket_ptr;

int successful_connections = 0;
int failed_connections = 0;
tcp::endpoint ep;
char send_buf[400];
int send_buf_size = 0;

struct connection
{
	void start(boost::asio::io_service& ios)
	{
		socket.reset(new tcp::socket(ios));
		socket->async_connect(ep, boost::bind(&connection::on_connect, this, _1));
	}

	void on_connect(boost::system::error_code const& e)
	{
		if (e)
		{
			++failed_connections;
			start(socket->io_service());
			return;
		}

		boost::asio::async_write(*socket, boost::asio::buffer(send_buf, send_buf_size), boost::bind(&connection::on_write, this, _1));
	}

	void on_write(boost::system::error_code const& e)
	{
		if (e)
		{
			++failed_connections;
			start(socket->io_service());
			return;
		}

		boost::asio::async_read(*socket, boost::asio::buffer(receive_buf, 1000), boost::bind(&connection::on_receive, this, _1, _2));
	}

	void on_receive(boost::system::error_code const& e, size_t bytes_transferred)
	{
		if (e != boost::asio::error::eof)
		{
			std::cerr << e.message() << std::endl;
		}
		++successful_connections;
		start(socket->io_service());
	}

	char receive_buf[1000];
	socket_ptr socket;
};

connection connections[100];

int main(int argc, char* argv[])
{
    using namespace boost::posix_time;

    try
    {
        if (argc != 4)
        {
            std::cout << "Usage: qps_tester <server> <port> <path>\n";
            std::cout << "Example:\n";
            std::cout << "  qps_tester localhost 8080 /announce?info_hash=foo\n";
            return 1;
        }

        boost::asio::io_service io_service;

        time_t start_time = time(NULL);

        // Get a list of endpoints corresponding to the server name.
        tcp::resolver resolver(io_service);
        tcp::resolver::query query(argv[1], argv[2]);
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        tcp::resolver::iterator end;
        ep = *endpoint_iterator;  // fuck iterating

        // Form the request. We specify the "Connection: close" header
        // so that the server will close the socket after transmitting
        // the response. This will allow us to treat all data up until
        // the EOF as the content.
        std::ostringstream request_stream;
        request_stream << "GET " << argv[3] << " HTTP/1.0\r\n";
        request_stream << "Host: " << argv[1] << "\r\n";
        request_stream << "Accept: */*\r\n";
        request_stream << "Connection: close\r\n\r\n";
        std::string const& str = request_stream.str();
        std::copy(str.begin(), str.end(), send_buf);
        send_buf_size = str.size();

		for (int i = 0; i < 100; ++i)
		{
			connections[i].start(io_service);
		}

		boost::asio::deadline_timer timer(io_service);
		timer.expires_from_now(boost::posix_time::seconds(20));
		timer.async_wait(boost::bind(&boost::asio::io_service::stop, &io_service));
		io_service.run();

		for (int i = 0; i < 100; ++i)
			connections[i].socket.reset();

		double qps = successful_connections / 20.0;
        std::cout << "done! " << qps << "qps, " << failed_connections << " failed" << std::endl;
    }
    catch (std::exception& e)
    {
        std::cout << "Exception: " << e.what() << "\n";
    }

    return 0;
}
