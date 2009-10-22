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

#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
#include <time.h>
#include "xplat_hash_map.hpp"
#include <boost/lexical_cast.hpp>
#include "reply.hpp"
#include "request.hpp"
#include "swarm.hpp"
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/escape_string.hpp>
#include <libtorrent/hasher.hpp>
#include "authorizer.hpp"
#include "utils.hpp"
#include "boost_utils.hpp"
#include "server.hpp"
#include "header.hpp"
#include "helix_handler.hpp"
#include "natcheck.hpp"
#include "control.hpp"

// the number of minutes between checkpoints
int checkpoint_timer = 5;

namespace http {
namespace server {

USING_NAMESPACE_EXT

time_t start_time;
double saved_qps;
uint64 saved_num_swarms;
uint64 saved_num_peers;
std::string saved_cpu_percent;

uint64_t total_requests, prev_total_requests;
hash_map<std::string, Swarm*> swarms;

// TODO: Should be config based.
SaltyAuthorizer saltyauth;

helix_handler::helix_handler(boost::asio::io_service& io_service,
                             const std::string& port) :
    request_handler(io_service, port),
    _io_service(io_service),
    _periodic(io_service),
    pm_("Helix"),
    _last_checkpoint(time(0)),
    control_only_from_localhost_(true),
    enforce_db_blacklist_(true),
    enforce_auth_token_(false),
    secret_auth_token_("sekret")
{
    using boost::asio::ip::tcp;
    tcp::resolver resolver(io_service);
    tcp::resolver::query query(_hostname, port);

    tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
    tcp::resolver::iterator end;

    boost::system::error_code error = boost::asio::error::host_not_found;
    tcp::endpoint ep;
    for (;endpoint_iterator != end; endpoint_iterator++)
    {
        ep = *endpoint_iterator;
        if (ep.address().is_v4())
            break;
    }
    if (endpoint_iterator == end)
      throw boost::system::system_error(error);

    uint32 ip = ep.address().to_v4().to_ulong();
    ip = ntohl(ip);
    uint16 nport = ep.port();
    nport = ntohs(nport);

    // I know there are more efficient ip+port packing mechanisms, but
    // I wanted to use enough bytes to allow for more unique ids in
    // the future.
    snprintf(_myid, sizeof(_myid), "%.8X%.4X", ip, nport);
    logger << "Trackerid: [" << _myid << "]" << std::endl;

    std::ifstream checkpoint("tracker_checkpoint", std::ios::binary);
    if (checkpoint.good())
    {
        checkpoint.seekg(0, std::ios::end);
        int file_size = checkpoint.tellg();
        checkpoint.seekg(0);
        // have some sane limit on file size. 50 million peers
        if (file_size > 0 && file_size < 50000000 * 35)
        {
            std::vector<char> swarm_data(file_size);
            checkpoint.read(&swarm_data[0], file_size);
            file_size = checkpoint.gcount();
            char const* flat_file = &swarm_data[0];
            int size = file_size;
            int num_peers, tot_peers = 0;
            while (size > 24)
            {
                // size is passed by reference to the constructor
                // and it will be decreased by the number of bytes that are read
                int read = size;
                Swarm* s = new Swarm(flat_file, size, _io_service, num_peers);
                swarms.insert(std::make_pair(s->info_hash, s));
                read -= size;
                // read is the number of bytes that was consumed by the Swarm constructor
                flat_file += read;
                tot_peers += num_peers;
            }
            logger << "loaded tracker state from checkpoint. "
                << swarms.size() << " swarms, " << tot_peers << " peers total"
                << std::endl;
        }
    }
    controls.add_variable(
            "control_only_from_localhost",
            boost::bind(&ControlAPI::set_bool, &control_only_from_localhost_, _1),
            boost::bind(&ControlAPI::get_bool, &control_only_from_localhost_));
    controls.add_variable("enforce_auth_token",
            boost::bind(&ControlAPI::set_bool, &enforce_auth_token_, _1),
            boost::bind(&ControlAPI::get_bool, &enforce_auth_token_));
    controls.add_variable("enforce_db_blacklist",
            boost::bind(&ControlAPI::set_bool, &enforce_db_blacklist_, _1),
            boost::bind(&ControlAPI::get_bool, &enforce_db_blacklist_));
    controls.add_variable("secret_auth_token",
            boost::bind(&ControlAPI::set_string, &secret_auth_token_, _1),
            boost::bind(&ControlAPI::get_string, &secret_auth_token_));

#ifndef DISABLE_DNADB
    dba.setup_controls(controls);
#endif
    Swarm::setup_controls(controls);


    _periodic.start(boost::posix_time::seconds(1),
                    boost::bind(&helix_handler::periodic, this));

    start_time = time(NULL);
    total_requests = 0;
}

void helix_handler::start()
{
#ifndef DISABLE_DNADB
    dba.start();
#endif
}

bool helix_handler::set_torrents_enabled(bool enabled, std::vector< std::string > &args)
{
    int num_handled = 0;

    for(std::vector< std::string >::iterator it = args.begin();
            it != args.end();
            it++
       )
    {
        std::stringstream is(*it);
        sha1_hash h;

        is >> h;
        std::string info_hash(h.begin(), h.end());

        if (info_hash.length() != 20) {
            logger << ">" << *it << "< is not 20 bytes\n";
            continue;
        }

        if (swarms.count(info_hash) == 0)
        {
            logger << "No swarm found matching " << h << std::endl;
            continue;
        }

        num_handled++;
        if (enabled)
        {
            logger << "unblacklisting " << h << std::endl;
            swarms[info_hash]->enable();
        }
        else
        {
            logger << "blacklisting " << h << std::endl;
            swarms[info_hash]->disable();
        }
    }

    return num_handled > 0;
}

std::string helix_handler::get_torrent_blacklist()
{
    std::stringstream os;
    bool printed = false;

    for(hash_map<std::string, Swarm*>::iterator it = swarms.begin();
            it != swarms.end();
            it++)
    {
        if (it->second->is_disabled())
        {
            if (printed)
                os << " ";
            os << sha1_hash(it->first);
            printed = true;
        }
    }
    return os.str();
}

std::string helix_handler::handler_name(void)
{
    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    std::string ret = string_format("%d@%s", (int)getpid(), hostname);
    return ret;
}

typedef std::pair<size_t, Swarm*> load_t;

// this is intentionally a greater than comparator, to
// make the list sorted in decending order
bool compare_load(const load_t& lhs, const load_t& rhs)
{
    return lhs.first > rhs.first;
}

void helix_handler::periodic()
{
    std::vector<load_t> load_list;
    size_t load_total = 0;

    hash_map<std::string, Swarm*>::const_iterator it;
    for (it = swarms.begin(); it != swarms.end(); it++)
    {
        Swarm* s = it->second;
        size_t l = s->get_load_metric();
        load_list.push_back(load_t(l, s));
        load_total += l;
    }

    std::sort(load_list.begin(), load_list.end(), &compare_load);

    for (size_t i = 0; i < load_list.size(); i++)
    {
        load_t& p = load_list[i];
        Swarm* s = p.second;
        double load_frac = (double)p.first / (double)load_total;
        load_frac *= _cpu_monitor.get_cpu_percent();
        s->set_rank(i);
        s->set_cpuload(load_frac);
    }

    time_t time_now = time(0);
    // checkpoint regularly
    if (time_now > _last_checkpoint + checkpoint_timer * 60)
    {
        StopWatch sw;
        _last_checkpoint = time_now;

        std::vector<char> swarm_data;
        // guess that the average swarm size is 100 peers
        swarm_data.reserve(swarms.size() * (20 + 4 + 100 * (20 + 4 + 1 + 4 + 2)));

        for (hash_map<std::string, Swarm*>::iterator i = swarms.begin();
                i != swarms.end(); ++i)
        {
            i->second->save_state(swarm_data);
        }

        double pre_write = sw.get_msec();

        std::ofstream checkpoint("tracker_checkpoint", std::ios::binary | std::ios::trunc);
        if (!swarm_data.empty())
            checkpoint.write(&swarm_data[0], swarm_data.size());

        logger << "saved checkpoint " << swarms.size() << " swarms, "
            << swarm_data.size() << " bytes in " << sw.get_msec() << "ms ( " << pre_write << "ms )" << std::endl;
    }
    do_helix_statistics();
}

void helix_handler::reply_text(Result &res, const std::string &content)
{
    reply rep;

    rep.status = reply::ok;
    rep.content = content;
    rep.headers.resize(3);
    rep.headers[0].name = "Content-Length";
    rep.headers[0].value = boost::lexical_cast<std::string>((unsigned int)content.length());
    rep.headers[1].name = "Content-Type";
    rep.headers[1].value = "text/plain";
    rep.headers[2].name = "X-Server";
    rep.headers[2].value = _hostname;

    res.finished(rep);
}

void helix_handler::do_helix_statistics(void)
{
    time_t runtime = time(NULL) - start_time;

    if (runtime > 15)
    {
        int64_t num_peers = 0;
        for(hash_map<std::string, Swarm*>::iterator iter = swarms.begin();
                iter != swarms.end();
                iter++)
        {
            num_peers += (iter->second->get_num_peers() +
                    iter->second->get_num_seeds());
        }
        saved_qps = (total_requests - prev_total_requests) / (double)runtime;
        saved_num_swarms = swarms.size();
        saved_num_peers = num_peers;
        saved_cpu_percent = string_format("%.2f", _cpu_monitor.get_cpu_percent());
        start_time = time(NULL);
        logger << "*** " << std::setw(4) << saved_qps << "qps; " << std::setw(6) << saved_num_swarms << " swarms; " << std::setw(7) << saved_num_peers << " peers; " << std::setw(5) << saved_cpu_percent << " %cpu; " << start_time << "s; ***" << std::endl;
        prev_total_requests = total_requests;
        pm_.log_status_and_clear();
    }
}

void helix_handler::reply_bencoded(Result& res, libtorrent::entry::dictionary_type &dict, Swarm* s, reply::status_type status)
{
    std::stringstream st;
    bencode(std::ostream_iterator<char>(st), dict);
    const std::string& str = st.str();

    reply rep;

    rep.status = status;
    rep.content.append(str);
    rep.headers.resize(4);
    rep.headers[0].name = "Content-Length";
    rep.headers[0].value = boost::lexical_cast<std::string>((unsigned int)str.length());
    rep.headers[1].name = "Content-Type";
    rep.headers[1].value = "text/plain";
    rep.headers[2].name = "X-Server";
    rep.headers[2].value = _hostname;
    rep.headers[3].name = "X-CPU";
    rep.headers[3].value = string_format("%.2f", _cpu_monitor.get_cpu_percent());
    if (s)
    {
        rep.headers.resize(rep.headers.size() + 2);
        rep.headers[4].name = "X-Swarm-CPU";
        rep.headers[4].value = string_format("%.2f", s->get_cpuload());
        rep.headers[5].name = "X-Swarm-Rank";
        rep.headers[5].value = boost::lexical_cast<std::string>((unsigned int)s->get_rank());
    }

    res.finished(rep);

    total_requests++;
}


std::string helix_handler::class_stats()
{
    std::stringstream st;

    st << "Helix statistics for period ending: " << start_time << std::endl;
    st << "Helix QPS: " << saved_qps << std::endl;
    st << "Helix number of swarms: " << saved_num_swarms << std::endl;
    st << "Helix number of peers: " << saved_num_peers << std::endl;
    st << "Helix CPU percentage: " << saved_cpu_percent << std::endl;
    st << "Helix requests: " << total_requests << std::endl;

    return st.str();
}

bool helix_handler::endpoint_ok_for_control_set(const boost::asio::ip::tcp::endpoint &endpoint)
{
    if (control_only_from_localhost_)
        return endpoint.address() == boost::asio::ip::address_v4::loopback();
    else
        return true;
}

std::string helix_handler::get_swarm_flags(const std::string &infohash)
{
    sha1_hash h(boost::lexical_cast<sha1_hash>(infohash));
    std::string info_hash(h.begin(), h.end());
    std::stringstream os;

    if (swarms.count(info_hash) == 0)
    {
        throw std::runtime_error(infohash + std::string(": no such swarm\n"));
    }
    os << "Flags: " << swarms[info_hash]->get_flags() << std::endl;
    return os.str();
}

bool helix_handler::set_swarm_flags(const std::string &infohash,
        const hash_map< std::string, std::vector<std::string> > & query_params)
{
    std::stringstream is(infohash);
    sha1_hash h;
    is >> h;
    std::string info_hash(h.begin(), h.end());

    if (swarms.count(info_hash) == 0)
    {
        throw std::runtime_error(infohash + std::string(": no such swarm\n"));
    }
    return swarms[info_hash]->set_flags(query_params);
}

void helix_handler::handle_request(server& http_server,
                                   const boost::asio::ip::tcp::endpoint& endpoint, const request& req, Result& res)
{
    try
    {
        scoped_perf_sampler ps(pm_);

        if (verbose_logging)
            logger << endpoint.address().to_string() << ":" << endpoint.port() << " : " << req.uri << std::endl;

        // Decode url to path.
        std::string request_path;
        hash_map< std::string, std::vector<std::string> > query_params;
        if (!url_parse(req.uri, request_path, query_params))
        {
            res.finished(reply::stock_reply(reply::bad_request));
            return;
        }

        //logger << request_path << std::endl;

        if (request_path == "/announce")
        {
            using namespace libtorrent;
            entry::dictionary_type dict;

            if (!query_params.count("info_hash"))
            {
                dict["failure reason"] = "No info_hash given.";
                reply_bencoded(res, dict);
                return;
            }

            std::string info_hash = query_params["info_hash"][0];
            std::string tid = info_hash;
            if (query_params.count("tid") > 0) tid = query_params["tid"][0];

            if (info_hash.size() != 20)
            {
                dict["failure reason"] = "invalid info_hash given.";
                reply_bencoded(res, dict);
                return;
            }

            if (enforce_auth_token_)
            {
                bool passed = false;
                if (query_params.count("auth"))
                {
                    std::string auth_token = query_params["auth"][0];
                    std::string s = info_hash + tid + secret_auth_token_;
                    if (boost::lexical_cast<std::string>(hasher(&s[0], s.length()).final()) == auth_token)
                        passed = true;
                }

                if (!passed)
                {
                    dict["failure reason"] = "Requested download is not authorized for use with this tracker.";
                    reply_bencoded(res, dict);
                    return;
                }
            }

#ifndef DISABLE_DNADB 
            if (enforce_db_blacklist_ && !dba.is_allowed(tid)) 
            { 
                dict["failure reason"] = "Requested download is not authorized for use with this tracker.";
               // dict["terminate swarm"] = 1; 
                reply_bencoded(res, dict); 
                return; 
            } 
#endif

            Swarm* swarm;
            if (swarms.count(info_hash) == 0)
            {
                swarm = new Swarm(info_hash, _io_service);
                swarms.insert(std::pair<std::string, Swarm*>(info_hash, swarm));
                //logger << "new swarm! " << swarms.size() << " known" << std::endl;
            }
            else
            {
                swarm = swarms[info_hash];
            }
            dict["info_hash"] = swarm->info_hash;

            if (swarm->is_disabled())
            {
                dict["failure reason"] = "Swarm is blacklisted.";
                reply_bencoded(res, dict, swarm);
                return;
            }

            if (!query_params.count("peer_id"))
            {
                dict["failure reason"] = "No peer_id given.";
                reply_bencoded(res, dict, swarm);
                return;
            }

            std::string peer_id = query_params["peer_id"][0];
            if (peer_id.size() != 20)
            {
                dict["failure reason"] = "invalid peer_id given.";
                reply_bencoded(res, dict);
                return;
            }

            int numwant = 50;
            if (query_params.count("numwant"))
            {
                numwant =
                    safe_lexical_cast<uint16_t>(query_params["numwant"][0]);
            }

            stats_struct stats;

            std::string event = "";
            if (query_params.count("event"))
            {
                event = query_params["event"][0];
            }

            if (event == "started")
            {
                stats.event = STARTED;
            }
            else if (event == "completed")
            {
                stats.event = COMPLETED;
            }
            else if (event == "stopped")
            {
                stats.event = STOPPED;
            }
            else if (event == "paused")
            {
                stats.event = PAUSED;
            }
            else if (event == "")
            {
                stats.event = EMPTY;
            }
            else
            {
                dict["failure reason"] = "invalid event given.";
                reply_bencoded(res, dict);
                return;
            }

            stats.t_checkin = 0;
            if (query_params.count("t_checkin"))
            {
                stats.t_checkin = safe_lexical_cast<time_t>(query_params["t_checkin"][0]);
            }

            stats.left = 0;
            if (query_params.count("left"))
            {
                try
                {
                    stats.left = safe_lexical_cast<uint64>(query_params["left"][0]);
                }
                catch (const boost::bad_lexical_cast& e)
                {
                    // can't decode left. 
                    // this happens with clients that give negative numbers,
                    if (query_params["left"][0][0] == '-')
                    {
                        // so just pretend there's only one tiny bit left.
                        stats.left = 16384;
                    }
                    else
                    {
                        // otherwise fail the request.
                        throw;
                    }
                }
            }

            stats.w_downloaded = 0;
            if (query_params.count("w_downloaded"))
            {
                stats.w_downloaded = safe_lexical_cast<uint64>(query_params["w_downloaded"][0]);
            }

            stats.p_downloaded = 0;
            if (query_params.count("p_downloaded"))
            {
                stats.p_downloaded = safe_lexical_cast<uint64>(query_params["p_downloaded"][0]);
            }

            stats.p_uploaded = 0;
            if (query_params.count("p_uploaded"))
            {
                stats.p_uploaded = safe_lexical_cast<uint64>(query_params["p_uploaded"][0]);
            }

            stats.c_bytes = 0;
            if (query_params.count("c_bytes"))
            {
                stats.c_bytes = safe_lexical_cast<uint64>(query_params["c_bytes"][0]);
            }

            stats.w_bad = 0;
            if (query_params.count("w_bad"))
            {
                stats.w_bad = safe_lexical_cast<uint64>(query_params["w_bad"][0]);
            }

            stats.w_fail = 0;
            if (query_params.count("w_fail"))
            {
                stats.w_fail = safe_lexical_cast<size_t>(query_params["w_fail"][0]);
            }

            uint16_t port = 0;
            if (query_params.count("port"))
            {
                port = safe_lexical_cast<uint16_t>(query_params["port"][0]);
            }

            if (query_params.count("report_w_bad"))
            {
                dict["w_bad"] = swarm->get_w_bad();
                dict["c_w_bad"] = swarm->get_cumulative_w_bad();
                reply_bencoded(res, dict);
                return;
            }

            boost::asio::ip::address external_ip = endpoint.address();
            boost::asio::ip::address_v4 in_v4;
            boost::asio::ip::address_v6 in_v6;
            int port_v4 =-1;
            int port_v6 =-1;

            std::string peers;
            std::string peers6;

            bool ip_in_header = false;
            for (size_t i = 0; i < req.headers.size(); i++)
            {
                const struct header& h = req.headers[i];
                std::string name_lower = h.name;
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
                if (name_lower == "x-forwarded-for" ||
                    name_lower == "clientipaddr")
                {
                    //logger << "got " << h.name << " " << h.value << std::endl;
                    boost::system::error_code ec;
                    boost::asio::ip::address a = boost::asio::ip::address::from_string(h.value, ec);
                    if (ec) continue;

                    if (a.is_v4()) { in_v4 = a.to_v4(); port_v4 = port; }
                    else { in_v6 = a.to_v6(); port_v6 = port; }
                    external_ip = a;
                    ip_in_header = true;
                    break;
                }
            }

            if (!ip_in_header)
            {
                if (endpoint.address().is_v4()) { in_v4 = endpoint.address().to_v4(); port_v4 = port; }
                else { in_v6 = endpoint.address().to_v6(); port_v6 = port; }
            }

            if (port_v6 == -1 && query_params.count("ipv6"))
            {
                boost::system::error_code ec;
                std::string const& adr = query_params["ipv6"][0];
                boost::asio::ip::address_v6 a = boost::asio::ip::address_v6::from_string(
                   adr, ec);
                if (!ec)
                {
                    in_v6 = a;
                    port_v6 = port;
                }
                else
                {
                    // TODO: interpret adr as an endpoint
                    dict["warning"] = "IPv6 endpoints are not supported in &ipv6= argument";
                }
            }

            if (port_v4 == -1 && query_params.count("ipv4"))
            {
                boost::system::error_code ec;
                std::string const& adr = query_params["ipv4"][0];
                boost::asio::ip::address_v4 a = boost::asio::ip::address_v4::from_string(
                   adr, ec);
                if (!ec)
                {
                    in_v4 = a;
                    port_v4 = port;
                }
                else
                {
                    // TODO: interpret adr as an endpoint
                    dict["warning"] = "IPv4 endpoints are not supported in &ipv4= argument";
                }
            }

            bool client_debug = false;
            if (query_params.count("s"))
            {
                if (query_params["s"][0] == "0e29c350")
                    client_debug = true;
            }

            if (swarm->is_terminated())
            {
                dict["terminate swarm"] = 1;
                reply_bencoded(res, dict);
                return;
            }

            if (port_v6 == -1) port_v6 = port;
            if (port_v4 == -1) port_v4 = port;
            try
            {
                swarm->handle_announce(peer_id,
                    in_v4, port_v4,
                    in_v6, port_v6,
                    numwant, stats,
                    peers, peers6,
                    client_debug);
            }
            catch (std::runtime_error& e)
            {
                dict["failure reason"] = e.what();
                reply_bencoded(res, dict);
                return;
            }

            //swarm->print_peers();
            dict["peers"] = peers;
            if (!peers6.empty()) dict["peers6"] = peers6;
            dict["interval"] = INTERVAL + int((rand() / float(RAND_MAX) - .5f) * INTERVAL_RANDOM);
            dict["min interval"] = MIN_INTERVAL;
            if (external_ip.is_v4())
                dict["external ip"] = std::string((char*)&external_ip.to_v4().to_bytes()[0], 4);
            else
                dict["external ip"] = std::string((char*)&external_ip.to_v6().to_bytes()[0], 16);
            dict["snapdelta"] = SNAP_DELTA;

            reply_bencoded(res, dict);
        }
        else if (request_path == "/scrape")
        {
            using namespace libtorrent;
            entry::dictionary_type dict;
            entry::dictionary_type files;

            std::vector<std::string>& info_hashes = query_params["info_hash"];
            for (size_t i = 0; i < info_hashes.size(); i++)
            {
                entry::dictionary_type stats;
                std::string info_hash = info_hashes[i];

                if (swarms.count(info_hash))
                {
                    Swarm* swarm = swarms[info_hash];

                    stats["complete"] = swarm->get_num_seeds();
                    stats["incomplete"] = swarm->get_num_peers();
                    stats["downloaded"] = swarm->get_num_completes();
                    stats["downloaders"] = swarm->get_num_downloaders();

                    files[info_hash] = stats;
                }
            }

            dict["files"] = files;

            reply_bencoded(res, dict);
        }
        else if (request_path == "/statistics")
        {
            std::stringstream stats;

            stats << "Current time: " << time(NULL) << std::endl;
            stats << NatCheck::class_stats();
            stats << helix_handler::class_stats();
            stats << pm_.instance_stats();
            stats << Swarm::class_stats();

            reply_text(res, stats.str());
        }
        else if (request_path == "/control")
        {
            if (req.method != "GET") {
                res.finished(reply::stock_reply(reply::not_implemented));
                return;
            }
            reply_text(res, controls.dump());
            return;
        }
        else if (request_path == "/control/set")
        {
            if (req.method != "PUT")
            {
                res.finished(reply::stock_reply(reply::not_implemented));
                return;
            }
            if (!endpoint_ok_for_control_set(endpoint))
            {
                res.finished(reply::stock_reply(reply::forbidden));
                return;
            }
            try {
                if (controls.process(endpoint, req, query_params) == 0)
                {
                    res.finished(reply::stock_reply(reply::bad_request));
                    return;
                }
            }
            catch (const std::runtime_error &e)
            {
                res.finished(reply::stock_reply(reply::bad_request, e.what()));
                return;
            }
            res.finished(reply::stock_reply(reply::ok));
            return;
        }
        else if (request_path == "/control/blacklist")
        {
            if (req.method == "GET")
            {
                if (!query_params.empty())
                {
                    res.finished(reply::stock_reply(reply::bad_request));
                    return;
                }

                std::stringstream os;
                os << "torrent_blacklist: " << get_torrent_blacklist() << std::endl;
                reply_text(res, os.str());
                return;
            }
            else if (req.method == "PUT")
            {
                int failures = 0;

                if (!endpoint_ok_for_control_set(endpoint))
                {
                    res.finished(reply::stock_reply(reply::forbidden));
                    return;
                }
                if (query_params.count("add") > 0)
                {
                    if (!set_torrents_enabled(false, query_params["add"]))
                    {
                        failures++;
                    }
                }
                if (query_params.count("del") > 0)
                {
                    if (!set_torrents_enabled(true, query_params["del"]))
                    {
                        failures++;
                    }
                }
                if (failures == 0)
                {
                    res.finished(reply::stock_reply(reply::ok));
                }
                else
                {
                    res.finished(reply::stock_reply(reply::bad_request));
                }
                return;
            }
        }
        else if (starts_with(request_path, "/control/flags/"))
        {
            const int prefixlen = 15; // strlen("/control/flags/")
            std::string infohash(request_path, prefixlen, request_path.size() - prefixlen);

            if (req.method == "GET")
            {
                try {
                    reply_text(res, get_swarm_flags(infohash));
                    return;
                }
                catch (std::runtime_error &e)
                {
                    res.finished(reply::stock_reply(reply::not_found, e.what()));
                    return;
                }
            }
            else if (req.method == "PUT")
            {
                if (!endpoint_ok_for_control_set(endpoint))
                {
                    res.finished(reply::stock_reply(reply::forbidden));
                    return;
                }
                try {
                    set_swarm_flags(infohash, query_params);
                    res.finished(reply::stock_reply(reply::ok));
                }
                catch (std::runtime_error &e)
                {
                    res.finished(reply::stock_reply(reply::not_found, e.what()));
                }
                catch (boost::bad_lexical_cast &e)
                {
                    res.finished(reply::stock_reply(reply::bad_request, e.what()));
                }
                return;
            }
        }
        else
        {
            logger << "unhandled request was " << req.method << " " << request_path << "<\n";
            res.finished(reply::stock_reply(reply::not_found));
            return;
        }
    }
    catch (std::exception& e)
    {
        logger << "Exception handling request: " << e.what() << " (request was " << req.uri << ")" << std::endl;
        entry::dictionary_type dict;
        dict["failure reason"] = "error handling request";
        reply_bencoded(res, dict);
    }
    catch (...)
    {
        logger << "Critical exception handling request!" << " (request was " << req.uri << ")" << std::endl;
        entry::dictionary_type dict;
        dict["failure reason"] = "critical error handling request";
        reply_bencoded(res, dict);
    }
}

} // namespace server
} // namespace http

