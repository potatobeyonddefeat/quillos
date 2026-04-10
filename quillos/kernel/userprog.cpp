#include "userprog.h"
#include "process.h"
#include "vmm.h"
#include <stdint.h>
#include <stddef.h>

extern void console_print(const char* str);

// ================================================================
// Hand-assembled ring-3 test program.
//
// Layout (loaded at user VA 0x400000):
//   offset 0x000: code (47 bytes)
//   offset 0x100: string "Hello from ring 3 userspace!\n" (29 bytes)
//
// The code uses `movabs` to point RSI at 0x400100 directly, so
// we don't need any runtime relocation.
//
// Disassembly:
//   48 c7 c0 02 00 00 00       mov rax, 2        ; SYS_WRITE
//   48 c7 c7 01 00 00 00       mov rdi, 1        ; fd = stdout
//   48 be 00 01 40 00 00 00 00 00  movabs rsi, 0x400100
//   48 c7 c2 1d 00 00 00       mov rdx, 29       ; len
//   cd 80                      int 0x80          ; write(1, msg, 29)
//   48 c7 c0 01 00 00 00       mov rax, 1        ; SYS_EXIT
//   48 31 ff                   xor rdi, rdi      ; status = 0
//   cd 80                      int 0x80          ; exit(0)
//   eb fe                      jmp .             ; safety loop
// ================================================================

static constexpr uint64_t HELLO_LOAD_ADDR = 0x400000;
static constexpr uint64_t HELLO_MSG_ADDR  = 0x400100;

static const uint8_t hello_blob[] = {
    // offset 0x00: code
    0x48, 0xC7, 0xC0, 0x02, 0x00, 0x00, 0x00,   // mov rax, 2
    0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00,   // mov rdi, 1
    0x48, 0xBE, 0x00, 0x01, 0x40, 0x00,         // movabs rsi, 0x400100
    0x00, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC2, 0x1D, 0x00, 0x00, 0x00,   // mov rdx, 29
    0xCD, 0x80,                                  // int 0x80 (write)
    0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,   // mov rax, 1
    0x48, 0x31, 0xFF,                            // xor rdi, rdi
    0xCD, 0x80,                                  // int 0x80 (exit)
    0xEB, 0xFE,                                  // jmp .

    // offset 0x31 .. 0xFF: padding so the message lands at 0x100
    // (209 bytes of zeros)
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,  // 47..61 (15 bytes)
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 62..93
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 94..125
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 126..157
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 158..189
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 190..221
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 222..253
    0,0,                                                                // 254..255

    // offset 0x100: "Hello from ring 3 userspace!\n" (29 bytes)
    'H','e','l','l','o',' ','f','r','o','m',' ',
    'r','i','n','g',' ','3',' ',
    'u','s','e','r','s','p','a','c','e','!','\n',
};

// Compile-time size check: ensure the string is exactly at 0x100.
// 47 (code) + 209 (pad) + 29 (msg) = 285 = 0x11D
static_assert(sizeof(hello_blob) == 0x11D, "hello_blob size changed");

// ================================================================
// Tiny fork demo — parent and child each print a line then exit.
//
// Layout (loaded at 0x400000):
//   code at 0x000
//   "parent\n" at 0x200 (7 bytes)
//   "child\n"  at 0x210 (6 bytes)
//
// Disassembly:
//   48 c7 c0 04 00 00 00       mov rax, 4        ; SYS_FORK
//   cd 80                      int 0x80
//   48 85 c0                   test rax, rax
//   74 2f                      je .child         ; skip 47B -> child
//   .parent:
//   48 c7 c0 02 00 00 00       mov rax, 2
//   48 c7 c7 01 00 00 00       mov rdi, 1
//   48 be 00 02 40 00 00 00 00 00  movabs rsi, 0x400200
//   48 c7 c2 07 00 00 00       mov rdx, 7
//   cd 80                      int 0x80
//   48 c7 c0 01 00 00 00       mov rax, 1
//   48 31 ff                   xor rdi, rdi
//   cd 80                      int 0x80
//   eb fe                      jmp .
//   .child:
//   48 c7 c0 02 00 00 00       mov rax, 2
//   48 c7 c7 01 00 00 00       mov rdi, 1
//   48 be 10 02 40 00 00 00 00 00  movabs rsi, 0x400210
//   48 c7 c2 06 00 00 00       mov rdx, 6
//   cd 80                      int 0x80
//   48 c7 c0 01 00 00 00       mov rax, 1
//   48 31 ff                   xor rdi, rdi
//   cd 80                      int 0x80
//   eb fe                      jmp .
// ================================================================

static const uint8_t forker_blob[] = {
    // parent path
    0x48, 0xC7, 0xC0, 0x04, 0x00, 0x00, 0x00,   // mov rax, 4 (SYS_FORK)
    0xCD, 0x80,                                  // int 0x80
    0x48, 0x85, 0xC0,                            // test rax, rax
    0x74, 0x2F,                                  // je .child  (skip 47 bytes)

    0x48, 0xC7, 0xC0, 0x02, 0x00, 0x00, 0x00,   // mov rax, 2
    0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00,   // mov rdi, 1
    0x48, 0xBE, 0x00, 0x02, 0x40, 0x00,         // movabs rsi, 0x400200
    0x00, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC2, 0x07, 0x00, 0x00, 0x00,   // mov rdx, 7
    0xCD, 0x80,                                  // int 0x80
    0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,   // mov rax, 1
    0x48, 0x31, 0xFF,                            // xor rdi, rdi
    0xCD, 0x80,                                  // int 0x80
    0xEB, 0xFE,                                  // jmp .
    // .child starts here (byte offset 0x3D)
    0x48, 0xC7, 0xC0, 0x02, 0x00, 0x00, 0x00,   // mov rax, 2
    0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00,   // mov rdi, 1
    0x48, 0xBE, 0x10, 0x02, 0x40, 0x00,         // movabs rsi, 0x400210
    0x00, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC2, 0x06, 0x00, 0x00, 0x00,   // mov rdx, 6
    0xCD, 0x80,                                  // int 0x80
    0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,   // mov rax, 1
    0x48, 0x31, 0xFF,                            // xor rdi, rdi
    0xCD, 0x80,                                  // int 0x80
    0xEB, 0xFE,                                  // jmp .

    // Pad out to 0x200 (code so far is 108 bytes; need 500 bytes zero)
    // 0x200 - 108 = 404 padding bytes
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 32
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 64
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 96
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 128
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 160
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 192
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 224
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 256
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 288
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 320
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 352
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 384
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,                          // 404

    // offset 0x200: "parent\n" (7 bytes)
    'p','a','r','e','n','t','\n',

    // 9 bytes of padding to reach 0x210
    0,0,0,0,0,0,0,0,0,

    // offset 0x210: "child\n" (6 bytes)
    'c','h','i','l','d','\n',
};

static_assert(sizeof(forker_blob) == 0x216, "forker_blob size changed");

// ================================================================
// Public API
// ================================================================

namespace UserProg {

    uint32_t spawn_hello() {
        return Process::spawn_user_blob("hello", hello_blob,
                                        sizeof(hello_blob),
                                        HELLO_LOAD_ADDR);
    }

    uint32_t spawn_ticker() {
        // Same program as hello for now; distinct name for clarity.
        // A proper ticker would loop but we keep the machine code
        // small until we ship a userland toolchain.
        return Process::spawn_user_blob("ticker", hello_blob,
                                        sizeof(hello_blob),
                                        HELLO_LOAD_ADDR);
    }

    uint32_t spawn_forker() {
        return Process::spawn_user_blob("forker", forker_blob,
                                        sizeof(forker_blob),
                                        HELLO_LOAD_ADDR);
    }

} // namespace UserProg

// ================================================================
// Pseudo-path lookup for sys_exec(). Called by Process::sys_exec
// when a user program issues an exec() syscall.
// ================================================================

extern "C" bool userprog_find(const char* name,
                              const uint8_t** out_bytes,
                              uint64_t* out_len) {
    if (!name || !out_bytes || !out_len) return false;
    auto eq = [](const char* a, const char* b) {
        int i = 0;
        while (a[i] && b[i]) { if (a[i] != b[i]) return false; i++; }
        return a[i] == b[i];
    };
    if (eq(name, "/bin/hello") || eq(name, "hello")) {
        *out_bytes = hello_blob;
        *out_len   = sizeof(hello_blob);
        return true;
    }
    if (eq(name, "/bin/forker") || eq(name, "forker")) {
        *out_bytes = forker_blob;
        *out_len   = sizeof(forker_blob);
        return true;
    }
    return false;
}
