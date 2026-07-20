@echo off
title Stepbot Game Analyser
echo ================================
echo   Stepbot Game Analyser
echo ================================
echo.
echo Options:
echo   1. Analyse selfplay games  (depth 3)
echo   2. Analyse selfplay games  (depth 4, slower but more accurate)
echo   3. Analyse a custom PGN file
echo.
set /p choice="Enter choice (1-3): "

cd /d "%~dp0.."

if "%choice%"=="1" (
    python analyse.py --depth 3
    goto end
)
if "%choice%"=="2" (
    python analyse.py --depth 4
    goto end
)
if "%choice%"=="3" (
    set /p pgn="Path to PGN file: "
    python analyse.py --input "%pgn%" --depth 3
    goto end
)

echo Invalid choice.

:end
echo.
pause
