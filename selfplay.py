# selfplay.py
# Self-play engine for Stepbot.
#
# Makes Stepbot play against itself, records results, and updates
# opening book weights based on which lines led to wins or losses.
#
# Usage:
#   python selfplay.py              -- plays 10 games at depth 3
#   python selfplay.py --games 20   -- plays 20 games
#   python selfplay.py --depth 4    -- plays at depth 4 (slower but stronger)
#   python selfplay.py --no-update  -- plays without updating the book

import sys
import os
import argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from board import WHITE, BLACK, EMPTY, KING
from board import square, file_of, rank_of
from engine import board_from_fen, board_to_fen
from movegen import MoveGenerator
from search import Searcher
from book import OpeningBook

STARTING_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

# Maximum moves before declaring a draw (avoids infinite games)
MAX_MOVES = 200


# ─────────────────────────────────────────
# GAME RESULT DETECTION
# ─────────────────────────────────────────

def get_result(board, gen, position_history):
    """
    Return the game result from White's perspective:
      1  = White wins
     -1  = Black wins
      0  = Draw
      None = Game still in progress
    """
    moves = gen.generate_legal_moves(board)

    # No legal moves
    if not moves:
        if gen._king_in_check(board, board.turn):
            # Checkmate — the side to move has lost
            return -1 if board.turn == WHITE else 1
        else:
            # Stalemate
            return 0

    # 50-move rule
    if board.halfmove_clock >= 100:
        return 0

    # Threefold repetition
    current_fen_core = _fen_core(board)
    if position_history.count(current_fen_core) >= 3:
        return 0

    # Move limit
    if board.fullmove_number > MAX_MOVES:
        return 0

    return None  # Game still going


def _fen_core(board):
    """
    Return just the position part of the FEN (no clocks).
    Used for repetition detection — clocks don't matter for repetition.
    """
    full = board_to_fen(board)
    parts = full.split()
    # Keep piece placement, turn, castling, en passant — drop clocks
    return ' '.join(parts[:4])


# ─────────────────────────────────────────
# SINGLE GAME
# ─────────────────────────────────────────

def play_game(searcher, gen, book, depth, game_number, verbose=True):
    """
    Play one game of Stepbot vs Stepbot.

    Returns:
      result         : 1 = White wins, -1 = Black wins, 0 = Draw
      book_moves     : list of (fen, move_uci) pairs for opening book moves played
    """
    board            = board_from_fen(STARTING_FEN)
    position_history = []
    book_moves       = []   # (fen, move_uci) pairs while in book
    in_book          = True # Stop tracking once we leave the opening book
    move_count       = 0

    if verbose:
        print(f"\n  Game {game_number}:", end=" ", flush=True)

    while True:
        position_history.append(_fen_core(board))

        result = get_result(board, gen, position_history)
        if result is not None:
            break

        current_fen = board_to_fen(board)

        # ── Try the opening book first ──
        book_move = book.lookup(board) if in_book else None

        if book_move:
            move_uci = str(book_move)
            if book_move.promotion:
                promo_map = {2: 'n', 3: 'b', 4: 'r', 5: 'q'}
                move_uci += promo_map.get(book_move.promotion, '')
            book_moves.append((current_fen, move_uci))
            board = gen._apply_move(board, book_move)
            if verbose:
                print("B", end="", flush=True)  # B = book move
        else:
            # Left the book — use the searcher
            in_book = False
            best_move = searcher.find_best_move(board, max_depth=depth, time_limit=None)
            if best_move is None:
                # Shouldn't happen (get_result would have caught it), but be safe
                result = 0
                break
            board = gen._apply_move(board, best_move)
            if verbose:
                print(".", end="", flush=True)  # . = search move

        move_count += 1

    if verbose:
        result_str = {1: "White wins", -1: "Black wins", 0: "Draw"}.get(result, "?")
        print(f" {result_str} ({move_count} moves)")

    return result, book_moves


# ─────────────────────────────────────────
# SELF-PLAY SESSION
# ─────────────────────────────────────────

def run_session(num_games=10, depth=3, update_book=True):
    """
    Run a full self-play session.

    num_games   : number of games to play
    depth       : search depth per move
    update_book : whether to update and save the opening book
    """
    print("=" * 55)
    print("  Stepbot Self-Play Session")
    print("=" * 55)
    print(f"  Games : {num_games}")
    print(f"  Depth : {depth}")
    print(f"  Book update: {'yes' if update_book else 'no'}")
    print()

    gen      = MoveGenerator()
    searcher = Searcher()
    book     = OpeningBook()

    results     = {1: 0, -1: 0, 0: 0}   # White wins, Black wins, Draws
    all_book_moves = []  # (fen, move_uci, result) triples for weight updates

    for i in range(1, num_games + 1):
        result, book_moves = play_game(searcher, gen, book, depth, i, verbose=True)
        results[result] = results.get(result, 0) + 1

        # Record book moves with their result for weight updating
        # Result is from White's perspective; for Black's moves we flip it
        for fen, move_uci in book_moves:
            all_book_moves.append((fen, move_uci, result))

    # ── Summary ──
    print()
    print("=" * 55)
    print("  Results")
    print("=" * 55)
    print(f"  White wins : {results.get( 1, 0)}")
    print(f"  Black wins : {results.get(-1, 0)}")
    print(f"  Draws      : {results.get( 0, 0)}")
    print(f"  Total      : {num_games}")

    win_rate = results.get(1, 0) / num_games * 100
    draw_rate = results.get(0, 0) / num_games * 100
    print(f"\n  White win rate : {win_rate:.0f}%")
    print(f"  Draw rate      : {draw_rate:.0f}%")

    # ── Update opening book ──
    if update_book and all_book_moves:
        print()
        print("  Updating opening book weights...")

        # Group book moves by game and update weights
        # We call update_weights once per game with that game's book moves
        # Re-run through games to get per-game book moves
        # (We stored them per-move above — here we just call update_weights
        #  with each (fen, move_uci) and the overall result for that position)

        # Build per-position update: for each (fen, move_uci), pass result
        # relative to the side that played it
        for fen, move_uci, game_result in all_book_moves:
            # Determine which side played this move from the FEN
            parts = fen.split()
            side  = parts[1] if len(parts) > 1 else 'w'
            # Result from that side's perspective
            if side == 'w':
                local_result = game_result       # 1=white won, -1=white lost
            else:
                local_result = -game_result      # flip for black

            book.update_weights([(fen, move_uci)], local_result)

        book.save()
        print("  Opening book saved.")

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
