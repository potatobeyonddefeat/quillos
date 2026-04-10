#include "process.h"
#include "scheduler.h"
#include "cluster.h"
#include "network.h"
#include "djob.h"

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);
extern volatile uint64_t ticks;

namespace Process {

    static Info procs[MAX_PROCS];
    static uint32_t next_pid = 1;
    static bool initialized = false;

    // Per-process "should I stop?" flags (indexed by proc table slot)
    static volatile bool stop_flags[MAX_PROCS];

    static void str_copy(char* dst, const char* src, int max) {
        int i = 0;
        while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
        dst[i] = '\0';
    }

    static bool str_eq(const char* a, const char* b) {
        int i = 0;
        while (a[i] && b[i]) { if (a[i] != b[i]) return false; i++; }
        return a[i] == b[i];
    }

    // ================================================================
    // Process entry functions — these run as scheduler tasks
    // ================================================================

    // Find which proc slot this task belongs to
    static int find_my_slot() {
        uint32_t cur = Scheduler::get_current();
        for (uint32_t i = 0; i < MAX_PROCS; i++) {
            if (procs[i].state == PROC_RUNNING &&
                procs[i].sched_slot == (int32_t)cur) {
                return (int)i;
            }
        }
        return -1;
    }

    static void counter_entry() {
        int slot = find_my_slot();
        char buf[32];
        uint32_t count = 0;
        while (slot >= 0 && !stop_flags[slot]) {
            count++;
            console_print("\n[proc:");
            itoa(procs[slot].pid, buf); console_print(buf);
            console_print("] count=");
            itoa(count, buf); console_print(buf);
            procs[slot].cpu_ticks = count;
            Scheduler::sleep_ms(1000);
        }
        if (slot >= 0) {
            procs[slot].state = PROC_DEAD;
            console_print("\n[proc:");
            itoa(procs[slot].pid, buf); console_print(buf);
            console_print("] exited");
        }
    }

    static void stress_entry() {
        int slot = find_my_slot();
        char buf[32];
        uint64_t iterations = 0;
        while (slot >= 0 && !stop_flags[slot]) {
            // CPU-bound work
            volatile uint32_t x = 0;
            for (uint32_t i = 0; i < 100000; i++) x += i;
            iterations++;
            procs[slot].cpu_ticks = iterations;
            // Yield occasionally to not starve other tasks
            if (iterations % 10 == 0) Scheduler::sleep_ms(1);
        }
        if (slot >= 0) {
            procs[slot].state = PROC_DEAD;
            console_print("\n[proc:");
            itoa(procs[slot].pid, buf); console_print(buf);
            console_print("] stress done, iters=");
            itoa(iterations, buf); console_print(buf);
        }
    }

    static void monitor_entry() {
        int slot = find_my_slot();
        char buf[32];
        while (slot >= 0 && !stop_flags[slot]) {
            console_print("\n[monitor] tasks=");
            itoa(Scheduler::get_count(), buf); console_print(buf);
            console_print(" procs=");
            itoa(Process::count(), buf); console_print(buf);
            console_print(" peers=");
            itoa(Cluster::get_peer_count(), buf); console_print(buf);
            console_print(" up=");
            itoa(ticks / 1000, buf); console_print(buf);
            console_print("s");
            procs[slot].cpu_ticks++;
            Scheduler::sleep_ms(2000);
        }
        if (slot >= 0) {
            procs[slot].state = PROC_DEAD;
        }
    }

    static void worker_entry() {
        int slot = find_my_slot();
        while (slot >= 0 && !stop_flags[slot]) {
            // Idle worker — in a full system this would pull from a job queue
            procs[slot].cpu_ticks++;
            Scheduler::sleep_ms(500);
        }
        if (slot >= 0) {
            procs[slot].state = PROC_DEAD;
        }
    }

    typedef void (*entry_fn_t)();

    static entry_fn_t get_entry(Type t) {
        switch (t) {
            case TYPE_COUNTER: return counter_entry;
            case TYPE_STRESS:  return stress_entry;
            case TYPE_MONITOR: return monitor_entry;
            case TYPE_WORKER:  return worker_entry;
            default: return nullptr;
        }
    }

    // ================================================================
    // Find a free slot in the process table
    // ================================================================

    static int find_free() {
        for (uint32_t i = 0; i < MAX_PROCS; i++) {
            if (procs[i].state == PROC_UNUSED || procs[i].state == PROC_DEAD) {
                return (int)i;
            }
        }
        return -1;
    }

    // ================================================================
    // Spawn a process locally
    // ================================================================

    static uint32_t spawn_local(Type type, const char* name, uint32_t owner_ip) {
        int slot = find_free();
        if (slot < 0) return 0;

        entry_fn_t entry = get_entry(type);
        if (!entry) return 0;

        uint32_t pid = next_pid++;

        procs[slot].pid = pid;
        procs[slot].type = type;
        procs[slot].state = PROC_RUNNING;
        procs[slot].node_ip = 0; // Local
        procs[slot].owner_ip = owner_ip;
        procs[slot].started_at = ticks;
        procs[slot].cpu_ticks = 0;
        stop_flags[slot] = false;

        // Build a name like "counter:5"
        char full_name[24];
        str_copy(full_name, name, 16);
        int len = 0;
        while (full_name[len]) len++;
        full_name[len] = ':';
        char pbuf[8];
        itoa(pid, pbuf);
        int j = 0;
        while (pbuf[j] && len + 1 + j < 23) {
            full_name[len + 1 + j] = pbuf[j];
            j++;
        }
        full_name[len + 1 + j] = '\0';
        str_copy(procs[slot].name, full_name, 24);

        // Create a scheduler task
        int sched = Scheduler::create_task(full_name, entry);
        if (sched < 0) {
            procs[slot].state = PROC_UNUSED;
            return 0;
        }
        procs[slot].sched_slot = sched;

        return pid;
    }

    // ================================================================
    // Public API
    // ================================================================

    bool init() {
        for (uint32_t i = 0; i < MAX_PROCS; i++) {
            procs[i].state = PROC_UNUSED;
            procs[i].pid = 0;
            procs[i].sched_slot = -1;
            stop_flags[i] = false;
        }
        next_pid = 1;
        initialized = true;

        // Start status reporter background task
        Scheduler::create_task("proc-stat", status_report_entry);

        console_print("\n[PROC] Process manager ready");
        return true;
    }

    Type parse_type(const char* name) {
        if (str_eq(name, "counter")) return TYPE_COUNTER;
        if (str_eq(name, "stress"))  return TYPE_STRESS;
        if (str_eq(name, "monitor")) return TYPE_MONITOR;
        if (str_eq(name, "worker"))  return TYPE_WORKER;
        return (Type)0;
    }

    const char* type_name(Type t) {
        switch (t) {
            case TYPE_COUNTER: return "counter";
            case TYPE_STRESS:  return "stress";
            case TYPE_MONITOR: return "monitor";
            case TYPE_WORKER:  return "worker";
            default: return "unknown";
        }
    }

    uint32_t spawn(const char* type_name_str) {
        if (!initialized) return 0;

        Type type = parse_type(type_name_str);
        if (type == 0) return 0;

        // Use DJob's load balancer to pick a node
        // Check if peers exist with lower load
        uint32_t target_ip = 0;

        // Alternate between local and remote for distribution
        for (uint32_t i = 0; i < Cluster::get_peer_count(); i++) {
            const Cluster::Node* peer = Cluster::get_peer(i);
            if (peer) {
                // Alternate between local and remote for distribution
                static uint32_t rr = 0;
                rr++;
                if (rr % 2 == 0 && Cluster::get_peer_count() > 0) {
                    target_ip = peer->ip;
                }
                break;
            }
        }

        return spawn_on(type_name_str, target_ip);
    }

    uint32_t spawn_on(const char* type_name_str, uint32_t node_ip) {
        if (!initialized) return 0;

        Type type = parse_type(type_name_str);
        if (type == 0) return 0;

        char buf[32];

        if (node_ip == 0 || node_ip == Network::get_ip()) {
            // Local spawn
            uint32_t pid = spawn_local(type, type_name_str, 0);
            if (pid) {
                console_print("\n[PROC] Spawned ");
                console_print(type_name_str);
                console_print(" (pid="); itoa(pid, buf); console_print(buf);
                console_print(") LOCAL");
            }
            return pid;
        }

        // Remote spawn: allocate a tracking entry and send request
        int slot = find_free();
        if (slot < 0) return 0;

        uint32_t pid = next_pid++;
        procs[slot].pid = pid;
        procs[slot].type = type;
        procs[slot].state = PROC_RUNNING;
        procs[slot].node_ip = node_ip;
        procs[slot].owner_ip = Network::get_ip();
        procs[slot].started_at = ticks;
        procs[slot].cpu_ticks = 0;
        procs[slot].sched_slot = -1; // Not a local task
        str_copy(procs[slot].name, type_name_str, 16);

        // Send spawn request to remote node
        uint8_t msg[4 + 5];
        msg[0] = 0x10; // MSG_PROC_SPAWN
        msg[1] = 0;
        msg[2] = 5; msg[3] = 0;
        uint32_t pid_copy = pid;
        uint8_t* p = msg + 4;
        p[0] = (uint8_t)type;
        p[1] = (uint8_t)(pid_copy & 0xFF);
        p[2] = (uint8_t)((pid_copy >> 8) & 0xFF);
        p[3] = (uint8_t)((pid_copy >> 16) & 0xFF);
        p[4] = (uint8_t)((pid_copy >> 24) & 0xFF);

        Network::send_udp(node_ip, Cluster::CLUSTER_PORT, Cluster::CLUSTER_PORT, msg, 9);

        uint32_t ip_h = Network::ntohl(node_ip);
        console_print("\n[PROC] Spawned ");
        console_print(type_name_str);
        console_print(" (pid="); itoa(pid, buf); console_print(buf);
        console_print(") on 10.0.0.");
        itoa(ip_h & 0xFF, buf); console_print(buf);

        return pid;
    }

    bool kill(uint32_t pid) {
        for (uint32_t i = 0; i < MAX_PROCS; i++) {
            if (procs[i].pid == pid && procs[i].state == PROC_RUNNING) {
                if (procs[i].node_ip == 0) {
                    // Local: set stop flag, scheduler task will clean up
                    stop_flags[i] = true;
                    procs[i].state = PROC_EXITING;
                    if (procs[i].sched_slot >= 0) {
                        Scheduler::kill_task((uint32_t)procs[i].sched_slot);
                    }
                    procs[i].state = PROC_DEAD;
                    return true;
                } else {
                    // Remote: send kill request
                    uint8_t msg[4 + 4];
                    msg[0] = 0x12; // MSG_PROC_KILL
                    msg[1] = 0;
                    msg[2] = 4; msg[3] = 0;
                    msg[4] = (uint8_t)(pid & 0xFF);
                    msg[5] = (uint8_t)((pid >> 8) & 0xFF);
                    msg[6] = (uint8_t)((pid >> 16) & 0xFF);
                    msg[7] = (uint8_t)((pid >> 24) & 0xFF);
                    Network::send_udp(procs[i].node_ip, Cluster::CLUSTER_PORT,
                                      Cluster::CLUSTER_PORT, msg, 8);
                    procs[i].state = PROC_EXITING;
                    return true;
                }
            }
        }
        return false;
    }

    uint32_t count() {
        uint32_t c = 0;
        for (uint32_t i = 0; i < MAX_PROCS; i++) {
            if (procs[i].state == PROC_RUNNING || procs[i].state == PROC_SLEEPING ||
                procs[i].state == PROC_EXITING) c++;
        }
        return c;
    }

    const Info* get_by_index(uint32_t idx) {
        uint32_t c = 0;
        for (uint32_t i = 0; i < MAX_PROCS; i++) {
            if (procs[i].state != PROC_UNUSED && procs[i].state != PROC_DEAD) {
                if (c == idx) return &procs[i];
                c++;
            }
        }
        return nullptr;
    }

    const Info* find_pid(uint32_t pid) {
        for (uint32_t i = 0; i < MAX_PROCS; i++) {
            if (procs[i].pid == pid && procs[i].state != PROC_UNUSED) {
                return &procs[i];
            }
        }
        return nullptr;
    }

    // ================================================================
    // Remote process event handlers (called by cluster protocol)
    // ================================================================

    void on_remote_spawn_request(uint32_t from_ip, Type type, uint32_t remote_pid) {
        // A peer wants us to run a process
        const char* tname = type_name(type);
        uint32_t local_pid = spawn_local(type, tname, from_ip);

        char buf[16];
        console_print("\n[PROC] Remote spawn request from 10.0.0.");
        uint32_t ip_h = Network::ntohl(from_ip);
        itoa(ip_h & 0xFF, buf); console_print(buf);
        console_print(": "); console_print(tname);

        // Send back PROC_SPAWNED acknowledgment
        uint8_t msg[4 + 8];
        msg[0] = 0x11; // MSG_PROC_SPAWNED
        msg[1] = 0;
        msg[2] = 8; msg[3] = 0;
        // remote_pid (what the requester calls it)
        msg[4] = (uint8_t)(remote_pid & 0xFF);
        msg[5] = (uint8_t)((remote_pid >> 8) & 0xFF);
        msg[6] = (uint8_t)((remote_pid >> 16) & 0xFF);
        msg[7] = (uint8_t)((remote_pid >> 24) & 0xFF);
        // local_pid (what we call it)
        msg[8] = (uint8_t)(local_pid & 0xFF);
        msg[9] = (uint8_t)((local_pid >> 8) & 0xFF);
        msg[10] = (uint8_t)((local_pid >> 16) & 0xFF);
        msg[11] = (uint8_t)((local_pid >> 24) & 0xFF);
        Network::send_udp(from_ip, Cluster::CLUSTER_PORT, Cluster::CLUSTER_PORT, msg, 12);

        (void)local_pid;
    }

    void on_remote_kill_request(uint32_t pid) {
        // Find and kill the local process that matches
        for (uint32_t i = 0; i < MAX_PROCS; i++) {
            if (procs[i].pid == pid && procs[i].state == PROC_RUNNING && procs[i].node_ip == 0) {
                stop_flags[i] = true;
                if (procs[i].sched_slot >= 0) {
                    Scheduler::kill_task((uint32_t)procs[i].sched_slot);
                }
                procs[i].state = PROC_DEAD;

                char buf[16];
                console_print("\n[PROC] Killed pid=");
                itoa(pid, buf); console_print(buf);
                console_print(" by remote request");
                return;
            }
        }
    }

    void on_remote_spawned(uint32_t pid, uint32_t node_ip) {
        (void)pid; (void)node_ip;
    }

    void on_remote_died(uint32_t pid, uint32_t node_ip) {
        for (uint32_t i = 0; i < MAX_PROCS; i++) {
            if (procs[i].pid == pid && procs[i].node_ip == node_ip) {
                procs[i].state = PROC_DEAD;
                return;
            }
        }
    }

    void on_remote_status(uint32_t pid, uint32_t node_ip, State state, uint64_t cpu) {
        for (uint32_t i = 0; i < MAX_PROCS; i++) {
            if (procs[i].pid == pid && procs[i].node_ip == node_ip) {
                procs[i].cpu_ticks = cpu;
                if (state == PROC_DEAD) procs[i].state = PROC_DEAD;
                return;
            }
        }
    }

    // ================================================================
    // Background task: report process status to peers
    // ================================================================

    void status_report_entry() {
        Scheduler::sleep_ms(1000);

        while (true) {
            // Clean up dead local processes
            for (uint32_t i = 0; i < MAX_PROCS; i++) {
                if (procs[i].state == PROC_DEAD && procs[i].node_ip == 0) {
                    procs[i].state = PROC_UNUSED;
                    procs[i].pid = 0;
                }
            }

            Scheduler::sleep_ms(2000);
        }
    }
}
