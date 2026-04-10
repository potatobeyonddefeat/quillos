#include "cluster.h"
#include "network.h"
#include "scheduler.h"

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);
extern volatile uint64_t ticks;

namespace Cluster {

    // Protocol message types
    static constexpr uint8_t MSG_DISCOVER     = 0x01;
    static constexpr uint8_t MSG_DISCOVER_ACK = 0x02;
    static constexpr uint8_t MSG_JOB_SUBMIT   = 0x03;
    static constexpr uint8_t MSG_JOB_RESULT   = 0x04;

    // Message header (over UDP payload)
    struct Message {
        uint8_t  type;
        uint8_t  reserved;
        uint16_t payload_len;
        uint8_t  payload[256];
    } __attribute__((packed));

    // Job types
    static constexpr uint8_t JOB_SUM  = 1; // Sum an array of uint32_t
    static constexpr uint8_t JOB_ECHO = 2; // Echo back the data

    static Node peers[MAX_NODES];
    static uint32_t peer_count = 0;
    static bool initialized = false;
    static char local_name[16];
    static uint32_t local_ip = 0;
    static uint32_t next_job_id = 1;
    static JobResult last_result = {0, false, 0};

    static void str_copy(char* dst, const char* src, int max) {
        int i = 0;
        while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
        dst[i] = '\0';
    }

    static void net_memcpy(void* dst, const void* src, uint64_t n) {
        uint8_t* d = (uint8_t*)dst;
        const uint8_t* s = (const uint8_t*)src;
        for (uint64_t i = 0; i < n; i++) d[i] = s[i];
    }

    // ================================================================
    // Peer management
    // ================================================================

    static void add_or_update_peer(uint32_t ip, const char* name) {
        if (ip == local_ip) return; // Don't add self

        // Update existing
        for (uint32_t i = 0; i < MAX_NODES; i++) {
            if (peers[i].active && peers[i].ip == ip) {
                peers[i].last_seen = ticks;
                return;
            }
        }
        // Add new
        for (uint32_t i = 0; i < MAX_NODES; i++) {
            if (!peers[i].active) {
                peers[i].ip = ip;
                peers[i].active = true;
                peers[i].last_seen = ticks;
                str_copy(peers[i].name, name, 16);
                peer_count++;

                console_print("\n[CLUSTER] Peer discovered: ");
                console_print(name);
                console_print(" (");
                char buf[16];
                uint32_t host_ip = Network::ntohl(ip);
                itoa((host_ip >> 24) & 0xFF, buf); console_print(buf); console_print(".");
                itoa((host_ip >> 16) & 0xFF, buf); console_print(buf); console_print(".");
                itoa((host_ip >> 8) & 0xFF, buf); console_print(buf); console_print(".");
                itoa(host_ip & 0xFF, buf); console_print(buf);
                console_print(")");
                return;
            }
        }
    }

    // ================================================================
    // Job execution (when we receive a job to run)
    // ================================================================

    static void execute_job(uint32_t src_ip, const uint8_t* data, uint16_t len) {
        if (len < 5) return; // job_id(4) + type(1) minimum

        uint32_t job_id;
        net_memcpy(&job_id, data, 4);
        uint8_t job_type = data[4];
        const uint8_t* job_data = data + 5;
        uint16_t job_len = len - 5;

        uint32_t result = 0;

        if (job_type == JOB_SUM) {
            // Sum array of uint32_t
            uint32_t count = job_len / 4;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t val;
                net_memcpy(&val, job_data + i * 4, 4);
                result += val;
            }
            console_print("\n[CLUSTER] Computed sum job: ");
            char buf[16];
            itoa(result, buf);
            console_print(buf);
        } else if (job_type == JOB_ECHO) {
            // Just echo — result is the length
            result = job_len;
        }

        // Send result back
        uint8_t reply[12];
        reply[0] = MSG_JOB_RESULT;
        reply[1] = 0;
        reply[2] = 8; reply[3] = 0; // payload_len = 8
        net_memcpy(reply + 4, &job_id, 4);
        net_memcpy(reply + 8, &result, 4);

        Network::send_udp(src_ip, CLUSTER_PORT, CLUSTER_PORT, reply, 12);
    }

    // ================================================================
    // UDP message handler (registered with network stack)
    // ================================================================

    static void cluster_udp_handler(uint32_t src_ip, uint16_t src_port,
                                    const uint8_t* data, uint16_t len) {
        (void)src_port;
        if (len < 4) return;

        uint8_t type = data[0];
        uint16_t payload_len = data[2] | (data[3] << 8);
        const uint8_t* payload = data + 4;

        if (payload_len > len - 4) payload_len = len - 4;

        if (type == MSG_DISCOVER) {
            // Someone is looking for peers — add them and reply
            char name[16] = {0};
            for (int i = 0; i < 15 && i < payload_len; i++) name[i] = (char)payload[i];
            add_or_update_peer(src_ip, name);

            // Send ACK back
            uint8_t reply[4 + 16];
            reply[0] = MSG_DISCOVER_ACK;
            reply[1] = 0;
            int nlen = 0;
            while (local_name[nlen] && nlen < 15) nlen++;
            reply[2] = (uint8_t)nlen;
            reply[3] = 0;
            for (int i = 0; i < nlen; i++) reply[4 + i] = (uint8_t)local_name[i];

            Network::send_udp(src_ip, CLUSTER_PORT, CLUSTER_PORT, reply, 4 + nlen);
        }
        else if (type == MSG_DISCOVER_ACK) {
            char name[16] = {0};
            for (int i = 0; i < 15 && i < payload_len; i++) name[i] = (char)payload[i];
            add_or_update_peer(src_ip, name);
        }
        else if (type == MSG_JOB_SUBMIT) {
            execute_job(src_ip, payload, payload_len);
        }
        else if (type == MSG_JOB_RESULT) {
            if (payload_len >= 8) {
                net_memcpy(&last_result.job_id, payload, 4);
                net_memcpy(&last_result.result, payload + 4, 4);
                last_result.completed = true;

                console_print("\n[CLUSTER] Job result received: ");
                char buf[16];
                itoa(last_result.result, buf);
                console_print(buf);
            }
        }
    }

    // ================================================================
    // Background tasks
    // ================================================================

    void poll_task_entry() {
        while (true) {
            Network::poll();
            Scheduler::sleep_ms(10);
        }
    }

    void heartbeat_task_entry() {
        // Wait a moment for system to settle
        Scheduler::sleep_ms(500);

        while (true) {
            send_discover();

            // Expire stale peers (not seen in 10 seconds)
            for (uint32_t i = 0; i < MAX_NODES; i++) {
                if (peers[i].active && (ticks - peers[i].last_seen) > 10000) {
                    peers[i].active = false;
                    peer_count--;
                }
            }

            Scheduler::sleep_ms(3000);
        }
    }

    // ================================================================
    // Public API
    // ================================================================

    bool init() {
        for (uint32_t i = 0; i < MAX_NODES; i++) {
            peers[i].active = false;
        }
        peer_count = 0;

        if (!Network::is_present()) {
            console_print("\n[CLUSTER] No network — cluster disabled");
            return false;
        }

        local_ip = Network::get_ip();

        // Derive node name from IP last octet
        uint32_t host = Network::ntohl(local_ip) & 0xFF;
        local_name[0] = 'n'; local_name[1] = 'o'; local_name[2] = 'd'; local_name[3] = 'e';
        char buf[8];
        itoa(host, buf);
        int j = 4;
        for (int i = 0; buf[i] && j < 15; i++) local_name[j++] = buf[i];
        local_name[j] = '\0';

        // Register UDP handler for cluster port
        Network::register_udp_handler(CLUSTER_PORT, cluster_udp_handler);

        // Start background tasks
        Scheduler::create_task("net-poll", poll_task_entry);
        Scheduler::create_task("cluster-hb", heartbeat_task_entry);

        console_print("\n[CLUSTER] ");
        console_print(local_name);
        console_print(" ready, discovery active");

        initialized = true;
        return true;
    }

    void send_discover() {
        if (!initialized) return;

        uint8_t msg[4 + 16];
        msg[0] = MSG_DISCOVER;
        msg[1] = 0;
        int nlen = 0;
        while (local_name[nlen] && nlen < 15) nlen++;
        msg[2] = (uint8_t)nlen;
        msg[3] = 0;
        for (int i = 0; i < nlen; i++) msg[4 + i] = (uint8_t)local_name[i];

        // Broadcast on subnet
        uint32_t bcast = local_ip | Network::htonl(0x000000FF);
        Network::send_udp(bcast, CLUSTER_PORT, CLUSTER_PORT, msg, 4 + nlen);
    }

    bool submit_job(uint32_t target_ip, uint8_t job_type, const uint8_t* data, uint16_t len) {
        if (!initialized || len > 240) return false;

        uint32_t job_id = next_job_id++;
        last_result.completed = false;

        uint8_t msg[4 + 5 + 240];
        msg[0] = MSG_JOB_SUBMIT;
        msg[1] = 0;
        uint16_t plen = 5 + len;
        msg[2] = (uint8_t)(plen & 0xFF);
        msg[3] = (uint8_t)(plen >> 8);
        net_memcpy(msg + 4, &job_id, 4);
        msg[8] = job_type;
        net_memcpy(msg + 9, data, len);

        return Network::send_udp(target_ip, CLUSTER_PORT, CLUSTER_PORT, msg, 4 + plen);
    }

    uint32_t get_peer_count() { return peer_count; }

    const Node* get_peer(uint32_t idx) {
        uint32_t count = 0;
        for (uint32_t i = 0; i < MAX_NODES; i++) {
            if (peers[i].active) {
                if (count == idx) return &peers[i];
                count++;
            }
        }
        return nullptr;
    }

    const JobResult* get_last_result() { return &last_result; }
    uint32_t get_local_ip() { return local_ip; }
    const char* get_local_name() { return local_name; }
}
