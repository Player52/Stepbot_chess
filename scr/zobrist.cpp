// zobrist.cpp
// Zobrist hashing implementation.
// C++ equivalent of zobrist.py.

#include "zobrist.h"
#include <cstdlib>   // For rand()
#include <cstdint>

// ─────────────────────────────────────────
// TABLE DEFINITIONS
// These are the actual storage for the tables declared
// as 'extern' in zobrist.h.
// ─────────────────────────────────────────

Hash PIECE_SQUARE_TABLE[12][64];
Hash BLACK_TO_MOVE;
Hash CASTLING_RANDOM[4];
Hash EN_PASSANT_RANDOM[8];

// ─────────────────────────────────────────
// RANDOM NUMBER GENERATION
// We use a simple Linear Congruential Generator (LCG) with a fixed seed
// so we get the same numbers every run — just like Python's random.Random(seed).
//
// C++'s rand() is not great for this, so we roll our own.
// ─────────────────────────────────────────

// State for our LCG — static means it persists between calls
// (like a global variable but scoped to this file)
static uint64_t lcg_state = 20250307ULL;   // Same seed as zobrist.py

static Hash rand64() {
    // LCG formula: state = state * multiplier + increment
    // These constants are from Knuth's MMIX — widely used LCG values
    lcg_state = lcg_state * 6364136223846793005ULL + 1442695040888963407ULL;

    // Mix the bits further for better randomness
    // '^' is XOR, '>>' is right-shift (like Python's >> but on fixed-width ints)
    Hash x = lcg_state;
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

// ─────────────────────────────────────────
// INIT ZOBRIST
// Fill all tables with random numbers.
// Call this once at engine startup.
// ─────────────────────────────────────────

void init_zobrist() {
    // Fill piece-square table
    // Nested for loop — like Python's nested for i in range(12): for j in range(64)
    for (int piece = 0; piece < 12; piece++) {
        for (int sq = 0; sq < 64; sq++) {
            PIECE_SQUARE_TABLE[piece][sq] = rand64();
        }
    }

    BLACK_TO_MOVE = rand64();

    for (int i = 0; i < 4; i++) {
        CASTLING_RANDOM[i] = rand64();
    }

    for (int i = 0; i < 8; i++) {
        EN_PASSANT_RANDOM[i] = rand64();
    }
}

// ─────────────────────────────────────────
// PIECE INDEX
// Maps a piece value to a 0-11 index for the table.
// White: PAWN=0, KNIGHT=1, BISHOP=2, ROOK=3, QUEEN=4, KING=5
// Black: PAWN=6, KNIGHT=7, BISHOP=8, ROOK=9, QUEEN=10, KING=11
// ─────────────────────────────────────────

int piece_index(int piece) {
    int piece_type    = std::abs(piece) - 1;   // PAWN(1)->0 ... KING(6)->5
    int colour_offset = (piece > 0) ? 0 : 6;  // White=0, Black=6
    return piece_type + colour_offset;
}

// ─────────────────────────────────────────
// COMPUTE HASH
// Build hash from scratch — used when setting up a position.
// ─────────────────────────────────────────

Hash compute_hash(const Board& board) {
    Hash h = 0;

    // XOR in all pieces
    for (int sq = 0; sq < 64; sq++) {
        int piece = board.get_piece(sq);
        if (piece != EMPTY) {
            h ^= PIECE_SQUARE_TABLE[piece_index(piece)][sq];
        }
    }

    // XOR in turn
    if (board.turn == BLACK) {
        h ^= BLACK_TO_MOVE;
    }

    // XOR in castling rights
    // We use a fixed order: K=0, Q=1, k=2, q=3
    if (board.castling_rights.K) h ^= CASTLING_RANDOM[0];
    if (board.castling_rights.Q) h ^= CASTLING_RANDOM[1];
    if (board.castling_rights.k) h ^= CASTLING_RANDOM[2];
    if (board.castling_rights.q) h ^= CASTLING_RANDOM[3];

    // XOR in en passant file
    if (board.en_passant_sq != -1) {
        h ^= EN_PASSANT_RANDOM[file_of(board.en_passant_sq)];
    }

    return h;
}

// ─────────────────────────────────────────
// UPDATE HASH (make/unmake overload)
// Used after make_move when the pre-move board state is gone.
// prev_ep_sq and prev_castling come from UndoInfo.
// ─────────────────────────────────────────

Hash update_hash(Hash h, const Board& after, const Move& move,
                 int prev_ep_sq, const CastlingRights& prev_castling,
                 int captured_piece) {
    int colour = -after.turn;  // after.turn is the opponent; colour is who moved

    // Reconstruct the piece that was on from_sq before the move
    int moved_piece = move.promotion ? (colour * PAWN)
                                     : after.get_piece(move.to_sq);
    int piece_type = std::abs(moved_piece);

    // Remove piece from its origin square
    h ^= PIECE_SQUARE_TABLE[piece_index(moved_piece)][move.from_sq];

    // Remove captured piece from destination (if any)
    if (captured_piece != EMPTY)
        h ^= PIECE_SQUARE_TABLE[piece_index(captured_piece)][move.to_sq];

    // En passant: remove the captured pawn from its actual square
    if (piece_type == PAWN && move.to_sq == prev_ep_sq) {
        int ep_pawn_sq = move.to_sq - (colour == WHITE ? 8 : -8);
        int ep_pawn    = -colour * PAWN;
        h ^= PIECE_SQUARE_TABLE[piece_index(ep_pawn)][ep_pawn_sq];
    }

    // Add the landed piece to destination (promotion or normal)
    int landed = after.get_piece(move.to_sq);
    h ^= PIECE_SQUARE_TABLE[piece_index(landed)][move.to_sq];

    // Castling — update rook
    if (piece_type == KING) {
        int diff = move.to_sq - move.from_sq;
        if (diff == 2) {
            int rook = colour * ROOK;
            h ^= PIECE_SQUARE_TABLE[piece_index(rook)][move.from_sq + 3];
            h ^= PIECE_SQUARE_TABLE[piece_index(rook)][move.from_sq + 1];
        } else if (diff == -2) {
            int rook = colour * ROOK;
            h ^= PIECE_SQUARE_TABLE[piece_index(rook)][move.from_sq - 4];
            h ^= PIECE_SQUARE_TABLE[piece_index(rook)][move.from_sq - 1];
        }
    }

    // Flip turn
    h ^= BLACK_TO_MOVE;

    // XOR out old castling rights, XOR in new
    if (prev_castling.K) h ^= CASTLING_RANDOM[0];
    if (prev_castling.Q) h ^= CASTLING_RANDOM[1];
    if (prev_castling.k) h ^= CASTLING_RANDOM[2];
    if (prev_castling.q) h ^= CASTLING_RANDOM[3];
    if (after.castling_rights.K) h ^= CASTLING_RANDOM[0];
    if (after.castling_rights.Q) h ^= CASTLING_RANDOM[1];
    if (after.castling_rights.k) h ^= CASTLING_RANDOM[2];
    if (after.castling_rights.q) h ^= CASTLING_RANDOM[3];

    // XOR out old en passant file, XOR in new
    if (prev_ep_sq != -1)
        h ^= EN_PASSANT_RANDOM[file_of(prev_ep_sq)];
    if (after.en_passant_sq != -1)
        h ^= EN_PASSANT_RANDOM[file_of(after.en_passant_sq)];

    return h;
}

// ─────────────────────────────────────────
// UPDATE HASH
// Incrementally update after a move — much faster than recomputing.
// Mirrors the logic in zobrist.py's update_hash().
// ─────────────────────────────────────────

Hash update_hash(Hash h, const Board& before, const Move& move,
                 const Board& after) {
    int piece      = before.get_piece(move.from_sq);
    int piece_type = std::abs(piece);
    int colour     = (piece > 0) ? WHITE : BLACK;

    // Remove piece from its original square
    h ^= PIECE_SQUARE_TABLE[piece_index(piece)][move.from_sq];

    // Remove any captured piece
    int captured = before.get_piece(move.to_sq);
    if (captured != EMPTY) {
        h ^= PIECE_SQUARE_TABLE[piece_index(captured)][move.to_sq];
    }

    // En passant capture — remove the captured pawn
    if (piece_type == PAWN && move.to_sq == before.en_passant_sq) {
        int ep_pawn_sq = move.to_sq - (colour == WHITE ? 8 : -8);
        int ep_pawn    = before.get_piece(ep_pawn_sq);
        h ^= PIECE_SQUARE_TABLE[piece_index(ep_pawn)][ep_pawn_sq];
    }

    // Add piece to destination (account for promotion)
    int landed = move.promotion ? (colour * move.promotion) : piece;
    h ^= PIECE_SQUARE_TABLE[piece_index(landed)][move.to_sq];

    // Castling — update rook
    if (piece_type == KING) {
        int diff = move.to_sq - move.from_sq;
        if (diff == 2) {   // Kingside
            int rook = colour * ROOK;
            h ^= PIECE_SQUARE_TABLE[piece_index(rook)][move.from_sq + 3];
            h ^= PIECE_SQUARE_TABLE[piece_index(rook)][move.from_sq + 1];
        } else if (diff == -2) {   // Queenside
            int rook = colour * ROOK;
            h ^= PIECE_SQUARE_TABLE[piece_index(rook)][move.from_sq - 4];
            h ^= PIECE_SQUARE_TABLE[piece_index(rook)][move.from_sq - 1];
        }
    }

    // Flip turn
    h ^= BLACK_TO_MOVE;

    // Update castling rights — XOR out old, XOR in new
    if (before.castling_rights.K) h ^= CASTLING_RANDOM[0];
    if (before.castling_rights.Q) h ^= CASTLING_RANDOM[1];
    if (before.castling_rights.k) h ^= CASTLING_RANDOM[2];
    if (before.castling_rights.q) h ^= CASTLING_RANDOM[3];

    if (after.castling_rights.K) h ^= CASTLING_RANDOM[0];
    if (after.castling_rights.Q) h ^= CASTLING_RANDOM[1];
    if (after.castling_rights.k) h ^= CASTLING_RANDOM[2];
    if (after.castling_rights.q) h ^= CASTLING_RANDOM[3];

    // Update en passant
    if (before.en_passant_sq != -1)
        h ^= EN_PASSANT_RANDOM[file_of(before.en_passant_sq)];
    if (after.en_passant_sq != -1)
        h ^= EN_PASSANT_RANDOM[file_of(after.en_passant_sq)];

    return h;
}
