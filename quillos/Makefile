CC = gcc
AS = nasm

CFLAGS = -ffreestanding -O2 -Wall -Wextra -m64 -mcmodel=kernel -fno-pie -no-pie -Iinclude
LDFLAGS = -T linker.ld

KERNEL_OBJS = \
build/kernel.o \
build/font.o \
build/console.o \
build/idt.o \
build/keyboard.o \
build/interrupts.o

all: iso/kernel.elf

iso/kernel.elf: $(KERNEL_OBJS)
	ld $(LDFLAGS) -o iso/kernel.elf $(KERNEL_OBJS)

build/kernel.o: kernel/kernel.cpp
	$(CC) $(CFLAGS) -c $< -o $@

build/font.o: kernel/font.cpp
	$(CC) $(CFLAGS) -c $< -o $@

build/console.o: kernel/console.cpp
	$(CC) $(CFLAGS) -c $< -o $@

build/idt.o: kernel/idt.cpp
	$(CC) $(CFLAGS) -c $< -o $@

build/keyboard.o: kernel/keyboard.cpp
	$(CC) $(CFLAGS) -c $< -o $@

build/interrupts.o: kernel/interrupts.asm
	$(AS) -f elf64 $< -o $@

clean:
	rm -rf build/*
	rm -f iso/kernel.elf
