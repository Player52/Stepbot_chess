# selfplay.py
# Self-play engine for Stepbot.
#
# Makes Stepbot play against itself, records results, updates
# opening book weights, tracks ELO, and saves PGN logs.
#
# Usage:
#   python selfplay.py              -- plays 10 games at depth 3
#   python selfplay.py --games 20   -- plays 20 games
#   python selfplay.py --depth 4    -- plays at depth 4 (slower but stronger)
#   python selfplay.py --no-update  -- plays without updating the book

import sys
import os
import argparse
import datetime
import json
import math

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from board import WHITE, BLACK, EMPTY, KING, PAWN, KNIGHT, BISHOP, ROOK, QUEEN
from board import square, file_of, rank_of, square_name
from engine import board_from_fen, board_to_fen
from movegen import MoveGenerator, Move
from search import Searcher
from book import OpeningBook

STARTING_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

MAX_MOVES = 200

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

PGN_PATH = os.path.join(SCRIPT_DIR, 'Self_play', 'selfplay_games.pgn')
ELO_PATH = os.path.join(SCRIPT_DIR, 'Self_play', 'elo_history.json')

# ELO settings
ELO_K          = 32      # K-factor — how much each game shifts the rating
ELO_DEFAULT    = 1200    # Starting ELO if no history exists


# ─────────────────────────────────────────
# GAME RESULT DETECTION
# ─────────────────────────────────────────

def get_result(board, gen, position_history):
    moves = gen.generate_legal_moves(board)
    if not moves:
        if gen._king_in_check(board, board.turn):
            return -1 if board.turn == WHITE else 1
        else:
            return 0
    if board.halfmove_clock >= 100:
        return 0
    current_fen_core = _fen_core(board)
    if position_history.count(current_fen_core) >= 3:
        return 0
    if board.fullmove_number > MAX_MOVES:
        return 0
    return None


def _fen_core(board):
    full  = board_to_fen(board)
    parts = full.split()
    return ' '.join(parts[:4])


# ─────────────────────────────────────────
# UCI -> SAN CONVERTER
# ─────────────────────────────────────────

PIECE_LETTERS = {KNIGHT: 'N', BISHOP: 'B', ROOK: 'R', QUEEN: 'Q', KING: 'K'}

def move_to_san(board, move, gen):
    piece      = board.get_piece(move.from_sq)
    piece_type = abs(piece)
    from_name  = square_name(move.from_sq)
    to_name    = square_name(move.to_sq)

    if piece_type == KING:
        diff = move.to_sq - move.from_sq
        if diff == 2:  return 'O-O'
        if diff == -2: return 'O-O-O'

    is_capture = (not board.is_empty(move.to_sq) or
                  move.to_sq == board.en_passant_sq)

    if piece_type == PAWN:
        san = (from_name[0] + 'x' + to_name) if is_capture else to_name
        if move.promotion:
            promo_map = {KNIGHT: 'N', BISHOP: 'B', ROOK: 'R', QUEEN: 'Q'}
            san += '=' + promo_map.get(move.promotion, 'Q')
    else:
        piece_letter = PIECE_LETTERS.get(piece_type, '')
        legal_moves  = gen.generate_legal_moves(board)
        ambiguous    = [
            m for m in legal_moves
            if m.to_sq == move.to_sq
            and m.from_sq != move.from_sq
            and abs(board.get_piece(m.from_sq)) == piece_type
        ]
        disambig = ''
        if ambiguous:
            same_file = [m for m in ambiguous if file_of(m.from_sq) == file_of(move.from_sq)]
            same_rank = [m for m in ambiguous if rank_of(m.from_sq) == rank_of(move.from_sq)]
            if not same_file:
                disambig = from_name[0]
            elif not same_rank:
                disambig = from_name[1]
            else:
                disambig = from_name
        san = piece_letter + disambig + ('x' if is_capture else '') + to_name

    new_board = gen._apply_move(board, move)
    if gen._king_in_check(new_board, new_board.turn):
        follow_up = gen.generate_legal_moves(new_board)
        san += '#' if not follow_up else '+'

    return san


# ─────────────────────────────────────────
# PGN WRITER
# ─────────────────────────────────────────

def write_pgn(game_number, result, san_moves, date_str):
    result_str = {1: '1-0', -1: '0-1', 0: '1/2-1/2'}.get(result, '*')

    lines = [
        f'[Event "Stepbot Self-Play Game {game_number}"]',
        f'[Site "Stepbot"]',
        f'[Date "{date_str}"]',
        f'[Round "{game_number}"]',
        f'[White "Stepbot"]',
        f'[Black "Stepbot"]',
        f'[Result "{result_str}"]',
        '',
    ]

    move_text = ''
    for i, san in enumerate(san_moves):
        if i % 2 == 0:
            move_text += f'{i // 2 + 1}. '
        move_text += san + ' '
    move_text += result_str

    words, line, wrapped = move_text.split(), '', []
    for word in words:
        if len(line) + len(word) + 1 > 80:
            wrapped.append(line.rstrip())
            line = word + ' '
        else:
            line += word + ' '
    if line.strip():
        wrapped.append(line.rstrip())

    lines.extend(wrapped)
    lines.append('')
    lines.append('')

    os.makedirs(os.path.dirname(PGN_PATH), exist_ok=True)
    with open(PGN_PATH, 'a', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')


# ─────────────────────────────────────────
# ELO TRACKING
# ─────────────────────────────────────────

def load_elo():
    """Load ELO history from JSON. Returns (current_elo, history_list)."""
    if not os.path.exists(ELO_PATH):
        return ELO_DEFAULT, []
    try:
        with open(ELO_PATH, 'r') as f:
            data = json.load(f)
        return data.get('current_elo', ELO_DEFAULT), data.get('history', [])
    except Exception:
        return ELO_DEFAULT, []


def save_elo(current_elo, history):
    """Save ELO history to JSON."""
    os.makedirs(os.path.dirname(ELO_PATH), exist_ok=True)
    with open(ELO_PATH, 'w') as f:
        json.dump({'current_elo': current_elo, 'history': history}, f, indent=2)


def expected_score(rating_a, rating_b):
    """Expected score for player A against player B."""
    return 1.0 / (1.0 + math.pow(10, (rating_b - rating_a) / 400.0))


def update_elo(current_elo, results):
    """
    Update ELO based on a list of game results.
    Since both sides are Stepbot, we treat it as Stepbot playing against itself.
    We track the White side's ELO as the canonical rating —
    a win as White = improvement, a win as Black = same engine so still improves,
    draws = stable.

    In practice we just measure: did the engine play well overall?
    Win = +, Draw = neutral, Loss as White = slight -.
    """
    elo = float(current_elo)

    for result in results:
        # Both players have the same rating — expected score is always 0.5
        expected = 0.5

        # Actual score from White's perspective
        if result == 1:
            actual = 1.0   # White won
        elif result == -1:
            actual = 0.0   # Black won (White lost)
        else:
            actual = 0.5   # Draw

        elo += ELO_K * (actual - expected)

    return round(elo, 1)


def print_elo_history(history, current_elo):
    """Print a formatted ELO history table."""
    print()
    print("  ELO History")
    print("  " + "─" * 45)
    print(f"  {'Session':<10} {'Date':<14} {'ELO':>7} {'Change':>8}")
    print("  " + "─" * 45)

    for entry in history[-10:]:   # Show last 10 sessions
        change_str = f"{entry['change']:+.1f}" if entry['change'] != 0 else "  —"
        print(f"  {entry['session']:<10} {entry['date']:<14} "
              f"{entry['elo']:>7.1f} {change_str:>8}")

    print("  " + "─" * 45)
    print(f"  {'Current ELO:':<24} {current_elo:>7.1f}")
    print()


# ─────────────────────────────────────────
# SINGLE GAME
# ─────────────────────────────────────────

def play_game(searcher, gen, book, depth, game_number, date_str, verbose=True):
    board            = board_from_fen(STARTING_FEN)
    position_history = []
    book_moves       = []
    san_moves        = []
    in_book          = True
    move_count       = 0
    result           = 0

    if verbose:
        print(f"\n  Game {game_number}:", end=" ", flush=True)

    while True:
        position_history.append(_fen_core(board))
        result = get_result(board, gen, position_history)
        if result is not None:
            break

        current_fen = board_to_fen(board)
        book_move   = book.lookup(board) if in_book else None

        if book_move:
            san = move_to_san(board, book_move, gen)
            san_moves.append(san)
            move_uci = str(book_move)
            if book_move.promotion:
                promo_map = {2: 'n', 3: 'b', 4: 'r', 5: 'q'}
                move_uci += promo_map.get(book_move.promotion, '')
            book_moves.append((current_fen, move_uci))
            board = gen._apply_move(board, book_move)
            if verbose:
                print("B", end="", flush=True)
        else:
            in_book   = False
            best_move = searcher.find_best_move(board, max_depth=depth, time_limit=None)
            if best_move is None:
                result = 0
                break
            san = move_to_san(board, best_move, gen)
            san_moves.append(san)
            board = gen._apply_move(board, best_move)
            if verbose:
                print(".", end="", flush=True)

        move_count += 1

    if verbose:
        result_str = {1: "White wins", -1: "Black wins", 0: "Draw"}.get(result, "?")
        print(f" {result_str} ({move_count} moves)")

    write_pgn(game_number, result, san_moves, date_str)
    return result, book_moves


# ─────────────────────────────────────────
# SELF-PLAY SESSION
# ─────────────────────────────────────────

def run_session(num_games=10, depth=3, update_book=True):
    print("=" * 55)
    print("  Stepbot Self-Play Session")
    print("=" * 55)
    print(f"  Games      : {num_games}")
    print(f"  Depth      : {depth}")
    print(f"  Book update: {'yes' if update_book else 'no'}")
    print(f"  PGN output : {PGN_PATH}")
    print()

    gen      = MoveGenerator()
    searcher = Searcher()
    book     = OpeningBook()

    date_str       = datetime.date.today().strftime('%Y.%m.%d')
    results        = {1: 0, -1: 0, 0: 0}
    all_results    = []
    all_book_moves = []

    for i in range(1, num_games + 1):
        result, book_moves = play_game(
            searcher, gen, book, depth, i, date_str, verbose=True
        )
        results[result] = results.get(result, 0) + 1
        all_results.append(result)
        for fen, move_uci in book_moves:
            all_book_moves.append((fen, move_uci, result))

    # ── Results summary ──
    print()
    print("=" * 55)
    print("  Results")
    print("=" * 55)
    print(f"  White wins : {results.get( 1, 0)}")
    print(f"  Black wins : {results.get(-1, 0)}")
    print(f"  Draws      : {results.get( 0, 0)}")
    print(f"  Total      : {num_games}")
    win_rate  = results.get(1, 0) / num_games * 100
    draw_rate = results.get(0, 0) / num_games * 100
    print(f"\n  White win rate : {win_rate:.0f}%")
    print(f"  Draw rate      : {draw_rate:.0f}%")
    print(f"\n  Games saved to : {PGN_PATH}")

    # ── Update opening book ──
    if update_book and all_book_moves:
        print()
        print("  Updating opening book weights...")
        for fen, move_uci, game_result in all_book_moves:
            parts        = fen.split()
            side         = parts[1] if len(parts) > 1 else 'w'
            local_result = game_result if side == 'w' else -game_result
            book.update_weights([(fen, move_uci)], local_result)
        book.save()
        print("  Opening book saved.")

    # ── ELO tracking ──
    print()
    print("  Updating ELO...")
    current_elo, history = load_elo()
    old_elo     = current_elo
    current_elo = update_elo(current_elo, all_results)
    change      = current_elo - old_elo

    session_number = len(history) + 1
    history.append({
        'session': session_number,
        'date':    date_str,
        'elo':     current_elo,
        'change':  round(change, 1),
        'games':   num_games,
        'wins':    results.get(1, 0),
        'draws':   results.get(0, 0),
        'losses':  results.get(-1, 0),
    })

    save_elo(current_elo, history)
    print_elo_history(history, current_elo)

    change_str = f"{change:+.1f}" if change != 0 else "no change"
    print(f"  ELO: {old_elo:.1f} → {current_elo:.1f} ({change_str})")
    print()
    print("  Session complete!")
    print("=" * 55)


# ─────────────────────────────────────────
# ENTRY POINT
# ─────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Stepbot self-play session")
    parser.add_argument('--games',     type=int,  default=10,
                        help='Number of games to play (default: 10)')
    parser.add_argument('--depth',     type=int,  default=3,
                        help='Search depth per move (default: 3)')
    parser.add_argument('--no-update', action='store_true',
                        help='Do not update the opening book after playing')
    args = parser.parse_args()

    run_session(
        num_games   = args.games,
        depth       = args.depth,
        update_book = not args.no_update,
      )
