# generate_training_data.py
# Generates NNUE training positions by running Stepbot self-play.
# Each worker is a separate process running worker_game.py.
#
# Usage:
#   python generate_training_data.py
#   python generate_training_data.py --games 1000 --depth 9 --cores 5
#   python generate_training_data.py --append

import subprocess
import os
import sys
import argparse
import time
import json
import tempfile

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
WORKER_SCRIPT = os.path.join(SCRIPT_DIR, 'worker_game.py')
OUTPUT_DIR   = os.path.join(SCRIPT_DIR, 'Training_Data')
OUTPUT_FILE  = os.path.join(OUTPUT_DIR, 'positions.csv')
STATS_FILE   = os.path.join(OUTPUT_DIR, 'stats.json')

ENGINE_PATH  = os.path.join(SCRIPT_DIR, 'stepbot.exe')
if not os.path.exists(ENGINE_PATH):
    ENGINE_PATH = os.path.join(SCRIPT_DIR, 'stepbot')


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
        send(f'setoption name MaxDepth value {min(depth, 4)}')
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
# DEDUPLICATION
# ─────────────────────────────────────────

def deduplicate(positions):
    seen   = set()
    unique = []
    for fen, score in positions:
        key = ' '.join(fen.split()[:4])
        if key not in seen:
            seen.add(key)
            unique.append((fen, score))
    return unique


# ─────────────────────────────────────────
# SAVE
# ─────────────────────────────────────────

def save_positions(positions, path, append=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    mode = 'a' if append else 'w'
    with open(path, mode, encoding='utf-8') as f:
        if not append:
            f.write('fen,score_cp\n')
        for fen, score in positions:
            f.write(f'"{fen}",{score}\n')


def count_existing(path):
    if not os.path.exists(path):
        return 0
    with open(path, 'r') as f:
        return max(0, sum(1 for _ in f) - 1)


# ─────────────────────────────────────────
# MAIN
# ─────────────────────────────────────────

def run(total_games, depth, num_cores, output_file, append):
    print('=' * 60)
    print('  Stepbot NNUE Training Data Generator')
    print('=' * 60)
    print(f'  Engine : {ENGINE_PATH}')
    print(f'  Games  : {total_games}')
    print(f'  Depth  : {depth}')
    print(f'  Cores  : {num_cores}')
    print(f'  Output : {output_file}')
    print()

    if not os.path.exists(ENGINE_PATH):
        print(f'  ERROR: Engine not found at {ENGINE_PATH}')
        sys.exit(1)

    if not os.path.exists(WORKER_SCRIPT):
        print(f'  ERROR: worker_game.py not found at {WORKER_SCRIPT}')
        sys.exit(1)

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

    # Distribute games across workers
    base       = total_games // num_cores
    remainder  = total_games % num_cores
    games_each = [base + (1 if i < remainder else 0) for i in range(num_cores)]

    # Temp output file per worker
    tmp_files = []
    for i in range(num_cores):
        fd, path = tempfile.mkstemp(suffix=f'_w{i}.txt')
        os.close(fd)
        tmp_files.append(path)

    # Launch workers
    procs = []
    for i in range(num_cores):
        p = subprocess.Popen(
            [sys.executable, WORKER_SCRIPT,
             ENGINE_PATH,
             str(games_each[i]),
             str(depth),
             tmp_files[i],
             str(i)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        procs.append(p)
        time.sleep(0.5)

    print(f'  {num_cores} worker(s) launched.')
    print()

    # Monitor
    start_time   = time.time()
    game_counts  = [0] * num_cores
    done         = [False] * num_cores

    while not all(done):
        for i, p in enumerate(procs):
            if done[i]:
                continue
            line = p.stdout.readline()
            if not line:
                if p.poll() is not None:
                    done[i] = True
                continue
            line = line.strip()
            if line.startswith('PROGRESS'):
                parts = line.split()
                game_counts[i] = int(parts[1])
                total_done = sum(game_counts)
                elapsed    = time.time() - start_time
                rate       = (total_done / elapsed * 60) if elapsed > 0 else 0
                remaining  = total_games - total_done
                eta        = (remaining / (rate / 60)) if rate > 0 else 0
                print(f'\r  Games: {total_done}/{total_games} '
                      f'| Core {i}: {parts[1]}/{games_each[i]} '
                      f'| {rate:.1f} games/min '
                      f'| ETA: {eta:.0f}s    ',
                      end='', flush=True)
            elif line.startswith('DONE'):
                done[i] = True

        time.sleep(0.02)

    for p in procs:
        try:
            p.wait(timeout=5)
        except Exception:
            p.kill()

    elapsed = time.time() - start_time
    print(f'\n\n  All workers finished in {elapsed:.0f}s.')

    # Check stderr for errors
    for i, p in enumerate(procs):
        err = p.stderr.read() if p.stderr else ''
        if err.strip():
            print(f'  Core {i} error:\n{err[:500]}')

    # Collect results
    all_positions = []
    for i, tmp_file in enumerate(tmp_files):
        if os.path.exists(tmp_file):
            try:
                with open(tmp_file, 'r', encoding='utf-8') as f:
                    for line in f:
                        line = line.strip()
                        if '|||' in line:
                            fen, score_str = line.rsplit('|||', 1)
                            try:
                                all_positions.append((fen, int(score_str)))
                            except ValueError:
                                pass
                os.unlink(tmp_file)
            except Exception as e:
                print(f'  Warning: could not read worker {i} file: {e}')

    print(f'  Raw positions : {len(all_positions):,}')
    unique = deduplicate(all_positions)
    print(f'  After dedup   : {len(unique):,}')

    save_positions(unique, output_file, append=append)
    total_saved = existing + len(unique)
    print(f'  Total saved   : {total_saved:,}')

    stats = {
        'total_positions':      total_saved,
        'new_positions':        len(unique),
        'games_played':         total_games,
        'depth':                depth,
        'cores':                num_cores,
        'elapsed_seconds':      round(elapsed, 1),
        'positions_per_second': round(len(unique) / elapsed, 1) if elapsed > 0 else 0,
    }
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    with open(STATS_FILE, 'w') as f:
        json.dump(stats, f, indent=2)

    print()
    print('=' * 60)
    print(f'  Done! {len(unique):,} new positions saved.')
    if elapsed > 0 and len(unique) > 0:
        print(f'  Rate: {stats["positions_per_second"]:.1f} positions/sec')
    print(f'  Next: python train_nnue.py')
    print('=' * 60)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--games',  type=int, default=200)
    parser.add_argument('--depth',  type=int, default=13)
    parser.add_argument('--cores',  type=int, default=5)
    parser.add_argument('--output', default=OUTPUT_FILE)
    parser.add_argument('--append', action='store_true')
    args = parser.parse_args()
    run(args.games, args.depth, args.cores, args.output, args.append)
