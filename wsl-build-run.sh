#!/bin/bash
# Called by run-quillos.bat — builds and runs QuillOS inside WSL
set -e

QUILLOS_DIR="$1"

if [ -z "$QUILLOS_DIR" ] || [ ! -d "$QUILLOS_DIR/quillos" ]; then
    echo "[ERROR] Cannot find quillos directory at: $QUILLOS_DIR"
    exit 1
fi

cd "$QUILLOS_DIR/quillos"

# Install deps if missing (first run only)
if ! command -v nasm >/dev/null 2>&1; then
    echo "[+] Installing build tools (first time only)..."
    sudo apt-get update -qq
    sudo apt-get install -y -qq build-essential nasm xorriso qemu-system-x86 make
fi

mkdir -p build
echo "[+] Building QuillOS..."
make clean
make

# Build limine if needed
if [ ! -x ./limine/limine ]; then
    echo "[+] Building limine..."
    make -C limine
fi

# Create ISO
echo "[+] Creating bootable ISO..."
xorriso -as mkisofs \
    -b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
    -boot-info-table --efi-boot limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    iso -o quillos.iso 2>/dev/null

./limine/limine bios-install quillos.iso

# Create test disk if needed
if [ ! -f test_disk.img ]; then
    echo "[+] Creating 64MB test disk..."
    dd if=/dev/zero of=test_disk.img bs=1M count=64 2>/dev/null
    echo -n "QuillOS Test Disk" | dd of=test_disk.img conv=notrunc bs=1 2>/dev/null
fi

echo ""
echo "[+] Starting QEMU..."
echo ""

qemu-system-x86_64 \
    -cdrom quillos.iso \
    -drive file=test_disk.img,format=raw,if=ide \
    -m 128M
