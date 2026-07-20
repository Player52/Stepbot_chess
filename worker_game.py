# worker_game.py
# Run by generate_training_data.py as a separate process.
# Usage: python worker_game.py <engine_path> <num_games> <depth> <out_file> <worker_id> [engine2_path] [mode] [debug]
# mode: stepbot_vs_stepbot (default), stepbot_vs_custom, custom_vs_custom
# debug: 1 = print all UCI traffic to stderr

import subprocess
import sys
import os
import time
import random
import chess

ENGINE_PATH  = sys.argv[1]
NUM_GAMES    = int(sys.argv[2])
DEPTH        = int(sys.argv[3])
OUT_FILE     = sys.argv[4]
WORKER_ID    = int(sys.argv[5])
ENGINE2_PATH = sys.argv[6] if len(sys.argv) > 6 and sys.argv[6] else None
MODE         = sys.argv[7] if len(sys.argv) > 7 else 'stepbot_vs_stepbot'
DEBUG        = len(sys.argv) > 8 and sys.argv[8] == '1'

# Seed randomness uniquely per worker so parallel workers diverge
random.seed(os.getpid() ^ int(time.time() * 1000) ^ (WORKER_ID * 2654435761))

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

# ── Debug logging ──────────────────────────────────────────────────────────────

def log(tag, direction, msg):
    """Print a debug line to stderr. Only when DEBUG=True."""
    if not DEBUG:
        return
    arrow = '>>' if direction == 'send' else '<<'
    # Only print the first line of multi-line messages to keep output readable
    first_line = msg.split('\n')[0].strip()
    print(f'[W{WORKER_ID}] {arrow} {tag:20s}  {first_line}', file=sys.stderr, flush=True)

def log_info(msg):
    if DEBUG:
        print(f'[W{WORKER_ID}]    {"--- " + msg}', file=sys.stderr, flush=True)


# ── UCI helpers ────────────────────────────────────────────────────────────────

def send(proc, cmd, tag=''):
    log(tag, 'send', cmd)
    proc.stdin.write(cmd + '\n')
    proc.stdin.flush()


def wait_for(proc, keyword, tag='', timeout=15.0):
    start = time.time()
    while time.time() - start < timeout:
        if proc.poll() is not None:
            log_info(f'{tag} process died while waiting for "{keyword}"')
            return ''
        line = proc.stdout.readline().strip()
        if line:
            log(tag, 'recv', line)
        if keyword in line:
            return line
    log_info(f'{tag} TIMEOUT waiting for "{keyword}" after {timeout}s')
    return ''


def get_move_and_score(proc, moves, tag=''):
    """Ask an engine for its best move and score given a move list."""
    pos_cmd = 'position startpos moves ' + ' '.join(moves) if moves else 'position startpos'
    send(proc, pos_cmd, tag)
    send(proc, f'go depth {DEPTH}', tag)

    score_cp  = None
    best_move = None
    start     = time.time()

    while time.time() - start < 120.0:
        if proc.poll() is not None:
            log_info(f'{tag} process died while waiting for bestmove')
            break
        line = proc.stdout.readline().strip()
        if not line:
            continue
        log(tag, 'recv', line)

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

    if best_move is None:
        log_info(f'{tag} WARNING: no bestmove received!')

    return score_cp, best_move


def play_game(proc_white, proc_black, score_engine,
              white_tag, black_tag, score_tag,
              min_move=8, max_moves=150,
              random_plies=10, random_move_prob=0.15):
    """
    Play one game between proc_white and proc_black.
    score_engine evaluates positions for training data (always Stepbot).
    Random moves are injected early to ensure game variety at high depths.
    """
    opening = random.choice(OPENINGS)
    board   = chess.Board()
    moves   = []

    for uci in opening:
        try:
            board.push_uci(uci)
            moves.append(uci)
        except Exception:
            break

    log_info(f'Game start — opening: {opening if opening else "startpos"}')

    # Signal start of new game to all engines
    send(proc_white, 'ucinewgame', white_tag)
    send(proc_black, 'ucinewgame', black_tag)
    if score_engine is not proc_white and score_engine is not proc_black:
        send(score_engine, 'ucinewgame', score_tag)

    positions = []

    for move_num in range(max_moves):
        if board.is_game_over():
            log_info(f'Game over at move {move_num}: {board.result()}')
            break

        active_proc = proc_white if board.turn == chess.WHITE else proc_black
        active_tag  = white_tag  if board.turn == chess.WHITE else black_tag
        side        = 'White' if board.turn == chess.WHITE else 'Black'

        log_info(f'Move {move_num + 1} — {side} to play ({active_tag})')

        score_cp, best_move = get_move_and_score(active_proc, moves, active_tag)

        if best_move is None or best_move == '0000':
            log_info(f'{active_tag} returned null/0000 move — stopping game')
            break

        # Randomly deviate early in the game to produce diverse positions
        if move_num < random_plies and random.random() < random_move_prob:
            legal = list(board.legal_moves)
            if legal:
                rand_move = random.choice(legal).uci()
                log_info(f'Random move injection: {best_move} -> {rand_move}')
                best_move = rand_move
                score_cp  = None

        pre_fen = board.fen()

        try:
            board.push_uci(best_move)
        except Exception as e:
            log_info(f'Illegal move {best_move}: {e}')
            break

        moves.append(best_move)

        if move_num < min_move:
            continue
        if score_cp is None:
            continue
        if abs(score_cp) >= 29000:
            continue

        # If a non-scoring engine made the move, re-evaluate with score_engine
        if active_proc is not score_engine:
            log_info(f'Re-evaluating with {score_tag} for training score')
            score_cp, _ = get_move_and_score(score_engine, moves[:-1], score_tag)
            if score_cp is None or abs(score_cp) >= 29000:
                continue

        positions.append((pre_fen, score_cp))

    log_info(f'Game ended — {len(positions)} positions collected')
    return positions


# ── Engine launch ──────────────────────────────────────────────────────────────

def launch_engine(path, is_stepbot=True, tag='Engine'):
    """Start a UCI engine process and perform handshake."""
    log_info(f'Launching {tag}: {path}')
    proc = subprocess.Popen(
        [path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        bufsize=1,
    )

    send(proc, 'uci', tag)
    r = wait_for(proc, 'uciok', tag, timeout=20.0)
    if not r:
        print(f'ERROR: No uciok from {tag} ({path})', file=sys.stderr, flush=True)
        sys.exit(1)

    if is_stepbot:
        send(proc, f'setoption name MaxDepth value {max(DEPTH, 20)}', tag)
        send(proc, 'setoption name UseBook value false', tag)

    send(proc, 'isready', tag)
    r = wait_for(proc, 'readyok', tag, timeout=20.0)
    if not r:
        print(f'ERROR: No readyok from {tag} ({path})', file=sys.stderr, flush=True)
        sys.exit(1)

    log_info(f'{tag} ready.')
    return proc


def quit_engine(proc, tag=''):
    try:
        send(proc, 'quit', tag)
        proc.wait(timeout=5)
    except Exception:
        proc.kill()


# ── Determine engine roles based on mode ──────────────────────────────────────

if MODE == 'stepbot_vs_stepbot':
    proc_white   = launch_engine(ENGINE_PATH, is_stepbot=True,  tag='Stepbot-White')
    proc_black   = launch_engine(ENGINE_PATH, is_stepbot=True,  tag='Stepbot-Black')
    score_engine = proc_white
    white_tag    = 'Stepbot-White'
    black_tag    = 'Stepbot-Black'
    score_tag    = 'Stepbot-White'

elif MODE == 'stepbot_vs_custom':
    if not ENGINE2_PATH or not os.path.exists(ENGINE2_PATH):
        print(f'ERROR: ENGINE2_PATH not provided or not found: {ENGINE2_PATH}', file=sys.stderr, flush=True)
        sys.exit(1)
    engine2_name = os.path.splitext(os.path.basename(ENGINE2_PATH))[0].capitalize()
    proc_white   = launch_engine(ENGINE_PATH,  is_stepbot=True,  tag='Stepbot-White')
    proc_black   = launch_engine(ENGINE2_PATH, is_stepbot=False, tag=f'{engine2_name}-Black')
    score_engine = proc_white
    white_tag    = 'Stepbot-White'
    black_tag    = f'{engine2_name}-Black'
    score_tag    = 'Stepbot-White'

elif MODE == 'custom_vs_custom':
    if not ENGINE2_PATH or not os.path.exists(ENGINE2_PATH):
        print(f'ERROR: ENGINE2_PATH not provided or not found: {ENGINE2_PATH}', file=sys.stderr, flush=True)
        sys.exit(1)
    engine2_name = os.path.splitext(os.path.basename(ENGINE2_PATH))[0].capitalize()
    proc_white   = launch_engine(ENGINE2_PATH, is_stepbot=False, tag=f'{engine2_name}-White')
    proc_black   = launch_engine(ENGINE2_PATH, is_stepbot=False, tag=f'{engine2_name}-Black')
    score_engine = launch_engine(ENGINE_PATH,  is_stepbot=True,  tag='Stepbot-Scorer')
    white_tag    = f'{engine2_name}-White'
    black_tag    = f'{engine2_name}-Black'
    score_tag    = 'Stepbot-Scorer'

else:
    print(f'ERROR: Unknown mode "{MODE}"', file=sys.stderr, flush=True)
    sys.exit(1)


# ── Play games — save after each one ──────────────────────────────────────────

os.makedirs(os.path.dirname(OUT_FILE), exist_ok=True)
total_positions = 0

with open(OUT_FILE, 'w', encoding='utf-8') as out_f:
    for game_idx in range(NUM_GAMES):
        log_info(f'========== GAME {game_idx + 1}/{NUM_GAMES} ==========')
        positions = play_game(
            proc_white, proc_black, score_engine,
            white_tag, black_tag, score_tag
        )
        for fen, score in positions:
            out_f.write(f'{fen}|||{score}\n')
        out_f.flush()
        total_positions += len(positions)
        print(f'PROGRESS {game_idx + 1} {NUM_GAMES} {len(positions)}', flush=True)

quit_engine(proc_white, white_tag)
quit_engine(proc_black, black_tag)
if MODE == 'custom_vs_custom':
    quit_engine(score_engine, score_tag)

print(f'DONE {total_positions}', flush=True)
