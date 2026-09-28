# Stepbot ♟️

<img width="1000" height="980" alt="Image" src="https://github.com/user-attachments/assets/50b2b414-357a-4ea4-98d7-6b91f7870df8" />

A chess engine built from scratch in C++, with the long-term goal of one day rivalling Stockfish.

---

## Getting Started

### Requirements

- **C++ engine:** g++ with C++17 support (MSYS2 MinGW x64 on Windows)
- **Python tools:** Python 3.8+ (legacy self-play engine uses the standard library only)
- **NNUE training:** `python-chess` — install with `pip install -r Requirements.txt`
- No other external dependencies for the C++ engine — fully self-contained

### Building the engine

From the project root:

```bash
make
```

This compiles the C++ sources in `scr/` and produces `scr/stepbot.exe` (Windows) or `scr/stepbot` (Linux/Mac).

Other build targets:

```bash
make test          # smoke tests
make bench         # search benchmarks
make clean         # remove build artifacts
make rebuild       # clean + build
```

### Running the engine

```bash
./scr/stepbot.exe
```

Starts Stepbot in UCI mode, ready to receive commands.

On Windows you can also double-click `scr/stepbot.bat` or `scr/stepbot.exe`.

---

## Playing Against Stepbot in Lucas Chess

Stepbot works with any UCI-compatible chess GUI. The recommended option is **Lucas Chess** (free).

### Step 1 — Build the engine (one time only)

1. Install **MSYS2** from [msys2.org](https://www.msys2.org)
2. Open the **MSYS2 MinGW x64** terminal and run:
   ```
   pacman -S mingw-w64-x86_64-gcc
   ```
3. Open a command prompt in the project folder and run:
   ```
   make
   ```
4. `scr/stepbot.exe` will appear after a successful build

Recompile only when you change the C++ source files.

### Step 2 — Add Stepbot to Lucas Chess

1. Open Lucas Chess
2. Go to **Engines → Manage engines**
3. Click **Add** and browse to `scr/stepbot.exe`
4. Lucas Chess auto-detects it as a UCI engine
5. Set the **MaxDepth** option (recommended: 9 for fast games, 11 for longer games)
6. Start a game!

---

## Project Structure

```
Stepbot_chess/
├── README.md
├── Makefile                   ← delegates to scr/Makefile
├── Requirements.txt           ← Python deps (NNUE training)
├── opening_book.json          ← opening book data (shared by C++ and Python)
│
├── scr/                       ← C++ engine (primary)
│   ├── main.cpp               ← UCI protocol and entry point
│   ├── board.*                ← board representation
│   ├── movegen.*              ← legal move generation
│   ├── evaluate.*             ← position evaluation
│   ├── search.*               ← alpha-beta search
│   ├── zobrist.*              ← Zobrist hashing / transposition table
│   ├── smoke_tests.cpp        ← unit smoke tests
│   ├── bench_tests.cpp        ← search benchmarks
│   ├── stepbot.bat            ← double-click launcher (Windows)
│   ├── stepbot_screensaver.cpp
│   └── Makefile
│
├── python/                    ← legacy Python engine + training tools
│   ├── board.py, engine.py, movegen.py, search.py, evaluate.py, zobrist.py
│   ├── book.py                ← opening book loader (Python)
│   ├── run.py                 ← run the Python engine in UCI mode
│   ├── selfplay.py            ← self-play with ELO tracking
│   ├── analyse.py             ← blunder detection and game analysis
│   ├── tune.py                ← Texel tuning for evaluation weights
│   └── paths.py               ← shared project paths
│
├── training/                  ← NNUE training data generation
│   ├── generate_training_data.py
│   ├── worker_game.py
│   └── generate_training_data.bat
│
├── Training_Data/             ← generated NNUE positions (CSV + stats)
│   ├── positions.csv
│   └── stats.json
│
├── Self_play/                 ← launchers + generated self-play output
│   ├── selfplay.bat
│   ├── analyse.bat
│   ├── tune.bat
│   ├── selfplay_games.pgn     ← generated
│   ├── selfplay_analysis.pgn  ← generated
│   ├── elo_history.json       ← generated
│   └── tuned_weights.json     ← generated
│
└── docs/
    └── Stepbot NNUE plan.md   ← NNUE architecture research and roadmap
```

---

## How It Works

### Board Representation
Mailbox array — 64 integers. Positive = White, negative = Black, zero = empty.

### Move Generation
Generates all legal moves including castling, en passant, and promotion. Filters moves that leave the king in check.

### Evaluation
Scores positions in centipawns (100 = one pawn). Uses **tapered evaluation** — scores are smoothly interpolated between middlegame and endgame based on remaining material, using separate piece-square tables for each phase.

Evaluation components:
- Material and piece-square tables (tapered MG/EG)
- Pawn structure (doubled, isolated, passed pawns)
- King safety (pawn shield, attack zone, escape squares, queen proximity)
- Piece mobility
- Bishop pair bonus
- Rook on open/semi-open files
- Rook on seventh rank
- Knight outposts

### Search
Alpha-beta pruning with iterative deepening and the following enhancements:
- Quiescence search
- Transposition table (Zobrist hashing)
- Move ordering (TT move, MVV-LVA captures, killer moves, history heuristic)
- Null move pruning
- Late move reductions (LMR)
- Principal variation search (PVS)
- Aspiration windows
- Futility pruning

### Opening Book
JSON book covering main lines for both colours — Sicilian, Ruy Lopez, King's Indian, Queen's Gambit, London System, French, and Caro-Kann. Uses weighted random selection for variety. Weights update automatically after self-play sessions. Turn the book off with the `UseBook` UCI option.

### Time Management
When playing with a clock, Stepbot allocates time based on estimated moves remaining, incrementally deepens until the soft time limit is reached, and respects a hard limit to avoid flagging. The `MaxDepth` UCI option caps search depth regardless of time.

### Self-Play & Training
The **C++ engine** is what you play against in GUIs. The **Python tools** in `python/` provide self-play, analysis, and Texel tuning using a legacy Python implementation of the engine. NNUE training data is generated separately in `training/` by running parallel games through the compiled C++ engine.

---

## UCI Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `MaxDepth` | spin (1-20) | 9 | Maximum search depth per move |
| `UseBook` | check | true | Use the opening book for the first moves |

To set an option manually:
```
setoption name MaxDepth value 11
```

---

## UCI Commands

| Command | Description |
|---------|-------------|
| `uci` | Identify the engine |
| `isready` | Check engine is ready |
| `ucinewgame` | Reset for a new game |
| `position startpos` | Set up starting position |
| `position startpos moves e2e4 e7e5` | Starting position + moves |
| `position fen <fen>` | Set up from FEN string |
| `go depth 9` | Search to depth 9 |
| `go movetime 5000` | Search for 5 seconds |
| `go wtime 60000 btime 60000` | Search with clock (milliseconds) |
| `go infinite` | Search indefinitely (ignores MaxDepth) |
| `print` | Print the current board (debug) |
| `fen` | Print current FEN (debug) |
| `moves` | List all legal moves (debug) |
| `quit` | Exit |

---

## Python Tools

These use the **legacy Python engine** in `python/`. They are useful for self-play experiments, analysis, and weight tuning while NNUE development continues.

### Self-play

Double-click `Self_play/selfplay.bat`, or run:

```bash
python python/selfplay.py                        # 10 games at depth 3
python python/selfplay.py --games 20 --depth 4   # 20 games at depth 4
python python/selfplay.py --no-update            # play without updating the book
```

### Game analysis

Double-click `Self_play/analyse.bat`, or run:

```bash
python python/analyse.py                        # analyse selfplay_games.pgn
python python/analyse.py --depth 4              # more accurate, slower
python python/analyse.py --input my_games.pgn   # analyse any PGN file
```

Outputs an annotated PGN to `Self_play/selfplay_analysis.pgn`.

### Texel tuning

Double-click `Self_play/tune.bat`, or run:

```bash
python python/tune.py                    # 200 iterations
python python/tune.py --iterations 500   # deeper tuning
```

Saves optimised weights to `Self_play/tuned_weights.json`.

---

## NNUE Training Data

Generate labelled positions from C++ engine self-play for future NNUE training.

Double-click `training/generate_training_data.bat`, or run:

```bash
python training/generate_training_data.py
python training/generate_training_data.py --games 500 --depth 9 --cores 5
python training/generate_training_data.py --append
```

Requires a built engine at `scr/stepbot.exe` and `pip install -r Requirements.txt`.

Output is written to `Training_Data/positions.csv` with run statistics in `Training_Data/stats.json`.

See `docs/Stepbot NNUE plan.md` for the full NNUE architecture roadmap.

---

## Windows Screensaver (optional)

A chess-themed screensaver is included. From `scr/` in an MSYS2 MinGW x64 shell:

```bash
./build_and_install_screen_saver.bat
```

This builds `Stepbot.scr` and copies it to `System32` (Administrator may be required). Select **Stepbot** under Settings → Personalisation → Lock screen → Screen saver.

---

## Roadmap

### ✅ Phase 1 — Foundation
- [x] Board representation
- [x] Legal move generation
- [x] Alpha-beta search with iterative deepening
- [x] Quiescence search
- [x] Material + piece-square table evaluation
- [x] UCI protocol

### ✅ Phase 2 — Play Stronger
- [x] Transposition table (Zobrist hashing)
- [x] Improved move ordering (killer moves, history heuristic)
- [x] Pawn structure evaluation
- [x] King safety evaluation
- [x] Piece mobility evaluation
- [x] Endgame detection

### ✅ Phase 3 — Opening Book
- [x] JSON opening book
- [x] Self-play line weighting

### ✅ Phase 4 — Self-Play & Training
- [x] Self-play engine with PGN logging
- [x] Blunder detection and analysis
- [x] Texel tuning
- [x] ELO tracking

### ✅ Phase 4.5 — Windows Executable
- [x] C++ build system (Makefile)
- [x] Self-play / analyse / tune launchers

### ✅ Phase 5 — C++ Port
- [x] Full C++ engine (board, movegen, evaluate, search, UCI)
- [x] Makefile build system

### ✅ Phase 6 — Search Improvements
- [x] Null move pruning
- [x] Late move reductions (LMR)
- [x] Time management

### ✅ Phase 7 — Search Refinements
- [x] Aspiration windows
- [x] Principal variation search (PVS)
- [x] Futility pruning
- [x] MaxDepth UCI option

### ✅ Phase 8 — Evaluation Improvements
- [x] Rook on open/semi-open file bonus
- [x] Rook on seventh rank bonus
- [x] Knight outpost detection
- [x] Tapered evaluation (smooth MG/EG interpolation)

### 🔲 Phase 9 — NNUE Neural Evaluation
- [x] Generate training positions from self-play
- [ ] Train a small neural network
- [ ] Integrate NNUE into the search

---

*Built by James — a C++ chess engine that learns as it grows.*
