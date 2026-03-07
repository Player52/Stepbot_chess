# Stepbot ♟️

A chess engine built from scratch in Python, with the long-term goal of one day rivalling Stockfish.

---

## Getting Started

### Requirements
- Python 3.8 or higher
- No external Python packages needed

### Running the engine

```bash
python run.py
```

Starts Stepbot in UCI mode, ready to receive commands.

### Running tests

```bash
python run.py --test
```

Runs sanity checks on the board, move generator, and evaluator.

### On Windows

Double-click `stepbot.bat` to launch without opening a terminal manually.

---

## Playing Against Stepbot in Lucas Chess

Stepbot works with any UCI-compatible chess GUI. The recommended option is **Lucas Chess** (free).

### Step 1 — Compile the wrapper (one time only)

Stepbot needs a `.exe` file so Lucas Chess (or any other chess platform) can find it. The wrapper is a tiny file that just launches Python — you only need to compile it once.

1. Install **MSYS2** from [msys2.org](https://www.msys2.org)
2. Open the MSYS2 terminal and run:
   ```
   pacman -S mingw-w64-x86_64-gcc
   ```
3. Open a command prompt in your Stepbot_chess folder and run:
   ```
   g++ -o stepbot.exe stepbot_wrapper.cpp -m64 -static -lkernel32
   ```
4. `stepbot.exe` will appear in your folder

After this, you never need to recompile — updating the Python files updates the engine automatically.

### Step 2 — Add Stepbot to Lucas Chess

1. Open Lucas Chess
2. Go to **Engines → Manage engines**
3. Click **Add** and browse to `stepbot.exe`
4. Lucas Chess auto-detects it as a UCI engine
5. Set depth or time controls as you like
6. Start a game!

---

## Project Structure

```
Stepbot_chess/
├── README.md               ← This file
├── requirements.txt        ← Dependencies and build instructions
├── stepbot.bat             ← Windows double-click launcher
├── stepbot_wrapper.cpp     ← C++ source for the Lucas Chess wrapper
├── stepbot.exe             ← Compiled wrapper (after you build it)
├── run.py                  ← Main entry point
├── engine.py               ← UCI protocol interface
├── board.py                ← Board representation and helpers
├── movegen.py              ← Legal move generation
├── evaluate.py             ← Position evaluation
├── search.py               ← Alpha-beta search with iterative deepening
├── zobrist.py              ← Zobrist hashing for the transposition table
├── book.py                 ← Opening book loader
└── opening_book.json       ← Opening book data
```

---

## How It Works

### Board Representation
Mailbox array — a list of 64 integers. Positive = White, negative = Black, zero = empty.

### Move Generation
Generates all legal moves including castling, en passant, and promotion. Filters moves that leave the king in check.

### Evaluation
Scores positions in centipawns (100 = one pawn). Includes material, piece-square tables, pawn structure, king safety, piece mobility, and endgame detection.

### Search
Alpha-beta pruning with iterative deepening, quiescence search, transposition table (Zobrist hashing), killer moves, and history heuristic.

### Opening Book
JSON book covering main lines for both colours — Sicilian, Ruy Lopez, King's Indian, Queen's Gambit, London System, French, and Caro-Kann. Uses weighted random selection for variety.

---

## UCI Commands

| Command | Description |
|---|---|
| `uci` | Identify the engine |
| `isready` | Check engine is ready |
| `ucinewgame` | Reset for a new game |
| `position startpos` | Set up starting position |
| `position startpos moves e2e4 e7e5` | Starting position + moves |
| `position fen <fen>` | Set up from FEN string |
| `go depth 4` | Search to depth 4 |
| `go movetime 1000` | Search for 1 second |
| `print` | Print the current board (debug) |
| `fen` | Print current FEN (debug) |
| `moves` | List all legal moves (debug) |
| `quit` | Exit |

---

## Roadmap

### ✅ Phase 1 — Foundation
- [x] Board representation
- [x] Legal move generation (all pieces, castling, en passant, promotion)
- [x] Alpha-beta search with iterative deepening
- [x] Quiescence search
- [x] Material + piece-square table evaluation
- [x] UCI protocol

### ✅ Phase 2 — Play Stronger
- [x] Transposition table (Zobrist hashing)
- [x] Improved move ordering (killer moves, history heuristic)
- [x] Pawn structure evaluation (doubled, isolated, passed pawns)
- [x] King safety evaluation
- [x] Piece mobility evaluation
- [x] Endgame detection

### 🔄 Phase 3 — Opening Book
- [x] JSON opening book
- [ ] Self-play line weighting

### 🔲 Phase 4 — Self-Play & Training
- [ ] Self-play with PGN logging
- [ ] Blunder detection and analysis
- [ ] Evaluation weight tuning (Texel tuning)
- [ ] ELO tracking

### 🔲 Phase 4.5 — Windows Executable
- [x] C++ wrapper source (stepbot_wrapper.cpp)
- [ ] Compile stepbot.exe with MinGW (Optional for the user)

### 🔲 Phase 5 — C++ Port
- [ ] C++ move generator
- [ ] C++ search and evaluation
- [ ] Python/C++ UCI bridge

### 🔲 Phase 6 — Advanced
- [ ] Null move pruning
- [ ] Late move reductions
- [ ] Endgame tablebases
- [ ] NNUE neural evaluation

---

*Built by James — a Python chess engine that learns as it grows.*
