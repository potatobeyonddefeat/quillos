[bits 64]

; External C++ handler
extern keyboard_handler_main

; Exported symbols for idt.cpp
global keyboard_handler_stub
global load_idt
global dummy_handler

section .text

keyboard_handler_stub:
    push rbp
    mov rbp, rsp
    
    ; Save caller-saved registers (The "Scratch" registers)
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11

    ; Align stack to 16-bytes for C++ ABI
    ; We save the old RSP to restore it later
    mov rax, rsp
    and rsp, -16
    push rax 

    call keyboard_handler_main

    ; Restore original RSP from the top of the stack
    pop rax
    mov rsp, rax
    
    ; Restore registers
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    
    pop rbp
    iretq

load_idt:
    lidt [rdi]    ; RDI holds the pointer to idt_ptr
    ret

dummy_handler:
    iretq
