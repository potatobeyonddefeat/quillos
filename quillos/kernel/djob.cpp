#include "djob.h"
#include "jobs.h"
#include "cluster.h"
#include "scheduler.h"
#include "network.h"

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);
extern volatile uint64_t ticks;

namespace Jobs {
    // ================================================================
    // Local job execution engine
    // ================================================================

    static void mem_copy(void* dst, const void* src, uint64_t n) {
        uint8_t* d = (uint8_t*)dst;
        const uint8_t* s = (const uint8_t*)src;
        for (uint64_t i = 0; i < n; i++) d[i] = s[i];
    }

    uint32_t execute(Type type, const uint8_t* data, uint16_t len) {
        uint32_t count = len / 4;

        if (type == JOB_SUM) {
            uint32_t sum = 0;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t val;
                mem_copy(&val, data + i * 4, 4);
                sum += val;
            }
            return sum;
        }
        if (type == JOB_PRODUCT) {
            uint32_t prod = 1;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t val;
                mem_copy(&val, data + i * 4, 4);
                prod *= val;
            }
            return prod;
        }
        if (type == JOB_MAX) {
            uint32_t mx = 0;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t val;
                mem_copy(&val, data + i * 4, 4);
                if (val > mx) mx = val;
            }
            return mx;
        }
        if (type == JOB_PRIME) {
            // Count primes up to N (first uint32_t in payload)
            if (count == 0) return 0;
            uint32_t n;
            mem_copy(&n, data, 4);
            if (n < 2) return 0;
            uint32_t primes = 0;
            for (uint32_t i = 2; i <= n; i++) {
                bool is_prime = true;
                for (uint32_t j = 2; j * j <= i; j++) {
                    if (i % j == 0) { is_prime = false; break; }
                }
                if (is_prime) primes++;
            }
            return primes;
        }
        if (type == JOB_ECHO) {
            return len;
        }
        return 0;
    }

    const char* type_name(Type type) {
        switch (type) {
            case JOB_SUM:     return "sum";
            case JOB_PRODUCT: return "product";
            case JOB_MAX:     return "max";
            case JOB_PRIME:   return "prime";
            case JOB_ECHO:    return "echo";
            default:          return "unknown";
        }
    }
}

namespace DJob {

    // ================================================================
    // Node load tracking
    // ================================================================

    static constexpr uint32_t MAX_NODES = 8;
    static NodeLoad node_loads[MAX_NODES];

    // ================================================================
    // Job history ring buffer
    // ================================================================

    static Jobs::Job history[MAX_HISTORY];
    static uint32_t history_write = 0;
    static uint32_t history_count = 0;
    static uint32_t next_job_id = 1;
    static bool initialized = false;

    static void mem_copy(void* dst, const void* src, uint64_t n) {
        uint8_t* d = (uint8_t*)dst;
        const uint8_t* s = (const uint8_t*)src;
        for (uint64_t i = 0; i < n; i++) d[i] = s[i];
    }

    // ================================================================
    // Load balancing — pick the best node to run a job
    //
    // Returns 0 for local, or a peer IP for remote.
    // Decision: pick the node (including self) with the fewest tasks.
    // ================================================================

    static uint32_t pick_target() {
        uint32_t local_load = get_local_load();
        uint32_t best_ip = 0;    // 0 = local
        uint32_t best_load = local_load;

        for (uint32_t i = 0; i < MAX_NODES; i++) {
            if (!node_loads[i].valid) continue;
            // Only consider nodes seen recently (within 10 seconds)
            if (ticks - node_loads[i].last_updated > 10000) continue;

            if (node_loads[i].task_count < best_load) {
                best_load = node_loads[i].task_count;
                best_ip = node_loads[i].ip;
            }
        }

        return best_ip;
    }

    // ================================================================
    // Record a job in history
    // ================================================================

    static Jobs::Job* record_job(uint32_t id, Jobs::Type type,
                                  const uint8_t* data, uint16_t len,
                                  uint32_t target_ip) {
        Jobs::Job* j = &history[history_write];
        j->id = id;
        j->type = type;
        j->status = Jobs::STATUS_PENDING;
        j->result = 0;
        j->target_ip = target_ip;
        j->submitted_at = ticks;
        j->completed_at = 0;
        j->payload_len = len;
        if (len > Jobs::MAX_PAYLOAD) len = Jobs::MAX_PAYLOAD;
        mem_copy(j->payload, data, len);

        history_write = (history_write + 1) % MAX_HISTORY;
        if (history_count < MAX_HISTORY) history_count++;

        return j;
    }

    // ================================================================
    // Find a job in history by ID
    // ================================================================

    static Jobs::Job* find_job(uint32_t id) {
        for (uint32_t i = 0; i < MAX_HISTORY; i++) {
            if (history[i].id == id && history[i].id != 0) {
                return &history[i];
            }
        }
        return nullptr;
    }

    // ================================================================
    // Public API
    // ================================================================

    bool init() {
        for (uint32_t i = 0; i < MAX_NODES; i++) {
            node_loads[i].valid = false;
        }
        for (uint32_t i = 0; i < MAX_HISTORY; i++) {
            history[i].id = 0;
            history[i].status = Jobs::STATUS_PENDING;
        }
        history_write = 0;
        history_count = 0;
        next_job_id = 1;
        initialized = true;

        console_print("\n[DJOB] Distributed job scheduler ready");
        return true;
    }

    uint32_t submit(Jobs::Type type, const uint8_t* data, uint16_t len) {
        if (!initialized) return 0;

        uint32_t id = next_job_id++;
        uint32_t target = pick_target();

        Jobs::Job* j = record_job(id, type, data, len, target);
        char buf[32];

        if (target == 0) {
            // Run locally
            j->status = Jobs::STATUS_RUNNING;
            console_print("\n[DJOB] Job ");
            itoa(id, buf); console_print(buf);
            console_print(" ("); console_print(Jobs::type_name(type));
            console_print(") -> LOCAL");

            j->result = Jobs::execute(type, data, len);
            j->status = Jobs::STATUS_COMPLETED;
            j->completed_at = ticks;

            console_print(" = ");
            itoa(j->result, buf); console_print(buf);
        } else {
            // Send to remote node
            j->status = Jobs::STATUS_RUNNING;

            // Resolve which peer name
            uint32_t ip_h = Network::ntohl(target);
            console_print("\n[DJOB] Job ");
            itoa(id, buf); console_print(buf);
            console_print(" ("); console_print(Jobs::type_name(type));
            console_print(") -> 10.0.0.");
            itoa(ip_h & 0xFF, buf); console_print(buf);

            if (!Cluster::submit_job(target, (uint8_t)type, data, len)) {
                j->status = Jobs::STATUS_FAILED;
                console_print(" FAILED");
            } else {
                console_print(" sent");
            }
        }

        return id;
    }

    void on_remote_result(uint32_t job_id, uint32_t result) {
        Jobs::Job* j = find_job(job_id);
        if (j && j->status == Jobs::STATUS_RUNNING) {
            j->result = result;
            j->status = Jobs::STATUS_COMPLETED;
            j->completed_at = ticks;
        }
    }

    void update_node_load(uint32_t ip, uint32_t task_count) {
        // Update existing
        for (uint32_t i = 0; i < MAX_NODES; i++) {
            if (node_loads[i].valid && node_loads[i].ip == ip) {
                node_loads[i].task_count = task_count;
                node_loads[i].last_updated = ticks;
                return;
            }
        }
        // Add new
        for (uint32_t i = 0; i < MAX_NODES; i++) {
            if (!node_loads[i].valid) {
                node_loads[i].ip = ip;
                node_loads[i].task_count = task_count;
                node_loads[i].last_updated = ticks;
                node_loads[i].valid = true;
                return;
            }
        }
    }

    uint32_t get_local_load() {
        return Scheduler::get_count();
    }

    const Jobs::Job* get_job(uint32_t id) {
        return find_job(id);
    }

    const Jobs::Job* get_last_completed() {
        // Walk backward through history to find most recent completed
        for (int i = (int)MAX_HISTORY - 1; i >= 0; i--) {
            uint32_t idx = (history_write + (uint32_t)i) % MAX_HISTORY;
            if (history[idx].id != 0 && history[idx].status == Jobs::STATUS_COMPLETED) {
                return &history[idx];
            }
        }
        return nullptr;
    }

    uint32_t get_history_count() { return history_count; }

    const Jobs::Job* get_history(uint32_t idx) {
        if (idx >= history_count) return nullptr;
        // History is a ring buffer — calculate the actual index
        if (history_count < MAX_HISTORY) {
            return &history[idx];
        }
        uint32_t actual = (history_write + idx) % MAX_HISTORY;
        return &history[actual];
    }
}
