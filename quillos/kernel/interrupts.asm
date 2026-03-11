[bits 64]
extern keyboard_handler_main
extern timer_handler_main

global keyboard_handler_stub
global timer_handler_stub
global load_idt
global dummy_handler

section .text

; Helper Macro to wrap hardware interrupt stubs
%macro HW_INTERRUPT_STUB 2
%1:
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
    sub rsp, 8          ; Re-align for 16-byte boundary
    and rsp, -16        

    call %2

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
%endmacro

HW_INTERRUPT_STUB keyboard_handler_stub, keyboard_handler_main
HW_INTERRUPT_STUB timer_handler_stub, timer_handler_main

load_idt:
    lidt [rdi]
    ret

dummy_handler:
    push rax
    mov al, 0x20
    out 0x20, al
    pop rax
    iretq
