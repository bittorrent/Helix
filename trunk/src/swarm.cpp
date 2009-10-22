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

#include "swarm.hpp"
#include <boost/asio/ip/address.hpp>
#include <limits.h>
#include <time.h>
#include <stdlib.h>
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include "xplat_hash_map.hpp"
#include <boost/lexical_cast.hpp>
#include "reply.hpp"
#include "request.hpp"
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/invariant_check.hpp>
#include <libtorrent/io.hpp>
#include "server.hpp"
#include "natcheck.hpp"
#include "utils.hpp"

#ifdef DISABLE_INVARIANT_CHECK
#undef INVARIANT_CHECK
#define INVARIANT_CHECK do {} while (false)
#endif 

namespace http {
namespace server {

USING_NAMESPACE_EXT

int64_t nc_pass = 0;
int64_t nc_fail = 0;

int64_t Swarm::peers_delivered;
int64_t Swarm::num_peers_created;

bool Swarm::enforce_dna_only = false;
bool Swarm::default_dna_only = false;
std::string Swarm::dna_only_prefix = "DNA";
int Swarm::max_peer_handout_per_interval = 50;

Swarm::flagnames_t Swarm::flagnames[] = {
        { DISABLED, "disabled" },
        { DNA_ONLY, "dna_only" },
        { TERMINATE, "terminate" },
};

void ip_to_bytes(uint32 ip, unsigned char* b)
{
    b[0] = ((unsigned char*)&ip)[0];
    b[1] = ((unsigned char*)&ip)[1];
    b[2] = ((unsigned char*)&ip)[2];
    b[3] = ((unsigned char*)&ip)[3];
}

namespace
{
    std::string read_20_bytes(char const* flat_file)
    {
        std::string ret;
        ret.resize(20);
        memcpy(&ret[0], flat_file, 20);
        return ret;
    }
}

void peer_struct::update_status(stats_struct const& stats)
{
    last_check_in = time(NULL);
    if (stats.left == 0)
    {
       status |= IS_COMPLETE;
       status &= ~IS_DOWNLOADING; // if the peer is seeding, it's not downloading
    }
    else
    {
        status &= ~IS_COMPLETE;
        if (stats.event == PAUSED)
        {
           status &= ~IS_DOWNLOADING;
        }
        else
        {
           status |= IS_DOWNLOADING;
        }
    }
}

Swarm::Swarm(const std::string& info_hash, boost::asio::io_service& ios)
    : info_hash(info_hash),
      timeout(ios),
      io_service_(ios),
      flags(0)
{
    for (int i = 0; i < peer_struct::num_categories; ++i)
    {
       peer_counts[i] = 0;
       peer4_counts[i] = 0;
       peer4_list_cursor[i] = 0.f;
       next_handout4[i] = 0;
       peer6_counts[i] = 0;
       peer6_list_cursor[i] = 0.f;
       next_handout6[i] = 0;
    }

    rank = UINT_MAX;
    cpuload = 0;
    timeout.start(boost::posix_time::seconds(INTERVAL/2),
                  boost::bind(&Swarm::timeout_peers, this));
    if (default_dna_only)
        flags |= DNA_ONLY;
    //logger << sizeof(peer_endpoint_struct) << std::endl;
}

/*

serialization format:

20 bytes   info-hash
4 bytes    number of peers

for each peer:

20 bytes   peer-id
4 bytes    last check-in
1 byte     status
4 bytes    IP (ignore unless routable)
2 bytes    port (ignore unless routable)

*/

// save state
void Swarm::save_state(std::vector<char>& file) const
{
#ifdef NO_SAVE_STATE
    return;
#endif

    if (peers.empty()) return;

    // for write_* functions
    namespace io = libtorrent::detail;

    int num_peers = 0;
    for (int i = 0; i < peer_struct::num_categories; ++i)
    {
        num_peers += peer_endpoints[i].size();
        assert(peer_endpoints[i].size() == peer_endpoint_to_peer[i].size());
    }
    // save at most 40 peers per swarm
    if (num_peers > 40) num_peers = 40;

    int initial_size = file.size();

    file.resize(initial_size + 20 + 4 + num_peers * (20 + 4 + 1 + 4 + 2));
    char* out = &file[0] + initial_size;

    std::copy(info_hash.begin(), info_hash.end(), out);
    out += 20;

    io::write_uint32(num_peers, out);

    for (int category = 0; category < peer_struct::num_categories; ++category)
    {
        if (num_peers <= 0) break;
        typedef std::vector<peer_endpoint_struct> endpoints_t;
        typedef std::vector<peer_map::iterator> endpoint_to_peer_t;

        int size = std::min(int(peer_endpoints[category].size()), num_peers);
        endpoint_to_peer_t::const_iterator j = peer_endpoint_to_peer[category].begin();
        endpoints_t::const_iterator i = peer_endpoints[category].begin();
        endpoints_t::const_iterator end = peer_endpoints[category].begin() + size;

        for (;i != end; ++i, ++j)
        {
            peer_struct& p = (*j)->second;
            peer_id const& pid = (*j)->first;
            // peer_id (20 bytes)
            std::copy(pid.begin(), pid.begin() + 20, out);
            out += 20;
            // last_check_in (4 bytes)
            io::write_int32(p.last_check_in, out);
            // status (1 byte)
            io::write_int8(p.status & ~(IS_ROUTABLE6 | HAS_V6), out);
            assert(p.status & IS_ROUTABLE);
            // ip + port (6 bytes)
            peer_endpoint_struct const& pe = *i;
            memcpy(out, &pe, 6);
            out += 6;
            --num_peers;
        }
    }
}

// restore state
Swarm::Swarm(char const* flat_file, int& size, boost::asio::io_service& ios, int &num_peers)
    : info_hash(read_20_bytes(flat_file)),
      timeout(ios),
      io_service_(ios),
      flags(0)
{
    for (int i = 0; i < peer_struct::num_categories; ++i)
    {
       peer_counts[i] = 0;
       peer4_counts[i] = 0;
       peer4_list_cursor[i] = 0.f;
       next_handout4[i] = 0;
       peer6_counts[i] = 0;
       peer6_list_cursor[i] = 0.f;
       next_handout6[i] = 0;
    }

    rank = UINT_MAX;
    cpuload = 0;
    timeout.start(boost::posix_time::seconds(INTERVAL/2),
        boost::bind(&Swarm::timeout_peers, this));

    if (default_dna_only)
        flags |= DNA_ONLY;

    size -= 20;
    flat_file += 20;

    // for write_* functions
    namespace io = libtorrent::detail;

    if (size < 4) return;

    num_peers = io::read_int32(flat_file);
    size -= 4;
    if (size <= 0) return;

    if (num_peers < 0) return;
    if (num_peers > 40) num_peers = 40;

    for (int i = 0; i < num_peers; ++i)
    {
        if (size < 20 + 4 + 1 + 4 + 2) return;
        peer_id pid;
        std::copy(flat_file, flat_file + 20, &pid[0]);
        flat_file += 20;
        size -= 20;

        peer_struct peer;
        peer.last_check_in = io::read_int32(flat_file);
        size -= 4;
        peer.status = io::read_uint8(flat_file);
        size -= 1;

        if (peer.status & IS_COMPLETE)
            stats_logger.update_peer_counts(0, 1);
        else
            stats_logger.update_peer_counts(1, 0);

        peer.status |= HAS_V4;
        if ((peer.status & IS_ROUTABLE) == 0) return;
        // since we don't save v6 addresses, we must clear this flag
        peer.status &= ~(IS_ROUTABLE6 | HAS_V6);

        peer_endpoint_struct pe;
        pe.ip[0] = io::read_uint8(flat_file);
        pe.ip[1] = io::read_uint8(flat_file);
        pe.ip[2] = io::read_uint8(flat_file);
        pe.ip[3] = io::read_uint8(flat_file);
        pe.port = io::read_uint16(flat_file);
        size -= 6;
        int category = peer.category();
        ++peer4_counts[category];
        ++peer_counts[category];
        peer_endpoints[category].push_back(pe);
        peer.ep_pos = peer_endpoints[category].size() - 1;
        peer_map::iterator iter = this->peers.insert(std::make_pair(pid, peer)).first;
        peer_endpoint_to_peer[category].push_back(iter);
    }

    INVARIANT_CHECK;
}

void Swarm::nat_ok(peer_id const& pid, tcp::endpoint ep, int r)
{
    nc_pass++;
    //logger << "Natcheck pass! " << r << " (" << nc_pass << "/" << nc_fail << "=" << ((double)nc_pass/(nc_pass+nc_fail)) << ")" << "\n";
    peer_map::iterator i = peers.find(pid);
    if (i == peers.end()) return;
    add_peer_endpoint(i, ep.address(), ep.port());
}


void Swarm::nat_bad(peer_id const& pid, tcp::endpoint ep, const std::exception &e)
{
    nc_fail++;
    //logger << "Natcheck fail! " << e.what() << " (" << nc_pass << "/" << nc_fail << "=" << ((double)nc_pass/(nc_pass+nc_fail)) << ")" << "\n";
}

bool Swarm::peer_permitted(const std::string &peer_id)
{
    if (!enforce_dna_only)
        return true;
    if ((flags & DNA_ONLY) == 0)
        return true;
    if (starts_with(peer_id, dna_only_prefix))
        return true;
    return false;
}

void Swarm::handle_announce(const std::string &peer_id,
    boost::asio::ip::address_v4 ip, uint16 port,
    boost::asio::ip::address_v6 ipv6, uint16 port6,
    int numwant,
    stats_struct& stats,
    std::string& peers,
    std::string& peers6,
    bool client_debug)
{
    libtorrent::peer_id const pid(peer_id);

    INVARIANT_CHECK;

    if (verbose_logging)
        logger << "ANNOUNCE: " << pid << " (" << ip << ":" << port
           << ", [" << ipv6 << "]:" << port6 << ")" << std::endl;

    if (!peer_permitted(peer_id))
    {
        if (verbose_logging)
            logger << "   permission denied" << std::endl;
        throw std::runtime_error("Permission denied.");
    }

    stats_logger.log_request(stats);

    // stopped event should not return peers
    if (stats.event == STOPPED)
        numwant = 0;

    int category = stats.left == 0 ? peer_struct::seeding
       : stats.event == PAUSED ? peer_struct::paused
       : peer_struct::active;

    //logger << "returning " << peers.size() << " bytes" << std::endl;

    if (port != 0 || port6 != 0)
    {
        peer_map::iterator i = this->peers.find(pid);
        if (stats.event != STOPPED)
        {
            if (i == this->peers.end())
            {
                //logger << "adding: " << peer_id << std::endl;
                add_peer(pid, ip != boost::asio::ip::address_v4::any(),
                   ipv6 != boost::asio::ip::address_v6::any(), stats);

                if (ip != boost::asio::ip::address_v4::any())
                    start_natcheck(pid, tcp::endpoint(ip, port));
                if (ipv6 != boost::asio::ip::address_v6::any())
                    start_natcheck(pid, tcp::endpoint(ipv6, port6));
            }
            else
            {
                //logger << "updating" << std::endl;
                update_peer(i, ip, port, ipv6, port6, stats, client_debug);
            }
        }
        else
        {
            //logger << "removing" << std::endl;
            if (i != this->peers.end()) remove_peer(i);
        }
    }
    else
    {
        if (verbose_logging)
            logger << "   port = 0 rejected" << std::endl;
    }

    if (ip != boost::asio::ip::address_v4::any())
        get_peers(peers, numwant, category, false);
    if (ipv6 != boost::asio::ip::address_v6::any())
        get_peers(peers6, numwant, category, true);

    if (verbose_logging)
       logger << "   returned " << (peers.size() / 6) << " IPv4 peers and "
          << (peers6.size() / 18) << " IPv6 peers" << std::endl;
    //logger << this->peers.size() << " total peers known" << std::endl;
}

void Swarm::start_natcheck(libtorrent::peer_id const& pid, tcp::endpoint const& ep)
{
    CallbackHandler<int> handler(
        boost::bind(&Swarm::nat_ok, this, pid, ep, _1),
        boost::bind(&Swarm::nat_bad, this, pid, ep, _1));
    StartNatCheck(this->io_service_, ep, (const byte*)&this->info_hash[0],
        (byte*)&pid[0], handler);
}

void Swarm::add_peer(peer_id const& pid, bool ipv4, bool ipv6,
    stats_struct& stats)
{
    INVARIANT_CHECK;

    peer_struct peer;

    num_peers_created++;

    if (stats.left == 0)
    {
        peer.status |= IS_COMPLETE;
        stats_logger.update_peer_counts(0, 1);
    }
    else
    {
        stats_logger.update_peer_counts(1, 0);
    }

    peer.update_status(stats);
    assert(peer.ep_pos == -1);
    assert(peer.ep6_pos == -1);

    int category = peer.category();
    ++peer_counts[category];
    if (ipv4)
    {
       ++peer4_counts[category];
       peer.status |= HAS_V4;
    }
    if (ipv6)
    {
       ++peer6_counts[category];
       peer.status |= HAS_V6;
    }

    this->peers[pid] = peer;

    if (verbose_logging)
        logger << "   ADDED PEER category: " << category
            << std::endl;
}


void Swarm::add_peer_endpoint(peer_map::iterator peer,
    boost::asio::ip::address ip, uint16 port)
{
    INVARIANT_CHECK;

    // if the peer is already added, just ignore it
    // multiple pending NAT checks could complete if the peer
    // stops and restarts quickly. just drop later successes.
    if (ip.is_v6() && (peer->second.status & IS_ROUTABLE6)) return;
    if (ip.is_v4() && (peer->second.status & IS_ROUTABLE)) return;

    int category = peer->second.category();
    if (ip.is_v6())
    {
        peer6_endpoint_struct peer_endpoint;
        peer_endpoint.ip = ip.to_v6().to_bytes(); 
        peer_endpoint.port = htons(port);
        this->peer6_endpoints[category].push_back(peer_endpoint);
        this->peer6_endpoint_to_peer[category].push_back(peer);
        assert(peer->second.ep6_pos == -1);
        peer->second.ep6_pos = this->peer6_endpoints[category].size() - 1;
        peer->second.status |= IS_ROUTABLE6;
    }
    else
    {
        peer_endpoint_struct peer_endpoint;
        peer_endpoint.ip = ip.to_v4().to_bytes(); 
        peer_endpoint.port = htons(port);
        this->peer_endpoints[category].push_back(peer_endpoint);
        this->peer_endpoint_to_peer[category].push_back(peer);
        assert(peer->second.ep_pos == -1);
        peer->second.ep_pos = this->peer_endpoints[category].size() - 1;
        peer->second.status |= IS_ROUTABLE;
    }
}


void Swarm::update_peer(peer_map::iterator iter,
    boost::asio::ip::address_v4 ip, uint16 port,
    boost::asio::ip::address_v6 ipv6, uint16 port6,
    stats_struct& stats,
    bool client_debug)
{
    INVARIANT_CHECK;

    peer_struct& p = iter->second;
    peer_id const& pid = iter->first;

    int category = p.category();

    // if the peer has another endpoint that it didn't use
    // to have, issue a nat-check for it
    if (ip != boost::asio::ip::address_v4::any() && !(p.status & HAS_V4))
    {
        ++peer4_counts[category];
        p.status |= HAS_V4;
        start_natcheck(pid, tcp::endpoint(ip, port));
    }
    if (ipv6 != boost::asio::ip::address_v6::any() && !(p.status & HAS_V6))
    {
        ++peer6_counts[category];
        p.status |= HAS_V6;
        start_natcheck(pid, tcp::endpoint(ipv6, port6));
    }

    // is true if we allow the peer to check in sooner than the
    // min interval.
    bool grant_exception = false;

    // if this is defined clients can check in whenever they want to
#ifdef DISABLE_ENFORCE_INTERVAL
    grant_exception = true;
#endif

    // if the client passed the magic password, grant an exception
    if (client_debug)
        grant_exception = true;

    // if the peer just completed the download, allow an exception
    // in order to change state to completed
    if (!(p.status & IS_COMPLETE) && stats.event == COMPLETED)
        grant_exception = true;

    // if there is only one routable peer, allow anyone that joins
    // to announce often, since the peer might have joined
    // after the routable peer joined.
    // This is required by self-publish, since the bt seeder might be the
    // only routable peer, and it will always join the swarm second.
    // the published the has to announce again to be able to connect
    // to the bt-seeder, and upload to it.
    if (peer_endpoints[peer_struct::active].size()
        + peer_endpoints[peer_struct::seeding].size() <= 2)
        grant_exception = true;

    // peer-id used by the load tester
    if (std::memcmp(&pid[0], "MAGICMAG", 8) == 0)
        grant_exception = true;

    if (!grant_exception && time(NULL) - p.last_check_in < MIN_INTERVAL)
    {
        //logger << "Client " << peer_id << " checked in too early ("
        //          << time(NULL) - peer.last_check_in << " seconds)"
        //          << std::endl;
        throw std::runtime_error("Client checked in too early.");
    }

    int old_category = p.category();
    p.update_status(stats);
    int new_category = p.category();

    if (new_category != old_category)
    {
        --peer_counts[old_category];
        ++peer_counts[new_category];
        if (p.status & HAS_V4)
        {
            --peer4_counts[old_category];
            ++peer4_counts[new_category];
            assert(peer4_counts[old_category] >= 0);
        }
        if (p.status & HAS_V6)
        {
            --peer6_counts[old_category];
            ++peer6_counts[new_category];
            assert(peer6_counts[old_category] >= 0);
        }

        // move the endpoint from the old list to the new one

        if (p.status & IS_ROUTABLE)
        {
            assert(p.status & HAS_V4);
            peer_endpoint_struct endp = peer_endpoints[category][p.ep_pos];
            remove_endpoint(old_category, p.ep_pos);
            p.ep_pos = add_endpoint(iter, endp);
        }
        if (p.status & IS_ROUTABLE6)
        {
            assert(p.status & HAS_V6);
            peer6_endpoint_struct endp = peer6_endpoints[category][p.ep6_pos];
            remove_endpoint6(old_category, p.ep6_pos);
            p.ep6_pos = add_endpoint6(iter, endp);
        }

        if (new_category == peer_struct::seeding)
        {
            stats_logger.update_peer_counts(-1, 1);
        }
        else if (old_category == peer_struct::seeding)
        {
            // wtf, they were complete and now are not?
            stats_logger.update_peer_counts(1, -1);
        }
    }

    if (verbose_logging)
        logger << "   UPDATE PEER "
            << ((p.status & IS_DOWNLOADING)?"IS_DOWNLOADING ":"")
            << ((p.status & IS_COMPLETE)?"IS_COMPLETE ":"")
            << ((p.status & IS_ROUTABLE)?"IS_ROUTABLE ":"")
            << ((p.status & IS_ROUTABLE6)?"IS_ROUTABLE6 ":"")
            << std::endl;

    // TODO: compare the reported IP with the one in the endpoint table
    // If different, remove the endpoint (and ROUTABLE flag on the peer)
    // and issue a new NAT-check
    if (p.status & IS_ROUTABLE)
    {
        assert(p.ep_pos >= 0);
        peer_endpoint_struct& peer_endpoint = this->peer_endpoints[new_category][p.ep_pos];
        peer_endpoint.ip = ip.to_bytes();
        peer_endpoint.port = htons(port);
    }
    if (p.status & IS_ROUTABLE6)
    {
        assert(p.ep6_pos >= 0);
        peer6_endpoint_struct& peer_endpoint = this->peer6_endpoints[new_category][p.ep6_pos];
        peer_endpoint.ip = ipv6.to_bytes();
        peer_endpoint.port = htons(port6);
    }
}


void Swarm::remove_peer(peer_map::iterator& peer_iter)
{
    if (verbose_logging)
        logger << "   REMOVE PEER" << std::endl;
    INVARIANT_CHECK;
    assert(peer_iter != this->peers.end());

    peer_struct& peer = peer_iter->second;

    int category = peer.category();

    if (peer.status & IS_ROUTABLE)
    {
        assert(this->peer_endpoints[category].size() ==
               this->peer_endpoint_to_peer[category].size());
        assert(this->peer_endpoints[category].size() > 0);
        assert(this->peer_endpoint_to_peer[category][peer.ep_pos] == peer_iter);

        remove_endpoint(category, peer.ep_pos);
    }

    if (peer.status & IS_ROUTABLE6)
    {
        assert(this->peer6_endpoints[category].size() ==
               this->peer6_endpoint_to_peer[category].size());
        assert(this->peer6_endpoints[category].size() > 0);
        assert(this->peer6_endpoint_to_peer[category][peer.ep6_pos] == peer_iter);

        remove_endpoint6(category, peer.ep6_pos);
    }
    //assert(!contains(this->peer_endpoint_to_peer, peer));

    --peer_counts[category];
    if (peer.status & HAS_V4) --peer4_counts[category];
    if (peer.status & HAS_V6) --peer6_counts[category];
    assert(peer4_counts[category] >= 0);
    assert(peer6_counts[category] >= 0);

    if (peer.status & IS_COMPLETE)
    {
        stats_logger.update_peer_counts(0, -1);
    }
    else
    {
        stats_logger.update_peer_counts(-1, 0);
    }

    this->peers.erase(peer_iter);
}

float Swarm::get_handout_ratio(int num_category, int denom_category, bool ipv6) const
{
    if (ipv6)
    {
        if (peer6_counts[denom_category] == 0) return max_peer_handout_per_interval;
        return float(max_peer_handout_per_interval)
            * float(peer6_endpoints[num_category].size())
            / float(peer6_counts[denom_category]);
    }
    else
    {
        if (peer4_counts[denom_category] == 0) return max_peer_handout_per_interval;
        return float(max_peer_handout_per_interval)
            * float(peer_endpoints[num_category].size())
            / float(peer4_counts[denom_category]);
    }
}

void Swarm::get_peers(std::string& peers, int count, int category, bool ipv6)
{
    switch (category)
    {
    default:
    case peer_struct::active:
    {
        // active peers can receive peers from all categories
        // there should be a bias against paused peers though
        // calculate the number of peers to hand out based
        // on the downloaders/seeders ratio
        // to avoid handing out seeds too many times to downloaders
        // and to avoid handing out downloaders to too many seeds
        float max_peers = get_handout_ratio(peer_struct::seeding, peer_struct::active, ipv6);
        count -= get_peers_sequential(peers, std::min(float(count), max_peers), peer_struct::seeding, ipv6);
        if (count)
           count -= get_peers_sequential(peers, count, peer_struct::active, ipv6);
        if (count)
           count -= get_peers_sequential(peers, count, peer_struct::paused, ipv6);
        break;
    }
    case peer_struct::paused:
    {
        float max_peers = get_handout_ratio(peer_struct::active, peer_struct::paused, ipv6);
        count -= get_peers_sequential(peers, std::min(float(count), max_peers), peer_struct::active, ipv6);
        break;
    }
    case peer_struct::seeding:
    {
        float max_peers = get_handout_ratio(peer_struct::active, peer_struct::seeding, ipv6);
        count -= get_peers_sequential(peers, std::min(float(count), max_peers), peer_struct::active, ipv6);
        break;
    }
    }
}


Swarm::peer_selection_algorithm_t Swarm::peer_selection_algorithm = PRUNED;
Swarm::peer_selection_function_t Swarm::peer_selection_funcs[] = {
    { RANDOM, "random" },
    { PRUNED, "pruned" },
};

#define NELEM(array) (sizeof(array) / sizeof(array[0]))

void Swarm::set_peer_selection_algorithm(
            enum peer_selection_algorithm_t *p,
            std::vector<std::string> & args)
{
    std::string &name = args[0];

    for (int i = 0; i < (int)NELEM(peer_selection_funcs); i++)
    {
        if (name.compare(peer_selection_funcs[i].name) == 0)
        {
            peer_selection_algorithm = peer_selection_funcs[i].algo;
            return;
        }
    }
    throw std::runtime_error("not a valid peer selection algorithm");
}

std::string Swarm::get_peer_selection_algorithm(
            enum peer_selection_algorithm_t *p)
{
    for (int i = 0; i < (int)NELEM(peer_selection_funcs); i++)
    {
        if (*p == peer_selection_funcs[i].algo)
            return peer_selection_funcs[i].name;
    }
    throw std::runtime_error("not a valid peer selection algorithm");
}

int Swarm::get_peers_sequential(std::string& peers, float count, int category, bool ipv6)
{
    int num_peers;
    if (ipv6) num_peers = this->peer6_endpoints[category].size();
    else num_peers = this->peer_endpoints[category].size();
/*
    std::cout << "get_peers_sequential: " << count
        << " next: " << next_handout[category]
        << " cursor: " << peer_list_cursor[category]
        << " count: " << count 
        << " num_peers: " << num_peers 
        << " category: " << category
        << std::endl;
*/
    if (num_peers == 0) return 0;
    if (count > num_peers) count = num_peers;
    int hand_out;
    if (ipv6)
    {
        peer6_list_cursor[category] += count;
        if (peer6_list_cursor[category] < next_handout6[category])
            return 0;
        hand_out = ceil(peer6_list_cursor[category]) - next_handout6[category];
        if (next_handout6[category] >= num_peers)
        {
            peer6_list_cursor[category] -= num_peers;
            next_handout6[category] -= num_peers;
        }
    }
    else
    {
        peer4_list_cursor[category] += count;
        if (peer4_list_cursor[category] < next_handout4[category])
            return 0;
        hand_out = ceil(peer4_list_cursor[category]) - next_handout4[category];
        if (next_handout4[category] >= num_peers)
        {
            peer4_list_cursor[category] -= num_peers;
            next_handout4[category] -= num_peers;
        }
    }

/*
    std::cout << "  hand_out: " << hand_out
        << " next: " << next_handout[category]
        << " cursor: " << peer_list_cursor[category]
        << std::endl;
*/

    int start_peer;
    if (ipv6) start_peer = next_handout6[category] % num_peers;
    else start_peer = next_handout4[category] % num_peers;
    int ret = get_peers_at(peers, start_peer, hand_out, category, ipv6);

    if (ipv6)
    {
       next_handout6[category] += hand_out;
       assert(peer6_list_cursor[category] <= next_handout6[category]
          || peer6_list_cursor[category] > num_peers - 1);
    }
    else
    {
       next_handout4[category] += hand_out;
       assert(peer4_list_cursor[category] <= next_handout4[category]
          || peer4_list_cursor[category] > num_peers - 1);
    }
    return ret;
}

int Swarm::get_peers_at(std::string& peers, int start_peer, int count, int category, bool ipv6)
{
    assert(count >= 0);
    int ret = 0;
    int num_peers;
    if (ipv6) num_peers = this->peer6_endpoints[category].size();
    else num_peers = this->peer_endpoints[category].size();
    if (num_peers == 0) return 0;

    // TODO: this cast assumes that the peer_endpoint_struct
    // is packed.
    char* start_pos;
    if (ipv6) start_pos = (char*)&this->peer6_endpoints[category][start_peer];
    else start_pos = (char*)&this->peer_endpoints[category][start_peer];

    int count_avail = min<int>(count, num_peers - start_peer);

    Swarm::peers_delivered += count_avail;

    int len;
    if (ipv6) len = count_avail * sizeof(peer6_endpoint_struct);
    else len = count_avail * sizeof(peer_endpoint_struct);
    peers.append(start_pos, len);
    ret += count_avail;

    // wrap around and copy from the beginning if necessary
    if (count_avail < count)
    {
        if (ipv6) start_pos = (char*)&this->peer6_endpoints[category][0];
        else start_pos = (char*)&this->peer_endpoints[category][0];
        count_avail = min<int>(count - count_avail, start_peer);
        if (ipv6) len = count_avail * sizeof(peer6_endpoint_struct);
        else len = count_avail * sizeof(peer_endpoint_struct);
        peers.append(start_pos, len);
        ret += count_avail;
    }
    return ret;
}

int Swarm::get_peers_random(std::string& peers, int count, int category, bool ipv6)
{
    INVARIANT_CHECK;

    int num_peers = this->peer_endpoints[category].size();

    // start at a random peer
    int start_peer = (int)((rand() / (RAND_MAX + 1.0)) * num_peers);
    assert((start_peer < num_peers) ||
           ((start_peer == 0) && (num_peers == 0)));

    return get_peers_at(peers, start_peer, count, category, ipv6);
}

// incompletes only
size_t Swarm::get_num_peers() const
{
    return peer_counts[peer_struct::active]
        + peer_counts[peer_struct::paused];
}

size_t Swarm::get_num_downloaders() const
{
    return peer_counts[peer_struct::active];
}

size_t Swarm::get_num_paused() const
{
    return peer_counts[peer_struct::paused];
}

size_t Swarm::get_num_seeds() const
{
    return peer_counts[peer_struct::seeding];
}

size_t Swarm::get_num_completes() const
{
    return stats_logger.get_num_completes();
}


size_t Swarm::get_w_bad() const
{
    return stats_logger.get_w_bad();
}

size_t Swarm::get_cumulative_w_bad() const
{
    return stats_logger.get_cumulative_w_bad();
}

void Swarm::print_peers() const
{
    INVARIANT_CHECK;

    for (int c = 0; c < peer_struct::num_categories; ++c)
    {
       for (unsigned int i = 0; i < this->peer_endpoints[c].size(); i++)
       {
          peer_endpoint_struct const& peer_endpoint = this->peer_endpoints[c][i];
          logger
             << i << ": "
             << (int)peer_endpoint.ip[0] << "."
             << (int)peer_endpoint.ip[1] << "."
             << (int)peer_endpoint.ip[2] << "."
             << (int)peer_endpoint.ip[3] << ":"
             << peer_endpoint.port << std::endl;
       }
    }
}


void Swarm::timeout_peers()
{
    INVARIANT_CHECK;

    StopWatch sw;

    int out = 0;
    time_t now = time(NULL);
    if (this->peers.size() > 100)
    {
        logger << "starting timeout of " << this->peers.size() << " peers" << std::endl;
    }

    for(peer_map::iterator iter = this->peers.begin();
        !this->peers.empty() && iter != this->peers.end();)
    {
        peer_map::iterator cur = iter;
        iter++;
        peer_struct& peer = cur->second;
        if (now - peer.last_check_in > (INTERVAL + INTERVAL/10))
        {
            //logger << "timing out peer '" << peer_id << "'" << std::endl;
            //logger << this->peers.size() << " before remove" << std::endl;
            remove_peer(cur);
            //logger << this->peers.size() << " after remove" << std::endl;
            out++;
        }
    }

    if (sw.get_msec() > 10.0)
    {
        std::string dstr = string_format("%.0f ms", sw.get_msec());
        if (out > 0)
        {
            logger << "timed out " << out << " peers in " << dstr << ", " << this->peers.size() << " total peers remaining" << std::endl;
        }
        else
        {
            logger << "scanned " << this->peers.size() << " peers in " << dstr << std::endl;
        }
    }
}

std::string Swarm::class_stats()
{
    std::stringstream st;

    st << "Swarm peers delivered: " << peers_delivered << std::endl;
    st << "Swarm peers created: " << num_peers_created << std::endl;
    return st.str();
}

std::string Swarm::flag_name(int flag)
{
    int i;
    const int n = sizeof(flagnames) / sizeof(flagnames[0]);

    for (i = 0; i < n; i++)
    {
        if (flag == flagnames[i].flag)
            break;
    }
    if (i < n)
        return flagnames[i].name;

    char buf[2 + sizeof(flag) * 2 + 1];
    snprintf(buf, sizeof(buf), "0x%x", flag);
    return std::string(buf);
}

int Swarm::flag_from_name(const std::string &name)
{
    int i;
    const int n = sizeof(flagnames) / sizeof(flagnames[0]);

    for (i = 0; i < n; i++)
    {
        if (name == flagnames[i].name)
            break;
    }
    if (i < n)
        return flagnames[i].flag;
    return -1;
}

std::string Swarm::get_flags()
{
    std::stringstream os;

    for(int f = flags; f; f &= f-1)
    {
        int flag = f & ~(f-1);
        if (f != flags)
            os << ",";
        os << flag_name(flag);
    }
    return os.str();
}

bool Swarm::set_flags(const query_param_map& query_params)
{
    int new_flags = flags;

    for (query_param_map::const_iterator it = query_params.begin();
            it != query_params.end();
            it++)
    {
        if (it->second.size() != 1)
            return false;
        int f = flag_from_name(it->first);
        if (f == -1)
            throw std::runtime_error(std::string("No such flag '") + it->first + "'.\n");
        // if this throws, it'll be caught in helix_handler::handle_request
        // and turned into 400 Bad Request.
        if (boost::lexical_cast<bool>(it->second[0]))
            new_flags |= f;
        else
            new_flags &= ~f;
    }
    flags = new_flags;
    return true;
}

void Swarm::setup_controls(ControlAPI &controls)
{
    controls.add_variable("swarm_enforce_dna_only",
            boost::bind(&ControlAPI::set_bool, &enforce_dna_only, _1),
            boost::bind(&ControlAPI::get_bool, &enforce_dna_only));
    controls.add_variable("swarm_default_dna_only",
            boost::bind(&ControlAPI::set_bool, &default_dna_only, _1),
            boost::bind(&ControlAPI::get_bool, &default_dna_only));
    controls.add_variable("swarm_dna_only_prefix",
            boost::bind(&ControlAPI::set_string, &dna_only_prefix, _1),
            boost::bind(&ControlAPI::get_string, &dna_only_prefix));
    controls.add_variable("max_handouts_per_interval",
            boost::bind(&ControlAPI::set_int, &max_peer_handout_per_interval, _1),
            boost::bind(&ControlAPI::get_int, &max_peer_handout_per_interval));
}

#ifndef NDEBUG

void Swarm::check_invariant() const
{
    for (int c = 0; c < peer_struct::num_categories; ++c)
    {
        assert(peer_endpoints[c].size() == peer_endpoint_to_peer[c].size());
        assert(peer6_endpoints[c].size() == peer6_endpoint_to_peer[c].size());
    }

    int num_routable = 0;
    int num_complete = 0;
    int num_incomplete = 0;
    int num_downloading = 0;
    int num_paused = 0;
    for (peer_map::const_iterator i = peers.begin(),
         end(peers.end()); i != end; ++i)
    {
        // make sure the id is unique
        assert(peers.count(i->first) == 1);
        // peer-id has to be 20 bytes
        peer_struct const& p = i->second;
        int category = p.category();
        if (p.status & IS_ROUTABLE)
        {
            // if the peer is routable, there should be an entry in
            // the endpoint list
            assert(p.ep_pos >= 0);
            assert(p.ep_pos < int(peer_endpoints[category].size()));
            assert(peer_endpoint_to_peer[category][p.ep_pos]->first == i->first);
        }

        if (p.status & IS_ROUTABLE6)
        {
            // if the peer is routable, there should be an entry in
            // the endpoint list
            assert(p.ep6_pos >= 0);
            assert(p.ep6_pos < int(peer6_endpoints[category].size()));
            assert(peer6_endpoint_to_peer[category][p.ep6_pos]->first == i->first);
        }

        if (p.status & (IS_ROUTABLE | IS_ROUTABLE6))
            ++num_routable;

        if (p.status & IS_COMPLETE)
        {
            ++num_complete;
            assert((p.status & IS_DOWNLOADING) == 0);
        }
        else
        {
            ++num_incomplete;
            if (p.status & IS_DOWNLOADING)
                ++num_downloading;
            else
               ++num_paused;
        }
    }
    assert(int(get_num_peers()) == num_incomplete);
    assert(int(get_num_downloaders()) == num_downloading);
    assert(int(get_num_seeds()) == num_complete);
}

#endif

} // namespace server
} // namespace http

