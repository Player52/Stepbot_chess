# worker_game.py — optimized worker (Tier A + adjudication + binary output)
# Run by generate_training_data.py as a separate process.
# Usage: python worker_game.py <engine_path> <num_games> <depth> <out_file> <worker_id> [engine2_path] [mode] [debug]
#
# Output: binary .bin records (see nnue_bin.py), 40 bytes per position.

import subprocess
import sys
import os
import time
import random
import threading
import queue
import chess
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nnue_bin import encode

ENGINE_PATH  = sys.argv[1]
NUM_GAMES    = int(sys.argv[2])
DEPTH        = int(sys.argv[3])
OUT_FILE     = sys.argv[4]
WORKER_ID    = int(sys.argv[5])
ENGINE2_PATH = sys.argv[6] if len(sys.argv) > 6 and sys.argv[6] else None
MODE         = sys.argv[7] if len(sys.argv) > 7 else 'stepbot_vs_stepbot'
DEBUG        = len(sys.argv) > 8 and sys.argv[8] == '1'

# Coarse profiling accumulators
time_engine_search  = 0.0
time_fen_generation = 0.0
time_game_handling  = 0.0
total_nodes_searched = 0
nps_sum   = 0
nps_count = 0

random.seed(os.getpid() ^ int(time.time() * 1000) ^ (WORKER_ID * 2654435761))

OPENINGS = [
    [], ['e2e4'], ['d2d4'], ['c2c4'], ['g1f3'],
    ['e2e4', 'e7e5'], ['e2e4', 'c7c5'], ['e2e4', 'e7e6'], ['e2e4', 'c7c6'],
    ['d2d4', 'd7d5'], ['d2d4', 'g8f6'],
    ['e2e4', 'e7e5', 'g1f3'], ['e2e4', 'c7c5', 'g1f3'],
    ['d2d4', 'd7d5', 'c2c4'],
    ['e2e4', 'e7e5', 'g1f3', 'b8c6'], ['d2d4', 'g8f6', 'c2c4', 'e7e6'],
    ['e2e4', 'e7e5', 'f1c4'], ['d2d4', 'd7d5', 'c2c4', 'e7e6'],
    ['e2e4', 'e7e5', 'g1f3', 'g8f6'], ['c2c4', 'e7e5'],
]

# ── Debug logging ─────────────────────────────────────────────────────────────

def log(tag, direction, msg):
    if not DEBUG:
        return
    arrow = '>>' if direction == 'send' else '<<'
    if isinstance(msg, bytes):
        msg = msg.decode('utf-8', 'replace')
    print(f'[W{WORKER_ID}] {arrow} {tag:20s}  {msg.split(chr(10))[0].strip()}',
          file=sys.stderr, flush=True)

def log_info(msg):
    if DEBUG:
        print(f'[W{WORKER_ID}]    --- {msg}', file=sys.stderr, flush=True)

def emit_timing(category, seconds):
    print(f'TIMING {category} {seconds:.4f}', flush=True)

def emit_metric(category, value):
    print(f'METRIC {category} {value}', flush=True)

# ── UCI helpers (binary I/O) ──────────────────────────────────────────────────
#
# We use one daemon reader thread per engine process to drain stdout into
# a queue. The previous select()-based approach silently broke on Python's
# BufferedReader: when the engine writes a full UCI burst in one go, the
# first readline() pulls ALL the bytes off the OS pipe into Python's
# internal buffer. select() then reports the FD as "not readable" because
# the OS pipe is empty — even though Python's buffer still holds the
# remaining lines (uciok etc.). Result: spurious 20s timeouts and
# "No uciok" errors. A reader thread sidesteps the whole problem.
# ─────────────────────────────────────────────────────────────────────────────

class EngineReader:
    """Drains an engine's stdout in a daemon thread, exposing a
    timed `readline()` so callers can implement real timeouts without
    ever blocking on a buffered pipe."""

    def __init__(self, proc):
        self._q   = queue.Queue()
        self._proc = proc
        self._closed = False
        t = threading.Thread(target=self._pump, daemon=True)
        t.start()

    def _pump(self):
        try:
            for line in self._proc.stdout:
                self._q.put(line)
        except Exception:
            pass
        # Sentinel: stdout has closed (engine exited or pipe broke).
        self._q.put(None)
        self._closed = True

    def readline(self, timeout=None):
        """Return the next line (bytes), b'' on EOF, or None on timeout.

        - b''   : engine's stdout closed (EOF) — caller should treat as
                  fatal and stop talking to this engine.
        - None  : timeout expired with no data — caller may retry or give up.
        - bytes : a complete line (with trailing newline preserved).
        """
        try:
            return self._q.get(timeout=timeout)
        except queue.Empty:
            return None

    def is_alive(self):
        return self._proc.poll() is None


def send(proc, cmd, tag=''):
    data = cmd if isinstance(cmd, bytes) else cmd.encode('ascii')
    log(tag, 'send', data)
    proc.stdin.write(data + b'\n')
    proc.stdin.flush()


def wait_for(reader, keyword, tag='', timeout=20.0):
    kw = keyword if isinstance(keyword, bytes) else keyword.encode('ascii')
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not reader.is_alive():
            log_info(f'{tag} process died while waiting for "{keyword}"')
            return b''
        remaining = deadline - time.time()
        line = reader.readline(timeout=min(remaining, 0.5))
        if line is None:
            continue  # no data this slice; re-check deadline & liveness
        if not line:
            # EOF — engine's stdout closed.
            log_info(f'{tag} EOF while waiting for "{keyword}"')
            return b''
        log(tag, 'recv', line)
        if kw in line:
            return line
    log_info(f'{tag} TIMEOUT waiting for "{keyword}" after {timeout}s')
    return b''


def get_move_and_score(reader, pos_cmd, tag=''):
    """pos_cmd: bytearray — full 'position startpos moves ...' for CURRENT position."""
    global time_engine_search, total_nodes_searched, nps_sum, nps_count
    proc = reader._proc

    t0 = time.time()
    proc.stdin.write(bytes(pos_cmd) + b'\n')
    proc.stdin.write(b'go depth %d\n' % DEPTH)
    proc.stdin.flush()
    if DEBUG:
        log(tag, 'send', bytes(pos_cmd))
        log(tag, 'send', b'go depth %d' % DEPTH)

    score_cp   = None
    best_move  = None
    last_info  = None
    last_score = None
    deadline   = t0 + 120.0

    while True:
        remaining = deadline - time.time()
        if remaining <= 0:
            log_info(f'{tag} TIMEOUT waiting for bestmove after 120s')
            break
        line = reader.readline(timeout=min(remaining, 0.5))
        if line is None:
            # No data this slice — make sure the engine is still alive.
            if not reader.is_alive():
                log_info(f'{tag} process died while waiting for bestmove')
                break
            continue
        if not line:
            # EOF on the pipe.
            log_info(f'{tag} EOF while waiting for bestmove')
            break
        log(tag, 'recv', line)

        if line.startswith(b'info'):
            if b'score' in line:
                last_score = line
            last_info = line
        elif line.startswith(b'bestmove'):
            parts = line.split()
            if len(parts) > 1:
                best_move = parts[1].decode('ascii')
            break

    if last_score is not None:
        p = last_score.split()
        try:
            i = p.index(b'score')
            if p[i + 1] == b'cp':
                score_cp = int(p[i + 2])
            elif p[i + 1] == b'mate':
                m = int(p[i + 2])
                score_cp = 29000 if m > 0 else -29000
        except (ValueError, IndexError):
            pass
    if last_info is not None:
        p = last_info.split()
        try:
            total_nodes_searched += int(p[p.index(b'nodes') + 1])
        except (ValueError, IndexError):
            pass
        try:
            nps_sum += int(p[p.index(b'nps') + 1])
            nps_count += 1
        except (ValueError, IndexError):
            pass

    if best_move is None:
        log_info(f'{tag} WARNING: no bestmove received!')

    time_engine_search += time.time() - t0
    return score_cp, best_move


def play_game(reader_white, reader_black, reader_score,
              white_tag, black_tag, score_tag,
              min_move=8, max_moves=150,
              random_plies=10, random_move_prob=0.15,
              adjudicate_plies=6, adjudicate_cp=1000):
    global time_engine_search, time_fen_generation, time_game_handling

    game_t0  = time.time()
    es_prev  = time_engine_search
    fen_prev = time_fen_generation

    opening = random.choice(OPENINGS)
    board   = chess.Board()

    seen = Counter()
    seen[board._transposition_key()] += 1
    for uci in opening:
        try:
            board.push_uci(uci)
            seen[board._transposition_key()] += 1
        except Exception:
            break

    log_info(f'Game start — opening: {opening if opening else "startpos"}')

    send(reader_white._proc, 'ucinewgame', white_tag)
    send(reader_black._proc, 'ucinewgame', black_tag)
    if reader_score is not reader_white and reader_score is not reader_black:
        send(reader_score._proc, 'ucinewgame', score_tag)

    pos_cmd = bytearray(('position startpos'
                         + (' moves ' + ' '.join(opening) if opening else '')
                         ).encode('ascii'))

    positions   = []
    one_sided   = 0

    for move_num in range(max_moves):
        # Adjudication: game is clearly decided, remaining positions are junk.
        if one_sided >= adjudicate_plies:
            log_info('Adjudicated: game clearly decided')
            break

        # Cheap terminal checks.
        if board.halfmove_clock >= 100:
            log_info('Game over: halfmove clock reached 100')
            break
        if seen[board._transposition_key()] >= 5:
            log_info('Game over: fivefold repetition')
            break
        if board.is_insufficient_material():
            log_info('Game over: insufficient material')
            break
        if not any(board.legal_moves):
            log_info(f'Game over at move {move_num}: mate/stalemate')
            break

        active_reader = reader_white if board.turn == chess.WHITE else reader_black
        active_tag    = white_tag  if board.turn == chess.WHITE else black_tag

        score_cp, best_move = get_move_and_score(active_reader, pos_cmd, active_tag)

        if best_move is None or best_move == '0000':
            log_info(f'{active_tag} returned null/0000 move — stopping game')
            break

        if move_num < random_plies and random.random() < random_move_prob:
            legal = list(board.legal_moves)
            if legal:
                rand_move = random.choice(legal).uci()
                log_info(f'Random move injection: {best_move} -> {rand_move}')
                best_move = rand_move
                score_cp  = None

        store = (move_num >= min_move
                 and score_cp is not None
                 and abs(score_cp) < 29000)

        if store and active_reader is not reader_score:
            log_info(f'Re-evaluating with {score_tag} for training score')
            score_cp, _ = get_move_and_score(reader_score, pos_cmd, score_tag)
            store = (score_cp is not None and abs(score_cp) < 29000)

        pre_fen = None
        if store:
            t = time.time()
            pre_fen = board.fen()
            time_fen_generation += time.time() - t

        try:
            board.push_uci(best_move)
        except Exception as e:
            log_info(f'Illegal move {best_move}: {e}')
            break

        pos_cmd += b' ' + best_move.encode('ascii')
        seen[board._transposition_key()] += 1

        if store:
            positions.append((pre_fen, score_cp))

        # Adjudication counter update (after any re-evaluation).
        if score_cp is not None and abs(score_cp) > adjudicate_cp:
            one_sided += 1
        else:
            one_sided = 0

    log_info(f'Game ended — {len(positions)} positions collected')

    time_game_handling += ((time.time() - game_t0)
                           - (time_engine_search - es_prev)
                           - (time_fen_generation - fen_prev))
    return positions


# ── Engine launch ─────────────────────────────────────────────────────────────

def launch_engine(path, is_stepbot=True, tag='Engine'):
    log_info(f'Launching {tag}: {path}')
    proc = subprocess.Popen(
        [path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    reader = EngineReader(proc)
    send(proc, 'uci', tag)
    if not wait_for(reader, 'uciok', tag, timeout=20.0):
        print(f'ERROR: No uciok from {tag} ({path})', file=sys.stderr, flush=True)
        sys.exit(1)

    if is_stepbot:
        send(proc, f'setoption name MaxDepth value {max(DEPTH, 20)}', tag)
        send(proc, 'setoption name UseBook value false', tag)

    send(proc, 'isready', tag)
    if not wait_for(reader, 'readyok', tag, timeout=20.0):
        print(f'ERROR: No readyok from {tag} ({path})', file=sys.stderr, flush=True)
        sys.exit(1)

    log_info(f'{tag} ready.')
    return reader


def quit_engine(reader, tag=''):
    try:
        send(reader._proc, 'quit', tag)
        reader._proc.wait(timeout=5)
    except Exception:
        reader._proc.kill()


# ── Engine roles ──────────────────────────────────────────────────────────────

if MODE == 'stepbot_vs_stepbot':
    reader_white = launch_engine(ENGINE_PATH, is_stepbot=True,  tag='Stepbot-White')
    reader_black = launch_engine(ENGINE_PATH, is_stepbot=True,  tag='Stepbot-Black')
    reader_score = reader_white
    white_tag    = 'Stepbot-White'
    black_tag    = 'Stepbot-Black'
    score_tag    = 'Stepbot-White'

elif MODE == 'stepbot_vs_custom':
    if not ENGINE2_PATH or not os.path.exists(ENGINE2_PATH):
        print(f'ERROR: ENGINE2_PATH not provided or not found: {ENGINE2_PATH}',
              file=sys.stderr, flush=True)
        sys.exit(1)
    engine2_name = os.path.splitext(os.path.basename(ENGINE2_PATH))[0].capitalize()
    reader_white = launch_engine(ENGINE_PATH,  is_stepbot=True,  tag='Stepbot-White')
    reader_black = launch_engine(ENGINE2_PATH, is_stepbot=False, tag=f'{engine2_name}-Black')
    reader_score = reader_white
    white_tag    = 'Stepbot-White'
    black_tag    = f'{engine2_name}-Black'
    score_tag    = 'Stepbot-White'

elif MODE == 'custom_vs_custom':
    if not ENGINE2_PATH or not os.path.exists(ENGINE2_PATH):
        print(f'ERROR: ENGINE2_PATH not provided or not found: {ENGINE2_PATH}',
              file=sys.stderr, flush=True)
        sys.exit(1)
    engine2_name = os.path.splitext(os.path.basename(ENGINE2_PATH))[0].capitalize()
    reader_white = launch_engine(ENGINE2_PATH, is_stepbot=False, tag=f'{engine2_name}-White')
    reader_black = launch_engine(ENGINE2_PATH, is_stepbot=False, tag=f'{engine2_name}-Black')
    reader_score = launch_engine(ENGINE_PATH,  is_stepbot=True,  tag='Stepbot-Scorer')
    white_tag    = f'{engine2_name}-White'
    black_tag    = f'{engine2_name}-Black'
    score_tag    = 'Stepbot-Scorer'

else:
    print(f'ERROR: Unknown mode "{MODE}"', file=sys.stderr, flush=True)
    sys.exit(1)


# ── Play games — write binary records after each one ─────────────────────────

os.makedirs(os.path.dirname(OUT_FILE) or '.', exist_ok=True)
total_positions = 0

with open(OUT_FILE, 'wb') as out_f:
    for game_idx in range(NUM_GAMES):
        log_info(f'========== GAME {game_idx + 1}/{NUM_GAMES} ==========')
        positions = play_game(
            reader_white, reader_black, reader_score,
            white_tag, black_tag, score_tag
        )
        if not positions:
            # Non-debug runs swallow log_info; make game death VISIBLE.
            # stderr is piped to the parent, which prints it after the run.
            print(f'WARNING: game {game_idx + 1} produced 0 positions '
                  f'(possible engine stop_flag race — save this run\'s context)',
                  file=sys.stderr, flush=True)
            # One retry — recovers the data without masking the bug's existence.
            positions = play_game(
                reader_white, reader_black, reader_score,
                white_tag, black_tag, score_tag
            )
        for fen, score in positions:
            out_f.write(encode(chess.Board(fen), score))
            total_positions += 1
        out_f.flush()
        # Tell the parent how many games this worker has finished so it
        # can show live progress instead of blocking until everything
        # is done.
        print(f'PROGRESS {game_idx + 1}', flush=True)

quit_engine(reader_white, white_tag)
quit_engine(reader_black, black_tag)
if MODE == 'custom_vs_custom':
    quit_engine(reader_score, score_tag)

emit_timing('engine_search', time_engine_search)
emit_timing('game_handling', time_game_handling)
emit_timing('fen_generation', time_fen_generation)
emit_metric('nodes_searched', total_nodes_searched)
emit_metric('nps', nps_sum // nps_count if nps_count else 0)

print(f'DONE {total_positions}', flush=True)