# evaluate.py
# Evaluates how good a chess position is.
#
# Returns a score in "centipawns" — a pawn is worth 100 points.
# Positive = good for White, Negative = good for Black.
#
# Phase 1 evaluation includes:
#   - Material counting (piece values)
#   - Piece-square tables (reward good piece placement)

from board import (
    Board, WHITE, BLACK, EMPTY,
    PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    square, file_of, rank_of
)

# ─────────────────────────────────────────
# PIECE VALUES (in centipawns)
# ─────────────────────────────────────────

PIECE_VALUES = {
    PAWN:   100,
    KNIGHT: 320,
    BISHOP: 330,
    ROOK:   500,
    QUEEN:  900,
    KING:   20000,  # Effectively infinite — losing the king = losing the game
}

# ─────────────────────────────────────────
# PIECE-SQUARE TABLES
# These give bonuses/penalties based on WHERE a piece stands.
# Tables are from White's perspective (index 0 = a1, 63 = h8).
# For Black, we mirror the table vertically.
# ─────────────────────────────────────────

# Pawns: reward advancing, reward centre control
PAWN_TABLE = [
     0,  0,  0,  0,  0,  0,  0,  0,   # Rank 1 (shouldn't be here)
     5, 10, 10,-20,-20, 10, 10,  5,   # Rank 2
     5, -5,-10,  0,  0,-10, -5,  5,   # Rank 3
     0,  0,  0, 20, 20,  0,  0,  0,   # Rank 4
     5,  5, 10, 25, 25, 10,  5,  5,   # Rank 5
    10, 10, 20, 30, 30, 20, 10, 10,   # Rank 6
    50, 50, 50, 50, 50, 50, 50, 50,   # Rank 7 (about to promote)
     0,  0,  0,  0,  0,  0,  0,  0,   # Rank 8 (promoted)
]

# Knights: reward centre, penalise edges
KNIGHT_TABLE = [
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50,
]

# Bishops: reward diagonals and open positions
BISHOP_TABLE = [
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20,
]

# Rooks: reward open files and 7th rank
ROOK_TABLE = [
     0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0,
]

# Queens: mix of bishop and rook, avoid early development
QUEEN_TABLE = [
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -10,  5,  5,  5,  5,  5,  0,-10,
      0,  0,  5,  5,  5,  5,  0, -5,
     -5,  0,  5,  5,  5,  5,  0, -5,
    -10,  0,  5,  5,  5,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20,
]

# King (middlegame): hide behind pawns, stay castled
KING_TABLE_MG = [
     20, 30, 10,  0,  0, 10, 30, 20,
     20, 20,  0,  0,  0,  0, 20, 20,
    -10,-20,-20,-20,-20,-20,-20,-10,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
]

PIECE_SQUARE_TABLES = {
    PAWN:   PAWN_TABLE,
    KNIGHT: KNIGHT_TABLE,
    BISHOP: BISHOP_TABLE,
    ROOK:   ROOK_TABLE,
    QUEEN:  QUEEN_TABLE,
    KING:   KING_TABLE_MG,
}

# ─────────────────────────────────────────
# EVALUATOR CLASS
# ─────────────────────────────────────────

class Evaluator:

    def evaluate(self, board):
        """
        Return a score for the current position.
        Positive = good for White, Negative = good for Black.
        """
        score = 0

        for sq in range(64):
            piece = board.get_piece(sq)
            if piece == EMPTY:
                continue

            colour     = WHITE if piece > 0 else BLACK
            piece_type = abs(piece)

            # Material value
            material = PIECE_VALUES[piece_type]

            # Piece-square bonus
            # White uses table as-is (a1=index 0)
            # Black mirrors vertically (a8=index 0 for Black)
            table = PIECE_SQUARE_TABLES[piece_type]
            if colour == WHITE:
                ps_bonus = table[sq]
            else:
                # Mirror: flip rank (rank 0 <-> rank 7)
                mirrored_sq = (7 - rank_of(sq)) * 8 + file_of(sq)
                ps_bonus = table[mirrored_sq]

            # Add to score (positive for White, negative for Black)
            score += colour * (material + ps_bonus)

        return score


# ─────────────────────────────────────────
# QUICK TEST
# ─────────────────────────────────────────

if __name__ == "__main__":
    board = Board()
    ev = Evaluator()

    score = ev.evaluate(board)
    print(f"Starting position score: {score}")
    print("(Should be 0 — perfectly symmetric position)\n")
