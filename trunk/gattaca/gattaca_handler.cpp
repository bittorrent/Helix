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

#include "gattaca_handler.hpp"
#include "http_client.hpp"
#include "parsed_url.hpp"
#include "server.hpp"

namespace http {
namespace server {

USING_NAMESPACE_EXT

typedef boost::shared_ptr<HTTPClient> httpclient_ptr;

hash_map< std::string, httpclient_ptr > _trackers;

void gattaca_handler::set_helix_url(boost::asio::io_service& io_service, const std::string& helix_url)
{
    assert(_trackers.count("all") == 0);
    parsed_url parsed;
    bool succeeded = parsed.set(helix_url.c_str());
    assert(succeeded); // ...

    _trackers["all"].reset(new HTTPClient(io_service, parsed.host(), parsed.port()));
    /*
    for (size_t i = 0; i < 10; i++)
    {
        _trackers[boost::lexical_cast<std::string>(i)].reset(new HTTPClient(io_service, parsed.host(), parsed.port()));
    }
    */
}

void gattaca_handler::success(const std::string& s, Result& res)
{
    try
    {
        std::cout << "Request success: " << s.length() << std::endl;
        reply rep;

        rep.status = reply::ok;
        rep.content.append(s);
        rep.headers.resize(2);
        rep.headers[0].name = "Content-Length";
        rep.headers[0].value = boost::lexical_cast<std::string>((unsigned int)s.length());
        rep.headers[1].name = "Content-Type";
        rep.headers[1].value = "text/plain";

        res.finished(rep);
    }
    catch (std::exception& e)
    {
        std::cout << "Exception finishing request: " << e.what() << std::endl;
        res.finished(reply::stock_reply(reply::internal_server_error));
    }
    catch (...)
    {
        std::cout << "Critical exception finishing request!" << std::endl;
        res.finished(reply::stock_reply(reply::internal_server_error));
    }
}

void gattaca_handler::failure(const std::exception& e, Result& res)
{
    try
    {
        std::cout << "Request failure: " << e.what() << "!" << std::endl;
        res.finished(reply::stock_reply(reply::internal_server_error));
    }
    catch (std::exception& e)
    {
        std::cout << "Exception failing request: " << e.what() << std::endl;
        res.finished(reply::stock_reply(reply::internal_server_error));
    }
    catch (...)
    {
        std::cout << "Critical exception failing request!" << std::endl;
        res.finished(reply::stock_reply(reply::internal_server_error));
    }
}

void s1(const std::string& s)
{
}
void f1(const std::exception& e)
{
}

httpclient_ptr select_tracker(const std::string& infohash)
{
    // use a very advanced mechanism for choosing a tracker
    if (infohash.size() == 1)
        return _trackers[infohash];
    return _trackers["all"];
}

void gattaca_handler::handle_request(server& http_server,
                                     const boost::asio::ip::tcp::endpoint& endpoint, const request& req, Result& res)
{
    try
    {
        std::cout << "REQUEST: " << req.uri << std::endl;

        // Decode url to path.
        std::string request_path;
        hash_map< std::string, std::vector<std::string> > query_params;
        if (!url_parse(req.uri, request_path, query_params))
        {
            res.finished(reply::stock_reply(reply::bad_request));
            return;
        }

        std::string peer_ip = endpoint.address().to_v4().to_string();

        for (size_t i = 0; i < req.headers.size(); i++)
        {
            const struct header& h = req.headers[i];
            if (h.name == "clientipaddr")
            {
                peer_ip = h.value;
                break;
            }
        }

        std::vector<header> headers;
        headers.push_back(header());
        headers.back().name = "clientipaddr";
        headers.back().value = peer_ip;

        if (request_path == "/announce")
        {
            if (!query_params.count("info_hash"))
            {
                entry::dictionary_type dict;
                dict["failure reason"] = "No info_hash given.";
                reply_bencoded(res, dict);
                return;
            }

            std::string info_hash = query_params["info_hash"][0];
            httpclient_ptr t = select_tracker(info_hash);
            HTTPClient::callback_t handler(boost::bind(&gattaca_handler::success, this,
                                                       _1, boost::ref(res)),
                                           boost::bind(&gattaca_handler::failure, this,
                                                       _1, boost::ref(res)));
            t->get(req.uri, headers, handler);

            /*
            for (size_t i = 0; i < 10; i++)
            {
                httpclient_ptr t2 = select_tracker(boost::lexical_cast<std::string>(i));
                HTTPClient::callback_t handler2(s1, f1);
                t2->get(req.uri, headers, handler2);
            }
            */

            return;
        }
        else if (request_path == "/scrape")
        {
            std::cout << "look a scrape" << std::endl;
            if (!query_params.count("info_hash"))
            {
                entry::dictionary_type dict;
                dict["failure reason"] = "No info_hash given.";
                reply_bencoded(res, dict);
                return;
            }

            std::vector<std::string>& info_hashes = query_params["info_hash"];
            assert(info_hashes.size());
            std::cout << "scraping " << info_hashes.size() << std::endl;
            for (size_t i = 0; i < info_hashes.size(); i++)
            {
                // BUG: OW OW OW. this needs to assemble a response!
                httpclient_ptr t = select_tracker(info_hashes[i]);
                HTTPClient::callback_t handler(boost::bind(&gattaca_handler::success, this,
                                                        _1, boost::ref(res)),
                                               boost::bind(&gattaca_handler::failure, this,
                                                        _1, boost::ref(res)));
                t->get(req.uri, headers, handler);
                std::cout << "done with scrape" << std::endl;
                break;
            }
            std::cout << "oh yes" << std::endl;
            return;
        }
        else
        {
            res.finished(reply::stock_reply(reply::not_found));
            return;
        }
    }
    catch (std::exception& e)
    {
        std::cout << "Exception handling request: " << e.what() << " (request was " << req.uri << ")" << std::endl;
        entry::dictionary_type dict;
        dict["failure reason"] = "error handling request";
        reply_bencoded(res, dict);
    }
    catch (...)
    {
        std::cout << "Critical exception handling request!" << " (request was " << req.uri << ")" << std::endl;
        entry::dictionary_type dict;
        dict["failure reason"] = "critical error handling request";
        reply_bencoded(res, dict);
    }
}


} // namespace server
} // namespace http
