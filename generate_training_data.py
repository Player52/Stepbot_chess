# generate_training_data.py
# Generates training positions for NNUE by running Stepbot self-play
# using the compiled C++ engine for speed.
#
# Launches multiple engine instances in parallel, extracts positions
# from each game with their evaluations, and saves to a training dataset.
#
# Usage:
#   python generate_training_data.py                  # default settings
#   python generate_training_data.py --games 1000     # 1000 games total
#   python generate_training_data.py --depth 10       # eval at depth 10
#   python generate_training_data.py --cores 5        # use 5 cores
#
# Output:
#   Training_Data/positions.csv   — fen,score_cp per line
#   Training_Data/stats.json      — generation statistics

import subprocess
import os
import sys
import argparse
import threading
import time
import json
import random
import queue

SCRIPT_DIR  = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR  = os.path.join(SCRIPT_DIR, 'Training_Data')
OUTPUT_FILE = os.path.join(OUTPUT_DIR, 'positions.csv')
STATS_FILE  = os.path.join(OUTPUT_DIR, 'stats.json')

ENGINE_PATH = os.path.join(SCRIPT_DIR, 'stepbot.exe')
if not os.path.exists(ENGINE_PATH):
    ENGINE_PATH = os.path.join(SCRIPT_DIR, 'stepbot')

STARTING_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

# ─────────────────────────────────────────
# UCI ENGINE WRAPPER
# ─────────────────────────────────────────

class UCIEngine:
    def __init__(self, engine_path, depth):
        self.depth = depth
        self.proc  = subprocess.Popen(
            [engine_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        self._send('uci')
        self._wait_for('uciok')
        self._send(f'setoption name MaxDepth value {depth}')
        self._send('isready')
        self._wait_for('readyok')

    def _send(self, cmd):
        self.proc.stdin.write(cmd + '\n')
        self.proc.stdin.flush()

    def _wait_for(self, keyword, timeout=10.0):
        start = time.time()
        while time.time() - start < timeout:
            line = self.proc.stdout.readline().strip()
            if keyword in line:
                return line
        return ''

    def get_eval_and_move(self, fen, moves=None):
        """
        Sets the position and searches to self.depth.
        Returns (score_cp, best_move_uci).
        Score is from White's perspective.
        Returns (None, None) on timeout or error.
        """
        if moves:
            self._send(f'position fen {fen} moves {" ".join(moves)}')
        else:
            self._send(f'position fen {fen}')

        self._send(f'go depth {self.depth}')

        score_cp  = None
        best_move = None
        start     = time.time()

        while time.time() - start < 60.0:
            line = self.proc.stdout.readline().strip()
            if not line:
                continue
            if line.startswith('info') and 'score cp' in line:
                parts = line.split()
                try:
                    idx      = parts.index('cp')
                    score_cp = int(parts[idx + 1])
                except (ValueError, IndexError):
                    pass
            elif line.startswith('info') and 'score mate' in line:
                parts = line.split()
                try:
                    idx      = parts.index('mate')
                    mate_in  = int(parts[idx + 1])
                    score_cp = 29000 if mate_in > 0 else -29000
                except (ValueError, IndexError):
                    pass
            elif line.startswith('bestmove'):
                parts     = line.split()
                best_move = parts[1] if len(parts) > 1 else None
                break

        return score_cp, best_move

    def quit(self):
        try:
            self._send('quit')
            self.proc.wait(timeout=3.0)
        except Exception:
            self.proc.kill()


# ─────────────────────────────────────────
# OPENING BOOK
# Random opening lines to diversify training positions
# ─────────────────────────────────────────

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


# ─────────────────────────────────────────
# GAME PLAYER
# Plays one complete game and records positions + evaluations
# ─────────────────────────────────────────

def play_game(engine, min_move=8, max_moves=150):
    """
    Plays a game from a random opening using the engine for both sides.

    Records (fen_with_moves_applied, score_cp) for each position
    from move min_move onwards, skipping positions with forced mates.

    The score is always from White's perspective so the neural network
    has a consistent target regardless of whose turn it is.
    """
    opening   = random.choice(OPENINGS)
    moves     = list(opening)
    positions = []

    for move_number in range(max_moves):
        score_cp, best_move = engine.get_eval_and_move(STARTING_FEN, moves)

        # Game over
        if best_move is None or best_move == '0000':
            break

        # Record position if past opening and not a forced mate
        if move_number >= min_move and score_cp is not None:
            if abs(score_cp) < 29000:
                # Build the position FEN string for saving
                # We store as "startpos + moves" — the trainer will
                # need to convert this to a proper FEN using a chess library
                # For now store the move sequence compactly
                pos_key = STARTING_FEN + ' | ' + ' '.join(moves)
                positions.append((pos_key, score_cp))

        moves.append(best_move)

    return positions


# ─────────────────────────────────────────
# WORKER THREAD
# One worker = one engine instance = one core
# ─────────────────────────────────────────

def worker(worker_id, engine_path, depth, num_games,
           result_queue, progress_queue):
    try:
        engine        = UCIEngine(engine_path, depth)
        all_positions = []

        for game_idx in range(num_games):
            positions = play_game(engine)
            all_positions.extend(positions)
            progress_queue.put((worker_id, game_idx + 1, num_games,
                                 len(positions)))

        engine.quit()
        result_queue.put(('done', worker_id, all_positions))

    except Exception as e:
        result_queue.put(('error', worker_id, str(e)))


# ─────────────────────────────────────────
# DEDUPLICATION
# ─────────────────────────────────────────

def deduplicate(positions):
    seen   = set()
    unique = []
    for pos, score in positions:
        if pos not in seen:
            seen.add(pos)
            unique.append((pos, score))
    return unique


# ─────────────────────────────────────────
# SAVE
# ─────────────────────────────────────────

def save_positions(positions, path, append=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    mode = 'a' if append else 'w'
    with open(path, mode, encoding='utf-8') as f:
        if not append:
            f.write('position,score_cp\n')
        for pos, score in positions:
            f.write(f'"{pos}",{score}\n')


def count_existing(path):
    if not os.path.exists(path):
        return 0
    with open(path, 'r') as f:
        return max(0, sum(1 for _ in f) - 1)


# ─────────────────────────────────────────
# MAIN
# ─────────────────────────────────────────

def run(total_games, depth, num_cores, output_file, append):
    print("=" * 60)
    print("  Stepbot NNUE Training Data Generator")
    print("=" * 60)
    print(f"  Engine     : {ENGINE_PATH}")
    print(f"  Games      : {total_games}")
    print(f"  Depth      : {depth}")
    print(f"  Cores      : {num_cores}")
    print(f"  Output     : {output_file}")

    if not os.path.exists(ENGINE_PATH):
        print(f"\n  ERROR: Engine not found at {ENGINE_PATH}")
        print("  Please run 'make' to compile the engine first.")
        sys.exit(1)

    existing = count_existing(output_file) if append else 0
    if existing > 0:
        print(f"  Appending to {existing:,} existing positions.")
    else:
        append = False

    print()

    # Distribute games across cores
    base       = total_games // num_cores
    remainder  = total_games % num_cores
    games_each = [base + (1 if i < remainder else 0) for i in range(num_cores)]

    result_q   = queue.Queue()
    progress_q = queue.Queue()

    threads = []
    for i in range(num_cores):
        t = threading.Thread(
            target=worker,
            args=(i, ENGINE_PATH, depth, games_each[i],
                  result_q, progress_q),
            daemon=True
        )
        t.start()
        threads.append(t)
        # Stagger starts slightly to avoid disk contention
        time.sleep(0.3)

    print(f"  {num_cores} engine instances started.")
    print()

    # Progress display
    start_time   = time.time()
    workers_done = 0
    all_positions = []
    game_counts  = [0] * num_cores

    while workers_done < num_cores:
        # Drain progress queue
        try:
            while True:
                wid, game_num, total, pos_count = progress_q.get_nowait()
                game_counts[wid] = game_num
                total_done = sum(game_counts)
                elapsed    = time.time() - start_time
                rate       = total_done / elapsed if elapsed > 0 else 0
                eta        = (total_games - total_done) / rate if rate > 0 else 0
                print(f"\r  Games: {total_done}/{total_games} "
                      f"| Core {wid}: {game_num}/{games_each[wid]} "
                      f"| {rate:.1f} games/min "
                      f"| ETA: {eta:.0f}s    ",
                      end='', flush=True)
        except queue.Empty:
            pass

        # Drain result queue
        try:
            while True:
                result = result_q.get_nowait()
                if result[0] == 'done':
                    _, wid, positions = result
                    all_positions.extend(positions)
                    workers_done += 1
                elif result[0] == 'error':
                    _, wid, err = result
                    print(f"\n  Core {wid} error: {err}")
                    workers_done += 1
        except queue.Empty:
            pass

        time.sleep(0.2)

    elapsed = time.time() - start_time
    print(f"\n\n  All cores finished in {elapsed:.0f}s.")

    # Deduplicate and save
    print(f"  Raw positions collected: {len(all_positions):,}")
    unique = deduplicate(all_positions)
    print(f"  After deduplication:     {len(unique):,}")

    save_positions(unique, output_file, append=append)
    total_saved = existing + len(unique)
    print(f"  Total in dataset:        {total_saved:,}")
    print(f"  Saved to: {output_file}")

    # Stats
    stats = {
        'total_positions':      total_saved,
        'new_positions':        len(unique),
        'games_played':         total_games,
        'depth':                depth,
        'cores':                num_cores,
        'elapsed_seconds':      round(elapsed, 1),
        'positions_per_second': round(len(unique) / elapsed, 1) if elapsed > 0 else 0,
    }
    with open(STATS_FILE, 'w') as f:
        json.dump(stats, f, indent=2)

    print()
    print("=" * 60)
    print(f"  Done! {len(unique):,} new positions saved.")
    print(f"  Rate: {stats['positions_per_second']:.1f} positions/sec")
    print(f"  Next step: python train_nnue.py")
    print("=" * 60)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Generate NNUE training positions from Stepbot self-play'
    )
    parser.add_argument('--games',  type=int, default=200,
                        help='Total games to play (default: 200)')
    parser.add_argument('--depth',  type=int, default=9,
                        help='Evaluation depth (default: 9)')
    parser.add_argument('--cores',  type=int, default=5,
                        help='Parallel engine instances (default: 5)')
    parser.add_argument('--output', default=OUTPUT_FILE,
                        help='Output CSV file')
    parser.add_argument('--append', action='store_true',
                        help='Append to existing dataset')
    args = parser.parse_args()

    run(
        total_games = args.games,
        depth       = args.depth,
        num_cores   = args.cores,
        output_file = args.output,
        append      = args.append,
    )
