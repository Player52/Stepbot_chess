# selfplay.py
# Self-play engine for Stepbot.
#
# Makes Stepbot play against itself, records results, updates
# opening book weights, and saves games as PGN.
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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from board import WHITE, BLACK, EMPTY, KING, PAWN, KNIGHT, BISHOP, ROOK, QUEEN
from board import square, file_of, rank_of, square_name
from engine import board_from_fen, board_to_fen
from movegen import MoveGenerator, Move
from search import Searcher
from book import OpeningBook

STARTING_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
MAX_MOVES    = 200

# PGN file lives in the Self_play folder next to this script
PGN_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        'Self_play', 'selfplay_games.pgn')


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
# PGN MOVE CONVERSION
# ─────────────────────────────────────────

PIECE_LETTER = {
    KNIGHT: 'N',
    BISHOP: 'B',
    ROOK:   'R',
    QUEEN:  'Q',
    KING:   'K',
}

def move_to_san(board, move, gen):
    """Convert a Move to Standard Algebraic Notation. Call BEFORE applying the move."""
    piece      = board.get_piece(move.from_sq)
    piece_type = abs(piece)
    colour     = board.turn
    to_name    = square_name(move.to_sq)
    from_file  = 'abcdefgh'[file_of(move.from_sq)]
    from_rank  = str(rank_of(move.from_sq) + 1)

    # Castling
    if piece_type == KING:
        diff = move.to_sq - move.from_sq
        if diff == 2:
            return _append_check(board, move, gen, 'O-O')
        if diff == -2:
            return _append_check(board, move, gen, 'O-O-O')

    # Pawn moves
    if piece_type == PAWN:
        is_capture = (not board.is_empty(move.to_sq)) or (move.to_sq == board.en_passant_sq)
        san = (from_file + 'x' + to_name) if is_capture else to_name
        if move.promotion:
            san += '=' + PIECE_LETTER.get(move.promotion, 'Q')
        return _append_check(board, move, gen, san)

    # Piece moves
    letter      = PIECE_LETTER.get(piece_type, '')
    legal_moves = gen.generate_legal_moves(board)
    ambiguous   = [m for m in legal_moves
                   if m != move
                   and abs(board.get_piece(m.from_sq)) == piece_type
                   and board.colour_at(m.from_sq) == colour
                   and m.to_sq == move.to_sq]

    disambig = ''
    if ambiguous:
        same_file = any(file_of(m.from_sq) == file_of(move.from_sq) for m in ambiguous)
        same_rank = any(rank_of(m.from_sq) == rank_of(move.from_sq) for m in ambiguous)
        if not same_file:
            disambig = from_file
        elif not same_rank:
            disambig = from_rank
        else:
            disambig = from_file + from_rank

    capture_str = 'x' if not board.is_empty(move.to_sq) else ''
    san = letter + disambig + capture_str + to_name
    return _append_check(board, move, gen, san)


def _append_check(board, move, gen, san):
    """Append + for check or # for checkmate."""
    new_board = gen._apply_move(board, move)
    if gen._king_in_check(new_board, new_board.turn):
        if not gen.generate_legal_moves(new_board):
            san += '#'
        else:
            san += '+'
    return san


# ─────────────────────────────────────────
# PGN WRITER
# ─────────────────────────────────────────

def build_pgn(game_number, result, san_moves, date_str):
    """Build a PGN string for one game."""
    result_str = {1: '1-0', -1: '0-1', 0: '1/2-1/2'}.get(result, '*')
    lines = [
        f'[Event "Stepbot Self-Play"]',
        f'[Site "Local"]',
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

    words = move_text.split()
    line  = ''
    for word in words:
        if len(line) + len(word) + 1 > 80:
            lines.append(line.rstrip())
            line = word + ' '
        else:
            line += word + ' '
    if line.strip():
        lines.append(line.rstrip())

    lines.append('')
    return '\n'.join(lines) + '\n'


def save_pgn(pgn_str):
    """Append a PGN game to selfplay_games.pgn."""
    os.makedirs(os.path.dirname(PGN_PATH), exist_ok=True)
    with open(PGN_PATH, 'a') as f:
        f.write(pgn_str)


# ─────────────────────────────────────────
# SINGLE GAME
# ─────────────────────────────────────────

def play_game(searcher, gen, book, depth, game_number, verbose=True):
    board            = board_from_fen(STARTING_FEN)
    position_history = []
    book_moves       = []
    san_moves        = []
    in_book          = True
    move_count       = 0

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
            move_uci = str(book_move)
            if book_move.promotion:
                promo_map = {2: 'n', 3: 'b', 4: 'r', 5: 'q'}
                move_uci += promo_map.get(book_move.promotion, '')
            book_moves.append((current_fen, move_uci))
            san_moves.append(move_to_san(board, book_move, gen))
            board = gen._apply_move(board, book_move)
            if verbose:
                print("B", end="", flush=True)
        else:
            in_book   = False
            best_move = searcher.find_best_move(board, max_depth=depth, time_limit=None)
            if best_move is None:
                result = 0
                break
            san_moves.append(move_to_san(board, best_move, gen))
            board = gen._apply_move(board, best_move)
            if verbose:
                print(".", end="", flush=True)

        move_count += 1

    if verbose:
        result_str = {1: "White wins", -1: "Black wins", 0: "Draw"}.get(result, "?")
        print(f" {result_str} ({move_count} moves)")

    return result, book_moves, san_moves


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
    print(f"  PGN file   : {PGN_PATH}")
    print()

    gen      = MoveGenerator()
    searcher = Searcher()
    book     = OpeningBook()

    results        = {1: 0, -1: 0, 0: 0}
    all_book_moves = []
    date_str       = datetime.date.today().strftime("%Y.%m.%d")

    for i in range(1, num_games + 1):
        result, book_moves, san_moves = play_game(
            searcher, gen, book, depth, i, verbose=True
        )
        results[result] = results.get(result, 0) + 1

        for fen, move_uci in book_moves:
            all_book_moves.append((fen, move_uci, result))

        pgn = build_pgn(i, result, san_moves, date_str)
        save_pgn(pgn)

    # Summary
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

    # Update opening book
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
