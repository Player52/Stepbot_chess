@echo off
title Stepbot Self-Play
echo ================================
echo   Stepbot Self-Play Session
echo ================================
echo.
echo Options:
echo   1. Quick test  (2 games, depth 2)
echo   2. Standard    (10 games, depth 3)
echo   3. Deep        (10 games, depth 4)
echo   4. Custom
echo.
set /p choice="Enter choice (1-4): "

if "%choice%"=="1" (
    python ..\selfplay.py --games 2 --depth 2
    goto end
)
if "%choice%"=="2" (
    python ..\selfplay.py --games 10 --depth 3
    goto end
)
if "%choice%"=="3" (
    python ..\selfplay.py --games 10 --depth 4
    goto end
)
if "%choice%"=="4" (
    set /p games="Number of games: "
    set /p depth="Depth: "
    python ..\selfplay.py --games %games% --depth %depth%
    goto end
)

echo Invalid choice.

:end
echo.
pause
