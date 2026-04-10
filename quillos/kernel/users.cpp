#include "users.h"
#include "disk.h"

extern void console_print(const char* str);

// Persistent user storage on disk sector 100
static constexpr uint32_t USER_MAGIC  = 0x53524551;  // "QERS"
static constexpr uint32_t USER_SECTOR = 100;

namespace Users {

    static User users[MAX_USERS];
    static User* current_user = nullptr;

    static bool str_eq(const char* a, const char* b) {
        int i = 0;
        while (a[i] && b[i]) {
            if (a[i] != b[i]) return false;
            i++;
        }
        return a[i] == b[i];
    }

    static void str_copy(char* dst, const char* src, int max) {
        int i = 0;
        while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
        dst[i] = '\0';
    }

    static void mem_copy(void* dst, const void* src, uint64_t n) {
        uint8_t* d = (uint8_t*)dst;
        const uint8_t* s = (const uint8_t*)src;
        for (uint64_t i = 0; i < n; i++) d[i] = s[i];
    }

    static void mem_zero(void* dst, uint64_t n) {
        uint8_t* d = (uint8_t*)dst;
        for (uint64_t i = 0; i < n; i++) d[i] = 0;
    }

    // Load user table from disk sector USER_SECTOR.
    // Returns true if valid data was loaded.
    static bool load_from_disk() {
        if (!Disk::is_present()) return false;
        uint8_t sector[512];
        if (!Disk::read_sector(USER_SECTOR, sector)) return false;

        uint32_t magic = 0;
        mem_copy(&magic, sector, 4);
        if (magic != USER_MAGIC) return false;

        // Version check (reserved for future use)
        uint32_t version = 0;
        mem_copy(&version, sector + 4, 4);
        if (version != 1) return false;

        // Copy user entries
        uint64_t total_size = sizeof(User) * MAX_USERS;
        if (total_size > 512 - 8) total_size = 512 - 8;
        mem_copy(users, sector + 8, total_size);
        return true;
    }

    // Save user table to disk sector USER_SECTOR.
    static bool save_to_disk() {
        if (!Disk::is_present()) return false;
        uint8_t sector[512];
        mem_zero(sector, 512);

        uint32_t magic = USER_MAGIC;
        uint32_t version = 1;
        mem_copy(sector, &magic, 4);
        mem_copy(sector + 4, &version, 4);

        uint64_t total_size = sizeof(User) * MAX_USERS;
        if (total_size > 512 - 8) total_size = 512 - 8;
        mem_copy(sector + 8, users, total_size);

        return Disk::write_sector(USER_SECTOR, sector);
    }

    bool init() {
        for (uint32_t i = 0; i < MAX_USERS; i++) {
            users[i].active = false;
            users[i].name[0] = '\0';
            users[i].pass_hash = 0;
            users[i].usecase = USECASE_NONE;
            users[i].onboarded = false;
        }
        current_user = nullptr;

        // Try loading existing users from disk
        if (load_from_disk()) {
            console_print("\n[USERS] Loaded user table from disk (");
            char buf[8];
            uint32_t n = count();
            // inline itoa
            int i = 0;
            if (n == 0) buf[i++] = '0';
            char tmp[8]; int t = 0;
            uint32_t nn = n;
            while (nn > 0) { tmp[t++] = '0' + (nn % 10); nn /= 10; }
            while (t > 0) buf[i++] = tmp[--t];
            buf[i] = '\0';
            console_print(buf);
            console_print(" users)");
        } else {
            console_print("\n[USERS] User system ready (no saved users)");
        }
        return true;
    }

    // Public: force persist current table (called after updates)
    void persist() {
        save_to_disk();
    }

    // FNV-1a 64-bit hash — simple but sufficient for a hobby OS
    uint64_t hash_password(const char* password) {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (int i = 0; password[i]; i++) {
            hash ^= (uint64_t)(uint8_t)password[i];
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }

    bool create(const char* name, const char* password) {
        if (!name || !password || name[0] == '\0') return false;
        if (find(name)) return false; // Already exists

        for (uint32_t i = 0; i < MAX_USERS; i++) {
            if (!users[i].active) {
                str_copy(users[i].name, name, 32);
                users[i].pass_hash = hash_password(password);
                users[i].usecase = USECASE_NONE;
                users[i].onboarded = false;
                users[i].active = true;
                save_to_disk();  // Persist to disk
                return true;
            }
        }
        return false;
    }

    User* find(const char* name) {
        if (!name) return nullptr;
        for (uint32_t i = 0; i < MAX_USERS; i++) {
            if (users[i].active && str_eq(users[i].name, name)) {
                return &users[i];
            }
        }
        return nullptr;
    }

    bool verify(const char* name, const char* password) {
        User* u = find(name);
        if (!u) return false;
        return u->pass_hash == hash_password(password);
    }

    uint32_t count() {
        uint32_t c = 0;
        for (uint32_t i = 0; i < MAX_USERS; i++) {
            if (users[i].active) c++;
        }
        return c;
    }

    User* get_by_index(uint32_t idx) {
        uint32_t c = 0;
        for (uint32_t i = 0; i < MAX_USERS; i++) {
            if (users[i].active) {
                if (c == idx) return &users[i];
                c++;
            }
        }
        return nullptr;
    }

    User* current() { return current_user; }
    void set_current(User* u) { current_user = u; }
    void clear_current() { current_user = nullptr; }

    void set_usecase(User* u, UseCase uc) {
        if (u) {
            u->usecase = uc;
            save_to_disk();
        }
    }

    void complete_onboarding(User* u) {
        if (u) {
            u->onboarded = true;
            save_to_disk();
        }
    }

    const char* usecase_name(UseCase uc) {
        switch (uc) {
            case USECASE_DEV:     return "Developer";
            case USECASE_HOSTING: return "Hosting";
            case USECASE_CLUSTER: return "Cluster";
            case USECASE_DAILY:   return "Daily Driver";
            default:              return "None";
        }
    }
}
