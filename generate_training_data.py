# generate_training_data.py
# Generates NNUE training positions by running Stepbot self-play.
# Output: binary .bin format (see nnue_bin.py) — 40 bytes per position.
#
# Usage:
#   python generate_training_data.py
#   python generate_training_data.py --games 1000 --depth 6 --cores 4
#   python generate_training_data.py --append
#   python generate_training_data.py --debug   (single worker, full UCI log)

import subprocess
import os
import sys
import argparse
import time
import json
import hashlib
import tempfile
import threading
import queue
from collections import defaultdict

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(SCRIPT_DIR)
sys.path.insert(0, os.path.join(ROOT, 'python'))
sys.path.insert(0, SCRIPT_DIR)

from paths import TRAINING_DATA, engine_path
from nnue_bin import MAGIC, VERSION, HEADER, RECORD_SIZE, read_records

WORKER_SCRIPT = os.path.join(SCRIPT_DIR, 'worker_game.py')
OUTPUT_DIR    = TRAINING_DATA
OUTPUT_FILE   = os.path.join(OUTPUT_DIR, 'positions.bin')
STATS_FILE    = os.path.join(OUTPUT_DIR, 'stats.json')
ENGINE_PATH   = engine_path()

VALID_MODES = ('stepbot_vs_stepbot', 'stepbot_vs_custom', 'custom_vs_custom')


def engine_id(path):
    """Short content hash of the engine binary — dataset provenance stamp."""
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()[:12]


# ─────────────────────────────────────────
# ENGINE TEST
# ─────────────────────────────────────────

def test_engine(engine_path, depth):
    try:
        proc = subprocess.Popen(
            [engine_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )

        def send(cmd):
            proc.stdin.write(cmd + '\n')
            proc.stdin.flush()

        def wait(keyword, timeout=10.0):
            start = time.time()
            while time.time() - start < timeout:
                if proc.poll() is not None:
                    return ''
                line = proc.stdout.readline().strip()
                if keyword in line:
                    return line
            return ''

        send('uci')
        if not wait('uciok'):
            return False, 'No uciok'
        # Book OFF — otherwise the probe move comes from the book with no
        # score line, and the test fails to parse a score (score=Nonecp).
        send('setoption name UseBook value false')
        send('isready')
        if not wait('readyok'):
            return False, 'No readyok'
        send('position startpos')
        send(f'go depth {min(depth, 4)}')

        score = None
        move  = None
        start = time.time()
        while time.time() - start < 30.0:
            if proc.poll() is not None:
                break
            line = proc.stdout.readline().strip()
            if 'score cp' in line:
                parts = line.split()
                try:
                    score = int(parts[parts.index('cp') + 1])
                except Exception:
                    pass
            elif line.startswith('bestmove'):
                parts = line.split()
                move  = parts[1] if len(parts) > 1 else None
                break

        send('quit')
        try:
            proc.wait(timeout=3)
        except Exception:
            proc.kill()

        if move is None:
            return False, 'No bestmove returned'
        return True, f'move={move} score={score}cp'

    except Exception as e:
        return False, str(e)


# ─────────────────────────────────────────
# DEDUPLICATION / SAVE (binary)
# ─────────────────────────────────────────

def deduplicate(records):
    # Key = board + stm + castling + ep (first 35 bytes) — exactly the old
    # ' '.join(fen.split()[:4]) semantics.
    seen   = set()
    unique = []
    for r in records:
        key = r[:35]
        if key not in seen:
            seen.add(key)
            unique.append(r)
    return unique


def save_positions(records, path, append=False):
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    existing = read_records(path) if (append and os.path.exists(path)) else []
    with open(path, 'wb') as f:
        f.write(HEADER.pack(MAGIC, VERSION, 0, len(existing) + len(records)))
        f.write(b''.join(existing))
        f.write(b''.join(records))


def count_existing(path):
    if not os.path.exists(path):
        return 0
    return max(0, (os.path.getsize(path) - HEADER.size) // RECORD_SIZE)


# ─────────────────────────────────────────
# MAIN
# ─────────────────────────────────────────

def run(total_games, depth, num_cores, output_file, append, engine2, mode, debug):
    prof_times = defaultdict(float)

    if debug:
        num_cores   = 1
        total_games = 1
        print('=' * 60)
        print('  DEBUG MODE — 1 core, 1 game, full UCI log')
        print('  All engine communication printed below.')
        print('=' * 60)
    else:
        print('=' * 60)
        print('  Stepbot NNUE Training Data Generator')
        print('=' * 60)

    print(f'  Engine  : {ENGINE_PATH} (id {engine_id(ENGINE_PATH)})')
    if engine2:
        print(f'  Engine2 : {engine2}')
    print(f'  Mode    : {mode}')
    print(f'  Games   : {total_games}')
    print(f'  Depth   : {depth}')
    print(f'  Cores   : {num_cores}')
    print(f'  Output  : {output_file} (binary v{VERSION}, {RECORD_SIZE}B/pos)')
    print()

    if not os.path.exists(ENGINE_PATH):
        print(f'  ERROR: Engine not found at {ENGINE_PATH}')
        sys.exit(1)

    if not os.path.exists(WORKER_SCRIPT):
        print(f'  ERROR: worker_game.py not found at {WORKER_SCRIPT}')
        sys.exit(1)

    if mode not in VALID_MODES:
        print(f'  ERROR: Invalid mode "{mode}". Choose from: {VALID_MODES}')
        sys.exit(1)

    if mode in ('stepbot_vs_custom', 'custom_vs_custom'):
        if not engine2:
            print(f'  ERROR: --engine2 is required for mode "{mode}"')
            sys.exit(1)
        if not os.path.exists(engine2):
            print(f'  ERROR: Custom engine not found at {engine2}')
            sys.exit(1)

    if not debug:
        print('  Testing engine...')
        ok, msg = test_engine(ENGINE_PATH, depth)
        if not ok:
            print(f'  ERROR: {msg}')
            sys.exit(1)
        print(f'  Engine OK — {msg}')
        print()

    existing = count_existing(output_file) if append else 0
    if existing > 0:
        print(f'  Appending to {existing:,} existing positions.')
    else:
        append = False

    base       = total_games // num_cores
    remainder  = total_games % num_cores
    games_each = [base + (1 if i < remainder else 0) for i in range(num_cores)]

    tmp_files = []
    for i in range(num_cores):
        fd, path = tempfile.mkstemp(suffix=f'_w{i}.bin')
        os.close(fd)
        tmp_files.append(path)

    procs = []
    for i in range(num_cores):
        cmd = [
            sys.executable, WORKER_SCRIPT,
            ENGINE_PATH,
            str(games_each[i]),
            str(depth),
            tmp_files[i],
            str(i),
            engine2 or '',
            mode,
            '1' if debug else '0',
        ]
        stderr_dest = None if debug else subprocess.PIPE
        p = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=stderr_dest,
            text=True,
            bufsize=1,
        )
        procs.append(p)
        time.sleep(0.5)

    if debug:
        print('  Worker launched — UCI log:')
        print('-' * 60)
        p = procs[0]
        while True:
            line = p.stdout.readline()
            if not line:
                if p.poll() is not None:
                    break
                continue
            line = line.strip()
            if line.startswith('PROGRESS'):
                print(f'\n  [PROGRESS] {line}')
            elif line.startswith('TIMING'):
                parts = line.split()
                if len(parts) >= 3:
                    prof_times[parts[1]] += float(parts[2])
                print(f'\n  [TIMING] {line}')
            elif line.startswith('METRIC'):
                parts = line.split()
                if len(parts) >= 3:
                    key = f'metric_{parts[1]}'
                    prof_times[key] = prof_times.get(key, 0) + int(parts[2])
                print(f'\n  [METRIC] {line}')
            elif line.startswith('DONE'):
                print(f'\n  [DONE] {line}')
            else:
                print(f'  [stdout] {line}')
        p.wait()
    else:
        print(f'  {num_cores} worker(s) launched.')
        print()

        start_time  = time.time()
        game_counts = [0] * num_cores
        done        = [False] * num_cores

        # ------------------------------------------------------------------
        # Concurrent stdout reader.
        #
        # The old code called p.stdout.readline() inside a for-loop over
        # workers. readline() BLOCKS, so if worker 0 was mid-game (no
        # output) the parent froze on it and never drained workers 1..N —
        # which then blocked on their own stdout writes. Result: deadlock
        # and the script "sat there indefinitely" after launching workers.
        #
        # Fix: one daemon thread per worker pushes lines into a shared
        # queue. The main loop pulls with a short timeout so it services
        # every worker and never blocks on a single pipe.
        # ------------------------------------------------------------------
        line_queue = queue.Queue()

        def worker_reader(proc, idx):
            try:
                for line in proc.stdout:
                    line_queue.put((idx, line.rstrip('\n')))
            except Exception:
                pass
            line_queue.put((idx, None))  # sentinel: stdout closed

        for i, p in enumerate(procs):
            t = threading.Thread(target=worker_reader, args=(p, i), daemon=True)
            t.start()

        while not all(done):
            try:
                idx, line = line_queue.get(timeout=0.5)
            except queue.Empty:
                # No worker output in the last 0.5s — loop and re-check.
                continue

            if line is None:
                # Worker's stdout closed (it finished or crashed).
                done[idx] = True
                continue

            if line.startswith('PROGRESS'):
                parts = line.split()
                if len(parts) >= 2:
                    game_counts[idx] = int(parts[1])
                total_done = sum(game_counts)
                elapsed    = time.time() - start_time
                rate       = (total_done / elapsed * 60) if elapsed > 0 else 0
                remaining  = total_games - total_done
                eta        = (remaining / (rate / 60)) if rate > 0 else 0
                print(f'\r  Games: {total_done}/{total_games} '
                      f'| Core {idx}: {game_counts[idx]}/{games_each[idx]} '
                      f'| {rate:.1f} games/min '
                      f'| ETA: {eta:.0f}s    ',
                      end='', flush=True)
            elif line.startswith('TIMING'):
                parts = line.split()
                if len(parts) >= 3:
                    prof_times[parts[1]] += float(parts[2])
            elif line.startswith('METRIC'):
                parts = line.split()
                if len(parts) >= 3:
                    key = f'metric_{parts[1]}'
                    prof_times[key] = prof_times.get(key, 0) + int(parts[2])
            elif line.startswith('DONE'):
                done[idx] = True

        for p in procs:
            try:
                p.wait(timeout=5)
            except Exception:
                p.kill()

    elapsed = time.time() - start_time if not debug else 0

    if not debug:
        print(f'\n\n  All workers finished in {elapsed:.0f}s.')

        for i, p in enumerate(procs):
            err = p.stderr.read() if p.stderr else ''
            if err.strip():
                print(f'  Core {i} stderr:\n{err[:500]}')

    # Collect binary records from workers
    collect_start  = time.time()
    all_positions  = []
    for i, tmp_file in enumerate(tmp_files):
        try:
            all_positions.extend(read_records(tmp_file))
            os.unlink(tmp_file)
        except Exception as e:
            print(f'  Warning: could not read worker {i} file: {e}')
    prof_times['file_collection'] += time.time() - collect_start

    print(f'  Raw positions : {len(all_positions):,}')

    dedup_start = time.time()
    unique = deduplicate(all_positions)
    prof_times['deduplication'] += time.time() - dedup_start
    print(f'  After dedup   : {len(unique):,}')

    if not debug:
        save_start = time.time()
        save_positions(unique, output_file, append=append)
        prof_times['file_writing'] += time.time() - save_start
        total_saved = existing + len(unique)
        print(f'  Total saved   : {total_saved:,}')

        stats = {
            'total_positions':      total_saved,
            'new_positions':        len(unique),
            'games_played':         total_games,
            'depth':                depth,
            'cores':                num_cores,
            'mode':                 mode,
            'engine2':              engine2 or 'N/A',
            'engine_id':            engine_id(ENGINE_PATH),
            'format':               f'bin-v{VERSION}',
            'elapsed_seconds':      round(elapsed, 1),
            'positions_per_second': round(len(unique) / elapsed, 1) if elapsed > 0 else 0,
        }
        os.makedirs(OUTPUT_DIR, exist_ok=True)
        with open(STATS_FILE, 'w') as f:
            json.dump(stats, f, indent=2)

        # Profiling summary
        timing_categories = [k for k in prof_times.keys() if not k.startswith('metric_')]
        total_prof_time = sum(prof_times[k] for k in timing_categories
                              if isinstance(prof_times[k], float))
        print()
        print('-' * 60)
        print('  PROFILING SUMMARY')
        print('-' * 60)
        for cat in ('engine_search', 'game_handling', 'fen_generation',
                    'file_collection', 'deduplication', 'file_writing'):
            if cat in prof_times:
                pct = (prof_times[cat] / total_prof_time * 100) if total_prof_time > 0 else 0
                print(f'    {cat:30s}: {prof_times[cat]:8.2f}s ({pct:5.1f}%)')
        if 'metric_nodes_searched' in prof_times or 'metric_nps' in prof_times:
            print()
            print('    Engine metrics:')
            if 'metric_nodes_searched' in prof_times:
                print(f'      Total nodes searched : {prof_times["metric_nodes_searched"]:>15,}')
            if 'metric_nps' in prof_times:
                print(f'      Nodes/second (avg)   : {prof_times["metric_nps"]:>15,}')
        print(f'    {"Total profiled time":30s}: {total_prof_time:8.2f}s')
        print('-' * 60)

        print()
        print('=' * 60)
        print(f'  Done! {len(unique):,} new positions saved.')
        if elapsed > 0 and len(unique) > 0:
            print(f'  Rate: {stats["positions_per_second"]:.1f} positions/sec')
        print(f'  Next: python train_nnue.py')
        print('=' * 60)
    else:
        print()
        print('=' * 60)
        print(f'  Debug run complete. {len(unique):,} positions collected.')
        print('  (Not saved — debug mode does not write to dataset)')
        print('=' * 60)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description='''
Stepbot NNUE Training Data Generator (binary output)

Modes:
  stepbot_vs_stepbot  — Stepbot plays both sides (default)
  stepbot_vs_custom   — Stepbot (white) vs custom engine (black)
  custom_vs_custom    — Custom engine vs itself; Stepbot evaluates positions

Examples:
  python generate_training_data.py --games 2500 --depth 6 --cores 4
  python generate_training_data.py --mode stepbot_vs_custom --engine2 path/to/stockfish
  python generate_training_data.py --append
  python generate_training_data.py --debug
''')
    parser.add_argument('--games',   type=int, default=200)
    parser.add_argument('--depth',   type=int, default=6)
    parser.add_argument('--cores',   type=int, default=4)
    parser.add_argument('--output',  default=OUTPUT_FILE)
    parser.add_argument('--append',  action='store_true')
    parser.add_argument('--debug',   action='store_true',
                        help='Print all UCI traffic. Forces 1 core, 1 game.')
    parser.add_argument('--mode',    default='stepbot_vs_stepbot',
                        choices=VALID_MODES,
                        help='Which engines play each other')
    parser.add_argument('--engine2', default=None,
                        help='Path to custom/Stockfish engine')
    args = parser.parse_args()
    run(args.games, args.depth, args.cores, args.output, args.append,
        args.engine2, args.mode, args.debug)