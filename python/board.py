# board.py
# The core board representation for our chess engine.
# We use a mailbox array: a list of 64 integers, one per square.
# Squares are indexed 0-63, where 0 = a1, 7 = h1, 56 = a8, 63 = h8.

# ─────────────────────────────────────────
# PIECE CONSTANTS
# We represent pieces as integers.
# Positive = White, Negative = Black, Zero = Empty.
# ─────────────────────────────────────────

EMPTY  = 0
PAWN   = 1
KNIGHT = 2
BISHOP = 3
ROOK   = 4
QUEEN  = 5
KING   = 6

WHITE =  1
BLACK = -1

# Piece symbols for printing the board
PIECE_SYMBOLS = {
    0:  '.',
    1:  'P',  -1:  'p',   # Pawns
    2:  'N',  -2:  'n',   # Knights
    3:  'B',  -3:  'b',   # Bishops
    4:  'R',  -4:  'r',   # Rooks
    5:  'Q',  -5:  'q',   # Queens
    6:  'K',  -6:  'k',   # Kings
}

# ─────────────────────────────────────────
# DIRECTION CONSTANTS
# Shared across movegen, evaluate, and zobrist.
# ─────────────────────────────────────────

DIAGONAL_DIRS  = [9, 7, -7, -9]
STRAIGHT_DIRS  = [8, -8, 1, -1]
ALL_DIRS       = DIAGONAL_DIRS + STRAIGHT_DIRS
KNIGHT_OFFSETS = [17, 15, 10, 6, -6, -10, -15, -17]

# ─────────────────────────────────────────
# HELPER FUNCTIONS
# ─────────────────────────────────────────

def square(file, rank):
    """
    Convert file (0-7, a-h) and rank (0-7, 1-8) to a square index (0-63).
    Example: square(0, 0) = a1 = 0
             square(4, 7) = e8 = 60
    """
    return rank * 8 + file

def file_of(sq):
    """Return the file (0-7) of a square index."""
    return sq % 8

def rank_of(sq):
    """Return the rank (0-7) of a square index."""
    return sq // 8

def square_name(sq):
    """Convert a square index to algebraic notation (e.g. 0 -> 'a1')."""
    files = 'abcdefgh'
    return files[file_of(sq)] + str(rank_of(sq) + 1)

def name_to_square(name):
    """Convert algebraic notation to a square index (e.g. 'e4' -> 28)."""
    files = 'abcdefgh'
    file = files.index(name[0])
    rank = int(name[1]) - 1
    return square(file, rank)

# ─────────────────────────────────────────
# BOARD CLASS
# ─────────────────────────────────────────

class Board:
    def __init__(self):
        """Initialise the board to the standard chess starting position."""

        # The mailbox: 64 squares, each holding a piece value.
        # Index 0 = a1 (bottom-left from White's perspective).
        self.squares = [EMPTY] * 64

        # Whose turn is it? WHITE or BLACK.
        self.turn = WHITE

        # Castling rights: four booleans.
        self.castling_rights = {
            'K': True,  # White kingside
            'Q': True,  # White queenside
            'k': True,  # Black kingside
            'q': True,  # Black queenside
        }

        # En passant target square (-1 means none available).
        # If a pawn just moved two squares, this is the square it passed through.
        self.en_passant_sq = -1

        # Half-move clock: counts moves since last capture or pawn move.
        # Used for the 50-move draw rule.
        self.halfmove_clock = 0

        # Full-move number: starts at 1, increments after Black's move.
        self.fullmove_number = 1

        # Set up the starting position.
        self._setup_starting_position()

    def _setup_starting_position(self):
        """Place all pieces in their starting squares."""

        # White pieces (rank 1, index 0-7)
        back_rank = [ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK]
        for file, piece in enumerate(back_rank):
            self.squares[square(file, 0)] = WHITE * piece   # Rank 1
            self.squares[square(file, 7)] = BLACK * piece   # Rank 8

        # Pawns (rank 2 for White, rank 7 for Black)
        for file in range(8):
            self.squares[square(file, 1)] =  WHITE * PAWN
            self.squares[square(file, 6)] =  BLACK * PAWN

    def get_piece(self, sq):
        """Return the piece at a given square index."""
        return self.squares[sq]

    def set_piece(self, sq, piece):
        """Place a piece on a given square."""
        self.squares[sq] = piece

    def is_empty(self, sq):
        """Return True if the square is empty."""
        return self.squares[sq] == EMPTY

    def colour_at(self, sq):
        """Return WHITE, BLACK, or None if the square is empty."""
        piece = self.squares[sq]
        if piece > 0:
            return WHITE
        elif piece < 0:
            return BLACK
        return None

    def print_board(self):
        """
        Print the board to the terminal.
        Rank 8 is printed at the top (as you'd see on a real board).
        """
        print()
        print("  a b c d e f g h")
        print("  ─────────────────")
        for rank in range(7, -1, -1):  # Print rank 8 down to rank 1
            row = f"{rank + 1}│ "
            for file in range(8):
                piece = self.squares[square(file, rank)]
                row += PIECE_SYMBOLS[piece] + ' '
            print(row)
        print()
        print(f"  Turn: {'White' if self.turn == WHITE else 'Black'}")
        print(f"  Castling: {'K' if self.castling_rights['K'] else '-'}"
              f"{'Q' if self.castling_rights['Q'] else '-'}"
              f"{'k' if self.castling_rights['k'] else '-'}"
              f"{'q' if self.castling_rights['q'] else '-'}")
        ep = square_name(self.en_passant_sq) if self.en_passant_sq != -1 else '-'
        print(f"  En passant: {ep}")
        print(f"  Move: {self.fullmove_number}")
        print()


# ─────────────────────────────────────────
# QUICK TEST — run this file directly to
# check the board prints correctly.
# ─────────────────────────────────────────

if __name__ == "__main__":
    board = Board()
    board.print_board()

    print("Square name tests:")
    print(f"  square(0,0) = {square(0,0)} → should be 0 (a1)")
    print(f"  square(4,0) = {square(4,0)} → should be 4 (e1)")
    print(f"  square(4,7) = {square(4,7)} → should be 60 (e8)")
    print(f"  square_name(0)  = {square_name(0)}  → should be a1")
    print(f"  square_name(60) = {square_name(60)} → should be e8")
    print(f"  name_to_square('e4') = {name_to_square('e4')} → should be 28")
