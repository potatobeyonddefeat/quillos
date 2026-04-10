#include "shell.h"
#include "io.h"
#include "idt.h"
#include "filesystem.h"
#include "memory.h"
#include "scheduler.h"
#include "pci.h"
#include "disk.h"
#include "e1000.h"
#include "network.h"
#include "cluster.h"

extern void console_print(const char* str);
extern void console_clear();
extern void console_backspace();
extern void console_putc(char c);
extern void set_bg_color(uint32_t color);
extern void itoa(uint64_t n, char* str);
extern volatile uint64_t ticks;

// Hex helper for PCI output
static void itoa_hex(uint32_t n, char* str) {
    const char* hex = "0123456789ABCDEF";
    str[0] = '0'; str[1] = 'x';
    for (int i = 3; i >= 0; i--) {
        str[2 + (3 - i)] = hex[(n >> (i * 4)) & 0xF];
    }
    str[6] = '\0';
}

// Global filesystem instance
QuillFS::Filesystem g_fs;

#define kprint console_print
#define SHELL_BUFFER_SIZE 256
char shell_buffer[SHELL_BUFFER_SIZE];
int shell_ptr = 0;

// Current working directory
static char cwd[QuillFS::MAX_PATH_LEN] = "/";

// Internal safe string length
int safe_strlen(const char* s) {
    int len = 0;
    while (s[len] != '\0') len++;
    return len;
}

// Internal safe string compare
bool safe_compare(const char* a, const char* b) {
    int i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return false;
        i++;
    }
    return a[i] == b[i];
}

static void safe_strcpy(char* dst, const char* src) {
    int i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void safe_strcat(char* dst, const char* src) {
    int len = safe_strlen(dst);
    int i = 0;
    while (src[i]) { dst[len + i] = src[i]; i++; }
    dst[len + i] = '\0';
}

// Build full path from cwd + relative name
static void build_path(const char* name, char* out) {
    if (name[0] == '/') {
        // Absolute path
        safe_strcpy(out, name);
    } else {
        // Relative to cwd
        safe_strcpy(out, cwd);
        if (!safe_compare(cwd, "/")) {
            safe_strcat(out, "/");
        }
        safe_strcat(out, name);
    }
}

void shell_init() {
    shell_ptr = 0;
    for(int i = 0; i < SHELL_BUFFER_SIZE; i++) shell_buffer[i] = 0;

    // Initialize the filesystem
    if (g_fs.init()) {
        kprint("\nQuillOS v0.1 - Filesystem ready\n> ");
    } else {
        kprint("\nQuillOS v0.1 - Filesystem error\n> ");
    }
}

void process_command(char* input) {
    char cmd[64];
    char arg[192];

    // Zero out local buffers
    for(int x = 0; x < 64; x++) cmd[x] = 0;
    for(int x = 0; x < 192; x++) arg[x] = 0;

    int i = 0;
    while (input[i] == ' ' && input[i] != '\0') i++;

    int c_idx = 0;
    while (input[i] != ' ' && input[i] != '\0' && c_idx < 63) {
        cmd[c_idx++] = input[i++];
    }
    cmd[c_idx] = '\0';

    while (input[i] == ' ' && input[i] != '\0') i++;

    int a_idx = 0;
    while (input[i] != '\0' && a_idx < 191) {
        arg[a_idx++] = input[i++];
    }
    arg[a_idx] = '\0';

    if (safe_strlen(cmd) == 0) return;

    if (safe_compare(cmd, "help")) {
        kprint("\nCommands:");
        kprint("\n  help          - Show this help");
        kprint("\n  cls           - Clear screen");
        kprint("\n  ver           - Show version");
        kprint("\n  uptime        - Show uptime");
        kprint("\n  halt          - Halt system");
        kprint("\n  reboot        - Reboot system");
        kprint("\n  color <name>  - Change background");
        kprint("\n  ls [path]     - List directory");
        kprint("\n  mkdir <name>  - Create directory");
        kprint("\n  touch <name>  - Create file");
        kprint("\n  cat <name>    - Read file");
        kprint("\n  write <name> <text> - Write to file");
        kprint("\n  rm <name>     - Remove file/dir");
        kprint("\n  pwd           - Print working dir");
        kprint("\n  cd <path>     - Change directory");
        kprint("\n  meminfo       - Memory statistics");
        kprint("\n  ps            - List tasks");
        kprint("\n  lspci         - List PCI devices");
        kprint("\n  diskinfo      - Disk drive info");
        kprint("\n  readsec <lba> - Read sector as ASCII");
        kprint("\n  hexdump <lba> - Hex dump a sector");
        kprint("\n  netinfo       - Network/NIC status");
        kprint("\n  discover      - Broadcast cluster discovery");
        kprint("\n  peers         - List cluster peers");
        kprint("\n  cluster       - Cluster overview");
        kprint("\n  rjob <ip> <n1 n2..> - Send sum job to peer");
        kprint("\n  intinfo       - Interrupt statistics");
        kprint("\n  heapinfo      - Heap allocator stats");
        kprint("\n  heaptest      - Test kmalloc/kfree");
        kprint("\n  spawn <name>  - Spawn a test task");
        kprint("\n  kill <slot>   - Kill task by slot");
    }
    else if (safe_compare(cmd, "cls")) {
        console_clear();
        shell_init();
    }
    else if (safe_compare(cmd, "ver")) {
        kprint("\nQuillOS v0.1");
    }
    else if (safe_compare(cmd, "uptime")) {
        char time_buf[32];
        itoa(ticks / 1000, time_buf);
        kprint("\nUptime: ");
        kprint(time_buf);
        kprint(" seconds.");
    }
    else if (safe_compare(cmd, "color")) {
        if (safe_compare(arg, "red")) set_bg_color(0xFF0000);
        else if (safe_compare(arg, "blue")) set_bg_color(0x0000FF);
        else if (safe_compare(arg, "green")) set_bg_color(0x00FF00);
        else if (safe_compare(arg, "dark")) set_bg_color(0x111111);
        else { kprint("\nTry: red, blue, green, or dark"); goto done; }

        console_clear();
        kprint("\nBackground updated.");
    }
    else if (safe_compare(cmd, "pwd")) {
        kprint("\n");
        kprint(cwd);
    }
    else if (safe_compare(cmd, "cd")) {
        if (safe_strlen(arg) == 0) {
            safe_strcpy(cwd, "/");
        } else if (safe_compare(arg, "/")) {
            safe_strcpy(cwd, "/");
        } else {
            // For now, only support absolute paths and simple names
            char path[QuillFS::MAX_PATH_LEN] = {0};
            build_path(arg, path);

            // Verify it's a valid directory by trying to ls it
            char tmp[16];
            int result = g_fs.ls(path, tmp, sizeof(tmp));
            // Also check if the path itself is root
            if (safe_compare(path, "/") || result >= 0) {
                safe_strcpy(cwd, path);
            } else {
                kprint("\nNo such directory: ");
                kprint(arg);
            }
        }
    }
    else if (safe_compare(cmd, "ls")) {
        char path[QuillFS::MAX_PATH_LEN] = {0};
        if (safe_strlen(arg) > 0) {
            build_path(arg, path);
        } else {
            safe_strcpy(path, cwd);
        }

        char listing[1024] = {0};
        int count = g_fs.ls(path, listing, sizeof(listing));
        if (count < 0) {
            kprint("\nCannot list: ");
            kprint(path);
        } else if (count == 0) {
            kprint("\n(empty)");
        } else {
            kprint("\n");
            kprint(listing);
        }
    }
    else if (safe_compare(cmd, "mkdir")) {
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: mkdir <name>");
        } else {
            char path[QuillFS::MAX_PATH_LEN] = {0};
            build_path(arg, path);
            if (g_fs.mkdir(path)) {
                kprint("\nCreated directory: ");
                kprint(arg);
            } else {
                kprint("\nFailed to create directory: ");
                kprint(arg);
            }
        }
    }
    else if (safe_compare(cmd, "touch")) {
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: touch <name>");
        } else {
            char path[QuillFS::MAX_PATH_LEN] = {0};
            build_path(arg, path);
            if (g_fs.touch(path)) {
                kprint("\nCreated file: ");
                kprint(arg);
            } else {
                kprint("\nFailed to create file: ");
                kprint(arg);
            }
        }
    }
    else if (safe_compare(cmd, "cat")) {
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: cat <name>");
        } else {
            char path[QuillFS::MAX_PATH_LEN] = {0};
            build_path(arg, path);
            char content[QuillFS::MAX_FILE_DATA] = {0};
            if (g_fs.read_file(path, content, sizeof(content))) {
                kprint("\n");
                if (safe_strlen(content) > 0) {
                    kprint(content);
                } else {
                    kprint("(empty file)");
                }
            } else {
                kprint("\nCannot read: ");
                kprint(arg);
            }
        }
    }
    else if (safe_compare(cmd, "write")) {
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: write <name> <content>");
        } else {
            // Split arg into filename and content at first space
            char filename[64] = {0};
            char content[128] = {0};
            int j = 0;
            while (arg[j] != ' ' && arg[j] != '\0' && j < 63) {
                filename[j] = arg[j];
                j++;
            }
            filename[j] = '\0';

            // Skip spaces
            while (arg[j] == ' ') j++;

            int k = 0;
            while (arg[j] != '\0' && k < 127) {
                content[k++] = arg[j++];
            }
            content[k] = '\0';

            if (safe_strlen(filename) == 0 || safe_strlen(content) == 0) {
                kprint("\nUsage: write <name> <content>");
            } else {
                char path[QuillFS::MAX_PATH_LEN] = {0};
                build_path(filename, path);
                if (g_fs.write_file(path, content)) {
                    kprint("\nWrote to: ");
                    kprint(filename);
                } else {
                    kprint("\nFailed to write: ");
                    kprint(filename);
                }
            }
        }
    }
    else if (safe_compare(cmd, "rm")) {
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: rm <name>");
        } else {
            char path[QuillFS::MAX_PATH_LEN] = {0};
            build_path(arg, path);
            if (g_fs.rm(path)) {
                kprint("\nRemoved: ");
                kprint(arg);
            } else {
                kprint("\nFailed to remove: ");
                kprint(arg);
            }
        }
    }
    else if (safe_compare(cmd, "meminfo")) {
        char buf[32];
        kprint("\nPhysical Memory (PMM):");
        kprint("\n  Total:  "); itoa(Memory::pmm_total_pages(), buf); kprint(buf); kprint(" pages (");
        itoa(Memory::pmm_total_bytes() / 1024, buf); kprint(buf); kprint(" KB)");
        kprint("\n  Used:   "); itoa(Memory::pmm_used_pages(), buf); kprint(buf); kprint(" pages");
        kprint("\n  Free:   "); itoa(Memory::pmm_free_pages(), buf); kprint(buf); kprint(" pages");
        kprint("\nKernel Heap:");
        kprint("\n  Size:   "); itoa(Memory::heap_total_size() / 1024, buf); kprint(buf); kprint(" KB");
        kprint("\n  Used:   "); itoa(Memory::heap_allocated_bytes(), buf); kprint(buf); kprint(" bytes (");
        itoa(Memory::heap_alloc_count(), buf); kprint(buf); kprint(" allocs)");
        kprint("\n  Free:   "); itoa(Memory::heap_free_bytes(), buf); kprint(buf); kprint(" bytes");
        kprint("\n  Blocks: "); itoa(Memory::heap_block_count(), buf); kprint(buf);
        kprint("\n  Largest free: "); itoa(Memory::heap_largest_free(), buf); kprint(buf); kprint(" bytes");
    }
    else if (safe_compare(cmd, "heapinfo")) {
        char buf[32];
        kprint("\nHeap Allocator Details:");
        kprint("\n  Committed:     "); itoa(Memory::heap_total_size(), buf); kprint(buf); kprint(" bytes");
        kprint("\n  Allocated:     "); itoa(Memory::heap_allocated_bytes(), buf); kprint(buf); kprint(" bytes");
        kprint("\n  Free:          "); itoa(Memory::heap_free_bytes(), buf); kprint(buf); kprint(" bytes");
        kprint("\n  Live allocs:   "); itoa(Memory::heap_alloc_count(), buf); kprint(buf);
        kprint("\n  Total blocks:  "); itoa(Memory::heap_block_count(), buf); kprint(buf);
        kprint("\n  Largest free:  "); itoa(Memory::heap_largest_free(), buf); kprint(buf); kprint(" bytes");
    }
    else if (safe_compare(cmd, "heaptest")) {
        kprint("\nRunning heap allocator test...");
        char buf[32];

        // Test 1: Basic alloc/free
        kprint("\n  Test 1: kmalloc(64)...");
        void* p1 = Memory::kmalloc(64);
        if (p1) { kprint(" OK"); } else { kprint(" FAIL"); }

        kprint("\n  Test 2: kmalloc(256)...");
        void* p2 = Memory::kmalloc(256);
        if (p2) { kprint(" OK"); } else { kprint(" FAIL"); }

        kprint("\n  Test 3: kmalloc(1024)...");
        void* p3 = Memory::kmalloc(1024);
        if (p3) { kprint(" OK"); } else { kprint(" FAIL"); }

        kprint("\n  Allocs live: "); itoa(Memory::heap_alloc_count(), buf); kprint(buf);

        // Test 4: Free middle block (tests coalescing won't corrupt)
        kprint("\n  Test 4: kfree(p2)...");
        Memory::kfree(p2);
        kprint(" OK");

        // Test 5: Re-allocate into freed space
        kprint("\n  Test 5: kmalloc(128) into freed space...");
        void* p4 = Memory::kmalloc(128);
        if (p4) { kprint(" OK"); } else { kprint(" FAIL"); }

        // Test 6: Free everything
        kprint("\n  Test 6: Free all...");
        Memory::kfree(p1);
        Memory::kfree(p3);
        Memory::kfree(p4);
        kprint(" OK");

        kprint("\n  Allocs live: "); itoa(Memory::heap_alloc_count(), buf); kprint(buf);
        kprint(" (should be 0)");

        // Test 7: PMM page alloc/free
        kprint("\n  Test 7: pmm_alloc_page...");
        uint64_t phys = Memory::pmm_alloc_page();
        if (phys) {
            kprint(" OK (phys=");
            itoa(phys, buf); kprint(buf);
            kprint(")");
            Memory::pmm_free_page(phys);
            kprint(" freed");
        } else {
            kprint(" FAIL");
        }

        kprint("\n  All tests passed!");
    }
    else if (safe_compare(cmd, "ps")) {
        char buf[32];
        kprint("\nTasks ("); itoa(Scheduler::get_count(), buf); kprint(buf); kprint(" active):");
        kprint("\n  SLOT  ID  STATE     TICKS   NAME");
        for (uint32_t t = 0; t < Scheduler::MAX_TASKS; t++) {
            const Scheduler::Task* task = Scheduler::get_task(t);
            if (!task || task->state == Scheduler::TASK_UNUSED) continue;
            kprint("\n  ");
            itoa(t, buf); kprint(buf);
            kprint("     ");
            itoa(task->id, buf); kprint(buf);
            kprint("   ");
            if (task->state == Scheduler::TASK_RUNNING)  kprint("running  ");
            else if (task->state == Scheduler::TASK_READY) kprint("ready    ");
            else if (task->state == Scheduler::TASK_SLEEPING) kprint("sleeping ");
            else if (task->state == Scheduler::TASK_DEAD)  kprint("dead     ");
            itoa(task->ticks_used, buf); kprint(buf);
            kprint("  ");
            kprint(task->name);
            if (t == Scheduler::get_current()) kprint(" *");
        }
    }
    else if (safe_compare(cmd, "spawn")) {
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: spawn <name>");
            kprint("\n  Available: counter, busy, sleeper");
        } else {
            kprint("\nSpawning not yet available from shell");
            kprint("\n(Tasks are created programmatically for now)");
        }
    }
    else if (safe_compare(cmd, "kill")) {
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: kill <slot>");
        } else {
            // Parse slot number
            uint32_t slot = 0;
            int j = 0;
            while (arg[j] >= '0' && arg[j] <= '9') {
                slot = slot * 10 + (arg[j] - '0');
                j++;
            }
            if (Scheduler::kill_task(slot)) {
                kprint("\nKilled task in slot ");
                kprint(arg);
            } else {
                kprint("\nCannot kill slot ");
                kprint(arg);
                kprint(" (invalid, kernel, idle, or already dead)");
            }
        }
    }
    else if (safe_compare(cmd, "lspci")) {
        char buf[32];
        uint32_t count = PCI::get_count();
        kprint("\nPCI Devices ("); itoa(count, buf); kprint(buf); kprint("):");
        kprint("\n  BUS:DEV  VENDOR  DEVICE  CLASS");
        for (uint32_t p = 0; p < count; p++) {
            const PCI::Device* dev = PCI::get_device(p);
            if (!dev) continue;
            kprint("\n  ");
            itoa(dev->bus, buf); kprint(buf); kprint(":");
            itoa(dev->device, buf); kprint(buf);
            kprint("    ");
            itoa_hex(dev->vendor_id, buf); kprint(buf);
            kprint("  ");
            itoa_hex(dev->device_id, buf); kprint(buf);
            kprint("  ");
            itoa_hex(dev->class_code, buf); kprint(buf);
            kprint(":");
            itoa_hex(dev->subclass, buf); kprint(buf);
        }
    }
    else if (safe_compare(cmd, "diskinfo")) {
        const Disk::BlockDevice* info = Disk::get_info();
        if (info && info->present) {
            char buf[32];
            kprint("\nATA Disk (Primary Bus, PIO Mode):");
            kprint("\n  Model:   "); kprint(info->model);
            kprint("\n  Serial:  "); kprint(info->serial);
            kprint("\n  Sectors: "); itoa(info->total_sectors, buf); kprint(buf);
            kprint("\n  Size:    "); itoa(info->size_mb, buf); kprint(buf); kprint(" MB");
            kprint("\n  LBA:     "); kprint(info->lba_supported ? "yes" : "no");
        } else {
            kprint("\nNo ATA drive detected");
        }
    }
    else if (safe_compare(cmd, "readsec")) {
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: readsec <lba>");
        } else {
            uint32_t lba = 0;
            int j = 0;
            while (arg[j] >= '0' && arg[j] <= '9') {
                lba = lba * 10 + (uint32_t)(arg[j] - '0');
                j++;
            }
            uint8_t sector[512];
            if (Disk::read_sector(lba, sector)) {
                char buf[32];
                kprint("\nSector "); itoa(lba, buf); kprint(buf);
                kprint(" (first 128 bytes as ASCII):\n  ");
                for (int b = 0; b < 128; b++) {
                    char c = (char)sector[b];
                    if (c >= 32 && c < 127) {
                        console_putc(c);
                    } else {
                        console_putc('.');
                    }
                    if ((b + 1) % 64 == 0) kprint("\n  ");
                }
            } else {
                kprint("\nFailed to read sector ");
                kprint(arg);
            }
        }
    }
    else if (safe_compare(cmd, "hexdump")) {
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: hexdump <lba>");
        } else {
            uint32_t lba = 0;
            int j = 0;
            while (arg[j] >= '0' && arg[j] <= '9') {
                lba = lba * 10 + (uint32_t)(arg[j] - '0');
                j++;
            }
            uint8_t sector[512];
            if (Disk::read_sector(lba, sector)) {
                char buf[32];
                const char* hex = "0123456789ABCDEF";
                kprint("\nSector "); itoa(lba, buf); kprint(buf); kprint(":");
                // Show first 256 bytes (16 rows of 16 bytes)
                for (int row = 0; row < 16; row++) {
                    kprint("\n  ");
                    // Offset
                    itoa_hex((uint32_t)(row * 16), buf); kprint(buf); kprint(": ");
                    // Hex bytes
                    for (int col = 0; col < 16; col++) {
                        uint8_t b = sector[row * 16 + col];
                        char h[3];
                        h[0] = hex[b >> 4];
                        h[1] = hex[b & 0xF];
                        h[2] = '\0';
                        kprint(h);
                        kprint(" ");
                        if (col == 7) kprint(" "); // Gap in middle
                    }
                    kprint(" ");
                    // ASCII
                    for (int col = 0; col < 16; col++) {
                        char c = (char)sector[row * 16 + col];
                        if (c >= 32 && c < 127) console_putc(c);
                        else console_putc('.');
                    }
                }
            } else {
                kprint("\nFailed to read sector ");
                kprint(arg);
            }
        }
    }
    else if (safe_compare(cmd, "netinfo")) {
        if (Network::is_present()) {
            char buf[32];
            kprint("\nNetwork (E1000 NIC):");
            uint32_t ip_n = Network::get_ip();
            uint32_t ip_h = Network::ntohl(ip_n);
            kprint("\n  IP:   ");
            itoa((ip_h >> 24) & 0xFF, buf); kprint(buf); kprint(".");
            itoa((ip_h >> 16) & 0xFF, buf); kprint(buf); kprint(".");
            itoa((ip_h >> 8) & 0xFF, buf); kprint(buf); kprint(".");
            itoa(ip_h & 0xFF, buf); kprint(buf);
            const uint8_t* mac = Network::get_mac();
            kprint("\n  MAC:  ");
            const char* hex = "0123456789ABCDEF";
            for (int m = 0; m < 6; m++) {
                char h[4]; h[0] = hex[mac[m]>>4]; h[1] = hex[mac[m]&0xF];
                h[2] = (m < 5) ? ':' : '\0'; h[3] = '\0';
                kprint(h);
            }
            kprint("\n  Sent: "); itoa(Network::get_packets_sent(), buf); kprint(buf);
            kprint("\n  Recv: "); itoa(Network::get_packets_received(), buf); kprint(buf);
        } else {
            kprint("\nNetwork: Not available");
        }
    }
    else if (safe_compare(cmd, "peers")) {
        char buf[32];
        uint32_t cnt = Cluster::get_peer_count();
        kprint("\nCluster peers ("); itoa(cnt, buf); kprint(buf); kprint("):");
        if (cnt == 0) {
            kprint("\n  No peers discovered yet. Try 'discover'.");
        }
        for (uint32_t p = 0; p < cnt; p++) {
            const Cluster::Node* peer = Cluster::get_peer(p);
            if (!peer) continue;
            kprint("\n  "); kprint(peer->name); kprint("  ");
            uint32_t ip_h = Network::ntohl(peer->ip);
            itoa((ip_h >> 24) & 0xFF, buf); kprint(buf); kprint(".");
            itoa((ip_h >> 16) & 0xFF, buf); kprint(buf); kprint(".");
            itoa((ip_h >> 8) & 0xFF, buf); kprint(buf); kprint(".");
            itoa(ip_h & 0xFF, buf); kprint(buf);
        }
    }
    else if (safe_compare(cmd, "discover")) {
        Cluster::send_discover();
        kprint("\nDiscovery broadcast sent. Use 'peers' to see results.");
    }
    else if (safe_compare(cmd, "cluster")) {
        char buf[32];
        kprint("\nCluster Status:");
        kprint("\n  Local: "); kprint(Cluster::get_local_name());
        uint32_t ip_h = Network::ntohl(Cluster::get_local_ip());
        kprint(" (");
        itoa((ip_h >> 24) & 0xFF, buf); kprint(buf); kprint(".");
        itoa((ip_h >> 16) & 0xFF, buf); kprint(buf); kprint(".");
        itoa((ip_h >> 8) & 0xFF, buf); kprint(buf); kprint(".");
        itoa(ip_h & 0xFF, buf); kprint(buf);
        kprint(")");
        kprint("\n  Peers: "); itoa(Cluster::get_peer_count(), buf); kprint(buf);
        const Cluster::JobResult* jr = Cluster::get_last_result();
        if (jr && jr->completed) {
            kprint("\n  Last job result: "); itoa(jr->result, buf); kprint(buf);
        }
    }
    else if (safe_compare(cmd, "rjob")) {
        // Usage: rjob <ip_last_octet> <numbers...>
        // Example: rjob 2 10 20 30  -> sends sum job to 10.0.0.2
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: rjob <node_ip_last_byte> <n1> <n2> ...");
            kprint("\n  Sends a sum job to the target node.");
            kprint("\n  Example: rjob 2 100 200 300");
        } else {
            // Parse target IP last octet
            int j = 0;
            uint32_t target_last = 0;
            while (arg[j] >= '0' && arg[j] <= '9') {
                target_last = target_last * 10 + (uint32_t)(arg[j] - '0');
                j++;
            }
            while (arg[j] == ' ') j++;

            // Parse numbers for the sum job
            uint32_t numbers[16];
            int num_count = 0;
            while (arg[j] && num_count < 16) {
                uint32_t val = 0;
                while (arg[j] >= '0' && arg[j] <= '9') {
                    val = val * 10 + (uint32_t)(arg[j] - '0');
                    j++;
                }
                numbers[num_count++] = val;
                while (arg[j] == ' ') j++;
            }

            if (num_count == 0) {
                kprint("\nNo numbers provided for sum job");
            } else {
                uint32_t target_ip = Network::htonl(0x0A000000 | target_last);
                char buf[16];
                kprint("\nSending sum job (");
                itoa(num_count, buf); kprint(buf);
                kprint(" values) to 10.0.0.");
                itoa(target_last, buf); kprint(buf);

                if (Cluster::submit_job(target_ip, 1, (const uint8_t*)numbers, num_count * 4)) {
                    kprint(" ... sent!");
                } else {
                    kprint(" ... FAILED (ARP not resolved? Try 'discover' first)");
                }
            }
        }
    }
    else if (safe_compare(cmd, "intinfo")) {
        char buf[32];
        kprint("\nInterrupt System:");
        kprint("\n  CPU exceptions caught: ");
        itoa(isr_get_total_exceptions(), buf); kprint(buf);
        kprint("\n  IRQ counts:");
        const char* irq_names[] = {
            "Timer", "Keyboard", "Cascade", "COM2",
            "COM1", "LPT2", "Floppy", "LPT1/Spurious",
            "RTC", "ACPI", "Open", "Open",
            "Mouse", "FPU", "ATA Primary", "ATA Secondary"
        };
        for (int irq = 0; irq < 16; irq++) {
            uint64_t count = irq_get_count((uint8_t)irq);
            if (count > 0) {
                kprint("\n    IRQ ");
                itoa(irq, buf); kprint(buf);
                kprint(" ("); kprint(irq_names[irq]); kprint("): ");
                itoa(count, buf); kprint(buf);
            }
        }
    }
    else if (safe_compare(cmd, "reboot")) {
        kprint("\nRebooting...");
        for (int j = 0; j < 100; j++) {
            outb(0x64, 0xFE);
        }

        struct {
            uint16_t limit;
            uint64_t base;
        } __attribute__((packed)) invalid_idtr = {0, 0};

        asm volatile("lidt %0; int3" :: "m"(invalid_idtr));
        return;
    }
    else if (safe_compare(cmd, "halt")) {
        kprint("\nHalting...");
        outb(0xf4, 0x10);
        asm volatile("cli; hlt");
    }
    else {
        kprint("\nUnknown command. Type 'help' for commands.");
    }

done:
    (void)0; // Label needs a statement
}

void shell_update(char c) {
    if (c == '\n') {
        shell_buffer[shell_ptr] = '\0';
        process_command(shell_buffer);
        shell_ptr = 0;
        for(int i = 0; i < SHELL_BUFFER_SIZE; i++) shell_buffer[i] = 0;
        kprint("\n> ");
    } else if (c == '\b') {
        if (shell_ptr > 0) {
            shell_ptr--;
            shell_buffer[shell_ptr] = '\0';
            console_backspace();
        }
    } else {
        if (shell_ptr < SHELL_BUFFER_SIZE - 1) {
            shell_buffer[shell_ptr++] = c;
            console_putc(c);
        }
    }
}
