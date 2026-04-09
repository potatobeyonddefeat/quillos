# Auto-detect macOS and use cross-compiler toolchain
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    CC = x86_64-elf-gcc
    LD = x86_64-elf-ld
else
    CC = gcc
    LD = ld
endif
AS = nasm

# ADDED -mno-red-zone to prevent interrupt stack corruption
CFLAGS = -ffreestanding -O2 -Wall -Wextra -m64 -mcmodel=kernel -fno-pie -no-pie -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -Iinclude
LDFLAGS = -T linker.ld -z noexecstack

KERNEL_OBJS = \
    build/kmain.o \
    build/kernel.o \
    build/font.o \
    build/console.o \
    build/idt.o \
    build/keyboard.o \
    build/interrupts.o \
    build/shell.o \
    build/timer.o \
    build/utils.o \
    build/filesystem.o \
    build/memory.o \
    build/scheduler.o \
    build/pci.o \
    build/disk.o \
    build/network.o \
    build/cluster.o

all: iso/kernel.elf

iso/kernel.elf: $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o iso/kernel.elf $(KERNEL_OBJS)

# Rule for Assembly files
build/interrupts.o: kernel/interrupts.asm
	$(AS) -f elf64 $< -o $@

# Pattern rule for all C++ files ensures CFLAGS are always applied
build/%.o: kernel/%.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build/*
	rm -f iso/kernel.elf
