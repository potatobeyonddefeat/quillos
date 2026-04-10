@echo off
REM ============================================================
REM QuillOS Launcher for Windows (via WSL)
REM
REM Builds and runs QuillOS in QEMU inside WSL.
REM Requires: WSL2 + Ubuntu with build tools
REM   Install tools: wsl sudo apt install build-essential nasm xorriso qemu-system-x86
REM ============================================================

echo.
echo  QuillOS - Building and launching...
echo.

REM Check if WSL is available
wsl --status >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] WSL is not installed.
    echo.
    echo Install WSL first:
    echo   1. Open PowerShell as Administrator
    echo   2. Run: wsl --install -d Ubuntu
    echo   3. Restart your PC
    echo   4. Run this again
    pause
    exit /b 1
)

REM Convert this script's directory to a WSL path
set "WINDIR=%~dp0"
REM Remove trailing backslash
set "WINDIR=%WINDIR:~0,-1%"

REM Use wslpath to convert Windows path to WSL path
for /f "tokens=*" %%i in ('wsl wslpath -a "%WINDIR%"') do set "WSLDIR=%%i"

echo  Repo path: %WSLDIR%
echo.

REM Build and run
wsl -d Ubuntu -- bash -c "
    cd '%WSLDIR%/quillos'

    # Install deps if missing
    if ! command -v nasm >/dev/null 2>&1; then
        echo '[+] Installing build tools (first time only)...'
        sudo apt-get update -qq
        sudo apt-get install -y -qq build-essential nasm xorriso qemu-system-x86 make
    fi

    mkdir -p build
    make clean && make

    # Build limine if needed
    if [ ! -x ./limine/limine ]; then
        make -C limine
    fi

    # Create ISO
    xorriso -as mkisofs \
        -b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
        -boot-info-table --efi-boot limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image --protective-msdos-label \
        iso -o quillos.iso 2>/dev/null

    ./limine/limine bios-install quillos.iso

    # Create test disk if needed
    if [ ! -f test_disk.img ]; then
        dd if=/dev/zero of=test_disk.img bs=1M count=64 2>/dev/null
        echo -n 'QuillOS Test Disk' | dd of=test_disk.img conv=notrunc bs=1 2>/dev/null
    fi

    echo ''
    echo '  Starting QEMU...'
    echo ''

    qemu-system-x86_64 \
        -cdrom quillos.iso \
        -drive file=test_disk.img,format=raw,if=ide \
        -m 128M
"

echo.
echo  QuillOS exited.
pause
