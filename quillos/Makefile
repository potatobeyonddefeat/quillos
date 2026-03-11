CC = gcc
AS = nasm

# ADDED -mno-red-zone to prevent interrupt stack corruption
CFLAGS = -ffreestanding -O2 -Wall -Wextra -m64 -mcmodel=kernel -fno-pie -no-pie -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -Iinclude
LDFLAGS = -T linker.ld

KERNEL_OBJS = \
    build/kernel.o \
    build/font.o \
    build/console.o \
    build/idt.o \
    build/keyboard.o \
    build/interrupts.o \
    build/shell.o \
    build/timer.o \
    build/utils.o

all: iso/kernel.elf

iso/kernel.elf: $(KERNEL_OBJS)
	ld $(LDFLAGS) -o iso/kernel.elf $(KERNEL_OBJS)

# Rule for Assembly files
build/interrupts.o: kernel/interrupts.asm
	$(AS) -f elf64 $< -o $@

# Pattern rule for all C++ files ensures CFLAGS are always applied
build/%.o: kernel/%.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build/*
	rm -f iso/kernel.elf
