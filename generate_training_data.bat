@echo off
setlocal enabledelayedexpansion
title Stepbot NNUE Training Data Generator
echo ============================================
echo   Stepbot NNUE Training Data Generator
echo ============================================
echo.
echo Options:
echo   1. Quick run    (200 games, depth 9, 5 cores)
echo   2. Standard     (500 games, depth 9, 5 cores)
echo   3. Accurate     (200 games, depth 10, 5 cores)
echo   4. Large        (1000 games, depth 9, 5 cores)
echo   5. Custom
echo   6. Append to existing dataset (200 games, depth 9)
echo.
set /p choice="Enter choice (1-6): "

cd /d "%~dp0"

if "%choice%"=="1" (
    python generate_training_data.py --games 200 --depth 9 --cores 5
    goto end
)
if "%choice%"=="2" (
    python generate_training_data.py --games 500 --depth 9 --cores 5
    goto end
)
if "%choice%"=="3" (
    python generate_training_data.py --games 200 --depth 10 --cores 5
    goto end
)
if "%choice%"=="4" (
    python generate_training_data.py --games 1000 --depth 9 --cores 5
    goto end
)
if "%choice%"=="5" (
    set /p games="Number of games: "
    set /p depth="Depth (9 or 10): "
    set /p cores="Cores (recommend 5): "
    python generate_training_data.py --games !games! --depth !depth! --cores !cores!
    goto end
)
if "%choice%"=="6" (
    python generate_training_data.py --games 200 --depth 9 --cores 5 --append
    goto end
)

echo Invalid choice.

:end
echo.
pause
