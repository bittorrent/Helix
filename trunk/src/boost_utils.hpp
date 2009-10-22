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

#ifndef __BOOST_UTILS_HPP__
#define __BOOST_UTILS_HPP__

#include <boost/thread/condition.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <stdarg.h>

template <typename T>
struct locked_variable
{
    T operator=(const T& n)
    {
        boost::mutex::scoped_lock lock(_mutex);
        _x = n;
        // is the temp variable needed?
        T t = _x;
        return t;
    }
    operator T()
    {
        boost::mutex::scoped_lock lock(_mutex);
        // is the temp variable needed?
        T t = _x;
        return t;
    }

    boost::mutex _mutex;
    T _x;
};


// for fresh breath
template <typename T>
class Scope
{
public:
    typedef boost::function<void (T)> destructor_t;

    Scope(T& t, destructor_t destructor) : _t(t), _destructor(destructor) {}
    ~Scope() { _destructor(_t); }
    T& _t;
    destructor_t _destructor;
};

// generic contains(). some containers obviously have better implementations
template <typename T, typename V>
bool contains(T container, V val)
{
    for (typename T::iterator iter = container.begin(); iter != container.end(); iter++)
    {
        if (*iter == val)
            return true;
    }
    return false;
}

#ifdef WIN32
#define vsnprintf _vsnprintf
#endif

std::string _format_arg_list(const char *fmt, va_list args);
std::string string_format(const char *fmt, ...);

class CancellableTask
{
public:
    typedef boost::function<void (const boost::system::error_code& err)> handler_t;

    CancellableTask(handler_t nhandler)
    {
        _handler = nhandler;
    }

    void operator()(const boost::system::error_code& err)
    {
        if (_handler)
        {
            _handler(err);
            _handler.clear();
        }
    }

    void cancel()
    {
        _handler.clear();
    }

    bool finished()
    {
        return !(bool)_handler;
    }

private:
    handler_t _handler;
};

typedef boost::shared_ptr<CancellableTask> cancellable_task_ptr;

class Timer : public boost::asio::deadline_timer
{
public:

    explicit Timer(boost::asio::io_service& io_service) :
       boost::asio::deadline_timer(io_service)
    {
    }

    ~Timer()
    {
        cancel();
    }

    /// Cancel any asynchronous operations that are waiting on the timer.
    /**
    * This function forces the completion of any pending asynchronous wait
    * operations against the timer. The handler for each cancelled operation will
    * be invoked with the boost::asio::error::operation_aborted error code.
    *
    * Cancelling the timer does not change the expiry time.
    *
    * @return The number of asynchronous operations that were cancelled.
    *
    * @throws boost::system::system_error Thrown on failure.
    */
    std::size_t cancel()
    {
        boost::system::error_code ec;
        _cancel_task();
        std::size_t s = boost::asio::deadline_timer::cancel(ec);
        boost::asio::detail::throw_error(ec);
        return s;
    }

    /// Cancel any asynchronous operations that are waiting on the timer.
    /**
    * This function forces the completion of any pending asynchronous wait
    * operations against the timer. The handler for each cancelled operation will
    * be invoked with the boost::asio::error::operation_aborted error code.
    *
    * Cancelling the timer does not change the expiry time.
    *
    * @param ec Set to indicate what error occurred, if any.
    *
    * @return The number of asynchronous operations that were cancelled.
    */
    std::size_t cancel(boost::system::error_code& ec)
    {
        _cancel_task();
        return boost::asio::deadline_timer::cancel(ec);
    }

    /// Set the timer's expiry time as an absolute time.
    /**
    * This function sets the expiry time. Any pending asynchronous wait
    * operations will be cancelled. The handler for each cancelled operation will
    * be invoked with the boost::asio::error::operation_aborted error code.
    *
    * @param expiry_time The expiry time to be used for the timer.
    *
    * @return The number of asynchronous operations that were cancelled.
    *
    * @throws boost::system::system_error Thrown on failure.
    */
    std::size_t expires_at(const boost::asio::deadline_timer::time_type& expiry_time)
    {
        boost::system::error_code ec;
        _cancel_task();
        std::size_t s = boost::asio::deadline_timer::expires_at(expiry_time, ec);
        boost::asio::detail::throw_error(ec);
        return s;
    }

    /// Set the timer's expiry time as an absolute time.
    /**
    * This function sets the expiry time. Any pending asynchronous wait
    * operations will be cancelled. The handler for each cancelled operation will
    * be invoked with the boost::asio::error::operation_aborted error code.
    *
    * @param expiry_time The expiry time to be used for the timer.
    *
    * @param ec Set to indicate what error occurred, if any.
    *
    * @return The number of asynchronous operations that were cancelled.
    */
    std::size_t expires_at(const boost::asio::deadline_timer::time_type& expiry_time,
        boost::system::error_code& ec)
    {
        _cancel_task();
        return boost::asio::deadline_timer::expires_at(expiry_time, ec);
    }

    /// Set the timer's expiry time relative to now.
    /**
    * This function sets the expiry time. Any pending asynchronous wait
    * operations will be cancelled. The handler for each cancelled operation will
    * be invoked with the boost::asio::error::operation_aborted error code.
    *
    * @param expiry_time The expiry time to be used for the timer.
    *
    * @return The number of asynchronous operations that were cancelled.
    *
    * @throws boost::system::system_error Thrown on failure.
    */
    std::size_t expires_from_now(const boost::asio::deadline_timer::duration_type& expiry_time)
    {
        boost::system::error_code ec;
        _cancel_task();
        std::size_t s = boost::asio::deadline_timer::expires_from_now(expiry_time, ec);
        boost::asio::detail::throw_error(ec);
        return s;
    }

    /// Set the timer's expiry time relative to now.
    /**
    * This function sets the expiry time. Any pending asynchronous wait
    * operations will be cancelled. The handler for each cancelled operation will
    * be invoked with the boost::asio::error::operation_aborted error code.
    *
    * @param expiry_time The expiry time to be used for the timer.
    *
    * @param ec Set to indicate what error occurred, if any.
    *
    * @return The number of asynchronous operations that were cancelled.
    */
    std::size_t expires_from_now(const boost::asio::deadline_timer::duration_type& expiry_time,
        boost::system::error_code& ec)
    {
        _cancel_task();
        return boost::asio::deadline_timer::expires_from_now(expiry_time, ec);
    }

    /// Start an asynchronous wait on the timer.
    /**
    * This function may be used to initiate an asynchronous wait against the
    * timer. It always returns immediately.
    *
    * For each call to async_wait(), the supplied handler will be called exactly
    * once. The handler will be called when:
    *
    * @li The timer has expired.
    *
    * @li The timer was cancelled, in which case the handler is passed the error
    * code boost::asio::error::operation_aborted.
    *
    * @param handler The handler to be called when the timer expires. Copies
    * will be made of the handler as required. The function signature of the
    * handler must be:
    * @code void handler(
    *   const boost::system::error_code& error // Result of operation.
    * ); @endcode
    * Regardless of whether the asynchronous operation completes immediately or
    * not, the handler will not be invoked from within this function. Invocation
    * of the handler will be performed in a manner equivalent to using
    * boost::asio::io_service::post().
    */
    template <typename WaitHandler>
    void async_wait(WaitHandler handler)
    {
        assert(!current_task);
        current_task.reset(new CancellableTask(handler));
        boost::asio::deadline_timer::async_wait(boost::bind(&CancellableTask::operator(), 
                                                            current_task,
                                                            boost::asio::placeholders::error));
    }

    void _cancel_task()
    {
        if (current_task)
        {
            current_task->cancel();
            current_task.reset();
        }
    }

    cancellable_task_ptr current_task;
};

class LoopingCall
{
public:
    typedef boost::function<void (void)> void_f_t;
    
    LoopingCall(boost::asio::io_service& io_service) :
        _io_service(io_service), _timer(io_service)
    {}

    void start(boost::posix_time::time_duration interval, void_f_t handler)
    {
        set_handler(handler);
        set_interval(interval);
        reset_timer();
    }

    void post_one()
    {
        _io_service.post(_handler);
    }

    void on_timer()
    {
        if (_handler)
            _handler();
        reset_timer();
    }

    void reset_timer()
    {
        _timer.expires_from_now(_interval);
        _timer.async_wait(boost::bind(&LoopingCall::on_timer, this));
    }

    void set_interval(boost::posix_time::time_duration interval)
    {
        if (interval <= boost::posix_time::seconds(0)) {
            throw std::runtime_error("non-positive time interval");
        }
        _interval = interval;
    }

    void set_handler(void_f_t handler)
    {
        _handler = handler;
    }

    void stop()
    {
        _handler.clear();
        _timer.cancel();
    }

    void_f_t _handler;
    boost::asio::io_service& _io_service;
    Timer _timer;
    boost::posix_time::time_duration _interval;
};


class ThreadPool : private boost::noncopyable
{
public:
    ThreadPool() : _io_service(), _keepalive(_io_service), _running(false) {}

    ~ThreadPool()
    {
        if (_running)
        {
            stop();
        }
    }

    void start(size_t num_threads)
    {
        _running = true;
        _keepalive.start(boost::posix_time::hours(1), NULL);
        for (size_t i = 0; i < num_threads; i++)
        {
            _threads.create_thread(boost::bind(&boost::asio::io_service::run,
                                               &_io_service));
        }
    }

    void stop()
    {
        _io_service.stop();
        _threads.join_all();
        _running = false;
    }

    /// Get the io_service associated with the object.
    boost::asio::io_service& io_service() { return _io_service; }

    template <typename CompletionHandler>
    void post(CompletionHandler handler)
    {
        _io_service.post(handler);
    }

private:

    boost::asio::io_service _io_service;
    LoopingCall _keepalive;
    boost::thread_group _threads;
    bool _running;
};


template <typename Connection>
class ConnectionPool : private boost::noncopyable
{
public:
    typedef boost::function<Connection (void)> make_connection_t;

    ConnectionPool(size_t max_connections,
                   make_connection_t make_connection) :
        _max_connections(max_connections), _current_connections(0),
        _make_connection(make_connection)
    {}

    ~ConnectionPool() {};

    Connection get_connection()
    {
        boost::mutex::scoped_lock lk(_mutex);
        while (_connections.empty())
        {
            if (_current_connections < _max_connections)
            {
                Connection nc = _make_connection();
                if (!nc)
                {
                    // connection failed. let the handler deal
                    // with it.
                    return nc;
                }
                _current_connections += 1;
                _connections.push_back(nc);
                break;
            }
            _have_connections.wait(lk);
        }
        Connection c = _connections.front();
        _connections.pop_front();
        return c;
    }

    void put_connection(Connection c)
    {
        boost::mutex::scoped_lock lk(_mutex);
        _connections.push_back(c);
        _have_connections.notify_one();
    }

    void lost_connection()
    {
        _current_connections--;
    }

private:

    size_t _max_connections;
    size_t _current_connections;
    boost::mutex _mutex;
    boost::condition _have_connections;
    std::list<Connection> _connections;
    make_connection_t _make_connection;
};

#endif //__BOOST_UTILS_HPP__
