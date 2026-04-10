#include "process.h"
#include "scheduler.h"
#include "cluster.h"
#include "network.h"
#include "djob.h"
#include "vmm.h"
#include "elf.h"
#include "idt.h"
#include "memory.h"

extern void console_print(const char* str);
extern void console_putc(char c);
extern void itoa(uint64_t n, char* str);
extern volatile uint64_t ticks;

// Defined in scheduler.cpp — lets exec() retarget the current
// scheduler task to a new address space without tearing it down.
extern "C" void sched_retarget_current_cr3(uint64_t new_cr3);

// Defined in userprog.cpp — looks up an embedded user program
// by pseudo-path (e.g. "/bin/hello").
extern "C" bool userprog_find(const char* name,
                              const uint8_t** out_bytes,
                              uint64_t* out_len);

// Standard user-space layout used for embedded programs / ELF fallback
static constexpr uint64_t USER_STACK_TOP  = 0x0000000000800000ULL;
static constexpr uint64_t USER_STACK_SIZE = 16 * 1024;   // 16 KB user stack

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
        procs[slot].cr3 = 0;
        procs[slot].exit_status = 0;
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
        Scheduler::set_slot_pid((uint32_t)sched, pid);

        return pid;
    }

    // ================================================================
    // User-mode process creation
    // ================================================================

    static bool map_user_stack(uint64_t cr3) {
        // Map USER_STACK_SIZE worth of pages ending at USER_STACK_TOP
        uint64_t base = USER_STACK_TOP - USER_STACK_SIZE;
        for (uint64_t off = 0; off < USER_STACK_SIZE; off += 4096) {
            uint64_t virt = base + off;
            if (!VMM::alloc_user_page(cr3, virt,
                    VMM::PAGE_USER | VMM::PAGE_WRITE | VMM::PAGE_PRESENT)) {
                return false;
            }
        }
        return true;
    }

    static uint32_t finish_user_spawn(const char* name,
                                      uint64_t cr3,
                                      uint64_t entry,
                                      uint64_t stack_top) {
        int slot = find_free();
        if (slot < 0) return 0;

        uint32_t pid = next_pid++;
        procs[slot].pid = pid;
        procs[slot].type = TYPE_USER;
        procs[slot].state = PROC_RUNNING;
        procs[slot].node_ip = 0;
        procs[slot].owner_ip = Network::get_ip();
        procs[slot].started_at = ticks;
        procs[slot].cpu_ticks = 0;
        procs[slot].cr3 = cr3;
        procs[slot].exit_status = 0;
        stop_flags[slot] = false;

        char full_name[24];
        str_copy(full_name, name, 16);
        int nlen = 0;
        while (full_name[nlen]) nlen++;
        full_name[nlen] = ':';
        char pbuf[8];
        itoa(pid, pbuf);
        int j = 0;
        while (pbuf[j] && nlen + 1 + j < 23) {
            full_name[nlen + 1 + j] = pbuf[j];
            j++;
        }
        full_name[nlen + 1 + j] = '\0';
        str_copy(procs[slot].name, full_name, 24);

        int sched = Scheduler::create_user_task(full_name, cr3, entry, stack_top);
        if (sched < 0) {
            procs[slot].state = PROC_UNUSED;
            VMM::destroy_address_space(cr3);
            return 0;
        }
        procs[slot].sched_slot = sched;
        Scheduler::set_slot_pid((uint32_t)sched, pid);
        return pid;
    }

    uint32_t spawn_user_elf(const char* name, const void* elf_bytes, uint64_t len) {
        if (!initialized) return 0;

        uint64_t cr3 = VMM::create_address_space();
        if (!cr3) {
            console_print("\n[PROC] OOM creating user address space");
            return 0;
        }

        ELF::LoadResult r = ELF::load(cr3, elf_bytes, len);
        if (!r.ok) {
            VMM::destroy_address_space(cr3);
            return 0;
        }

        if (!map_user_stack(cr3)) {
            console_print("\n[PROC] OOM mapping user stack");
            VMM::destroy_address_space(cr3);
            return 0;
        }

        uint32_t pid = finish_user_spawn(name, cr3, r.entry, USER_STACK_TOP);
        if (pid) {
            char buf[16];
            console_print("\n[PROC] user ELF '");
            console_print(name);
            console_print("' pid=");
            itoa(pid, buf); console_print(buf);
            console_print(" entry=0x");
            const char* hex = "0123456789ABCDEF";
            char h[17]; h[16] = 0;
            for (int i = 15; i >= 0; i--) { h[15 - i] = hex[(r.entry >> (i*4)) & 0xF]; }
            console_print(h);
        }
        return pid;
    }

    uint32_t spawn_user_blob(const char* name, const void* code,
                             uint64_t code_len, uint64_t load_addr) {
        if (!initialized || !code || code_len == 0) return 0;
        if (load_addr == 0) load_addr = 0x400000;

        uint64_t cr3 = VMM::create_address_space();
        if (!cr3) return 0;

        // Map enough user pages to hold the blob (code + data)
        uint64_t base = load_addr & ~0xFFFULL;
        uint64_t end  = (load_addr + code_len + 0xFFF) & ~0xFFFULL;
        for (uint64_t v = base; v < end; v += 4096) {
            if (!VMM::alloc_user_page(cr3, v,
                    VMM::PAGE_USER | VMM::PAGE_WRITE | VMM::PAGE_PRESENT)) {
                VMM::destroy_address_space(cr3);
                return 0;
            }
        }

        // Copy the blob bytes into the new address space
        if (!VMM::copy_to_user(cr3, load_addr, code, code_len)) {
            VMM::destroy_address_space(cr3);
            return 0;
        }

        if (!map_user_stack(cr3)) {
            VMM::destroy_address_space(cr3);
            return 0;
        }

        uint32_t pid = finish_user_spawn(name, cr3, load_addr, USER_STACK_TOP);
        if (pid) {
            char buf[16];
            console_print("\n[PROC] user blob '");
            console_print(name);
            console_print("' pid=");
            itoa(pid, buf); console_print(buf);
        }
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
            procs[i].cr3 = 0;
            procs[i].exit_status = 0;
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
        procs[slot].cr3 = 0;
        procs[slot].exit_status = 0;
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
            if (procs[i].pid == pid &&
                (procs[i].state == PROC_RUNNING || procs[i].state == PROC_SLEEPING ||
                 procs[i].state == PROC_EXITING)) {
                if (procs[i].node_ip == 0) {
                    // Local: kill the scheduler task immediately, then clean up
                    stop_flags[i] = true;
                    if (procs[i].sched_slot >= 0) {
                        Scheduler::kill_task((uint32_t)procs[i].sched_slot);
                    }
                    // Reclaim user address space (if any). Safe because
                    // killing a user task swaps CR3 next schedule tick.
                    if (procs[i].cr3) {
                        VMM::destroy_address_space(procs[i].cr3);
                        procs[i].cr3 = 0;
                    }
                    procs[i].state = PROC_DEAD;
                    procs[i].sched_slot = -1;

                    char buf[16];
                    console_print("\n[PROC] Killed pid=");
                    itoa(pid, buf); console_print(buf);
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
                    // Tear down any lingering address space
                    if (procs[i].cr3) {
                        VMM::destroy_address_space(procs[i].cr3);
                        procs[i].cr3 = 0;
                    }
                    procs[i].state = PROC_UNUSED;
                    procs[i].pid = 0;
                }
            }

            Scheduler::sleep_ms(2000);
        }
    }

    // ================================================================
    // Syscalls
    // ================================================================

    int current_slot() {
        uint32_t sched = Scheduler::get_current();
        uint32_t pid = Scheduler::get_slot_pid(sched);
        if (!pid) return -1;
        for (uint32_t i = 0; i < MAX_PROCS; i++) {
            if (procs[i].pid == pid && procs[i].state != PROC_UNUSED) {
                return (int)i;
            }
        }
        return -1;
    }

    uint64_t sys_write(int fd, const char* buf, uint64_t len) {
        // fd 1 = stdout (console), fd 2 = stderr (also console)
        if (fd != 1 && fd != 2) return (uint64_t)-1;
        if (!buf || len == 0) return 0;

        // Cap length to avoid runaway writes
        if (len > 4096) len = 4096;

        for (uint64_t i = 0; i < len; i++) {
            char c = buf[i];
            if (c == 0) break;
            console_putc(c);
        }
        return len;
    }

    uint64_t sys_read(int fd, char* buf, uint64_t len) {
        (void)fd; (void)buf; (void)len;
        // Not yet implemented
        return 0;
    }

    int32_t sys_getpid() {
        int slot = current_slot();
        if (slot < 0) return -1;
        return (int32_t)procs[slot].pid;
    }

    uint64_t sys_exit(int32_t status, InterruptFrame* frame) {
        int slot = current_slot();
        if (slot >= 0) {
            procs[slot].exit_status = status;
            procs[slot].state = PROC_DEAD;

            char buf[16];
            console_print("\n[proc:");
            itoa(procs[slot].pid, buf); console_print(buf);
            console_print("] exit(");
            itoa((uint64_t)(uint32_t)status, buf); console_print(buf);
            console_print(")");

            // Mark the scheduler task dead. Do NOT free the address
            // space here -- we're still running on a kernel stack
            // inside it. The status reporter will reap it.
            uint32_t sched = Scheduler::get_current();
            (void)sched;
        }
        // Force a reschedule via the scheduler exit path
        return Scheduler::exit_current((uint64_t)frame);
    }

    uint64_t sys_sleep_ms(uint32_t ms, InterruptFrame* frame) {
        return Scheduler::sleep_ms_from_frame(ms, (uint64_t)frame);
    }

    int32_t sys_fork(InterruptFrame* parent_frame) {
        int parent_slot = current_slot();
        if (parent_slot < 0) return -1;
        if (procs[parent_slot].type != TYPE_USER) return -1;
        if (!procs[parent_slot].cr3) return -1;

        int child_slot = find_free();
        if (child_slot < 0) return -1;

        // Clone address space
        uint64_t child_cr3 = VMM::clone_address_space(procs[parent_slot].cr3);
        if (!child_cr3) return -1;

        // Fill in process slot
        uint32_t pid = next_pid++;
        procs[child_slot].pid = pid;
        procs[child_slot].type = TYPE_USER;
        procs[child_slot].state = PROC_RUNNING;
        procs[child_slot].node_ip = 0;
        procs[child_slot].owner_ip = Network::get_ip();
        procs[child_slot].started_at = ticks;
        procs[child_slot].cpu_ticks = 0;
        procs[child_slot].cr3 = child_cr3;
        procs[child_slot].exit_status = 0;
        stop_flags[child_slot] = false;

        // Copy name and add pid suffix
        char full_name[24];
        str_copy(full_name, procs[parent_slot].name, 16);
        int nlen = 0;
        while (full_name[nlen] && full_name[nlen] != ':') nlen++;
        full_name[nlen] = ':';
        char pbuf[8];
        itoa(pid, pbuf);
        int j = 0;
        while (pbuf[j] && nlen + 1 + j < 23) {
            full_name[nlen + 1 + j] = pbuf[j];
            j++;
        }
        full_name[nlen + 1 + j] = '\0';
        str_copy(procs[child_slot].name, full_name, 24);

        int sched = Scheduler::fork_user_task(full_name, child_cr3, parent_frame);
        if (sched < 0) {
            VMM::destroy_address_space(child_cr3);
            procs[child_slot].state = PROC_UNUSED;
            procs[child_slot].cr3 = 0;
            return -1;
        }
        procs[child_slot].sched_slot = sched;
        Scheduler::set_slot_pid((uint32_t)sched, pid);

        // Parent gets the child pid
        return (int32_t)pid;
    }

    int32_t sys_exec(const char* path, InterruptFrame* frame) {
        // Minimal exec: no filesystem-backed user-mode loader yet.
        // Accept pseudo-paths that map to embedded programs (see
        // userprog.cpp).
        int slot = current_slot();
        if (slot < 0) return -1;
        if (procs[slot].type != TYPE_USER) return -1;

        const uint8_t* bytes = nullptr;
        uint64_t len = 0;
        if (!path || !userprog_find(path, &bytes, &len)) return -1;

        uint64_t old_cr3 = procs[slot].cr3;
        if (!old_cr3) return -1;

        // Build a brand new address space, load the ELF + stack.
        uint64_t new_cr3 = VMM::create_address_space();
        if (!new_cr3) return -1;

        ELF::LoadResult r = ELF::load(new_cr3, bytes, len);
        if (!r.ok) {
            VMM::destroy_address_space(new_cr3);
            return -1;
        }
        if (!map_user_stack(new_cr3)) {
            VMM::destroy_address_space(new_cr3);
            return -1;
        }

        // Disable interrupts so no timer tick can try to switch
        // back to old_cr3 while we're mid-swap.
        uint64_t flags;
        asm volatile("pushfq; pop %0; cli" : "=r"(flags));

        // Retarget scheduler's cached cr3 BEFORE we switch, so any
        // apply_task_context() call after this point picks new_cr3.
        sched_retarget_current_cr3(new_cr3);
        procs[slot].cr3 = new_cr3;
        VMM::switch_to(new_cr3);

        // The kernel stack lives in the shared upper half, so
        // dropping the old address space now is safe.
        VMM::destroy_address_space(old_cr3);

        // Rewrite the iretq frame on the kernel stack to land at
        // the new entry point in ring 3.
        frame->rip    = r.entry;
        frame->rsp    = USER_STACK_TOP;
        frame->cs     = 0x3B;
        frame->ss     = 0x43;
        frame->rflags = 0x202;
        frame->rax = frame->rbx = frame->rcx = frame->rdx = 0;
        frame->rdi = frame->rsi = frame->rbp = 0;
        frame->r8  = frame->r9  = frame->r10 = frame->r11 = 0;
        frame->r12 = frame->r13 = frame->r14 = frame->r15 = 0;

        asm volatile("push %0; popfq" :: "r"(flags) : "memory");
        return 0;
    }
}

