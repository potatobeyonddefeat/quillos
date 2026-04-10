#pragma once
#include <stdint.h>

namespace Network {

    // Byte-order helpers
    static inline uint16_t htons(uint16_t x) { return (x >> 8) | (x << 8); }
    static inline uint16_t ntohs(uint16_t x) { return htons(x); }
    static inline uint32_t htonl(uint32_t x) {
        return ((x>>24)&0xFF)|((x>>8)&0xFF00)|((x<<8)&0xFF0000)|((x<<24)&0xFF000000);
    }
    static inline uint32_t ntohl(uint32_t x) { return htonl(x); }

    // Protocol structures
    struct EthHeader {
        uint8_t  dst[6];
        uint8_t  src[6];
        uint16_t ethertype;
    } __attribute__((packed));

    struct ArpPacket {
        uint16_t htype, ptype;
        uint8_t  hlen, plen;
        uint16_t oper;
        uint8_t  sha[6]; uint32_t spa;
        uint8_t  tha[6]; uint32_t tpa;
    } __attribute__((packed));

    struct Ipv4Header {
        uint8_t  ver_ihl;
        uint8_t  tos;
        uint16_t total_len;
        uint16_t id;
        uint16_t flags_frag;
        uint8_t  ttl;
        uint8_t  protocol;
        uint16_t checksum;
        uint32_t src_ip;
        uint32_t dst_ip;
    } __attribute__((packed));

    struct UdpHeader {
        uint16_t src_port;
        uint16_t dst_port;
        uint16_t length;
        uint16_t checksum;
    } __attribute__((packed));

    // UDP receive callback
    typedef void (*udp_handler_t)(uint32_t src_ip, uint16_t src_port,
                                  const uint8_t* data, uint16_t len);

    // Init — call after E1000::init()
    bool init();

    // Send a UDP datagram
    bool send_udp(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
                  const uint8_t* data, uint16_t len);

    // Send raw Ethernet frame
    bool send_raw(const uint8_t* dst_mac, uint16_t ethertype,
                  const uint8_t* payload, uint16_t len);

    // Register handler for incoming UDP on a port
    void register_udp_handler(uint16_t port, udp_handler_t handler);

    // Poll for incoming packets (call periodically)
    void poll();

    // Info
    bool is_present();
    uint32_t get_ip();
    const uint8_t* get_mac();
    uint64_t get_packets_sent();
    uint64_t get_packets_received();
}
