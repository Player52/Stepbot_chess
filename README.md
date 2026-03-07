# Stepbot ♟️

A chess engine built from scratch in Python, with the long-term goal of one day rivalling Stockfish.

---

## Getting Started

### Requirements
- Python 3.8 or higher
- No external packages needed (yet)

### Running the engine

```bash
python run.py
```

This starts Stepbot in UCI mode, ready to receive commands.

### Running tests

```bash
python run.py --test
```

Runs sanity checks on the board, move generator, and evaluator.

### On Windows

Double-click `stepbot.bat` to launch without opening a terminal manually.

---

## Connecting to a Chess GUI

Stepbot uses the **UCI protocol**, which means it works with any standard chess GUI.

**Recommended (free):** [Arena Chess](http://www.playwitharena.de/)

1. Open Arena → Engines → Install New Engine
2. Browse to `engine.py` (or `run.py`)
3. Set the engine command to `python engine.py`
4. Start a game!

---

## Project Structure

```
stepbot/
├── run.py            ← Entry point — start here
├── engine.py         ← UCI protocol interface
├── board.py          ← Board representation and helpers
├── movegen.py        ← Legal move generation
├── evaluate.py       ← Position evaluation
├── search.py         ← Alpha-beta search with iterative deepening
├── stepbot.bat       ← Windows double-click launcher
└── requirements.txt  ← Python dependencies
```

---

## How It Works

### Board Representation
The board is a mailbox array — a list of 64 integers, one per square.
Positive numbers are White pieces, negative are Black, zero is empty.

### Move Generation
Generates all legal moves for the side to move. Handles castling, en passant,
and pawn promotion. Filters out moves that leave the king in check.

### Evaluation
Scores a position in centipawns (100 = one pawn).
Currently uses material values and piece-square tables.

### Search
Alpha-beta pruning with iterative deepening and quiescence search.
Searches 4 moves deep by default.

---

## Roadmap

### ✅ Phase 1 — Foundation
- [x] Board representation
- [x] Legal move generation (all pieces, castling, en passant, promotion)
- [x] Alpha-beta search with iterative deepening
- [x] Quiescence search
- [x] Material + piece-square table evaluation
- [x] UCI protocol

### 🔲 Phase 2 — Play Stronger
- [ ] Transposition table (Zobrist hashing)
- [ ] Improved move ordering (killer moves, history heuristic)
- [ ] Pawn structure evaluation (doubled, isolated, passed pawns)
- [ ] King safety evaluation
- [ ] Piece mobility evaluation
- [ ] Basic endgame detection

### 🔲 Phase 3 — Opening Book
- [ ] JSON opening book
- [ ] Self-play line weighting

### 🔲 Phase 4 — Self-Play & Training
- [ ] Self-play with PGN logging
- [ ] Blunder detection and analysis
- [ ] Evaluation weight tuning (Texel tuning)
- [ ] ELO tracking

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

## UCI Commands

Once running, Stepbot accepts these commands:

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

*Built by James — a Python chess engine that learns as it grows.*
