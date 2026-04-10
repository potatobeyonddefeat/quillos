#include "users.h"

extern void console_print(const char* str);

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

    bool init() {
        for (uint32_t i = 0; i < MAX_USERS; i++) {
            users[i].active = false;
            users[i].name[0] = '\0';
            users[i].pass_hash = 0;
            users[i].usecase = USECASE_NONE;
            users[i].onboarded = false;
        }
        current_user = nullptr;
        console_print("\n[USERS] User system ready");
        return true;
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
        if (u) u->usecase = uc;
    }

    void complete_onboarding(User* u) {
        if (u) u->onboarded = true;
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
