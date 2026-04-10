#!/bin/bash
# ============================================================
# QuillOS Build & Run
#
# Builds the kernel, creates a bootable ISO, and launches QEMU.
# Auto-detects platform for hardware acceleration:
#   macOS  -> HVF
#   Linux  -> KVM (if available)
#   WSL    -> no accel (software emulation)
# ============================================================

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

# Create a test disk image if it doesn't exist
DISK_IMG="test_disk.img"
if [ ! -f "$DISK_IMG" ]; then
    echo "Creating 64MB test disk image..."
    dd if=/dev/zero of="$DISK_IMG" bs=1M count=64 2>/dev/null
    echo -n "QuillOS Test Disk - Created $(date +%Y-%m-%d)" | \
        dd of="$DISK_IMG" conv=notrunc bs=1 2>/dev/null
    echo "Test disk created: $DISK_IMG"
fi

# Detect platform and pick QEMU acceleration
QEMU_ACCEL=""
IS_WSL=false
if grep -qi microsoft /proc/version 2>/dev/null; then
    IS_WSL=true
fi

if [ "$(uname -s)" = "Darwin" ]; then
    QEMU_ACCEL="-accel hvf"
    echo "Platform: macOS (HVF acceleration)"
elif [ "$IS_WSL" = true ]; then
    # WSL typically doesn't support KVM; use software emulation
    echo "Platform: WSL (software emulation)"
elif [ -e /dev/kvm ]; then
    QEMU_ACCEL="-accel kvm"
    echo "Platform: Linux (KVM acceleration)"
else
    echo "Platform: Linux (software emulation)"
fi

# Launch QEMU
echo "Starting QuillOS in QEMU..."
qemu-system-x86_64 \
    $QEMU_ACCEL \
    -cdrom quillos.iso \
    -drive file="$DISK_IMG",format=raw,if=ide \
    -m 128M
