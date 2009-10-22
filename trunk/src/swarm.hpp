//
// swarm.hpp
// ~~~~~~~~~
//
// by Steven Hazel
//

#ifndef HTTP_SWARM_HPP
#define HTTP_SWARM_HPP

#include <string>
#include <boost/asio/ip/address.hpp>
#include "xplat_hash_map.hpp"
#include "templates.h"
#include <boost/noncopyable.hpp>
#include "boost_utils.hpp"
#include "stats.hpp"
#include "server.hpp"
#include "libtorrent/peer_id.hpp"
#include <boost/asio/ip/tcp.hpp>

namespace http {
namespace server {

USING_NAMESPACE_EXT

#ifdef FAST_TIMEOUT
#define INTERVAL 30
#define INTERVAL_RANDOM 5
#define MIN_INTERVAL 15
#else
#define INTERVAL 30 * 60
#define INTERVAL_RANDOM 5 * 60
#define MIN_INTERVAL 15 * 60
#endif

// interval for DNA "snap stats"
#define SNAP_DELTA (5 * 60)

void ip_to_bytes(uint32 ip, unsigned char* b);

struct peer_endpoint_struct
{
    boost::asio::ip::address_v4::bytes_type ip;
    uint16 port;
};

struct peer6_endpoint_struct
{
    boost::asio::ip::address_v6::bytes_type ip;
    uint16 port;
};

// peer status bits
// v4 address is routable
#define IS_ROUTABLE 1
#define IS_COMPLETE 2
#define IS_DOWNLOADING 4
// v6 address is routable
#define IS_ROUTABLE6 8

#define HAS_V4 16
#define HAS_V6 32

struct peer_struct
{
    int ep_pos;
    int ep6_pos;
    int last_check_in;
    unsigned char status;

    peer_struct(): ep_pos(-1), ep6_pos(-1), last_check_in(0), status(0) {}
    enum category_t { seeding, active, paused, num_categories };
    int category() const
    {
        if (status & IS_COMPLETE) return seeding;
        if (status & IS_DOWNLOADING) return active;
        return paused;
    }

    void update_status(stats_struct const& stats);
};

using libtorrent::peer_id;

struct hash_fun
{
	size_t operator()(peer_id const& pid) const
	{
		unsigned long h = 0;
		for (unsigned char const* i = &pid[0]; i < &pid[0] + 20; ++i)
			h = 5 * h + *i;
		return size_t(h);
	}
};

/// Our view of a swarm we're tracking.
class Swarm : private boost::noncopyable
{
public:
    // TODO: this could be replaced with a
    // boost.multi_index for better performance
    typedef hash_map<peer_id, peer_struct, hash_fun> peer_map;

    Swarm(const std::string& info_hash, boost::asio::io_service& ios);
    Swarm(char const* flat_file, int& size, boost::asio::io_service& ios, int &num_peers);

    std::string info_hash;

    void handle_announce(const std::string &peer_id,
        boost::asio::ip::address_v4 ip, uint16 port,
        boost::asio::ip::address_v6 ipv6, uint16 port6,
        int numwant, stats_struct& stats,
        std::string& peers, std::string& peers6, bool client_debug);
    void add_peer(peer_id const& pid, bool ipv4, bool ipv6,
        stats_struct& stats);
    void update_peer(peer_map::iterator i,
        boost::asio::ip::address_v4 ip, uint16 port,
        boost::asio::ip::address_v6 ipv6, uint16 port6,
        stats_struct& stats, bool client_debug);
    void remove_peer(peer_map::iterator& peer_iter);
    void get_peers(std::string& peers, int count, int category, bool ipv6);
    void timeout_peers();
    void print_peers() const;
    float get_handout_ratio(int num_category, int denom_category, bool ipv6) const;
    size_t get_num_peers() const; // incompletes only
    size_t get_num_downloaders() const;
    size_t get_num_paused() const;
    size_t get_num_seeds() const;
    size_t get_num_completes() const;
    size_t get_w_bad() const;
    size_t get_cumulative_w_bad() const;
    static std::string class_stats();

    size_t get_load_metric()
    {
        // This should return some sort of load metric
        // comparable to other swarms. First guess is number
        // of peers (including non-natted). Another could be req/s.
        // Probably this should adjust for seeds having a different
        // checkin interval.
        return peers.size();
    }

    void save_state(std::vector<char>& flat_file) const;

    void set_rank(size_t nrank) { rank = nrank; }
    size_t get_rank() { return rank; }

    void set_cpuload(double ncpuload) { cpuload = ncpuload; }
    double get_cpuload() { return cpuload; }

    std::string get_flags();
    typedef hash_map< std::string, std::vector<std::string> > query_param_map;
    bool set_flags(const query_param_map &);

#ifndef NDEBUG
    void check_invariant() const;
#endif

    bool is_disabled() { return (flags & Swarm::DISABLED) != 0; }
    bool disable()
    {
        bool ret = (flags & Swarm::DISABLED) != 0;

        flags |= Swarm::DISABLED;
        return ret;
    }
    bool enable()
    {
        bool ret = (flags & Swarm::DISABLED) != 0;

        flags &= ~Swarm::DISABLED;
        return ret;
    }

    bool is_terminated() { return (flags & Swarm::TERMINATE) != 0; }

    static void setup_controls(ControlAPI &);

private:

    void start_natcheck(libtorrent::peer_id const& pid, boost::asio::ip::tcp::endpoint const& ep);
    
    typedef std::vector<peer_endpoint_struct> peer_endpoint_t;
    typedef std::vector<peer6_endpoint_struct> peer6_endpoint_t;
    // by looking at the source code of <ext/hash_map>
    // it seems like iterators are only invalidated
    // when the elements they refer to are ereased.
    // i.e. it is safe to store these iterators as
    // long as their elements still exist
    typedef std::vector<peer_map::iterator> peer_endpoint_to_peer_t;
    peer_endpoint_t peer_endpoints[peer_struct::num_categories];
    peer6_endpoint_t peer6_endpoints[peer_struct::num_categories];
    peer_endpoint_to_peer_t peer_endpoint_to_peer[peer_struct::num_categories];
    peer_endpoint_to_peer_t peer6_endpoint_to_peer[peer_struct::num_categories];

    void remove_endpoint(int category, int index)
    {
        assert(category >= 0 && category < peer_struct::num_categories);
        peer_endpoint_t& endpoints = peer_endpoints[category];
        assert(index >= 0 && index < endpoints.size());

        int last = endpoints.size() - 1;
        endpoints[index] = endpoints[last];
        peer_endpoint_to_peer[category][last]->second.ep_pos = index;
        peer_endpoint_to_peer[category][index] = peer_endpoint_to_peer[category][last];
        endpoints.pop_back();
        peer_endpoint_to_peer[category].pop_back();
    }

    int add_endpoint(peer_map::iterator iter, peer_endpoint_struct const& endp)
    {
        int category = iter->second.category();
        assert(category >= 0 && category < peer_struct::num_categories);
        int ret = peer_endpoints[category].size();
        peer_endpoints[category].push_back(endp);
        peer_endpoint_to_peer[category].push_back(iter);
        return ret;
    }

    void remove_endpoint6(int category, int index)
    {
        assert(category >= 0 && category < peer_struct::num_categories);
        peer6_endpoint_t& endpoints = peer6_endpoints[category];
        assert(index >= 0 && index < endpoints.size());

        int last = endpoints.size() - 1;
        endpoints[index] = endpoints[last];
        peer6_endpoint_to_peer[category][last]->second.ep6_pos = index;
        peer6_endpoint_to_peer[category][index] = peer6_endpoint_to_peer[category][last];
        endpoints.pop_back();
        peer6_endpoint_to_peer[category].pop_back();
    }

    int add_endpoint6(peer_map::iterator iter, peer6_endpoint_struct const& endp)
    {
        int category = iter->second.category();
        assert(category >= 0 && category < peer_struct::num_categories);
        int ret = peer6_endpoints[category].size();
        peer6_endpoints[category].push_back(endp);
        peer6_endpoint_to_peer[category].push_back(iter);
        return ret;
    }

    peer_map peers;

    // the number of peers in each category
    int peer4_counts[peer_struct::num_categories];
    float peer4_list_cursor[peer_struct::num_categories];
    int next_handout4[peer_struct::num_categories];

    int peer6_counts[peer_struct::num_categories];
    float peer6_list_cursor[peer_struct::num_categories];
    int next_handout6[peer_struct::num_categories];

    int peer_counts[peer_struct::num_categories];

    mutable StatsLogger stats_logger;
    LoopingCall timeout;

    boost::asio::io_service& io_service_;

    size_t rank;
    double cpuload;

    void add_peer_endpoint(peer_map::iterator peer,
        boost::asio::ip::address ip, uint16 port);
    void nat_ok(peer_id const& pid, boost::asio::ip::tcp::endpoint ep, int r);
    void nat_bad(peer_id const& pid, boost::asio::ip::tcp::endpoint ep, const std::exception &e);

    static int64_t peers_delivered;
    static int64_t num_peers_created;
    enum {
        DISABLED = 0x1,
        DNA_ONLY = 0x2,
        TERMINATE = 0x4,
    };
    int flags;
    struct flagnames_t {
        int flag;
        char *name;
    };
    static flagnames_t flagnames[];
    std::string flag_name(int flag);
    int flag_from_name(const std::string &);

    enum peer_selection_algorithm_t {
        RANDOM,
        PRUNED,
    };
    static peer_selection_algorithm_t peer_selection_algorithm;

    struct peer_selection_function_t {
        enum peer_selection_algorithm_t algo;
        char *name;
    };
    static peer_selection_function_t peer_selection_funcs[];

    int get_peers_sequential(std::string& peers, float count, int category, bool ipv6);
    int get_peers_random(std::string& peers, int count, int category, bool ipv6);
    int get_peers_at(std::string& peers, int start_peer, int count, int category, bool ipv6);
//    void get_peers_pruned(std::string& peers, int count, int category);
    static void set_peer_selection_algorithm(
            enum peer_selection_algorithm_t *,
            std::vector<std::string> &);
    static std::string get_peer_selection_algorithm(
            enum peer_selection_algorithm_t *);

    bool peer_permitted(const std::string &peer_id);

public:
    // should helix enforce the blacklist from the db?
    static bool enforce_db_blacklist;
    // should helix enforce correct auth tokens in announces
    static bool enforce_auth_token;
    // should helix enforce the DNA-only mode?
    static bool enforce_dna_only;
    // what is the default for new swarms?
    static bool default_dna_only;
    // what is the string to test the peer_id against?
    static std::string dna_only_prefix;
    // this is an upper limit on the number of times a
    // single peer can be handed out during one announce
    // interval (typically around 30 minutes).
    static int max_peer_handout_per_interval;
};

} // namespace server
} // namespace http

#endif // HTTP_SWARM_HPP

