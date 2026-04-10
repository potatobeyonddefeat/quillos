@echo off
REM ============================================================
REM QuillOS Launcher for Windows (via WSL)
REM
REM Builds and runs QuillOS in QEMU inside WSL.
REM First run will auto-install build tools.
REM ============================================================

echo.
echo  QuillOS - Building and launching...
echo.

wsl --status >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] WSL is not installed.
    echo.
    echo   1. Open PowerShell as Administrator
    echo   2. Run: wsl --install -d Ubuntu
    echo   3. Restart your PC
    echo   4. Run this script again
    echo.
    pause
    exit /b 1
)

for /f "tokens=*" %%i in ('wsl wslpath -a "%~dp0."') do set "WSLDIR=%%i"

echo  WSL path: %WSLDIR%
echo.

wsl -d Ubuntu -- bash "%WSLDIR%/wsl-build-run.sh" "%WSLDIR%"

echo.
echo  QuillOS exited.
pause
