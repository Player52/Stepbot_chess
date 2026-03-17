# worker_game.py
# Run by generate_training_data.py as a separate process.
# Usage: python worker_game.py <engine_path> <num_games> <depth> <out_file> <worker_id>

import subprocess
import sys
import os
import time
import random
import json
import chess

ENGINE_PATH = sys.argv[1]
NUM_GAMES   = int(sys.argv[2])
DEPTH       = int(sys.argv[3])
OUT_FILE    = sys.argv[4]
WORKER_ID   = int(sys.argv[5])

OPENINGS = [
    [],
    ['e2e4'],
    ['d2d4'],
    ['c2c4'],
    ['g1f3'],
    ['e2e4', 'e7e5'],
    ['e2e4', 'c7c5'],
    ['e2e4', 'e7e6'],
    ['e2e4', 'c7c6'],
    ['d2d4', 'd7d5'],
    ['d2d4', 'g8f6'],
    ['e2e4', 'e7e5', 'g1f3'],
    ['e2e4', 'c7c5', 'g1f3'],
    ['d2d4', 'd7d5', 'c2c4'],
    ['e2e4', 'e7e5', 'g1f3', 'b8c6'],
    ['d2d4', 'g8f6', 'c2c4', 'e7e6'],
    ['e2e4', 'e7e5', 'f1c4'],
    ['d2d4', 'd7d5', 'c2c4', 'e7e6'],
    ['e2e4', 'e7e5', 'g1f3', 'g8f6'],
    ['c2c4', 'e7e5'],
]


def send(proc, cmd):
    proc.stdin.write(cmd + '\n')
    proc.stdin.flush()


def wait_for(proc, keyword, timeout=15.0):
    start = time.time()
    while time.time() - start < timeout:
        if proc.poll() is not None:
            return ''
        line = proc.stdout.readline().strip()
        if keyword in line:
            return line
    return ''


def get_move_and_score(proc, moves):
    if moves:
        send(proc, 'position startpos moves ' + ' '.join(moves))
    else:
        send(proc, 'position startpos')
    send(proc, f'go depth {DEPTH}')

    score_cp  = None
    best_move = None
    start     = time.time()

    while time.time() - start < 120.0:
        if proc.poll() is not None:
            break
        line = proc.stdout.readline().strip()
        if not line:
            continue
        if line.startswith('info') and 'score cp' in line:
            parts = line.split()
            try:
                idx      = parts.index('cp')
                score_cp = int(parts[idx + 1])
            except Exception:
                pass
        elif line.startswith('info') and 'score mate' in line:
            parts = line.split()
            try:
                idx      = parts.index('mate')
                m        = int(parts[idx + 1])
                score_cp = 29000 if m > 0 else -29000
            except Exception:
                pass
        elif line.startswith('bestmove'):
            parts     = line.split()
            best_move = parts[1] if len(parts) > 1 else None
            break

    return score_cp, best_move


def play_game(proc, min_move=8, max_moves=150):
    send(proc, 'ucinewgame')

    opening = random.choice(OPENINGS)
    board   = chess.Board()
    moves   = []

    for uci in opening:
        try:
            board.push_uci(uci)
            moves.append(uci)
        except Exception:
            break

    positions = []

    for move_num in range(max_moves):
        if board.is_game_over():
            break

        score_cp, best_move = get_move_and_score(proc, moves)

        if best_move is None or best_move == '0000':
            break

        pre_fen = board.fen()

        try:
            board.push_uci(best_move)
        except Exception:
            break

        moves.append(best_move)

        if move_num < min_move:
            continue
        if score_cp is None:
            continue
        if abs(score_cp) >= 29000:
            continue

        positions.append((pre_fen, score_cp))

    return positions


# ── Launch engine ──
proc = subprocess.Popen(
    [ENGINE_PATH],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.DEVNULL,
    text=True,
    bufsize=1,
)

send(proc, 'uci')
r = wait_for(proc, 'uciok')
if not r:
    print(f'ERROR: No uciok from engine', file=sys.stderr)
    sys.exit(1)

send(proc, f'setoption name MaxDepth value {DEPTH}')
send(proc, 'setoption name UseBook value false')
send(proc, 'isready')
r = wait_for(proc, 'readyok')
if not r:
    print(f'ERROR: No readyok from engine', file=sys.stderr)
    sys.exit(1)

# ── Play games ──
all_positions = []
for game_idx in range(NUM_GAMES):
    positions = play_game(proc)
    all_positions.extend(positions)
    print(f'PROGRESS {game_idx + 1} {NUM_GAMES} {len(positions)}',
          flush=True)

send(proc, 'quit')
try:
    proc.wait(timeout=5)
except Exception:
    proc.kill()

# ── Write results ──
os.makedirs(os.path.dirname(OUT_FILE), exist_ok=True)
with open(OUT_FILE, 'w', encoding='utf-8') as f:
    for fen, score in all_positions:
        f.write(f'{fen}|||{score}\n')

print(f'DONE {len(all_positions)}', flush=True)
