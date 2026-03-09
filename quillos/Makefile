CC = gcc
CFLAGS = -ffreestanding -O2 -Wall -Wextra -m64 -mcmodel=kernel -fno-pie -no-pie -Iinclude

all: iso/kernel.elf

iso/kernel.elf: build/kernel.o build/console.o linker.ld
	ld -T linker.ld -o iso/kernel.elf build/kernel.o build/console.o

build/kernel.o: kernel/kernel.cpp
	$(CC) $(CFLAGS) -c kernel/kernel.cpp -o build/kernel.o

build/console.o: kernel/console.cpp
	$(CC) $(CFLAGS) -c kernel/console.cpp -o build/console.o

clean:
	rm -rf build/*
	rm -f iso/kernel.elf
