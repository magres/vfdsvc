@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion
title vfdsvc installer for Android TV boxes
rem Work from the folder this .bat lives in; if it was started from a
rem subfolder (e.g. scripts\), step back until the archive root is found.
cd /d "%~dp0"
if not exist "tooling\windows\adb.exe" if exist "..\tooling\windows\adb.exe" cd /d "%~dp0.."

echo =====================================================================
echo   vfdsvc - driver for the front LED "clock" display of Android TV box
echo   Tested on Tanix TX92 (Amlogic S912). Requires root on the firmware.
echo.
echo   [1] INSTALL (install on the box + automatic reboot)
echo   [0] UNINSTALL (rollback/uninstall: restore display to original state)
echo   [T] TEST (display test: all segments light up for 10 seconds)
echo   [Q] EXIT
echo =====================================================================
choice /c 10TQ /n /m "= Select action: "
if errorlevel 4 goto end
if errorlevel 3 set ACT=test& goto ip
if errorlevel 2 set ACT=uninstall& goto ip
set ACT=install
goto ip

:ip
set IP=
if not "%~1"=="" set "IP=%~1"
if not defined IP set /p "IP=Box IP, for example 192.168.1.100: "
if not defined IP ( echo No IP entered. & pause & goto end )
if "%IP::=%"=="%IP%" ( set CONNECT=%IP%:5555 ) else ( set CONNECT=%IP% )

echo == Locating adb ...
set ADB=tooling\windows\adb.exe
if not exist "%ADB%" set ADB=adb
if not exist "%ADB%" (
  echo [ERROR] adb.exe not found in tooling\windows and not in PATH.
  pause
  exit /b 1
)

echo.
echo == Connecting to %CONNECT% ...
"%ADB%" connect %CONNECT%
"%ADB%" connect %CONNECT% >nul
"%ADB%" devices

echo == Checking root access ...
"%ADB%" root >nul 2>&1
"%ADB%" shell id -u 2>nul | findstr "=0" >nul
if errorlevel 1 (
  "%ADB%" shell "su -c 'id -u'" 2>nul | findstr "=0" >nul
  if errorlevel 1 (
    echo [ERROR] No root access via ADB.
    echo     How to fix: in the console on the PC run: %ADB% root
    echo     or enable "su" access in the box firmware.
    pause
    goto end
  )
  set SU=su
) else (
  set SU=
)

echo == Determining CPU architecture ...
"%ADB%" shell getprop ro.product.cpu.abilist 2>nul | findstr /i "arm64" >nul
if not errorlevel 1 ( set BIN=vfdsvc-arm64 ) else ( set BIN=vfdsvc-armv7 )
echo    using %BIN%

if "%ACT%"=="test" (
  echo == Display test: all segments and icons light up for 10 seconds ...
  "%ADB%" shell "stop sys_leds" >nul 2>&1
  "%ADB%" shell "killall vfdsvc" >nul 2>&1
  "%ADB%" shell "/data/local/tmp/vfdsvc-installer/vfdsvc -c /system/etc/vfdsvc.conf --test 10"
  "%ADB%" shell "start sys_leds" >nul 2>&1
  pause
  goto end
)

echo == Uploading files to the box ...
mkdir "%TEMP%\vfdsvc-payload" 2>nul
if "%ACT%"=="install" (
  "%ADB%" push %BIN% /data/local/tmp/vfdsvc-installer/vfdsvc
  "%ADB%" push conf\tx92-vfd.conf /data/local/tmp/vfdsvc-installer/vfdsvc.conf
  "%ADB%" push scripts\sys_leds /data/local/tmp/vfdsvc-installer/sys_leds
  "%ADB%" push scripts\device-install.sh /data/local/tmp/vfdsvc-installer/device-install.sh
  "%ADB%" push scripts\device-uninstall.sh /data/local/tmp/vfdsvc-installer/device-uninstall.sh
)
if "%ACT%"=="uninstall" (
  "%ADB%" push scripts\device-uninstall.sh /data/local/tmp/vfdsvc-installer/device-uninstall.sh
)

if "%ACT%"=="install" (
  echo == Installing ...
  if "%SU%"=="su" (
    "%ADB%" shell "su -c 'sh /data/local/tmp/vfdsvc-installer/device-install.sh'"
  ) else (
    "%ADB%" shell "sh /data/local/tmp/vfdsvc-installer/device-install.sh"
  )
  echo == Rebooting the box: the display will show the clock in about 30-60 sec ...
  "%ADB%" reboot
)
if "%ACT%"=="uninstall" (
  echo == Rollback ...
  if "%SU%"=="su" (
    "%ADB%" shell "su -c 'sh /data/local/tmp/vfdsvc-installer/device-uninstall.sh'"
  ) else (
    "%ADB%" shell "sh /data/local/tmp/vfdsvc-installer/device-uninstall.sh"
  )
  "%ADB%" reboot
)

echo.
echo ~ Done! Close the window.
timeout /t 6 /nobreak >nul 2>nul
goto end

:end
endlocal
