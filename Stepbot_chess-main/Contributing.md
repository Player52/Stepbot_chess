# Contributing to Stepbot

Thanks for your interest in Stepbot! This is a personal learning project — a chess engine built from scratch in C++ with the long-term goal of rivalling Stockfish. Contributions are welcome, but please read this first.

---

## What Kind of Contributions Are Welcome?

- **Bug fixes** — if you find something broken, a fix is always appreciated
- **Performance improvements** — faster move generation, better search pruning, etc.
- **Evaluation improvements** — better piece-square tables, new evaluation terms
- **Documentation** — clearer comments, better README sections
- **Testing** — scripts or tools that help measure engine strength

## What to Avoid

- Replacing core algorithms wholesale without discussion first
- Adding external library dependencies — Stepbot is intentionally self-contained
- Changes that break UCI compatibility with Lucas Chess or other GUIs

---

## How to Contribute

### 1. Fork and clone the repo

```bash
git clone https://github.com/your-username/Stepbot_chess.git
cd Stepbot_chess
```

### 2. Build the engine

You'll need g++ with C++17 support. On Windows, use MSYS2 MinGW x64.

```bash
make
```

### 3. Make your changes

- Keep changes focused — one fix or feature per pull request
- Follow the existing code style (comments, naming conventions, file structure)
- If you're touching `evaluate.cpp`, test that the engine still plays sensible moves afterwards

### 4. Test your changes

Run the engine manually and verify it works with UCI:

```bash
./stepbot
uci
isready
position startpos
go depth 6
```

If you've changed evaluation or search, consider running a quick self-play test:

```bash
python selfplay.py --games 5 --depth 4
```

### 5. Open a pull request

- Describe what you changed and why
- Include any test results if relevant
- Be patient — this is a one-person project

---

## Project Structure

| File | Purpose |
|------|---------|
| `board.h / board.cpp` | Board representation |
| `movegen.h / movegen.cpp` | Legal move generation |
| `evaluate.h / evaluate.cpp` | Position evaluation |
| `search.h / search.cpp` | Alpha-beta search |
| `zobrist.h / zobrist.cpp` | Zobrist hashing |
| `main.cpp` | UCI protocol interface |
| `Makefile` | Build system |
| `selfplay.py` | Self-play and ELO tracking |
| `analyse.py` | Game analysis and blunder detection |
| `tune.py` | Texel tuning |

---

## Roadmap

See the roadmap section in [README.md](README.md) for what's planned next. If you want to work on something from the roadmap, open an issue first so we can discuss the approach.

---

*Stepbot is built by James as a learning project. Be kind, be constructive, and enjoy the chess.*
