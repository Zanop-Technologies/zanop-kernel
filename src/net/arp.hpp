#ifndef NET_ARP_HPP
#define NET_ARP_HPP

#include "../types.hpp"

// Address Resolution Protocol (see docs/arp.md).
//
// ARP has zero authentication: any host may claim to be the gateway. This
// implementation therefore keeps the logic minimal and trusts replies it
// receives, exactly like every other IP stack.

namespace Net {

struct ArpEntry {
    uint32_t ip;
    uint8_t  mac[6];
    uint64_t last_seen; // ticks (1 ms each) of the last confirmed mapping
};

// Local IPv4 address used as the ARP sender address. Defaults to the QEMU
// user-mode networking guest address; set_ip() overrides it.
uint32_t local_ip();
void set_ip(uint32_t ip);

void init();

// Look up `target_ip` in the cache. On a hit (and only then) `out_mac` is
// filled in and true is returned. On a miss an ARP request is broadcast and
// false is returned: the caller must drop or queue the packet.
bool resolve(uint32_t target_ip, uint8_t out_mac[6]);

// Feed one received Ethernet frame here (handles type 0x0806 only).
void on_receive(const uint8_t* packet, size_t len);

// Advance the cache clock; call roughly once per millisecond.
void periodic();

// Send a gratuitous ARP (broadcast) announcing our IP -> MAC mapping.
void announce();

// Debug: current number of cached entries.
size_t cache_size();

} // namespace Net

#endif
