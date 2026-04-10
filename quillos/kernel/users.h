#pragma once
#include <stdint.h>

namespace Users {

    enum UseCase : uint8_t {
        USECASE_NONE    = 0,
        USECASE_DEV     = 1,
        USECASE_HOSTING = 2,
        USECASE_CLUSTER = 3,
        USECASE_DAILY   = 4,
    };

    struct User {
        char     name[32];
        uint64_t pass_hash;
        UseCase  usecase;
        bool     onboarded;
        bool     active;
    };

    static constexpr uint32_t MAX_USERS = 8;

    bool init();

    // Password hashing (FNV-1a 64-bit)
    uint64_t hash_password(const char* password);

    // User management
    bool create(const char* name, const char* password);
    User* find(const char* name);
    bool verify(const char* name, const char* password);
    uint32_t count();
    User* get_by_index(uint32_t idx);

    // Current user session
    User* current();
    void set_current(User* u);
    void clear_current();

    // Onboarding
    void set_usecase(User* u, UseCase uc);
    void complete_onboarding(User* u);
    const char* usecase_name(UseCase uc);
}
