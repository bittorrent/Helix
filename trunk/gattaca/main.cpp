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
#include <string>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/program_options.hpp>
#include "gattaca_handler.hpp"
#include "server.hpp"
#include "utils.hpp"

#if !defined(_WIN32)

#include <boost/thread.hpp>
#include <pthread.h>
#include <signal.h>

#else

boost::function0<void> console_ctrl_function;

BOOL WINAPI console_ctrl_handler(DWORD ctrl_type)
{
    switch (ctrl_type)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            console_ctrl_function();
            return TRUE;
        default:
            return FALSE;
    }
}

#endif

bool verbose_logging = false;

int main(int argc, char* argv[])
{
    //try
    {
        std::string port;
        std::string helix_url;
        bool daemon;
        std::string username;
        std::string groupname;
        std::string pidfilename;
        std::string logfilename;

#ifndef _GLIBCXX_DEBUG
        // Check command line arguments.
        namespace po = boost::program_options;
        po::options_description desc("supported options");
        desc.add_options()
            ("help,h", "display this help message")
            ("port,p", po::value<std::string>(&port),
             "set listening port")
            ("helix_url,s", po::value<std::string>(&helix_url),
             "URL of the helix server (temporary option)")
            ("daemon,d", po::value<bool>(&daemon)->default_value(false),
             "Runs as a daemon")
            ("username", po::value<std::string>(&username)->default_value(""),
             "When running as a daemon, run as this user")
            ("groupname",
             po::value<std::string>(&groupname)->default_value(""),
             "When running as a daemon, run as this group")
            ("pidfile",
             po::value<std::string>(&pidfilename)->default_value(""),
             "When running as a daemon, write the process id to this file on startup")
            ("logfile",
             po::value<std::string>(&logfilename)->default_value(""),
             "When running as a daemon, write logs to this file")
            ;

        po::positional_options_description p;
        p.add("port", 1);
        p.add("helix_url", 1);

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).
                  options(desc).positional(p).run(), vm);
        po::notify(vm);

        if (vm.count("help")
            || vm.count("port") + vm.count("helix_url") != 2)
        {
            std::cerr << "Usage: gattaca <port> <helix_url>\n";
            std::cerr << "  For IPv4, try:\n";
            std::cerr << "    gattaca 6969 http://localhost:8000/\n";
            std::cerr << desc << "\n";
            return 1;
        }
#else //!_GLIBCXX_DEBUG
        helix_url = "http://localhost:8000/";
        port = "6969";
        daemon = false;
#endif //_GLIBCXX_DEBUG


#if !defined(WIN32)
        if (daemon)
        {
            daemonize(username, groupname, pidfilename, logfilename, "gattaca");
        }
#ifdef NDEBUG
        // Block all signals for background thread.
        sigset_t new_mask;
        sigfillset(&new_mask);
        sigset_t old_mask;
        pthread_sigmask(SIG_BLOCK, &new_mask, &old_mask);
#endif
#endif

        std::vector<std::string> addresses;
        addresses.push_back("0.0.0.0");
        addresses.push_back("::");
        http::server::server s(addresses, port);
        http::server::gattaca_handler rh(s.io_service(), port);
        s.set_request_handler(&rh);
        rh.set_helix_url(s.io_service(), helix_url);

#if defined(WIN32)

        GTOD_Initialize();

        // Set console control handler to allow server to be stopped.
        console_ctrl_function = boost::bind(&http::server::server::stop, &s);
        SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

        // Run the server until stopped.
        s.run();
#else
#ifndef NDEBUG
        // to make it possible to break into gdb when running in debug mode
        s.run();
        return 0;
#else
        // Run server in background thread.
        boost::thread t(boost::bind(&http::server::server::run, &s));

        // Restore previous signals.
        pthread_sigmask(SIG_SETMASK, &old_mask, 0);

        // Wait for signal indicating time to shut down.
        sigset_t wait_mask;
        sigemptyset(&wait_mask);
        sigaddset(&wait_mask, SIGINT);
        sigaddset(&wait_mask, SIGQUIT);
        sigaddset(&wait_mask, SIGTERM);
        pthread_sigmask(SIG_BLOCK, &wait_mask, 0);
        int sig = 0;
        int r;

        do {
            r = sigwait(&wait_mask, &sig);
        } while (r == EINTR);

        // Stop the server.
        s.stop();
        t.join();
#endif
#endif
    }
    /*
    catch (std::exception& e)
    {
        std::cerr << "exception: " << e.what() << "\n";
    }
    */

    return 0;
}
