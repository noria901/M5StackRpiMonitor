@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

:: ============================================================
:: M5Dial Firmware - Windows Flash Script
:: ESP-IDF v5.1.3 setup, build, flash, and monitor
:: ============================================================

set "IDF_VERSION=5.1.3"
set "IDF_INSTALL_DIR=%USERPROFILE%\esp"
set "IDF_PATH=%IDF_INSTALL_DIR%\esp-idf-v%IDF_VERSION%"
set "IDF_TOOLS_PATH=%IDF_INSTALL_DIR%\tools"
set "SERIAL_PORT=auto"
set "BAUD_RATE=1500000"
set "SCRIPT_DIR=%~dp0"

:: Parse arguments
:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="-h"      goto :show_help
if /i "%~1"=="--help"   goto :show_help
if /i "%~1"=="-p"       set "SERIAL_PORT=%~2" & shift & shift & goto :parse_args
if /i "%~1"=="--port"   set "SERIAL_PORT=%~2" & shift & shift & goto :parse_args
if /i "%~1"=="setup"    goto :setup_only
if /i "%~1"=="build"    goto :build_only
if /i "%~1"=="flash"    goto :flash_only
if /i "%~1"=="monitor"  goto :monitor_only
if /i "%~1"=="clean"    goto :clean_only
echo Unknown option: %~1
goto :show_help

:args_done

:: Default action: build + flash + monitor
call :check_idf
if errorlevel 1 (
    echo.
    echo ESP-IDF not found. Running setup first...
    call :setup_idf
    if errorlevel 1 goto :error
)
call :activate_idf
call :do_build
if errorlevel 1 goto :error
call :do_flash
if errorlevel 1 goto :error
call :do_monitor
goto :end

:: ============================================================
:: Help
:: ============================================================
:show_help
echo.
echo M5Dial Firmware Flash Tool for Windows
echo.
echo Usage: flash.bat [options] [command]
echo.
echo Commands:
echo   setup     Install ESP-IDF v%IDF_VERSION% (first time only)
echo   build     Build firmware only
echo   flash     Flash firmware to device
echo   monitor   Open serial monitor
echo   clean     Clean build artifacts
echo   (none)    Build + Flash + Monitor (default)
echo.
echo Options:
echo   -p, --port PORT   Serial port (default: auto-detect)
echo   -h, --help        Show this help
echo.
echo Examples:
echo   flash.bat                     Build, flash, and monitor
echo   flash.bat setup               Install ESP-IDF only
echo   flash.bat -p COM3 flash       Flash to COM3
echo   flash.bat monitor             Open serial monitor
echo.
goto :end

:: ============================================================
:: Setup ESP-IDF
:: ============================================================
:setup_only
call :setup_idf
goto :end

:setup_idf
echo.
echo ============================================================
echo  ESP-IDF v%IDF_VERSION% Setup
echo ============================================================
echo.

:: Check prerequisites
echo [1/5] Checking prerequisites...

where git >nul 2>&1
if errorlevel 1 (
    echo ERROR: git is not installed.
    echo   Download from: https://git-scm.com/download/win
    exit /b 1
)
echo   - git: OK

where python >nul 2>&1
if errorlevel 1 (
    where python3 >nul 2>&1
    if errorlevel 1 (
        echo ERROR: Python is not installed.
        echo   Download from: https://www.python.org/downloads/
        echo   IMPORTANT: Check "Add Python to PATH" during installation
        exit /b 1
    )
)
echo   - Python: OK

:: Create install directory
echo.
echo [2/5] Creating install directory...
if not exist "%IDF_INSTALL_DIR%" mkdir "%IDF_INSTALL_DIR%"

:: Clone ESP-IDF
echo.
echo [3/5] Cloning ESP-IDF v%IDF_VERSION%...
if exist "%IDF_PATH%" (
    echo   ESP-IDF already cloned at %IDF_PATH%
    echo   Skipping clone.
) else (
    git clone -b v%IDF_VERSION% --recursive --depth 1 ^
        https://github.com/espressif/esp-idf.git "%IDF_PATH%"
    if errorlevel 1 (
        echo ERROR: Failed to clone ESP-IDF.
        exit /b 1
    )
)

:: Install ESP-IDF tools
echo.
echo [4/5] Installing ESP-IDF tools (this may take a while)...
set "IDF_TOOLS_PATH=%IDF_TOOLS_PATH%"
call "%IDF_PATH%\install.bat" esp32s3
if errorlevel 1 (
    echo ERROR: Failed to install ESP-IDF tools.
    exit /b 1
)

:: Verify installation
echo.
echo [5/5] Verifying installation...
call "%IDF_PATH%\export.bat" >nul 2>&1
where idf.py >nul 2>&1
if errorlevel 1 (
    echo ERROR: idf.py not found after setup.
    exit /b 1
)

echo.
echo ============================================================
echo  ESP-IDF v%IDF_VERSION% setup complete!
echo ============================================================
echo.
exit /b 0

:: ============================================================
:: Check if ESP-IDF is installed
:: ============================================================
:check_idf
if not exist "%IDF_PATH%\export.bat" (
    exit /b 1
)
exit /b 0

:: ============================================================
:: Activate ESP-IDF environment
:: ============================================================
:activate_idf
echo.
echo Activating ESP-IDF v%IDF_VERSION%...
call "%IDF_PATH%\export.bat" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to activate ESP-IDF.
    exit /b 1
)
echo   ESP-IDF environment ready.
exit /b 0

:: ============================================================
:: Build
:: ============================================================
:build_only
call :check_idf
if errorlevel 1 (
    echo ERROR: ESP-IDF not installed. Run "flash.bat setup" first.
    goto :end
)
call :activate_idf
call :do_build
goto :end

:do_build
echo.
echo ============================================================
echo  Building firmware...
echo ============================================================
echo.
cd /d "%SCRIPT_DIR%"
idf.py set-target esp32s3
if errorlevel 1 (
    echo ERROR: Failed to set target.
    exit /b 1
)
idf.py build
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)
echo.
echo Build successful!
exit /b 0

:: ============================================================
:: Flash
:: ============================================================
:flash_only
call :check_idf
if errorlevel 1 (
    echo ERROR: ESP-IDF not installed. Run "flash.bat setup" first.
    goto :end
)
call :activate_idf
call :do_flash
goto :end

:do_flash
echo.
echo ============================================================
echo  Flashing firmware...
echo ============================================================
echo.
cd /d "%SCRIPT_DIR%"

if /i "%SERIAL_PORT%"=="auto" (
    echo Auto-detecting serial port...
    call :detect_port
    if errorlevel 1 (
        echo ERROR: No M5Stack device found.
        echo   - Connect M5Dial via USB
        echo   - Install driver if needed: https://docs.m5stack.com/en/download
        echo   - Or specify port manually: flash.bat -p COM3 flash
        exit /b 1
    )
)

echo Using port: %SERIAL_PORT%
idf.py -p %SERIAL_PORT% flash -b %BAUD_RATE%
if errorlevel 1 (
    echo.
    echo Flash failed. Retrying with lower baud rate...
    idf.py -p %SERIAL_PORT% flash -b 460800
    if errorlevel 1 (
        echo ERROR: Flash failed.
        exit /b 1
    )
)
echo.
echo Flash successful!
exit /b 0

:: ============================================================
:: Monitor
:: ============================================================
:monitor_only
call :check_idf
if errorlevel 1 (
    echo ERROR: ESP-IDF not installed. Run "flash.bat setup" first.
    goto :end
)
call :activate_idf
call :do_monitor
goto :end

:do_monitor
echo.
echo ============================================================
echo  Opening serial monitor (Ctrl+] to exit)
echo ============================================================
echo.
cd /d "%SCRIPT_DIR%"

if /i "%SERIAL_PORT%"=="auto" (
    call :detect_port
    if errorlevel 1 (
        echo ERROR: No device found. Specify port with -p option.
        exit /b 1
    )
)

idf.py -p %SERIAL_PORT% monitor
exit /b 0

:: ============================================================
:: Clean
:: ============================================================
:clean_only
call :check_idf
if errorlevel 1 (
    echo ERROR: ESP-IDF not installed. Run "flash.bat setup" first.
    goto :end
)
call :activate_idf
echo.
echo Cleaning build artifacts...
cd /d "%SCRIPT_DIR%"
idf.py fullclean
echo Clean complete.
goto :end

:: ============================================================
:: Auto-detect serial port
:: ============================================================
:detect_port
:: Look for common M5Stack/ESP32 USB serial ports
for /f "tokens=*" %%a in ('reg query HKLM\HARDWARE\DEVICEMAP\SERIALCOMM 2^>nul ^| findstr /i "USB"') do (
    for /f "tokens=3" %%b in ("%%a") do (
        set "SERIAL_PORT=%%b"
        echo   Found: %%b
        exit /b 0
    )
)
exit /b 1

:: ============================================================
:: Error / End
:: ============================================================
:error
echo.
echo ============================================================
echo  An error occurred. See messages above.
echo ============================================================
exit /b 1

:end
endlocal
