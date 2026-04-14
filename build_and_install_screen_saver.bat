@echo off
REM Build and install the Stepbot screensaver
REM Run from MSYS2 MinGW x64 shell, or adjust path to g++ below

cd /d "%~dp0"

set SCR=stepbot_screensaver.cpp
set OUT=Stepbot.scr
set DEST=%SystemRoot%\System32\Stepbot.scr

echo Building %OUT%...
g++ -O2 -std=c++17 -mwindows -municode -o %OUT% %SCR% -lgdi32 -luser32 -lkernel32

if errorlevel 1 (
    echo.
    echo BUILD FAILED. Make sure you're running from MSYS2 MinGW x64.
    pause
    exit /b 1
)

echo Build OK.
echo.
echo Installing to %DEST%...
echo (You may need to run this script as Administrator)
copy /Y %OUT% "%DEST%"

if errorlevel 1 (
    echo.
    echo Copy failed. Try right-clicking and "Run as Administrator".
    pause
    exit /b 1
)

echo.
echo Done! Go to: Settings ^> Personalisation ^> Lock screen ^> Screen saver
echo and select "Stepbot" from the dropdown.
pause
