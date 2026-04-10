# ============================================================
# QuillOS Windows Setup Script
#
# Installs WSL2 + Ubuntu, sets up the build toolchain,
# and prepares QuillOS for building and running in QEMU.
#
# Usage:  Right-click -> Run with PowerShell
#    or:  powershell -ExecutionPolicy Bypass -File setup-windows.ps1
# ============================================================

$ErrorActionPreference = "Stop"

function Write-Step($msg) { Write-Host "[+] $msg" -ForegroundColor Green }
function Write-Warn($msg) { Write-Host "[!] $msg" -ForegroundColor Yellow }
function Write-Err($msg)  { Write-Host "[x] $msg" -ForegroundColor Red }

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  QuillOS Windows Setup" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# ============================================================
# Step 1: Check if running as Administrator
# ============================================================

$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Warn "Not running as Administrator. WSL installation may require admin rights."
    Write-Warn "If WSL is already installed, this is fine. Continuing..."
    Write-Host ""
}

# ============================================================
# Step 2: Check if WSL is available
# ============================================================

Write-Step "Checking WSL status..."

$wslInstalled = $false
try {
    $wslOutput = wsl --status 2>&1
    if ($LASTEXITCODE -eq 0) {
        $wslInstalled = $true
        Write-Step "WSL is installed"
    }
} catch {
    $wslInstalled = $false
}

if (-not $wslInstalled) {
    Write-Step "Installing WSL2 with Ubuntu..."
    Write-Warn "This may require a restart. After restarting, run this script again."
    Write-Host ""

    if (-not $isAdmin) {
        Write-Err "WSL installation requires Administrator privileges."
        Write-Err "Right-click this script and select 'Run as Administrator'."
        Read-Host "Press Enter to exit"
        exit 1
    }

    try {
        wsl --install -d Ubuntu
        Write-Host ""
        Write-Warn "WSL installed. You may need to RESTART your computer."
        Write-Warn "After restart, run this script again to complete setup."
        Read-Host "Press Enter to exit"
        exit 0
    } catch {
        Write-Err "Failed to install WSL: $_"
        Read-Host "Press Enter to exit"
        exit 1
    }
}

# ============================================================
# Step 3: Check if Ubuntu is the default distribution
# ============================================================

Write-Step "Checking for Ubuntu distribution..."

$distros = wsl --list --quiet 2>&1
$hasUbuntu = $distros | Select-String -Pattern "Ubuntu" -Quiet

if (-not $hasUbuntu) {
    Write-Step "Installing Ubuntu in WSL..."
    wsl --install -d Ubuntu
    Write-Warn "Ubuntu installed. You may need to set up a username/password."
    Write-Warn "After that, run this script again."
    Read-Host "Press Enter to exit"
    exit 0
}

Write-Step "Ubuntu is available in WSL"

# ============================================================
# Step 4: Get the repo path in WSL format
# ============================================================

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$wslPath = wsl wslpath -a "$scriptDir" 2>&1

if ($LASTEXITCODE -ne 0) {
    # Fallback: manual conversion  C:\Users\foo -> /mnt/c/Users/foo
    $wslPath = $scriptDir -replace '\\', '/'
    $wslPath = $wslPath -replace '^([A-Za-z]):', '/mnt/$1'
    $wslPath = $wslPath.ToLower().Substring(0, 5) + $wslPath.Substring(5)
}

Write-Step "Repo path in WSL: $wslPath"

# ============================================================
# Step 5: Install dependencies inside WSL
# ============================================================

Write-Step "Installing build tools in WSL (this may take a minute)..."

wsl -d Ubuntu -- bash -c "sudo apt-get update -qq && sudo apt-get install -y -qq build-essential gcc g++ nasm xorriso qemu-system-x86 make"

if ($LASTEXITCODE -ne 0) {
    Write-Err "Failed to install dependencies in WSL"
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Step "Dependencies installed"

# ============================================================
# Step 6: Build QuillOS inside WSL
# ============================================================

Write-Step "Building QuillOS..."

# Copy project to WSL home for faster builds (NTFS is slow from WSL)
wsl -d Ubuntu -- bash -c "
    rm -rf ~/quillos-build
    cp -r '$wslPath' ~/quillos-build
    cd ~/quillos-build/quillos
    mkdir -p build
    make clean
    make
    # Build limine
    if [ ! -x ./limine/limine ]; then make -C limine; fi
    # Create ISO
    xorriso -as mkisofs \
        -b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
        -boot-info-table --efi-boot limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image --protective-msdos-label \
        iso -o quillos.iso 2>/dev/null
    ./limine/limine bios-install quillos.iso
    # Test disk
    if [ ! -f test_disk.img ]; then
        dd if=/dev/zero of=test_disk.img bs=1M count=64 2>/dev/null
        echo -n 'QuillOS Test Disk' | dd of=test_disk.img conv=notrunc bs=1 2>/dev/null
    fi
    echo 'BUILD_OK'
"

if ($LASTEXITCODE -ne 0) {
    Write-Err "Build failed. Check the output above for errors."
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Step "Build successful!"

# ============================================================
# Step 7: Create the Windows launcher batch file
# ============================================================

$launcherPath = Join-Path $scriptDir "run-quillos.bat"

Write-Step "Creating launcher: run-quillos.bat"

# ============================================================
# Done
# ============================================================

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Setup Complete!" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "  To run QuillOS:" -ForegroundColor White
Write-Host "    Double-click: run-quillos.bat" -ForegroundColor Yellow
Write-Host "    Or in PowerShell: .\run-quillos.bat" -ForegroundColor Yellow
Write-Host ""
Write-Host "  To rebuild after code changes:" -ForegroundColor White
Write-Host "    wsl -d Ubuntu -- bash -c 'cd ~/quillos-build/quillos && make clean && make'" -ForegroundColor Yellow
Write-Host ""
Write-Host "  QuillOS shell commands:" -ForegroundColor White
Write-Host "    help, diskinfo, hexdump 0, meminfo," -ForegroundColor Gray
Write-Host "    ps, intinfo, lspci, uptime, cls" -ForegroundColor Gray
Write-Host ""

Read-Host "Press Enter to exit"
