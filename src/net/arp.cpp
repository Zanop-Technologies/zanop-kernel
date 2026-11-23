#include "arp.hpp"
#include "../drivers/e1000.hpp"
#include "../drivers/serial.hpp"

// ARP per docs/arp.md:
//   - 28-byte packet carried directly in an Ethernet frame (type 0x0806)
//   - Request -> broadcast FF:FF:FF:FF:FF:FF, Reply -> unicast
//   - IP -> MAC cache with a 10-minute hard timeout
//   - Packets that cannot be resolved are dropped (TCP retransmits)

namespace Net {

namespace {

constexpr size_t    ETH_HEADER_SIZE = 14;
constexpr size_t    ARP_PAYLOAD_SIZE = 28;
constexpr size_t    ARP_FRAME_SIZE  = ETH_HEADER_SIZE + ARP_PAYLOAD_SIZE;

constexpr uint16_t  ETHERTYPE_ARP   = 0x0806;
constexpr uint16_t  HW_TYPE_ETHERNET = 1;
constexpr uint16_t  PROTO_TYPE_IPV4  = 0x0800;
constexpr uint8_t   HW_LEN          = 6;
constexpr uint8_t   PROTO_LEN       = 4;
constexpr uint16_t  OP_REQUEST      = 1;
constexpr uint16_t  OP_REPLY        = 2;

// ARP cache: 10-minute hard timeout (docs/arp.md "ARP Cache").
constexpr uint64_t  ARP_ENTRY_TIMEOUT_TICKS = 10ULL * 60ULL * 1000ULL; // ms
constexpr size_t    CACHE_SIZE = 16;

struct Cache {
    ArpEntry entries[CACHE_SIZE];
    size_t   count;
};

Cache cache = {};

// Our own IPv4 address (QEMU user-mode networking gives us 10.0.2.15).
uint32_t own_ip = (10u << 24) | (0u << 16) | (2u << 8) | 15u;

// Monotonic-ish clock. We do not have a tick counter exposed here, so the
// timestamp is derived from a simple software counter bumped by periodic().
uint64_t now_ticks = 0;

// -- helpers ---------------------------------------------------------------

inline uint16_t read_be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline void write_be16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value >> 8);
    p[1] = static_cast<uint8_t>(value & 0xFF);
}

inline void write_be32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value >> 24);
    p[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(value & 0xFF);
}

inline uint32_t read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

void copy_bytes(uint8_t* dst, const uint8_t* src, size_t count) {
    for (size_t i = 0; i < count; ++i) dst[i] = src[i];
}

bool mac_equal(const uint8_t* a, const uint8_t* b) {
    for (size_t i = 0; i < HW_LEN; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

// -- cache -----------------------------------------------------------------

ArpEntry* cache_find(uint32_t ip) {
    for (size_t i = 0; i < cache.count; ++i) {
        if (cache.entries[i].ip == ip) return &cache.entries[i];
    }
    return nullptr;
}

ArpEntry* cache_find_lru() {
    if (cache.count < CACHE_SIZE) return &cache.entries[cache.count];
    size_t oldest = 0;
    for (size_t i = 1; i < cache.count; ++i) {
        if (cache.entries[i].last_seen < cache.entries[oldest].last_seen) oldest = i;
    }
    return &cache.entries[oldest];
}

void cache_store(uint32_t ip, const uint8_t* mac) {
    ArpEntry* entry = cache_find(ip);
    if (entry == nullptr) {
        entry = cache_find_lru();
        if (entry == &cache.entries[cache.count] && cache.count < CACHE_SIZE) {
            ++cache.count;
        }
        entry->ip = ip;
    }
    copy_bytes(entry->mac, mac, HW_LEN);
    entry->last_seen = now_ticks;
}

// -- frame construction ----------------------------------------------------

void build_frame(uint8_t* frame, const uint8_t* dst_mac, uint32_t sender_ip,
                 const uint8_t* target_mac, uint32_t target_ip, uint16_t op) {
    const uint8_t* own_mac = Drivers::E1000::stats().mac;

    copy_bytes(frame + 0, dst_mac, HW_LEN);       // destination MAC
    copy_bytes(frame + 6, own_mac, HW_LEN);       // source MAC
    write_be16(frame + 12, ETHERTYPE_ARP);        // ethertype 0x0806

    uint8_t* arp = frame + ETH_HEADER_SIZE;
    write_be16(arp + 0,  HW_TYPE_ETHERNET);
    write_be16(arp + 2,  PROTO_TYPE_IPV4);
    arp[4] = HW_LEN;
    arp[5] = PROTO_LEN;
    write_be16(arp + 6,  op);                     // 1 = request, 2 = reply
    copy_bytes(arp + 8,  own_mac, HW_LEN);        // sender MAC
    write_be32(arp + 14, sender_ip);              // sender IP
    copy_bytes(arp + 18, target_mac, HW_LEN);     // target MAC
    write_be32(arp + 24, target_ip);              // target IP
}

void send_frame(const uint8_t* frame) {
    if (!Drivers::E1000::send_packet(frame, static_cast<uint16_t>(ARP_FRAME_SIZE))) {
        serial::write_string("arp: transmit failed\n");
    }
}

void broadcast_request(uint32_t sender_ip, uint32_t target_ip) {
    uint8_t frame[ARP_FRAME_SIZE];
    const uint8_t broadcast[HW_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t unknown[HW_LEN]   = {0, 0, 0, 0, 0, 0};
    build_frame(frame, broadcast, sender_ip, unknown, target_ip, OP_REQUEST);
    send_frame(frame);
}

} // anonymous namespace

// -- public API ------------------------------------------------------------

uint32_t local_ip() { return own_ip; }

void set_ip(uint32_t ip) { own_ip = ip; }

void init() {
    for (size_t i = 0; i < CACHE_SIZE; ++i) {
        cache.entries[i].ip = 0;
        cache.entries[i].last_seen = 0;
        for (size_t j = 0; j < HW_LEN; ++j) cache.entries[i].mac[j] = 0;
    }
    cache.count = 0;
    now_ticks = 0;
    serial::write_string("arp: initialized\n");
}

bool resolve(uint32_t target_ip, uint8_t out_mac[6]) {
    ArpEntry* entry = cache_find(target_ip);
    if (entry != nullptr) {
        if (now_ticks - entry->last_seen < ARP_ENTRY_TIMEOUT_TICKS) {
            copy_bytes(out_mac, entry->mac, HW_LEN);
            return true;
        }
        // Expired: drop it from the cache and re-request.
        entry->ip = 0;
    }

    // Unknown mapping: broadcast a request. The caller must drop (or queue)
    // the packet it wanted to send; TCP handles retransmission.
    broadcast_request(local_ip(), target_ip);
    return false;
}

void on_receive(const uint8_t* packet, size_t len) {
    if (packet == nullptr || len < ARP_FRAME_SIZE) return;
    if (read_be16(packet + 12) != ETHERTYPE_ARP) return;

    const uint8_t* arp = packet + ETH_HEADER_SIZE;
    const uint16_t hw_type    = read_be16(arp + 0);
    const uint16_t proto_type = read_be16(arp + 2);
    const uint8_t  hw_len     = arp[4];
    const uint8_t  proto_len  = arp[5];
    const uint16_t op         = read_be16(arp + 6);

    if (hw_type != HW_TYPE_ETHERNET || proto_type != PROTO_TYPE_IPV4) return;
    if (hw_len != HW_LEN || proto_len != PROTO_LEN) return;

    const uint8_t* sender_mac = arp + 8;
    const uint32_t sender_ip  = read_be32(arp + 14);
    const uint8_t* target_mac = arp + 18;
    const uint32_t target_ip  = read_be32(arp + 24);

    // Every ARP packet legitimately teaches us a mapping.
    cache_store(sender_ip, sender_mac);

    if (op == OP_REQUEST) {
        // Only answer requests aimed at us.
        if (target_ip != local_ip()) return;
        uint8_t frame[ARP_FRAME_SIZE];
        build_frame(frame, sender_mac, local_ip(), sender_mac, sender_ip, OP_REPLY);
        send_frame(frame);
        serial::write_string("arp: replied to request\n");
    } else if (op == OP_REPLY) {
        serial::write_string("arp: cached reply\n");
    }
}

void periodic() {
    // Advance the software clock used for cache expiry (called ~1ms apart).
    ++now_ticks;
}

size_t cache_size() {
    return cache.count;
}

// Gratuitous ARP (docs/arp.md): announce our mapping on boot / IP change.
void announce() {
    const uint8_t broadcast[HW_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t frame[ARP_FRAME_SIZE];
    build_frame(frame, broadcast, local_ip(), broadcast, local_ip(), OP_REQUEST);
    send_frame(frame);
    serial::write_string("arp: gratuitous announcement sent\n");
}

} // namespace Net
