#ifndef __CPU_MONITOR_HPP__
#define __CPU_MONITOR_HPP__

#include <vector>
#include "boost_utils.hpp"

/*
  Call cpu_usage_struct::periodic() at the resolution you care about.
  Check currret_cpu_percent for a weighted average.
 */


struct cpu_usage_struct {

    cpu_usage_struct();
    void periodic();

    void _add_sample(double s);

    /*
      Window function:
      2**(n-i-1) / 2**n - 1
    */

    double _weight(size_t p)
    {
        return ( 1.0 * // otherwise this is an int operation
                (1<<(WINDOW_SIZE - (p) - 1)) /
                ((1<<WINDOW_SIZE) - 1) );
    }
    
    size_t _index(size_t i)
    {
        return i % WINDOW_SIZE;
    }

    locked_variable<double> current_cpu_percent;

#ifndef WIN32

    struct cpu_info_struct {
        double user;
        double nice;
        double sys;
        double idle;
    };

    std::vector<cpu_info_struct> cpu_data;

#endif

    enum {
        WINDOW_SIZE = 8
    };
    size_t window_pos;
    double weight_constant;
    double sliding_window[WINDOW_SIZE];
};

typedef boost::shared_ptr<boost::thread> thread_ptr;

class CpuMonitorThread : private boost::noncopyable
{
public:
    CpuMonitorThread() :
        _periodic(_io_service)
    {
        _thread.reset(new boost::thread(boost::bind(&CpuMonitorThread::run,
                                        this)));
    }

    void run()
    {
        _periodic.start(boost::posix_time::seconds(1),
                        boost::bind(&cpu_usage_struct::periodic, &cpus));
        _io_service.run();
    }

    double get_cpu_percent()
    {
        return cpus.current_cpu_percent;
    }

    ~CpuMonitorThread()
    {
        _io_service.stop();
        _thread->join();
    }

    boost::asio::io_service _io_service;
    thread_ptr _thread;

    LoopingCall _periodic;
    cpu_usage_struct cpus;
};

#endif //__CPU_MONITOR_HPP__
