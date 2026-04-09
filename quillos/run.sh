#!/bin/bash
set -e

# Clean and build
make clean
make

# Build limine deploy tool from source if needed
LIMINE_BIN="./limine/limine"
if [ ! -x "$LIMINE_BIN" ]; then
    echo "Building limine deploy tool from source..."
    make -C limine
fi

# Create bootable ISO
xorriso -as mkisofs \
    -b limine-bios-cd.bin \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    --efi-boot limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image \
    --protective-msdos-label \
    iso -o quillos.iso

# Install limine bootloader
"$LIMINE_BIN" bios-install quillos.iso

# Launch in QEMU with hardware acceleration on macOS
QEMU_ACCEL=""
if [ "$(uname -s)" = "Darwin" ]; then
    QEMU_ACCEL="-accel hvf"
fi

qemu-system-x86_64 $QEMU_ACCEL -cdrom quillos.iso
