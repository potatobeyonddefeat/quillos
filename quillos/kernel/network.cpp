#include "network.h"
#include "e1000.h"
#include "memory.h"

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);

namespace Network {

    static uint8_t our_mac[6];
    static uint32_t our_ip = 0;
    static bool net_ready = false;
    static uint64_t pkts_sent = 0;
    static uint64_t pkts_recv = 0;

    // ARP cache
    struct ArpEntry { uint32_t ip; uint8_t mac[6]; bool valid; };
    static constexpr int ARP_TABLE_SIZE = 16;
    static ArpEntry arp_table[ARP_TABLE_SIZE];

    // UDP handler table
    struct UdpBinding { uint16_t port; udp_handler_t handler; };
    static constexpr int MAX_UDP_HANDLERS = 8;
    static UdpBinding udp_handlers[MAX_UDP_HANDLERS];

    static uint8_t recv_buf[2048];
    static uint8_t send_buf[2048];
    static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    // ================================================================
    // Helpers
    // ================================================================

    static void memcpy(void* dst, const void* src, uint64_t n) {
        uint8_t* d = (uint8_t*)dst;
        const uint8_t* s = (const uint8_t*)src;
        for (uint64_t i = 0; i < n; i++) d[i] = s[i];
    }

    static void memset(void* dst, uint8_t val, uint64_t n) {
        uint8_t* d = (uint8_t*)dst;
        for (uint64_t i = 0; i < n; i++) d[i] = val;
    }

    static uint16_t ip_checksum(const void* data, int len) {
        const uint16_t* words = (const uint16_t*)data;
        uint32_t sum = 0;
        for (int i = 0; i < len / 2; i++) sum += words[i];
        if (len & 1) sum += ((const uint8_t*)data)[len - 1];
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        return ~(uint16_t)sum;
    }

    // ================================================================
    // ARP
    // ================================================================

    static void arp_store(uint32_t ip, const uint8_t* mac) {
        // Update existing entry
        for (int i = 0; i < ARP_TABLE_SIZE; i++) {
            if (arp_table[i].valid && arp_table[i].ip == ip) {
                memcpy(arp_table[i].mac, mac, 6);
                return;
            }
        }
        // Add new entry
        for (int i = 0; i < ARP_TABLE_SIZE; i++) {
            if (!arp_table[i].valid) {
                arp_table[i].ip = ip;
                memcpy(arp_table[i].mac, mac, 6);
                arp_table[i].valid = true;
                return;
            }
        }
    }

    static bool arp_lookup(uint32_t ip, uint8_t* mac_out) {
        for (int i = 0; i < ARP_TABLE_SIZE; i++) {
            if (arp_table[i].valid && arp_table[i].ip == ip) {
                memcpy(mac_out, arp_table[i].mac, 6);
                return true;
            }
        }
        return false;
    }

    static void arp_send_request(uint32_t target_ip) {
        uint8_t pkt[sizeof(EthHeader) + sizeof(ArpPacket)];
        EthHeader* eth = (EthHeader*)pkt;
        ArpPacket* arp = (ArpPacket*)(pkt + sizeof(EthHeader));

        memcpy(eth->dst, BROADCAST_MAC, 6);
        memcpy(eth->src, our_mac, 6);
        eth->ethertype = htons(0x0806);

        arp->htype = htons(1);
        arp->ptype = htons(0x0800);
        arp->hlen = 6;
        arp->plen = 4;
        arp->oper = htons(1); // Request
        memcpy(arp->sha, our_mac, 6);
        arp->spa = our_ip; // Already network order
        memset(arp->tha, 0, 6);
        arp->tpa = target_ip;

        E1000::send(pkt, sizeof(pkt));
    }

    static void arp_send_reply(uint32_t target_ip, const uint8_t* target_mac) {
        uint8_t pkt[sizeof(EthHeader) + sizeof(ArpPacket)];
        EthHeader* eth = (EthHeader*)pkt;
        ArpPacket* arp = (ArpPacket*)(pkt + sizeof(EthHeader));

        memcpy(eth->dst, target_mac, 6);
        memcpy(eth->src, our_mac, 6);
        eth->ethertype = htons(0x0806);

        arp->htype = htons(1);
        arp->ptype = htons(0x0800);
        arp->hlen = 6;
        arp->plen = 4;
        arp->oper = htons(2); // Reply
        memcpy(arp->sha, our_mac, 6);
        arp->spa = our_ip;
        memcpy(arp->tha, target_mac, 6);
        arp->tpa = target_ip;

        E1000::send(pkt, sizeof(pkt));
    }

    static void handle_arp(const uint8_t* data, uint16_t len) {
        if (len < sizeof(ArpPacket)) return;
        const ArpPacket* arp = (const ArpPacket*)data;

        // Store sender in ARP cache
        arp_store(arp->spa, arp->sha);

        // If it's a request for our IP, reply
        if (ntohs(arp->oper) == 1 && arp->tpa == our_ip) {
            arp_send_reply(arp->spa, arp->sha);
        }
    }

    // ================================================================
    // IPv4 + UDP
    // ================================================================

    static void handle_udp(uint32_t src_ip, const uint8_t* data, uint16_t len) {
        if (len < sizeof(UdpHeader)) return;
        const UdpHeader* udp = (const UdpHeader*)data;

        uint16_t dst_port = ntohs(udp->dst_port);
        uint16_t src_port = ntohs(udp->src_port);
        uint16_t udp_len = ntohs(udp->length);
        if (udp_len < sizeof(UdpHeader) || udp_len > len) return;

        const uint8_t* payload = data + sizeof(UdpHeader);
        uint16_t payload_len = udp_len - sizeof(UdpHeader);

        // Dispatch to registered handler
        for (int i = 0; i < MAX_UDP_HANDLERS; i++) {
            if (udp_handlers[i].handler && udp_handlers[i].port == dst_port) {
                udp_handlers[i].handler(src_ip, src_port, payload, payload_len);
                return;
            }
        }
    }

    static void handle_ipv4(const uint8_t* data, uint16_t len) {
        if (len < sizeof(Ipv4Header)) return;
        const Ipv4Header* ip = (const Ipv4Header*)data;

        // Check it's for us or broadcast
        if (ip->dst_ip != our_ip && ip->dst_ip != 0xFFFFFFFF) {
            // Check subnet broadcast (10.0.0.255)
            uint32_t bcast = our_ip | htonl(0x000000FF);
            if (ip->dst_ip != bcast) return;
        }

        uint16_t hdr_len = (ip->ver_ihl & 0x0F) * 4;
        if (ip->protocol == 17) { // UDP
            handle_udp(ip->src_ip, data + hdr_len, len - hdr_len);
        }
    }

    // ================================================================
    // Packet receive dispatcher
    // ================================================================

    static void process_packet(const uint8_t* data, uint16_t len) {
        if (len < sizeof(EthHeader)) return;
        pkts_recv++;

        const EthHeader* eth = (const EthHeader*)data;
        uint16_t ethertype = ntohs(eth->ethertype);
        const uint8_t* payload = data + sizeof(EthHeader);
        uint16_t payload_len = len - sizeof(EthHeader);

        if (ethertype == 0x0806) {      // ARP
            handle_arp(payload, payload_len);
        } else if (ethertype == 0x0800) { // IPv4
            handle_ipv4(payload, payload_len);
        }
    }

    // ================================================================
    // Public API
    // ================================================================

    bool init() {
        memset(arp_table, 0, sizeof(arp_table));
        memset(udp_handlers, 0, sizeof(udp_handlers));

        if (!E1000::is_ready()) {
            console_print("\n[NET] No NIC available");
            return false;
        }

        memcpy(our_mac, E1000::get_mac(), 6);

        // Derive IP from MAC last byte: 10.0.0.{mac[5]}
        our_ip = htonl(0x0A000000 | our_mac[5]); // 10.0.0.X

        net_ready = true;

        char buf[16];
        console_print("\n[NET] IP: 10.0.0.");
        itoa(our_mac[5], buf);
        console_print(buf);

        return true;
    }

    bool send_raw(const uint8_t* dst_mac, uint16_t ethertype,
                  const uint8_t* payload, uint16_t len) {
        if (!net_ready || len + sizeof(EthHeader) > sizeof(send_buf)) return false;

        EthHeader* eth = (EthHeader*)send_buf;
        memcpy(eth->dst, dst_mac, 6);
        memcpy(eth->src, our_mac, 6);
        eth->ethertype = htons(ethertype);
        memcpy(send_buf + sizeof(EthHeader), payload, len);

        pkts_sent++;
        return E1000::send(send_buf, sizeof(EthHeader) + len);
    }

    bool send_udp(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
                  const uint8_t* data, uint16_t len) {
        if (!net_ready) return false;

        // Resolve destination MAC
        uint8_t dst_mac[6];
        bool is_broadcast = (dst_ip == 0xFFFFFFFF) ||
                            (dst_ip == (our_ip | htonl(0x000000FF)));
        if (is_broadcast) {
            memcpy(dst_mac, BROADCAST_MAC, 6);
        } else if (!arp_lookup(dst_ip, dst_mac)) {
            // Send ARP request and hope next attempt works
            arp_send_request(dst_ip);
            return false;
        }

        // Build packet: Eth + IPv4 + UDP + payload
        uint16_t udp_total = sizeof(UdpHeader) + len;
        uint16_t ip_total = sizeof(Ipv4Header) + udp_total;
        uint16_t pkt_total = sizeof(EthHeader) + ip_total;
        if (pkt_total > sizeof(send_buf)) return false;

        EthHeader* eth = (EthHeader*)send_buf;
        Ipv4Header* ip = (Ipv4Header*)(send_buf + sizeof(EthHeader));
        UdpHeader* udp = (UdpHeader*)(send_buf + sizeof(EthHeader) + sizeof(Ipv4Header));
        uint8_t* payload = send_buf + sizeof(EthHeader) + sizeof(Ipv4Header) + sizeof(UdpHeader);

        // Ethernet
        memcpy(eth->dst, dst_mac, 6);
        memcpy(eth->src, our_mac, 6);
        eth->ethertype = htons(0x0800);

        // IPv4
        static uint16_t ip_id = 1;
        ip->ver_ihl = 0x45; // Version 4, IHL 5
        ip->tos = 0;
        ip->total_len = htons(ip_total);
        ip->id = htons(ip_id++);
        ip->flags_frag = 0;
        ip->ttl = 64;
        ip->protocol = 17; // UDP
        ip->checksum = 0;
        ip->src_ip = our_ip;
        ip->dst_ip = dst_ip;
        ip->checksum = ip_checksum(ip, sizeof(Ipv4Header));

        // UDP
        udp->src_port = htons(src_port);
        udp->dst_port = htons(dst_port);
        udp->length = htons(udp_total);
        udp->checksum = 0; // Optional for UDP over IPv4

        // Payload
        memcpy(payload, data, len);

        pkts_sent++;
        return E1000::send(send_buf, pkt_total);
    }

    void register_udp_handler(uint16_t port, udp_handler_t handler) {
        for (int i = 0; i < MAX_UDP_HANDLERS; i++) {
            if (!udp_handlers[i].handler) {
                udp_handlers[i].port = port;
                udp_handlers[i].handler = handler;
                return;
            }
        }
    }

    void poll() {
        uint16_t len = 0;
        while (E1000::poll_receive(recv_buf, &len)) {
            process_packet(recv_buf, len);
        }
    }

    bool is_present() { return net_ready; }
    uint32_t get_ip() { return our_ip; }
    const uint8_t* get_mac() { return our_mac; }
    uint64_t get_packets_sent() { return pkts_sent; }
    uint64_t get_packets_received() { return pkts_recv; }
}
