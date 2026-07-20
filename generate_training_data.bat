@echo off
setlocal enabledelayedexpansion
title Stepbot NNUE Training Data Generator

echo ============================================
echo   Stepbot NNUE Training Data Generator
echo ============================================
echo.
echo  -- Stepbot vs Stepbot --
echo   1. Quick run    (200 games, depth 9,  5 cores)
echo   2. Standard     (500 games, depth 9,  5 cores)
echo   3. Accurate     (200 games, depth 10, 5 cores)
echo   4. Large        (1000 games, depth 9, 5 cores)
echo   5. Custom
echo   6. Append to existing dataset (custom games)
echo.
echo  -- With a second engine e.g. Stockfish --
echo   7. Stepbot vs Custom engine
echo   8. Custom engine vs itself  (Stepbot scores positions)
echo   9. Append using a second engine
echo.
set /p choice="Enter choice (1-9): "
cd /d "%~dp0"

if "%choice%"=="1" goto run1
if "%choice%"=="2" goto run2
if "%choice%"=="3" goto run3
if "%choice%"=="4" goto run4
if "%choice%"=="5" goto run5
if "%choice%"=="6" goto run6
if "%choice%"=="7" goto run7
if "%choice%"=="8" goto run8
if "%choice%"=="9" goto run9
echo Invalid choice.
goto end

:run1
python generate_training_data.py --games 200 --depth 9 --cores 5
goto end

:run2
python generate_training_data.py --games 500 --depth 9 --cores 5
goto end

:run3
python generate_training_data.py --games 200 --depth 10 --cores 5
goto end

:run4
python generate_training_data.py --games 1000 --depth 9 --cores 5
goto end

:run5
set /p games="Number of games: "
set /p depth="Depth (9 or 10): "
set /p cores="Cores (recommend 5): "
python generate_training_data.py --games !games! --depth !depth! --cores !cores!
goto end

:run6
echo.
echo How many games to append?
echo   Rough time guide at depth 9 with 5 cores:
echo     50  games  =  ~15 minutes
echo     100 games  =  ~30 minutes
echo     200 games  =  ~1 hour
echo     500 games  =  ~2.5 hours
echo     1000 games =  ~5 hours
echo.
set /p games="Number of games to append: "
set /p depth="Depth (9 or 10, default 9): "
if "!depth!"=="" set depth=9
set /p cores="Cores (default 5): "
if "!cores!"=="" set cores=5
python generate_training_data.py --games !games! --depth !depth! --cores !cores! --append
goto end

:run7
echo.
echo Stepbot (white) vs custom engine (black).
echo Stepbot evaluates all positions for consistent scores.
echo.
set /p engine2="Path to custom engine: "
set /p games="Number of games (default 200): "
if "!games!"=="" set games=200
set /p depth="Depth (default 9): "
if "!depth!"=="" set depth=9
set /p cores="Cores (default 5): "
if "!cores!"=="" set cores=5
python generate_training_data.py --mode stepbot_vs_custom --engine2 "!engine2!" --games !games! --depth !depth! --cores !cores!
goto end

:run8
echo.
echo Custom engine plays both sides. Stepbot evaluates positions.
echo Great for generating diverse positions from stronger play.
echo.
set /p engine2="Path to custom engine: "
set /p games="Number of games (default 200): "
if "!games!"=="" set games=200
set /p depth="Depth (default 9): "
if "!depth!"=="" set depth=9
set /p cores="Cores (default 5): "
if "!cores!"=="" set cores=5
python generate_training_data.py --mode custom_vs_custom --engine2 "!engine2!" --games !games! --depth !depth! --cores !cores!
goto end

:run9
echo.
echo Append games using a second engine to existing dataset.
echo.
echo Modes:
echo   a - Stepbot vs custom engine
echo   b - Custom engine vs itself
echo.
set /p submode="Choose mode (a or b): "
set mode=
if /i "!submode!"=="a" set mode=stepbot_vs_custom
if /i "!submode!"=="b" set mode=custom_vs_custom
if "!mode!"=="" (
    echo Invalid sub-choice.
    goto end
)
set /p engine2="Path to custom engine: "
set /p games="Number of games to append (default 200): "
if "!games!"=="" set games=200
set /p depth="Depth (default 9): "
if "!depth!"=="" set depth=9
set /p cores="Cores (default 5): "
if "!cores!"=="" set cores=5
python generate_training_data.py --mode !mode! --engine2 "!engine2!" --games !games! --depth !depth! --cores !cores! --append
goto end

:end
echo.
pause
