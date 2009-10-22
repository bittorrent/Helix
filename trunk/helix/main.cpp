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
#include "stats.hpp"
#include "server.hpp"
#include "utils.hpp"
#include "helix_handler.hpp"
#include "control.hpp"

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

// nice global variable for the number of
// minutes between checkpoints
extern int checkpoint_timer;
bool verbose_logging = false;

int main(int argc, char* argv[])
{
    //try
    {
        std::string port;
        bool daemon;
        std::string username;
        std::string groupname;
        std::string pidfilename;
        std::string logfilename;
        std::string configfilename;

#ifndef _GLIBCXX_DEBUG
        // Check command line arguments.
        namespace po = boost::program_options;
        po::options_description desc("supported options");
        desc.add_options()
            ("help,h", "display this help message")
            ("port,p", po::value<std::string>(&port),
             "set listening port")
            ("daemon,d", po::value<bool>(&daemon)->default_value(false),
             "Runs as a daemon")
            ("verbose", po::value<bool>(&verbose_logging)->default_value(false),
             "Prints verbose logs to stdout")
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
             "When running as a daemon, log messages to this file")
            ("checkpoint-time",
             po::value<int>(&checkpoint_timer)->default_value(5),
             "The number of minutes between checkpointing the tracker state")
            ("configfile",
             po::value<std::string>(&configfilename)->default_value(""),
             "Load configuration values from this file")
            ;

        po::positional_options_description p;
        p.add("port", 1);

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).
                  options(desc).positional(p).run(), vm);
        po::notify(vm);

        if (vm.count("help")
            || vm.count("port") != 1)
        {
            std::cerr << "Usage: helix <port>\n";
            std::cerr << "  For IPv4, try:\n";
            std::cerr << "    " << argv[0] << " 6969\n";
            std::cerr << desc << "\n";
            return 1;
        }
#else //!_GLIBCXX_DEBUG
        port = "8000";
        daemon = false;
        checkpoint_timer = 1;
#endif //_GLIBCXX_DEBUG


#if !defined(WIN32)
        if (daemon)
        {
            if(!daemonize(username, groupname, pidfilename, logfilename,
                    http::server::helix_handler::handler_name()))
            {
                logger << "Failed to daemonize, but continuing anyways.\n";
            }
        }
#ifndef RUN_IN_FOREGROUND
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
        http::server::helix_handler rh(s.io_service(), port);
        s.set_request_handler(&rh);

        if (configfilename != "")
        {
            bool success = rh.controls.read_file(configfilename);
            if (success)
            {
                logger << "Config loaded from " << configfilename << std::endl;
            }
            else
            {
                logger << "Errors loading config from " << configfilename
                    << std::endl;
            }
        }
	rh.start();

#if defined(WIN32)

        GTOD_Initialize();

        // Set console control handler to allow server to be stopped.
        console_ctrl_function = boost::bind(&http::server::server::stop, &s);
        SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

        // Run the server until stopped.
        s.run();
#else
#ifdef RUN_IN_FOREGROUND
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
        sigaddset(&wait_mask, SIGHUP);
        sigaddset(&wait_mask, SIGINT);
        sigaddset(&wait_mask, SIGQUIT);
        sigaddset(&wait_mask, SIGTERM);
        pthread_sigmask(SIG_BLOCK, &wait_mask, 0);

        while (1) {
            int r, sig = 0;

            do {
                r = sigwait(&wait_mask, &sig);
            } while(r == EINTR);

            switch(sig) {
            case SIGHUP:
                reopen_logfile(http::server::helix_handler::handler_name());
                break;
            case SIGINT:
            case SIGQUIT:
            case SIGTERM:
                logger << "helix " <<
                    http::server::helix_handler::handler_name() <<
                    " exiting on signal " << sig << std::endl;
                s.stop();
                t.join();
                return 0;
            }
        }
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
