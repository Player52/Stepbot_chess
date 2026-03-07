# book.py
# Opening book for Stepbot.
#
# Loads opening_book.json and provides move lookups by position hash.
# Uses weighted random selection so Stepbot plays varied openings.
#
# Flow:
#   1. Load JSON on startup
#   2. Convert each FEN to a Zobrist hash
#   3. Store as hash -> list of (move, weight) pairs
#   4. On lookup, pick a weighted random move

import json
import os
import random
from board import WHITE, BLACK, EMPTY, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING
from board import square, file_of, rank_of, name_to_square
from movegen import Move
from zobrist import compute_hash

# ─────────────────────────────────────────
# FEN PARSER (lightweight, for book loading)
# ─────────────────────────────────────────

FEN_PIECE_MAP = {
    'P':  1, 'N':  2, 'B':  3, 'R':  4, 'Q':  5, 'K':  6,
    'p': -1, 'n': -2, 'b': -3, 'r': -4, 'q': -5, 'k': -6,
}

def _board_from_fen(fen):
    """
    Parse a FEN string into a Board object.
    Imported from engine.py logic — duplicated here to avoid circular imports.
    """
    from board import Board
    board = Board()
    board.squares = [EMPTY] * 64

    parts           = fen.strip().split()
    piece_placement = parts[0]
    active_colour   = parts[1] if len(parts) > 1 else 'w'
    castling        = parts[2] if len(parts) > 2 else 'KQkq'
    en_passant      = parts[3] if len(parts) > 3 else '-'
    halfmove        = parts[4] if len(parts) > 4 else '0'
    fullmove        = parts[5] if len(parts) > 5 else '1'

    rank = 7
    file = 0
    for char in piece_placement:
        if char == '/':
            rank -= 1
            file  = 0
        elif char.isdigit():
            file += int(char)
        else:
            board.squares[square(file, rank)] = FEN_PIECE_MAP[char]
            file += 1

    board.turn = WHITE if active_colour == 'w' else BLACK

    board.castling_rights = {
        'K': 'K' in castling,
        'Q': 'Q' in castling,
        'k': 'k' in castling,
        'q': 'q' in castling,
    }

    if en_passant != '-':
        board.en_passant_sq = name_to_square(en_passant)
    else:
        board.en_passant_sq = -1

    board.halfmove_clock   = int(halfmove)
    board.fullmove_number  = int(fullmove)

    return board


# ─────────────────────────────────────────
# OPENING BOOK
# ─────────────────────────────────────────

class OpeningBook:

    def __init__(self, book_path=None):
        """
        Load the opening book from a JSON file.
        book_path defaults to opening_book.json in the same directory.
        """
        if book_path is None:
            book_path = os.path.join(
                os.path.dirname(os.path.abspath(__file__)),
                'opening_book.json'
            )

        # Internal storage: hash -> list of [move_uci, weight] pairs
        self._book = {}
        self._loaded = False

        self._load(book_path)

    def _load(self, path):
        """Load and index the opening book from JSON."""
        if not os.path.exists(path):
            print(f"  [Book] No opening book found at {path}")
            return

        try:
            with open(path, 'r') as f:
                data = json.load(f)
        except Exception as e:
            print(f"  [Book] Failed to load opening book: {e}")
            return

        entries = data.get('moves', [])
        loaded  = 0

        for entry in entries:
            fen    = entry.get('fen')
            move   = entry.get('move')
            weight = entry.get('weight', 1)

            if not fen or not move:
                continue

            try:
                board = _board_from_fen(fen)
                h     = compute_hash(board)
            except Exception:
                continue

            if h not in self._book:
                self._book[h] = []

            self._book[h].append([move, weight])
            loaded += 1

        self._loaded = True
        print(f"  [Book] Loaded {loaded} entries covering "
              f"{len(self._book)} positions")

    def lookup(self, board):
        """
        Look up the current position in the opening book.
        Returns a Move object if a book move is found, or None.

        Uses weighted random selection — higher weight = more likely chosen.
        """
        if not self._loaded:
            return None

        h       = compute_hash(board)
        entries = self._book.get(h)

        if not entries:
            return None

        # Weighted random selection
        total   = sum(w for _, w in entries)
        r       = random.uniform(0, total)
        cumulative = 0

        chosen_uci = None
        for move_uci, weight in entries:
            cumulative += weight
            if r <= cumulative:
                chosen_uci = move_uci
                break

        if chosen_uci is None:
            chosen_uci = entries[0][0]

        # Parse UCI string into a Move object
        return self._parse_uci(chosen_uci)

    def _parse_uci(self, uci_str):
        """Convert a UCI move string (e.g. 'e2e4') to a Move object."""
        from_sq = name_to_square(uci_str[0:2])
        to_sq   = name_to_square(uci_str[2:4])

        promotion = None
        if len(uci_str) == 5:
            promo_map = {'n': KNIGHT, 'b': BISHOP, 'r': ROOK, 'q': QUEEN}
            promotion = promo_map.get(uci_str[4].lower())

        return Move(from_sq, to_sq, promotion)

    def update_weights(self, played_moves, result):
        """
        Update move weights based on game result (for self-play learning).

        played_moves : list of (fen, move_uci) pairs from the game
        result       : 1 = win for side that played first move,
                      -1 = loss, 0 = draw
        """
        if result == 0:
            return  # Don't update on draws

        adjustment = 2 if result == 1 else -1

        for fen, move_uci in played_moves:
            try:
                board = _board_from_fen(fen)
                h     = compute_hash(board)
            except Exception:
                continue

            if h not in self._book:
                continue

            for entry in self._book[h]:
                if entry[0] == move_uci:
                    # Clamp weight between 1 and 50
                    entry[1] = max(1, min(50, entry[1] + adjustment))
                    break

    def save(self, book_path=None):
        """
        Save the current book (with updated weights) back to JSON.
        Called after self-play sessions to persist learning.
        """
        if book_path is None:
            book_path = os.path.join(
                os.path.dirname(os.path.abspath(__file__)),
                'opening_book.json'
            )

        # Rebuild the moves list from internal storage
        # We need the original FENs — store them during load
        print(f"  [Book] Saving is only supported if FENs are cached.")
        print(f"  [Book] Use save_with_fens() after a self-play session.")

    def is_loaded(self):
        """Return True if the book loaded successfully."""
        return self._loaded

    def size(self):
        """Return the number of positions in the book."""
        return len(self._book)


# ─────────────────────────────────────────
# QUICK TEST
# ─────────────────────────────────────────

if __name__ == "__main__":
    from board import Board

    book  = OpeningBook()
    board = Board()

    print(f"\nBook size: {book.size()} positions")

    move = book.lookup(board)
    if move:
        print(f"Book move for starting position: {move}")
    else:
        print("No book move found for starting position")

    # Test multiple lookups to verify weighted randomness
    print("\nTesting weighted randomness (10 lookups from start):")
    counts = {}
    for _ in range(10):
        m = book.lookup(board)
        if m:
            key = str(m)
            counts[key] = counts.get(key, 0) + 1
    for move_str, count in sorted(counts.items(), key=lambda x: -x[1]):
        print(f"  {move_str}: {count} times")
