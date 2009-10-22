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

#ifndef __GATTACA_HANDLER_HPP__
#define __GATTACA_HANDLER_HPP__

#include "request_handler.hpp"

namespace http {
namespace server {

class gattaca_handler: public request_handler
{
public:

    // why is this needed?
    explicit gattaca_handler(boost::asio::io_service& io_service, const std::string& port) :
        request_handler(io_service, port)
    {
    }

    void handle_request(server& http_server,
                        const boost::asio::ip::tcp::endpoint& endpoint, const request& req, Result& res);

    void set_helix_url(boost::asio::io_service& io_service, const std::string& helix_url);

private:

    void success(const std::string& s, Result& res);
    void failure(const std::exception& e, Result& res);
};

} // namespace server
} // namespace http

#endif // __GATTACA_HANDLER_HPP__
