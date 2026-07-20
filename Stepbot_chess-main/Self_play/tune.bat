@echo off
setlocal enabledelayedexpansion
title Stepbot Texel Tuner
echo ================================
echo   Stepbot Texel Tuner
echo ================================
echo.
echo Options:
echo   1. Quick tune   (100 iterations)
echo   2. Standard     (200 iterations)
echo   3. Deep tune    (500 iterations, slow)
echo   4. Custom
echo.
set /p choice="Enter choice (1-4): "

cd /d "%~dp0.."

if "%choice%"=="1" (
    python tune.py --iterations 100
    goto end
)
if "%choice%"=="2" (
    python tune.py --iterations 200
    goto end
)
if "%choice%"=="3" (
    python tune.py --iterations 500
    goto end
)
if "%choice%"=="4" (
    set /p iters="Iterations: "
    set /p maxpos="Max positions (default 5000): "
    python tune.py --iterations !iters! --max-positions !maxpos!
    goto end
)

echo Invalid choice.

:end
echo.
pause
