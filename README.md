# Stepbot ♟️

A chess engine built from scratch in C++, with the long-term goal of one day rivalling Stockfish.

---

## Getting Started

### Requirements
- A C++17 compatible compiler (g++ recommended)
- On Windows: MSYS2 MinGW x64
- No external dependencies — fully self-contained

### Building the engine

```bash
make
```

This compiles all C++ source files and produces `stepbot.exe` (Windows) or `stepbot` (Linux/Mac).

### Running the engine

```bash
./stepbot.exe
```

Starts Stepbot in UCI mode, ready to receive commands.

### On Windows

Double-click `stepbot.exe` to launch without opening a terminal manually. _This will also work for the same file on Linux or Mac_

---

## Playing Against Stepbot in Lucas Chess

Stepbot works with any UCI-compatible chess GUI. The recommended option is **Lucas Chess** (free).

### Step 1 — Build the engine (one time only)

1. Install **MSYS2** from [msys2.org](https://www.msys2.org)
2. Open the **MSYS2 MinGW x64** terminal and run:
   ```
   pacman -S mingw-w64-x86_64-gcc
   ```
3. Open a command prompt in your Stepbot_chess folder and run:
   ```
   make
   ```
4. `stepbot.exe` will appear in your folder

After this, you only need to recompile when you update the source files.

### Step 2 — Add Stepbot to Lucas Chess

1. Open Lucas Chess
2. Go to **Engines → Manage engines**
3. Click **Add** and browse to `stepbot.exe`
4. Lucas Chess auto-detects it as a UCI engine
5. Set the **MaxDepth** option (recommended: 9 for fast games, 11 for longer games)
6. Start a game!

---

## Project Structure

```
Stepbot_chess/
├── README.md                  ← This file
├── CONTRIBUTING.md            ← Contribution guidelines
├── CODE_OF_CONDUCT.md         ← Code of conduct
├── SECURITY.md                ← Security policy
├── Makefile                   ← Build system (run 'make' to compile)
├── stepbot.bat                ← Windows double-click launcher
├── stepbot_wrapper.cpp        ← Legacy Python wrapper (no longer needed)
├── main.cpp                   ← UCI protocol interface and entry point
├── board.h / board.cpp        ← Board representation and helpers
├── movegen.h / movegen.cpp    ← Legal move generation
├── evaluate.h / evaluate.cpp  ← Position evaluation
├── search.h / search.cpp      ← Alpha-beta search engine
├── zobrist.h / zobrist.cpp    ← Zobrist hashing for transposition table
├── opening_book.json          ← Opening book data
├── selfplay.py                ← Self-play engine with ELO tracking
├── analyse.py                 ← Blunder detection and game analysis
├── tune.py                    ← Texel tuning for evaluation weights
└── Self_play/
    ├── selfplay.bat           ← Launch self-play sessions
    ├── analyse.bat            ← Launch game analysis
    ├── tune.bat               ← Launch Texel tuner
    ├── selfplay_games.pgn     ← All self-play games (generated)
    ├── selfplay_analysis.pgn  ← Annotated analysis (generated)
    ├── elo_history.json       ← ELO rating history (generated)
    └── tuned_weights.json     ← Tuned evaluation weights (generated)
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
JSON book covering main lines for both colours — Sicilian, Ruy Lopez, King's Indian, Queen's Gambit, London System, French, and Caro-Kann. Uses weighted random selection for variety. Weights update automatically after self-play sessions.

### Time Management
When playing with a clock, Stepbot allocates time based on estimated moves remaining, incrementally deepens until the soft time limit is reached, and respects a hard limit to avoid flagging. The `MaxDepth` UCI option caps search depth regardless of time.

### Self-Play & Training
Stepbot can play against itself to improve over time. Each session logs games as PGN, updates opening book weights, and tracks an ELO rating. Games can be analysed for blunders and evaluation weights can be tuned using Texel tuning.

---

## UCI Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `MaxDepth` | spin (1-20) | 9 | Maximum search depth per move |

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

## Self-Play Tools

### Running a self-play session

Double-click `Self_play/selfplay.bat`, or run directly:

```bash
python selfplay.py                        # 10 games at depth 3
python selfplay.py --games 20 --depth 4   # 20 games at depth 4
python selfplay.py --no-update            # play without updating the book
```

### Analysing games

Double-click `Self_play/analyse.bat`, or run directly:

```bash
python analyse.py                        # analyse selfplay_games.pgn
python analyse.py --depth 4              # more accurate, slower
python analyse.py --input my_games.pgn   # analyse any PGN file
```

Outputs an annotated PGN to `Self_play/selfplay_analysis.pgn`.

### Texel tuning

Double-click `Self_play/tune.bat`, or run directly:

```bash
python tune.py                    # 200 iterations
python tune.py --iterations 500   # deeper tuning
```

Saves optimised weights to `tuned_weights.json`.

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
- [ ] Generate training positions from self-play
- [ ] Train a small neural network
- [ ] Integrate NNUE into the search

---

*Built by James — a C++ chess engine that learns as it grows.*
