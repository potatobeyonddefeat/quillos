#!/bin/bash
# ============================================================
# QuillOS Cluster Test — Launch 2 nodes on a virtual network
#
# Creates two QEMU instances connected via a socket-based
# virtual network so they can communicate:
#
#   Node 0: 10.0.0.2 (port 9000)  — window "QuillOS Node 0"
#   Node 1: 10.0.0.3 (port 9001)  — window "QuillOS Node 1"
#
# Usage:  bash run-cluster.sh
#         bash run-cluster.sh --build   (rebuild first)
# ============================================================

set -e

# Rebuild if requested or if ISO doesn't exist
if [ "$1" = "--build" ] || [ ! -f "quillos.iso" ]; then
    echo "[+] Building QuillOS..."
    make clean
    make

    LIMINE_BIN="./limine/limine"
    if [ ! -x "$LIMINE_BIN" ]; then
        make -C limine
    fi

    xorriso -as mkisofs \
        -b limine-bios-cd.bin \
        -no-emul-boot \
        -boot-load-size 4 \
        -boot-info-table \
        --efi-boot limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image \
        --protective-msdos-label \
        iso -o quillos.iso 2>/dev/null

    "$LIMINE_BIN" bios-install quillos.iso
    echo "[+] Build complete"
fi

# Create separate disk images for each node
for i in 0 1; do
    DISK="node${i}_disk.img"
    if [ ! -f "$DISK" ]; then
        echo "[+] Creating disk for node $i..."
        dd if=/dev/zero of="$DISK" bs=1M count=64 2>/dev/null
        echo -n "QuillOS Node $i Disk" | dd of="$DISK" conv=notrunc bs=1 2>/dev/null
    fi
done

# Detect acceleration
QEMU_ACCEL=""
IS_WSL=false
if grep -qi microsoft /proc/version 2>/dev/null; then
    IS_WSL=true
fi

if [ "$(uname -s)" = "Darwin" ]; then
    QEMU_ACCEL="-accel hvf"
elif [ "$IS_WSL" = false ] && [ -e /dev/kvm ]; then
    QEMU_ACCEL="-accel kvm"
fi

echo ""
echo "============================================"
echo "  QuillOS Cluster — Launching 2 nodes"
echo "============================================"
echo ""
echo "  Node 0: MAC 52:54:00:00:00:01"
echo "  Node 1: MAC 52:54:00:00:00:02"
echo "  Network: virtual switch (socket mcast)"
echo ""
echo "  Both windows will open. Use 'cluster'"
echo "  and 'netinfo' commands in each shell."
echo ""

# Launch Node 0 (background)
qemu-system-x86_64 \
    $QEMU_ACCEL \
    -name "QuillOS Node 0" \
    -cdrom quillos.iso \
    -drive file=node0_disk.img,format=raw,if=ide \
    -m 128M \
    -net nic,model=e1000,macaddr=52:54:00:00:00:01 \
    -net socket,mcast=230.0.0.1:1234 \
    &

# Small delay so windows don't overlap
sleep 1

# Launch Node 1 (foreground — script waits for this one)
qemu-system-x86_64 \
    $QEMU_ACCEL \
    -name "QuillOS Node 1" \
    -cdrom quillos.iso \
    -drive file=node1_disk.img,format=raw,if=ide \
    -m 128M \
    -net nic,model=e1000,macaddr=52:54:00:00:00:02 \
    -net socket,mcast=230.0.0.1:1234 \

# When Node 1 closes, kill Node 0
kill %1 2>/dev/null
wait 2>/dev/null

echo ""
echo "  Cluster shut down."
