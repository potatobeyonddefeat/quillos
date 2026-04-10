[bits 64]

; ============================================================
; QuillOS x86-64 Interrupt Stubs
;
; Layout:  32 ISR stubs (CPU exceptions, INT 0-31)
;        + 16 IRQ stubs (hardware interrupts, INT 32-47)
;        + isr_common / irq_common (save regs, call C dispatch)
;        + load_idt
;        + context_switch (for scheduler)
;
; Stack frame after common handler pushes (grows downward):
;   [r15] [r14] ... [rax]  <- pushed by common handler
;   [int_no] [error_code]  <- pushed by stub
;   [rip] [cs] [rflags] [rsp] [ss]  <- pushed by CPU
;
; This matches the InterruptFrame struct in idt.h exactly.
; ============================================================

extern isr_dispatch         ; C handler for CPU exceptions
extern irq_dispatch         ; C handler for hardware IRQs (returns new RSP)
extern syscall_dispatch     ; C handler for INT 0x80 syscall (returns new RSP)

global load_idt
global context_switch
global isr_stub_table
global sched_yield_stub
global gdt_flush
global tss_flush
global enter_usermode

section .text

; ============================================================
; ISR stubs — CPU Exceptions (INT 0-31)
;
; Some exceptions push an error code automatically.
; For those that don't, we push a dummy 0 to keep the
; stack layout uniform.
; ============================================================

%macro ISR_NOERRCODE 1
isr_%1:
    push qword 0            ; Dummy error code
    push qword %1           ; Interrupt number
    jmp isr_common
%endmacro

%macro ISR_ERRCODE 1
isr_%1:
    ; CPU already pushed the error code
    push qword %1           ; Interrupt number
    jmp isr_common
%endmacro

ISR_NOERRCODE 0             ; #DE  Divide by Zero
ISR_NOERRCODE 1             ; #DB  Debug
ISR_NOERRCODE 2             ;      NMI
ISR_NOERRCODE 3             ; #BP  Breakpoint
ISR_NOERRCODE 4             ; #OF  Overflow
ISR_NOERRCODE 5             ; #BR  Bound Range Exceeded
ISR_NOERRCODE 6             ; #UD  Invalid Opcode
ISR_NOERRCODE 7             ; #NM  Device Not Available
ISR_ERRCODE   8             ; #DF  Double Fault
ISR_NOERRCODE 9             ;      Coprocessor Segment Overrun
ISR_ERRCODE   10            ; #TS  Invalid TSS
ISR_ERRCODE   11            ; #NP  Segment Not Present
ISR_ERRCODE   12            ; #SS  Stack-Segment Fault
ISR_ERRCODE   13            ; #GP  General Protection Fault
ISR_ERRCODE   14            ; #PF  Page Fault
ISR_NOERRCODE 15            ;      Reserved
ISR_NOERRCODE 16            ; #MF  x87 FPU Error
ISR_ERRCODE   17            ; #AC  Alignment Check
ISR_NOERRCODE 18            ; #MC  Machine Check
ISR_NOERRCODE 19            ; #XM  SIMD FPU Exception
ISR_NOERRCODE 20            ; #VE  Virtualization Exception
ISR_ERRCODE   21            ; #CP  Control Protection
ISR_NOERRCODE 22            ;      Reserved
ISR_NOERRCODE 23            ;      Reserved
ISR_NOERRCODE 24            ;      Reserved
ISR_NOERRCODE 25            ;      Reserved
ISR_NOERRCODE 26            ;      Reserved
ISR_NOERRCODE 27            ;      Reserved
ISR_NOERRCODE 28            ;      Hypervisor Injection
ISR_ERRCODE   29            ;      VMM Communication
ISR_ERRCODE   30            ;      Security Exception
ISR_NOERRCODE 31            ;      Reserved

; ============================================================
; IRQ stubs — Hardware Interrupts (INT 32-47)
;
; These are from the PIC. We always push a dummy error code
; and the interrupt number, then jump to irq_common.
; ============================================================

%macro IRQ_STUB 2
irq_%1:
    push qword 0            ; Dummy error code
    push qword %2           ; Interrupt number (32 + irq_num)
    jmp irq_common
%endmacro

IRQ_STUB  0, 32             ; Timer
IRQ_STUB  1, 33             ; Keyboard
IRQ_STUB  2, 34             ; Cascade (slave PIC)
IRQ_STUB  3, 35             ; COM2
IRQ_STUB  4, 36             ; COM1
IRQ_STUB  5, 37             ; LPT2
IRQ_STUB  6, 38             ; Floppy
IRQ_STUB  7, 39             ; LPT1 / Spurious
IRQ_STUB  8, 40             ; RTC
IRQ_STUB  9, 41             ; ACPI
IRQ_STUB 10, 42             ; Open
IRQ_STUB 11, 43             ; Open
IRQ_STUB 12, 44             ; Mouse
IRQ_STUB 13, 45             ; FPU
IRQ_STUB 14, 46             ; Primary ATA
IRQ_STUB 15, 47             ; Secondary ATA

; ============================================================
; isr_common — Common handler for CPU exceptions
;
; At this point, the stack has:
;   [int_no] [error_code] [rip] [cs] [rflags] [rsp] [ss]
;
; We save all GP registers to complete the InterruptFrame,
; then call isr_dispatch(InterruptFrame* frame).
; ============================================================

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cld                         ; Clear direction flag (C ABI requirement)
    mov rdi, rsp                ; First argument = pointer to InterruptFrame
    call isr_dispatch

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16                 ; Remove int_no and error_code
    iretq

; ============================================================
; irq_common — Common handler for hardware IRQs
;
; Same register save/restore as isr_common, but calls
; irq_dispatch which handles EOI.
; ============================================================

irq_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cld
    mov rdi, rsp                ; First argument = pointer to InterruptFrame
    call irq_dispatch           ; Returns uint64_t: RSP to restore from
    mov rsp, rax                ; Switch stacks (no-op if same task)

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16                 ; Remove int_no and error_code
    iretq

; ============================================================
; INT 0x80 — System call / scheduler yield
;
; Dispatches by syscall number in RAX:
;   0 = yield,  1 = exit,  2 = write,  3 = sleep, ...
;
; Callable from both ring 0 (kernel task yield/sleep) and ring 3
; (userspace syscall). The IDT entry for 0x80 is DPL=3 so user
; code can issue the interrupt. On ring 3 entry, the CPU switches
; to TSS.rsp0 before pushing the exception frame.
; ============================================================

sched_yield_stub:
    push qword 0                ; Dummy error code
    push qword 0x80             ; Interrupt number

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cld
    mov rdi, rsp                ; InterruptFrame*
    call syscall_dispatch       ; Returns new RSP in rax
    mov rsp, rax                ; Switch stacks

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16                 ; Remove int_no and error_code
    iretq

; ============================================================
; gdt_flush — Load a new GDT and reload all segment registers
;
; void gdt_flush(GDTPointer* ptr, uint64_t kernel_cs, uint64_t kernel_ds)
;   rdi = pointer to GDTPointer
;   rsi = kernel code selector
;   rdx = kernel data selector
; ============================================================

gdt_flush:
    lgdt [rdi]

    ; Reload data segment registers
    mov ax, dx
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Reload CS via a far return. Build a (CS, RIP) pair on the
    ; stack and retfq to it.
    pop rax                     ; Grab our return address
    push rsi                    ; Push new CS
    push rax                    ; Push return RIP
    retfq

; ============================================================
; tss_flush — Load the task register
;
; void tss_flush(uint16_t sel)
;   di = selector (with RPL=0)
; ============================================================

tss_flush:
    mov ax, di
    ltr ax
    ret

; ============================================================
; enter_usermode — iretq into a brand new user context
;
; void enter_usermode(uint64_t user_rip,
;                     uint64_t user_rsp,
;                     uint64_t user_cs,
;                     uint64_t user_ss)
;   rdi = RIP
;   rsi = RSP
;   rdx = CS
;   rcx = SS
;
; Builds a synthetic iretq frame and drops to ring 3. Does not
; return. Only used for the very first transition (or when the
; scheduler doesn't yet own the task). Normal task scheduling
; goes through the IRQ/syscall iretq path.
; ============================================================

enter_usermode:
    cli
    push rcx                    ; SS
    push rsi                    ; RSP
    push qword 0x202            ; RFLAGS with IF=1
    push rdx                    ; CS
    push rdi                    ; RIP
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    iretq

; ============================================================
; load_idt — Load the IDT register
; ============================================================

load_idt:
    lidt [rdi]
    ret

; ============================================================
; context_switch — Cooperative task switch for scheduler
;
; void context_switch(uint64_t* old_rsp_ptr, uint64_t new_rsp)
;   rdi = address to store current RSP
;   rsi = new RSP to switch to
; ============================================================

context_switch:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp              ; Save current RSP
    mov rsp, rsi                ; Load new RSP
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

; ============================================================
; Stub address table — Used by idt.cpp to fill the IDT
;
; isr_stub_table[0..31]  = ISR stubs (CPU exceptions)
; isr_stub_table[32..47] = IRQ stubs (hardware interrupts)
; ============================================================

section .data
isr_stub_table:
    dq isr_0,  isr_1,  isr_2,  isr_3
    dq isr_4,  isr_5,  isr_6,  isr_7
    dq isr_8,  isr_9,  isr_10, isr_11
    dq isr_12, isr_13, isr_14, isr_15
    dq isr_16, isr_17, isr_18, isr_19
    dq isr_20, isr_21, isr_22, isr_23
    dq isr_24, isr_25, isr_26, isr_27
    dq isr_28, isr_29, isr_30, isr_31
    dq irq_0,  irq_1,  irq_2,  irq_3
    dq irq_4,  irq_5,  irq_6,  irq_7
    dq irq_8,  irq_9,  irq_10, irq_11
    dq irq_12, irq_13, irq_14, irq_15
