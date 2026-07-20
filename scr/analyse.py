# analyse.py
# Post-game blunder detection and analysis for Stepbot.
#
# Reads selfplay_games.pgn, replays each game move by move,
# and identifies blunders, mistakes, and inaccuracies by comparing
# the engine evaluation before and after each move.
#
# Outputs an annotated PGN to Self_play/selfplay_analysis.pgn
# which can be imported into chess.com or Lichess for review.
#
# Usage:
#   python analyse.py                        -- analyse all games in selfplay_games.pgn
#   python analyse.py --depth 4              -- analyse at depth 4 (more accurate, slower)
#   python analyse.py --input my_games.pgn   -- analyse a custom PGN file

import sys
import os
import argparse
import re

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from board import (
    WHITE, BLACK, EMPTY,
    PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    square, file_of, rank_of, square_name, name_to_square
)
from engine import board_from_fen, board_to_fen
from movegen import MoveGenerator, Move
from search import Searcher
from evaluate import Evaluator

STARTING_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

# Default file paths
SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
DEFAULT_INPUT  = os.path.join(SCRIPT_DIR, 'Self_play', 'selfplay_games.pgn')
DEFAULT_OUTPUT = os.path.join(SCRIPT_DIR, 'Self_play', 'selfplay_analysis.pgn')

# ─────────────────────────────────────────
# BLUNDER THRESHOLDS (centipawns)
# ─────────────────────────────────────────

INACCURACY_THRESHOLD = 20    # 0.2 pawns
MISTAKE_THRESHOLD    = 90    # 0.9 pawns
BLUNDER_THRESHOLD    = 300   # 3.0 pawns


# ─────────────────────────────────────────
# PGN PARSER
# Reads a PGN file and returns a list of games.
# Each game is a dict: {headers: {}, moves: [uci_or_san strings]}
# ─────────────────────────────────────────

def parse_pgn(path):
    """
    Parse a PGN file into a list of games.
    Each game is {'headers': dict, 'movetext': str}
    """
    if not os.path.exists(path):
        print(f"  [Analyse] File not found: {path}")
        return []

    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()

    games   = []
    # Split into individual games by double newline before a tag
    blocks  = re.split(r'\n\n(?=\[)', content.strip())

    for block in blocks:
        lines    = block.strip().split('\n')
        headers  = {}
        movetext_lines = []

        for line in lines:
            line = line.strip()
            if line.startswith('['):
                # Parse header tag e.g. [White "Stepbot"]
                m = re.match(r'\[(\w+)\s+"(.*)"\]', line)
                if m:
                    headers[m.group(1)] = m.group(2)
            else:
                movetext_lines.append(line)

        movetext = ' '.join(movetext_lines).strip()
        if movetext:
            games.append({'headers': headers, 'movetext': movetext})

    return games


def extract_moves_from_movetext(movetext):
    """
    Extract a list of SAN move strings from PGN movetext.
    Strips move numbers, comments, and result strings.
    """
    # Remove comments in curly braces
    movetext = re.sub(r'\{[^}]*\}', '', movetext)
    # Remove NAG annotations like $1, $2
    movetext = re.sub(r'\$\d+', '', movetext)
    # Remove result
    movetext = re.sub(r'(1-0|0-1|1/2-1/2|\*)\s*$', '', movetext)
    # Remove move numbers (e.g. "1." "12.")
    movetext = re.sub(r'\d+\.+', '', movetext)

    tokens = movetext.split()
    return [t for t in tokens if t]


# ─────────────────────────────────────────
# SAN -> MOVE CONVERTER
# Given a board and a SAN string, find the matching legal Move object.
# ─────────────────────────────────────────

def san_to_move(board, san, gen):
    """
    Convert a SAN string to a Move object by matching against legal moves.
    Returns None if no match found.
    """
    legal_moves = gen.generate_legal_moves(board)

    # Strip check/checkmate symbols
    san_clean = san.rstrip('+#')

    # Castling
    if san_clean in ('O-O', '0-0'):
        for m in legal_moves:
            piece = board.get_piece(m.from_sq)
            if abs(piece) == KING and m.to_sq - m.from_sq == 2:
                return m
        return None

    if san_clean in ('O-O-O', '0-0-0'):
        for m in legal_moves:
            piece = board.get_piece(m.from_sq)
            if abs(piece) == KING and m.from_sq - m.to_sq == 2:
                return m
        return None

    # Promotion
    promotion = None
    promo_map = {'N': KNIGHT, 'B': BISHOP, 'R': ROOK, 'Q': QUEEN}
    if '=' in san_clean:
        parts     = san_clean.split('=')
        san_clean = parts[0]
        promotion = promo_map.get(parts[1][0].upper())

    # Determine destination square (always last two chars)
    to_sq = name_to_square(san_clean[-2:])

    # Determine piece type
    piece_letter_map = {'N': KNIGHT, 'B': BISHOP, 'R': ROOK, 'Q': QUEEN, 'K': KING}
    if san_clean[0].isupper() and san_clean[0] in piece_letter_map:
        piece_type = piece_letter_map[san_clean[0]]
        disambig   = san_clean[1:-2].replace('x', '')
    else:
        piece_type = PAWN
        disambig   = san_clean[:-2].replace('x', '')

    # Filter legal moves to candidates
    colour = board.turn
    candidates = []
    for m in legal_moves:
        if m.to_sq != to_sq:
            continue
        p = board.get_piece(m.from_sq)
        if abs(p) != piece_type:
            continue
        if promotion and m.promotion != promotion:
            continue
        if not promotion and piece_type == PAWN and m.promotion:
            continue
        candidates.append(m)

    if not candidates:
        return None

    if len(candidates) == 1:
        return candidates[0]

    # Disambiguation
    for m in candidates:
        from_name = square_name(m.from_sq)
        if len(disambig) == 2:
            if from_name == disambig:
                return m
        elif len(disambig) == 1:
            if disambig.isdigit():
                if from_name[1] == disambig:
                    return m
            else:
                if from_name[0] == disambig:
                    return m

    return candidates[0]


# ─────────────────────────────────────────
# EVALUATION HELPER
# ─────────────────────────────────────────

def evaluate_position(board, searcher, depth):
    """
    Return a centipawn score for the position from White's perspective.
    Uses a shallow search for accuracy.
    """
    # Suppress search output during analysis
    import io
    old_stdout = sys.stdout
    sys.stdout = io.StringIO()
    try:
        move = searcher.find_best_move(board, max_depth=depth, time_limit=None)
    finally:
        sys.stdout = old_stdout

    # Get the score from the evaluator directly for speed
    ev    = Evaluator()
    score = ev.evaluate(board)
    return score


def score_to_pawns(centipawns):
    """Convert centipawns to a pawn string e.g. +1.23"""
    pawns = centipawns / 100.0
    return f"{pawns:+.2f}"


# ─────────────────────────────────────────
# SINGLE GAME ANALYSER
# ─────────────────────────────────────────

def analyse_game(game, game_number, depth, gen, searcher):
    """
    Analyse one game for blunders, mistakes, and inaccuracies.

    Returns:
      annotated_moves : list of (san, comment) pairs
      stats           : dict of counts
    """
    san_list = extract_moves_from_movetext(game['movetext'])
    board    = board_from_fen(STARTING_FEN)
    ev       = Evaluator()

    annotated_moves = []
    stats = {
        'blunders':     0,
        'mistakes':     0,
        'inaccuracies': 0,
        'white_blunders': 0,
        'black_blunders': 0,
    }

    print(f"\n  Game {game_number}: ", end="", flush=True)

    for i, san in enumerate(san_list):
        side = "White" if board.turn == WHITE else "Black"

        # Score BEFORE the move (from White's perspective)
        score_before = ev.evaluate(board)

        # Find the move
        move = san_to_move(board, san, gen)
        if move is None:
            print(f"\n  [!] Could not parse move: {san} (move {i+1})")
            annotated_moves.append((san, None))
            continue

        # Find the best move at this position
        import io
        old_stdout = sys.stdout
        sys.stdout = io.StringIO()
        try:
            best_move = searcher.find_best_move(board, max_depth=depth, time_limit=None)
        finally:
            sys.stdout = old_stdout

        # Apply the actual move
        new_board   = gen._apply_move(board, move)
        score_after = ev.evaluate(new_board)

        # Score drop from the perspective of the side that moved
        if board.turn == WHITE:
            drop = score_before - score_after   # positive = got worse for White
        else:
            drop = score_after - score_before   # positive = got worse for Black

        # Classify
        comment = None
        if drop >= BLUNDER_THRESHOLD:
            stats['blunders'] += 1
            if board.turn == WHITE:
                stats['white_blunders'] += 1
            else:
                stats['black_blunders'] += 1

            best_san = _move_to_san_simple(board, best_move) if best_move else '?'
            comment  = (f"Blunder! {score_to_pawns(-drop if board.turn==WHITE else drop)}. "
                       f"Better was {best_san}")
            print("B", end="", flush=True)

        elif drop >= MISTAKE_THRESHOLD:
            stats['mistakes'] += 1
            best_san = _move_to_san_simple(board, best_move) if best_move else '?'
            comment  = (f"Mistake. {score_to_pawns(-drop if board.turn==WHITE else drop)}. "
                       f"Better was {best_san}")
            print("M", end="", flush=True)

        elif drop >= INACCURACY_THRESHOLD:
            stats['inaccuracies'] += 1
            comment = f"Inaccuracy. {score_to_pawns(-drop if board.turn==WHITE else drop)}."
            print("?", end="", flush=True)

        else:
            print(".", end="", flush=True)

        annotated_moves.append((san, comment))
        board = new_board

    result_str = game['headers'].get('Result', '*')
    print(f" {result_str} | Blunders: {stats['blunders']} "
          f"Mistakes: {stats['mistakes']} "
          f"Inaccuracies: {stats['inaccuracies']}")

    return annotated_moves, stats


def _move_to_san_simple(board, move):
    """Simplified SAN for the 'better was' suggestion — just UCI is fine here."""
    if move is None:
        return '?'
    piece      = board.get_piece(move.from_sq)
    piece_type = abs(piece)
    if piece_type == KING and move.to_sq - move.from_sq == 2:
        return 'O-O'
    if piece_type == KING and move.from_sq - move.to_sq == 2:
        return 'O-O-O'
    return str(move)


# ─────────────────────────────────────────
# ANNOTATED PGN WRITER
# ─────────────────────────────────────────

def write_annotated_pgn(path, game, game_number, annotated_moves, stats):
    """Write an annotated PGN game with inline comments."""
    headers    = game['headers']
    result_str = headers.get('Result', '*')

    lines = [
        f'[Event "{headers.get("Event", f"Stepbot Analysis Game {game_number}")}"]',
        f'[Site "{headers.get("Site", "Stepbot")}"]',
        f'[Date "{headers.get("Date", "????.??.??")}"]',
        f'[Round "{headers.get("Round", game_number)}"]',
        f'[White "{headers.get("White", "Stepbot")}"]',
        f'[Black "{headers.get("Black", "Stepbot")}"]',
        f'[Result "{result_str}"]',
        f'[Annotator "Stepbot Analyser"]',
        '',
    ]

    # Build annotated movetext
    move_text = (f'{{ Blunders: W={stats["white_blunders"]} '
                 f'B={stats["black_blunders"]} | '
                 f'Mistakes: {stats["mistakes"]} | '
                 f'Inaccuracies: {stats["inaccuracies"]} }} ')

    for i, (san, comment) in enumerate(annotated_moves):
        if i % 2 == 0:
            move_text += f'{i // 2 + 1}. '
        move_text += san + ' '
        if comment:
            move_text += '{ ' + comment + ' } '

    move_text += result_str

    # Wrap at 80 chars
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

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'a', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')


# ─────────────────────────────────────────
# MAIN SESSION
# ─────────────────────────────────────────

def run_analysis(input_path, output_path, depth):
    print("=" * 55)
    print("  Stepbot Game Analyser")
    print("=" * 55)
    print(f"  Input  : {input_path}")
    print(f"  Output : {output_path}")
    print(f"  Depth  : {depth}")
    print()
    print("  Legend: . = fine  ? = inaccuracy  M = mistake  B = blunder")

    games = parse_pgn(input_path)
    if not games:
        print("  No games found.")
        return

    print(f"  Found {len(games)} game(s) to analyse.")

    # Clear output file for fresh run
    if os.path.exists(output_path):
        os.remove(output_path)

    gen      = MoveGenerator()
    searcher = Searcher()

    total_stats = {
        'blunders': 0, 'mistakes': 0, 'inaccuracies': 0,
        'white_blunders': 0, 'black_blunders': 0,
    }

    for i, game in enumerate(games, 1):
        annotated_moves, stats = analyse_game(game, i, depth, gen, searcher)
        write_annotated_pgn(output_path, game, i, annotated_moves, stats)
        for key in total_stats:
            total_stats[key] += stats[key]

    print()
    print("=" * 55)
    print("  Analysis Complete")
    print("=" * 55)
    print(f"  Games analysed  : {len(games)}")
    print(f"  Total blunders  : {total_stats['blunders']} "
          f"(W: {total_stats['white_blunders']} "
          f"B: {total_stats['black_blunders']})")
    print(f"  Total mistakes  : {total_stats['mistakes']}")
    print(f"  Total inaccuracies: {total_stats['inaccuracies']}")
    print(f"\n  Annotated PGN saved to:")
    print(f"  {output_path}")
    print("=" * 55)


# ─────────────────────────────────────────
# ENTRY POINT
# ─────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Stepbot game analyser")
    parser.add_argument('--input',  default=DEFAULT_INPUT,
                        help=f'PGN file to analyse (default: selfplay_games.pgn)')
    parser.add_argument('--output', default=DEFAULT_OUTPUT,
                        help=f'Output annotated PGN (default: selfplay_analysis.pgn)')
    parser.add_argument('--depth',  type=int, default=3,
                        help='Analysis depth per move (default: 3)')
    args = parser.parse_args()

    run_analysis(args.input, args.output, args.depth)
