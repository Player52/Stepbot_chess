# tune.py
# Texel tuning for Stepbot's evaluation function.
#
# Reads self-play PGN games, extracts positions and results,
# then optimises evaluation weights to minimise prediction error.
#
# The tuned weights are saved to tuned_weights.json and can be
# loaded by evaluate.py to improve Stepbot's play.
#
# Usage:
#   python tune.py                  -- tune from selfplay_games.pgn
#   python tune.py --iterations 500 -- run more iterations
#   python tune.py --input my.pgn   -- tune from a custom PGN

import sys
import os
import json
import math
import argparse
import copy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from board import (
    WHITE, BLACK, EMPTY,
    PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    square, file_of, rank_of
)
from engine import board_from_fen
from movegen import MoveGenerator
from evaluate import (
    Evaluator,
    PIECE_VALUES,
    DOUBLED_PAWN_PENALTY,
    ISOLATED_PAWN_PENALTY,
    PASSED_PAWN_BONUS,
    PASSED_PAWN_BONUS_EG,
    PAWN_SHIELD_BONUS,
    OPEN_FILE_NEAR_KING,
    SEMI_OPEN_FILE_KING,
    MOBILITY_BONUS,
    BISHOP_PAIR_BONUS,
    ENDGAME_THRESHOLD,
)
from analyse import parse_pgn, extract_moves_from_movetext, san_to_move

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
DEFAULT_INPUT  = os.path.join(SCRIPT_DIR, 'Self_play', 'selfplay_games.pgn')
DEFAULT_OUTPUT = os.path.join(SCRIPT_DIR, 'tuned_weights.json')

STARTING_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

# ─────────────────────────────────────────
# SIGMOID FUNCTION
# Converts centipawn score to win probability (0.0 - 1.0)
# K controls how steeply the curve rises — standard value is 0.0025
# ─────────────────────────────────────────

K = 0.0025

def sigmoid(score):
    return 1.0 / (1.0 + math.exp(-K * score))


# ─────────────────────────────────────────
# WEIGHT VECTOR
# A flat list of all tunable values.
# We tune these and write them back into the evaluator.
# ─────────────────────────────────────────

def get_weights():
    """
    Extract all tunable weights into a flat list.
    Returns (weights, labels) where labels describe each weight.
    """
    weights = []
    labels  = []

    # Piece values (skip KING — not tunable)
    for pt in [PAWN, KNIGHT, BISHOP, ROOK, QUEEN]:
        weights.append(float(PIECE_VALUES[pt]))
        labels.append(f'piece_value_{pt}')

    # Pawn structure
    weights.append(float(DOUBLED_PAWN_PENALTY))
    labels.append('doubled_pawn_penalty')

    weights.append(float(ISOLATED_PAWN_PENALTY))
    labels.append('isolated_pawn_penalty')

    # Passed pawn bonuses (middlegame, ranks 1-6)
    for i in range(1, 7):
        weights.append(float(PASSED_PAWN_BONUS[i]))
        labels.append(f'passed_pawn_mg_rank{i}')

    # Passed pawn bonuses (endgame, ranks 1-6)
    for i in range(1, 7):
        weights.append(float(PASSED_PAWN_BONUS_EG[i]))
        labels.append(f'passed_pawn_eg_rank{i}')

    # King safety
    weights.append(float(PAWN_SHIELD_BONUS))
    labels.append('pawn_shield_bonus')

    weights.append(float(OPEN_FILE_NEAR_KING))
    labels.append('open_file_near_king')

    weights.append(float(SEMI_OPEN_FILE_KING))
    labels.append('semi_open_file_king')

    # Mobility bonuses
    for pt in [KNIGHT, BISHOP, ROOK, QUEEN]:
        weights.append(float(MOBILITY_BONUS[pt]))
        labels.append(f'mobility_{pt}')

    # Bishop pair
    weights.append(float(BISHOP_PAIR_BONUS))
    labels.append('bishop_pair_bonus')

    return weights, labels


def apply_weights(weights, labels):
    """
    Write a weight vector back into the evaluate module's globals.
    Returns a configured Evaluator that uses these weights.
    """
    import evaluate as ev_module

    idx = 0

    # Piece values
    pv = dict(ev_module.PIECE_VALUES)
    for pt in [PAWN, KNIGHT, BISHOP, ROOK, QUEEN]:
        pv[pt] = int(round(weights[idx]))
        idx += 1
    ev_module.PIECE_VALUES = pv

    ev_module.DOUBLED_PAWN_PENALTY  = int(round(weights[idx])); idx += 1
    ev_module.ISOLATED_PAWN_PENALTY = int(round(weights[idx])); idx += 1

    ppb_mg = list(ev_module.PASSED_PAWN_BONUS)
    for i in range(1, 7):
        ppb_mg[i] = int(round(weights[idx])); idx += 1
    ev_module.PASSED_PAWN_BONUS = ppb_mg

    ppb_eg = list(ev_module.PASSED_PAWN_BONUS_EG)
    for i in range(1, 7):
        ppb_eg[i] = int(round(weights[idx])); idx += 1
    ev_module.PASSED_PAWN_BONUS_EG = ppb_eg

    ev_module.PAWN_SHIELD_BONUS   = int(round(weights[idx])); idx += 1
    ev_module.OPEN_FILE_NEAR_KING = int(round(weights[idx])); idx += 1
    ev_module.SEMI_OPEN_FILE_KING = int(round(weights[idx])); idx += 1

    mob = dict(ev_module.MOBILITY_BONUS)
    for pt in [KNIGHT, BISHOP, ROOK, QUEEN]:
        mob[pt] = int(round(weights[idx])); idx += 1
    ev_module.MOBILITY_BONUS = mob

    ev_module.BISHOP_PAIR_BONUS = int(round(weights[idx])); idx += 1

    return Evaluator()


# ─────────────────────────────────────────
# POSITION EXTRACTOR
# Replays PGN games and collects (board, result) pairs
# ─────────────────────────────────────────

def extract_positions(pgn_path, max_positions=5000):
    """
    Replay all games in the PGN and collect positions with results.

    Returns a list of (fen_string, result) pairs where:
      result = 1.0 (White won), 0.5 (draw), 0.0 (Black won)
    """
    games = parse_pgn(pgn_path)
    if not games:
        print("  [Tune] No games found in PGN.")
        return []

    gen       = MoveGenerator()
    positions = []

    result_map = {'1-0': 1.0, '0-1': 0.0, '1/2-1/2': 0.5, '*': 0.5}

    print(f"  Extracting positions from {len(games)} game(s)...")

    for game in games:
        result_str = game['headers'].get('Result', '*')
        result     = result_map.get(result_str, 0.5)
        san_list   = extract_moves_from_movetext(game['movetext'])
        board      = board_from_fen(STARTING_FEN)

        # Skip the first few moves (opening theory — less informative)
        for i, san in enumerate(san_list):
            move = san_to_move(board, san, gen)
            if move is None:
                break

            # Collect positions from move 6 onwards (past the opening)
            if i >= 10:
                positions.append((board_from_fen(_board_fen(board)), result))

            board = gen._apply_move(board, move)

            if len(positions) >= max_positions:
                break

        if len(positions) >= max_positions:
            break

    print(f"  Collected {len(positions)} positions.")
    return positions


def _board_fen(board):
    """Get FEN string for a board (reuses engine's board_to_fen)."""
    from engine import board_to_fen
    return board_to_fen(board)


# ─────────────────────────────────────────
# ERROR FUNCTION
# Mean squared error between predicted and actual results
# ─────────────────────────────────────────

def mean_squared_error(positions, evaluator):
    """
    Calculate mean squared error across all positions.
    Lower = better weights.
    """
    total_error = 0.0
    for board, result in positions:
        score      = evaluator.evaluate(board)
        predicted  = sigmoid(score)
        error      = (result - predicted) ** 2
        total_error += error
    return total_error / len(positions)


# ─────────────────────────────────────────
# TUNER
# Local search: nudge each weight, keep if error improves
# ─────────────────────────────────────────

def tune(positions, iterations=200, step=5, verbose=True):
    """
    Optimise weights using local search (coordinate descent).

    For each weight, try nudging it up and down by `step`.
    Keep the change if it reduces the total error.
    Repeat for `iterations` passes over all weights.
    """
    weights, labels = get_weights()
    evaluator       = apply_weights(list(weights), labels)
    best_error      = mean_squared_error(positions, evaluator)

    print(f"\n  Starting error : {best_error:.6f}")
    print(f"  Weights to tune: {len(weights)}")
    print(f"  Iterations     : {iterations}")
    print(f"  Step size      : {step}")
    print()

    improved_total = 0

    for iteration in range(iterations):
        improved_this_pass = 0

        for i, (w, label) in enumerate(zip(weights, labels)):
            # Try nudging up
            weights[i] = w + step
            ev_up       = apply_weights(list(weights), labels)
            err_up      = mean_squared_error(positions, ev_up)

            # Try nudging down
            weights[i] = w - step
            ev_down     = apply_weights(list(weights), labels)
            err_down    = mean_squared_error(positions, ev_down)

            # Keep the best
            if err_up < best_error and err_up <= err_down:
                best_error = err_up
                w = w + step
                improved_this_pass += 1
            elif err_down < best_error:
                best_error = err_down
                w = w - step
                improved_this_pass += 1
            else:
                w = w  # No improvement — revert

            weights[i] = w

        improved_total += improved_this_pass

        if verbose and (iteration + 1) % 10 == 0:
            print(f"  Iteration {iteration + 1:4d} / {iterations} | "
                  f"Error: {best_error:.6f} | "
                  f"Improvements this pass: {improved_this_pass}")

        # Early stop if no improvements in a full pass
        if improved_this_pass == 0 and iteration > 20:
            print(f"\n  No improvements found — stopping early at iteration {iteration + 1}.")
            break

    print(f"\n  Final error    : {best_error:.6f}")
    print(f"  Total improvements: {improved_total}")

    # Apply final weights
    final_evaluator = apply_weights(list(weights), labels)
    return weights, labels, best_error


# ─────────────────────────────────────────
# SAVE / LOAD WEIGHTS
# ─────────────────────────────────────────

def save_weights(weights, labels, error, path):
    """Save tuned weights to a JSON file."""
    data = {
        'error':   error,
        'weights': {label: w for label, w in zip(labels, weights)}
    }
    with open(path, 'w') as f:
        json.dump(data, f, indent=2)
    print(f"  Weights saved to: {path}")


def load_weights(path):
    """Load weights from JSON. Returns (weights, labels) or None if not found."""
    if not os.path.exists(path):
        return None, None
    with open(path, 'r') as f:
        data = json.load(f)
    labels  = list(data['weights'].keys())
    weights = [data['weights'][l] for l in labels]
    return weights, labels


def print_weight_changes(original_weights, tuned_weights, labels):
    """Print a summary of what changed."""
    print("\n  Weight changes:")
    print(f"  {'Label':<30} {'Before':>8} {'After':>8} {'Change':>8}")
    print(f"  {'-'*30} {'-'*8} {'-'*8} {'-'*8}")
    for orig, tuned, label in zip(original_weights, tuned_weights, labels):
        change = tuned - orig
        if abs(change) > 0:
            print(f"  {label:<30} {orig:>8.1f} {tuned:>8.1f} {change:>+8.1f}")


# ─────────────────────────────────────────
# MAIN
# ─────────────────────────────────────────

def run_tuning(input_path, output_path, iterations, max_positions):
    print("=" * 55)
    print("  Stepbot Texel Tuner")
    print("=" * 55)
    print(f"  PGN input    : {input_path}")
    print(f"  Output       : {output_path}")
    print(f"  Iterations   : {iterations}")
    print(f"  Max positions: {max_positions}")
    print()

    # Extract positions
    positions = extract_positions(input_path, max_positions)
    if not positions:
        print("  Not enough positions to tune. Run more self-play games first.")
        return

    if len(positions) < 100:
        print(f"  Warning: only {len(positions)} positions found.")
        print("  More self-play games will give better tuning results.")
        print()

    # Save original weights for comparison
    original_weights, labels = get_weights()

    # Run tuning
    tuned_weights, labels, final_error = tune(
        positions, iterations=iterations, step=5, verbose=True
    )

    # Show what changed
    print_weight_changes(original_weights, tuned_weights, labels)

    # Save results
    print()
    save_weights(tuned_weights, labels, final_error, output_path)

    print()
    print("  Done! To use these weights, copy the values from")
    print(f"  {output_path}")
    print("  into evaluate.py, or run with --apply to patch automatically.")
    print("=" * 55)


# ─────────────────────────────────────────
# ENTRY POINT
# ─────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Stepbot Texel tuner")
    parser.add_argument('--input',         default=DEFAULT_INPUT,
                        help='PGN file to tune from (default: selfplay_games.pgn)')
    parser.add_argument('--output',        default=DEFAULT_OUTPUT,
                        help='Output weights file (default: tuned_weights.json)')
    parser.add_argument('--iterations',    type=int, default=200,
                        help='Tuning iterations (default: 200)')
    parser.add_argument('--max-positions', type=int, default=5000,
                        help='Max positions to use (default: 5000)')
    args = parser.parse_args()

    run_tuning(
        input_path     = args.input,
        output_path    = args.output,
        iterations     = args.iterations,
        max_positions  = args.max_positions,
    )
