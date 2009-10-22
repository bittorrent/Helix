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

// This handy functor holds two callbacks, success and failure, and can be called
// with either type of result - the template parameter (success) or an exception (failure).

#ifndef __CALLBACK_HANDLER_HPP__
#define __CALLBACK_HANDLER_HPP__

#include <boost/function.hpp>

template <typename Result>
static void swallow_success(Result r) {};
static void swallow_error(const std::exception& exc) {};

template <typename Result>
struct CallbackHandler {
    typedef boost::function<void (Result r)> success_callback_t;
    typedef boost::function<void (const std::exception& err)> failure_callback_t;

    CallbackHandler() {}

    CallbackHandler(success_callback_t nsuccess, failure_callback_t nfailure)
    {
        success = nsuccess;
        failure = nfailure;
    }

    bool empty() const { return !(success && failure); }
    void clear() { success.clear(); failure.clear(); }

    void operator () (Result r) { success(r); }
    void operator () (const std::exception& err) { failure(err); }

#if (defined __SUNPRO_CC) && (__SUNPRO_CC <= 0x530) && !(defined BOOST_NO_COMPILER_CONFIG)
    // Sun C++ 5.3 can't handle the safe_bool idiom, so don't use it
    operator bool () const { return !this->empty(); }
#else
  private:
    struct dummy {
      void nonnull() {};
    };

    typedef void (dummy::*safe_bool)();

  public:
    operator safe_bool () const
      { return (this->empty())? 0 : &dummy::nonnull; }

    bool operator!() const
      { return this->empty(); }
#endif

  private:
    struct clear_type {};

  public:
#ifndef BOOST_NO_SFINAE
    CallbackHandler& operator=(clear_type*)
    {
      this->clear();
      return *this;
    }
#else
    CallbackHandler& operator=(int zero)
    {
      BOOST_ASSERT(zero == 0);
      this->clear();
      return *this;
    }
#endif

  public:
    success_callback_t success;
    failure_callback_t failure;
};

#endif //__CALLBACK_HANDLER_HPP__
