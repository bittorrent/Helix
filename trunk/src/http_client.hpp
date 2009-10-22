#ifndef __HTTP_CLIENT_HPP__
#define __HTTP_CLIENT_HPP__

#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <libtorrent/buffer.hpp>
#include "callback_handler.hpp"
#include "boost_utils.hpp"
#include "http_parser.hpp"
#include "header.hpp"

using boost::asio::ip::tcp;

#define BUFFER_SIZE (65535)
#define HTTP_TIMEOUT 30

class HTTPClient
{
public:

    typedef CallbackHandler<const std::string&> callback_t;

    struct request_struct {
        std::string path;
        std::vector<header> headers;
        std::string body;
        callback_t handler;
    };

    HTTPClient(boost::asio::io_service& io_service) :
        _resolver(io_service),
        _socket(io_service),
        _timer(io_service)
    {
        init();
    }

    HTTPClient(boost::asio::io_service& io_service,
               const std::string& host, unsigned short port) :
        _resolver(io_service),
        _socket(io_service),
        _timer(io_service)
    {
        init();
        set_endpoint(host, port);
    }

    void init();

    void set_endpoint(const std::string& host, unsigned short port)
    {
        // this is for two-stage init only!
        assert(_host.empty() && _port == 0);
        _host = host;
        _port = port;
    }

    void get(const std::string& path,
             std::vector<header>& headers,
             callback_t handler);
    void post(const std::string& path, const std::string& body,
              callback_t handler);

private:

    request_struct& add_request();

    void submit_next_request();

    void start_connection();
    void handle_resolve(const boost::system::error_code& err,
                        tcp::resolver::iterator endpoint_iterator);
    void next_endpoint(tcp::resolver::iterator endpoint_iterator);
    void handle_connect(const boost::system::error_code& err,
                        tcp::resolver::iterator endpoint_iterator);
    void handle_write_request(const boost::system::error_code& err);

    void handle_timeout(const boost::system::error_code& err);
    void handle_wait(const boost::system::error_code& err);

    void abort_request();
    void abort_request(const std::exception& exc);
    void result(const std::string& r);
    void request_failed(const boost::system::error_code& err);
    void request_failed(const std::exception& exc);

    void start_reading();
    void on_receive(const boost::system::error_code& err,
                    std::size_t bytes_transferred);

    buffer::const_interval receive_buffer() const
    {
        return buffer::const_interval(&m_recv_buffer[0],
                                      &m_recv_buffer[0] + m_recv_pos);
    }
    void cut_receive_buffer(int size);

    std::string _host;
    unsigned short _port;
    bool _connecting;
    bool _writing;
    bool _reading;
    bool _waiting;

    int _maxRetries;
    int _retries;
    float _maxDelay;
    float _initialDelay;
    float _factor;
    float _jitter;
    float _delay;

    tcp::resolver _resolver;
    tcp::socket _socket;
    Timer _timer;

    size_t m_recv_pos;
    std::vector<char> m_recv_buffer;
    std::string m_send_buffer;

    std::list<request_struct> _pending_requests;
    std::list<request_struct> _active_requests;

    http_parser m_parser;
};

#endif //__HTTP_CLIENT_HPP__
