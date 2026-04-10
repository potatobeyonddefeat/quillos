#!/bin/bash
# ============================================================
# QuillOS Development Environment Setup
#
# Detects your platform (macOS / Linux / WSL) and installs
# everything needed to build and run QuillOS in QEMU.
#
# Usage:  chmod +x setup.sh && ./setup.sh
# ============================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[+]${NC} $1"; }
warn()  { echo -e "${YELLOW}[!]${NC} $1"; }
error() { echo -e "${RED}[x]${NC} $1"; exit 1; }

echo "============================================"
echo "  QuillOS Development Environment Setup"
echo "============================================"
echo ""

# ============================================================
# Detect platform
# ============================================================

OS="unknown"
IS_WSL=false

case "$(uname -s)" in
    Darwin)  OS="macos" ;;
    Linux)
        OS="linux"
        if grep -qi microsoft /proc/version 2>/dev/null; then
            IS_WSL=true
        fi
        ;;
    *)  error "Unsupported platform: $(uname -s)" ;;
esac

if [ "$IS_WSL" = true ]; then
    info "Detected: Linux (WSL on Windows)"
else
    info "Detected: $OS"
fi

# ============================================================
# Install dependencies
# ============================================================

install_macos() {
    info "Installing dependencies via Homebrew..."

    if ! command -v brew &>/dev/null; then
        error "Homebrew not found. Install it from https://brew.sh"
    fi

    # Cross-compiler (macOS clang can't produce ELF binaries)
    brew install x86_64-elf-gcc x86_64-elf-binutils 2>/dev/null || true
    brew install nasm xorriso qemu 2>/dev/null || true

    info "macOS dependencies installed"
}

install_linux() {
    info "Installing dependencies via apt..."

    # Detect package manager
    if command -v apt-get &>/dev/null; then
        sudo apt-get update -qq
        sudo apt-get install -y -qq \
            build-essential \
            gcc \
            g++ \
            nasm \
            xorriso \
            qemu-system-x86 \
            make
    elif command -v dnf &>/dev/null; then
        sudo dnf install -y \
            gcc gcc-c++ make \
            nasm xorriso \
            qemu-system-x86
    elif command -v pacman &>/dev/null; then
        sudo pacman -S --noconfirm --needed \
            gcc make nasm xorriso qemu-system-x86
    else
        error "No supported package manager found (need apt, dnf, or pacman)"
    fi

    info "Linux dependencies installed"
}

case "$OS" in
    macos) install_macos ;;
    linux) install_linux ;;
esac

# ============================================================
# Verify all tools are available
# ============================================================

info "Verifying toolchain..."

MISSING=""

check_tool() {
    if ! command -v "$1" &>/dev/null; then
        MISSING="$MISSING $1"
        warn "Missing: $1"
    else
        echo "  $1 ... ok"
    fi
}

if [ "$OS" = "macos" ]; then
    check_tool x86_64-elf-gcc
    check_tool x86_64-elf-ld
else
    check_tool gcc
    check_tool ld
fi
check_tool nasm
check_tool xorriso
check_tool qemu-system-x86_64
check_tool make

if [ -n "$MISSING" ]; then
    error "Missing tools:$MISSING — install them and re-run setup.sh"
fi

info "All tools verified"

# ============================================================
# Build QuillOS
# ============================================================

QUILLOS_DIR=""
if [ -d "quillos" ] && [ -f "quillos/Makefile" ]; then
    QUILLOS_DIR="quillos"
elif [ -f "Makefile" ] && [ -d "kernel" ]; then
    QUILLOS_DIR="."
else
    error "Cannot find QuillOS source. Run this script from the repo root."
fi

info "Building QuillOS (in $QUILLOS_DIR/)..."
cd "$QUILLOS_DIR"

# Ensure build directory exists
mkdir -p build

make clean
make

# Build limine deploy tool
LIMINE_BIN="./limine/limine"
if [ ! -x "$LIMINE_BIN" ]; then
    info "Building limine deploy tool..."
    make -C limine
fi

# Create bootable ISO
info "Creating bootable ISO..."
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

# Create test disk
if [ ! -f "test_disk.img" ]; then
    info "Creating 64MB test disk image..."
    dd if=/dev/zero of=test_disk.img bs=1M count=64 2>/dev/null
    echo -n "QuillOS Test Disk" | dd of=test_disk.img conv=notrunc bs=1 2>/dev/null
fi

info "Build complete!"

# ============================================================
# Summary
# ============================================================

echo ""
echo "============================================"
echo "  Setup Complete!"
echo "============================================"
echo ""
echo "  To run QuillOS:"
if [ "$IS_WSL" = true ]; then
    echo "    cd quillos && ./run.sh"
    echo ""
    echo "  Or from Windows:"
    echo "    Double-click run-quillos.bat"
fi
if [ "$OS" = "macos" ]; then
    echo "    cd quillos && ./run.sh"
fi
if [ "$OS" = "linux" ] && [ "$IS_WSL" = false ]; then
    echo "    cd quillos && ./run.sh"
fi
echo ""
echo "  Shell commands once booted:"
echo "    help, diskinfo, hexdump 0, meminfo,"
echo "    ps, intinfo, lspci, uptime"
echo ""
