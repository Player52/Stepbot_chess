# zobrist.py
# Zobrist hashing for the transposition table.
#
# Generates a unique (or near-unique) 64-bit integer for any chess position.
# Used as the key in the transposition table dictionary.
#
# How it works:
#   - At startup, generate random 64-bit numbers for every piece/square combo
#   - XOR them together based on what's on the board
#   - When a move is made, XOR out old pieces and XOR in new ones (very fast)

import random
from board import (
    WHITE, BLACK, EMPTY,
    PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    square, file_of, rank_of
)

# ─────────────────────────────────────────
# ZOBRIST TABLE
# ─────────────────────────────────────────

# We use a fixed seed so the same random numbers are generated every run.
# This means saved hashes are consistent across sessions.
_rng = random.Random(20250307)  # Seed = today's date — change if you like

def _rand64():
    """Generate a random 64-bit integer."""
    return _rng.getrandbits(64)

# Piece index mapping: we need a consistent index 0-11 for each piece/colour.
# White pieces: PAWN=0, KNIGHT=1, BISHOP=2, ROOK=3, QUEEN=4, KING=5
# Black pieces: PAWN=6, KNIGHT=7, BISHOP=8, ROOK=9, QUEEN=10, KING=11

def _piece_index(piece):
    """Convert a board piece value to a 0-11 index for the Zobrist table."""
    piece_type = abs(piece) - 1        # PAWN(1)->0, KNIGHT(2)->1, ... KING(6)->5
    colour_offset = 0 if piece > 0 else 6  # White=0, Black=6
    return piece_type + colour_offset

# Random number table: PIECE_SQUARE_TABLE[piece_index][square]
PIECE_SQUARE_TABLE = [
    [_rand64() for _ in range(64)]
    for _ in range(12)
]

# Random number for Black to move (XOR this in when it's Black's turn)
BLACK_TO_MOVE = _rand64()

# Random numbers for castling rights (one per right)
CASTLING_RANDOM = {
    'K': _rand64(),
    'Q': _rand64(),
    'k': _rand64(),
    'q': _rand64(),
}

# Random numbers for en passant file (one per file, 0-7)
EN_PASSANT_RANDOM = [_rand64() for _ in range(8)]


# ─────────────────────────────────────────
# HASH COMPUTATION
# ─────────────────────────────────────────

def compute_hash(board):
    """
    Compute the Zobrist hash for a board position from scratch.
    Used once when setting up a position; after that we update incrementally.
    """
    h = 0

    # XOR in all pieces on the board
    for sq in range(64):
        piece = board.get_piece(sq)
        if piece != EMPTY:
            h ^= PIECE_SQUARE_TABLE[_piece_index(piece)][sq]

    # XOR in turn
    if board.turn == BLACK:
        h ^= BLACK_TO_MOVE

    # XOR in castling rights
    for key, value in board.castling_rights.items():
        if value:
            h ^= CASTLING_RANDOM[key]

    # XOR in en passant file (if any)
    if board.en_passant_sq != -1:
        h ^= EN_PASSANT_RANDOM[file_of(board.en_passant_sq)]

    return h


def update_hash(h, board, move, new_board):
    """
    Incrementally update a Zobrist hash after a move.
    Much faster than recomputing from scratch.

    h         : the hash BEFORE the move
    board     : the board BEFORE the move
    move      : the Move that was made
    new_board : the board AFTER the move (used for castling/en passant state)

    Returns the new hash.
    """
    from board import PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, EMPTY

    piece      = board.get_piece(move.from_sq)
    piece_type = abs(piece)
    colour     = 1 if piece > 0 else -1

    # ── Remove piece from its original square ──
    h ^= PIECE_SQUARE_TABLE[_piece_index(piece)][move.from_sq]

    # ── Remove any captured piece ──
    captured = board.get_piece(move.to_sq)
    if captured != EMPTY:
        h ^= PIECE_SQUARE_TABLE[_piece_index(captured)][move.to_sq]

    # ── Handle en passant capture ──
    if piece_type == PAWN and move.to_sq == board.en_passant_sq:
        ep_pawn_sq = move.to_sq - (8 if colour == 1 else -8)
        ep_pawn    = board.get_piece(ep_pawn_sq)
        h ^= PIECE_SQUARE_TABLE[_piece_index(ep_pawn)][ep_pawn_sq]

    # ── Add piece to its new square (account for promotion) ──
    if move.promotion:
        landed_piece = colour * move.promotion
    else:
        landed_piece = piece
    h ^= PIECE_SQUARE_TABLE[_piece_index(landed_piece)][move.to_sq]

    # ── Handle castling: move the rook too ──
    if piece_type == KING:
        if move.to_sq - move.from_sq == 2:    # Kingside
            rook_from = move.from_sq + 3
            rook_to   = move.from_sq + 1
            rook      = colour * ROOK
            h ^= PIECE_SQUARE_TABLE[_piece_index(rook)][rook_from]
            h ^= PIECE_SQUARE_TABLE[_piece_index(rook)][rook_to]
        elif move.from_sq - move.to_sq == 2:  # Queenside
            rook_from = move.from_sq - 4
            rook_to   = move.from_sq - 1
            rook      = colour * ROOK
            h ^= PIECE_SQUARE_TABLE[_piece_index(rook)][rook_from]
            h ^= PIECE_SQUARE_TABLE[_piece_index(rook)][rook_to]

    # ── Update turn ──
    h ^= BLACK_TO_MOVE  # Flip turn (XOR toggles it)

    # ── Update castling rights ──
    # XOR out old rights, XOR in new rights
    for key in ['K', 'Q', 'k', 'q']:
        if board.castling_rights[key]:
            h ^= CASTLING_RANDOM[key]
        if new_board.castling_rights[key]:
            h ^= CASTLING_RANDOM[key]

    # ── Update en passant ──
    # XOR out old ep square, XOR in new one
    if board.en_passant_sq != -1:
        h ^= EN_PASSANT_RANDOM[file_of(board.en_passant_sq)]
    if new_board.en_passant_sq != -1:
        h ^= EN_PASSANT_RANDOM[file_of(new_board.en_passant_sq)]

    return h


# ─────────────────────────────────────────
# QUICK TEST
# ─────────────────────────────────────────

if __name__ == "__main__":
    from board import Board

    board = Board()
    h = compute_hash(board)
    print(f"Starting position hash: {h}")
    print(f"(Should be a large number, consistent across runs)\n")

    # Test: hash should be the same if computed twice
    h2 = compute_hash(board)
    assert h == h2, "Hash should be deterministic!"
    print("✓ Hash is deterministic")

    # Test: different positions should have different hashes
    from movegen import MoveGenerator
    gen   = MoveGenerator()
    moves = gen.generate_legal_moves(board)
    new_board = gen._apply_move(board, moves[0])
    h3 = compute_hash(new_board)
    assert h != h3, "Different positions should have different hashes!"
    print("✓ Different positions produce different hashes")

    # Test: incremental update should match full recompute
    h_incremental = update_hash(h, board, moves[0], new_board)
    h_full        = compute_hash(new_board)
    assert h_incremental == h_full, \
        f"Incremental hash {h_incremental} != full hash {h_full}"
    print("✓ Incremental update matches full recompute")

    print("\nAll Zobrist tests passed!")
