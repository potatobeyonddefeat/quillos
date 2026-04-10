@echo off
REM ============================================================
REM QuillOS Launcher for Windows (via WSL)
REM
REM Rebuilds and runs QuillOS in QEMU inside WSL.
REM Requires: WSL2 + Ubuntu (run setup-windows.ps1 first)
REM ============================================================

echo.
echo  QuillOS - Starting...
echo.

REM Check if WSL is available
wsl --status >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] WSL is not installed.
    echo Run setup-windows.ps1 first.
    pause
    exit /b 1
)

REM Get this script's directory in WSL path format
set "SCRIPT_DIR=%~dp0"

REM Build and run inside WSL
wsl -d Ubuntu -- bash -c "cd ~/quillos-build/quillos && make 2>&1 && echo '--- BUILD OK ---' && xorriso -as mkisofs -b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label iso -o quillos.iso 2>/dev/null && ./limine/limine bios-install quillos.iso 2>/dev/null && [ ! -f test_disk.img ] && dd if=/dev/zero of=test_disk.img bs=1M count=64 2>/dev/null; qemu-system-x86_64 -cdrom quillos.iso -drive file=test_disk.img,format=raw,if=ide -m 128M"

echo.
echo  QuillOS exited.
pause
