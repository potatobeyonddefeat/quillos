[bits 64]
extern keyboard_handler_main
global keyboard_handler_stub
global load_idt
global dummy_handler

section .text

keyboard_handler_stub:
    ; Save all registers that might be used
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push rbp

    cld                 ; Clear direction flag for C ABI
    mov rbp, rsp
    sub rsp, 8          ; Re-align if necessary (Pushing 10 regs = 80 bytes,
                        ; plus 5 regs from CPU = 40 bytes. Total 120.
                        ; 120 is not div by 16, so we sub 8.)
    and rsp, -16        ; Force 16-byte alignment

    call keyboard_handler_main

    ; Restore original stack pointer and registers
    mov rsp, rbp
    pop rbp
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    iretq

load_idt:
    lidt [rdi]
    ret

dummy_handler:
    push rax
    mov al, 0x20
    out 0x20, al
    pop rax
    iretq
