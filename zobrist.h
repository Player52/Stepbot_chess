// zobrist.h
// Zobrist hashing for the transposition table.
// C++ equivalent of zobrist.py.

#pragma once

#include "board.h"
#include "movegen.h"
#include <cstdint>   // For uint64_t — a guaranteed 64-bit unsigned integer

// ─────────────────────────────────────────
// TYPE ALIAS
// 'using Hash = uint64_t' means we can write Hash instead of uint64_t.
// Makes the code more readable — whenever you see Hash, you know
// it's a Zobrist hash value.
// ─────────────────────────────────────────

using Hash = uint64_t;

// ─────────────────────────────────────────
// ZOBRIST TABLES
// Declared as 'extern' — defined once in zobrist.cpp,
// but accessible from any file that includes this header.
// 'extern' is like saying "this exists, but it's defined elsewhere".
// ─────────────────────────────────────────

// Random number for each piece (0-11) on each square (0-63)
// piece index: white PAWN=0..KING=5, black PAWN=6..KING=11
extern Hash PIECE_SQUARE_TABLE[12][64];

// Random number XORed in when it's Black's turn
extern Hash BLACK_TO_MOVE;

// Random numbers for each castling right
extern Hash CASTLING_RANDOM[4];   // K, Q, k, q in that order

// Random numbers for en passant file (0-7)
extern Hash EN_PASSANT_RANDOM[8];

// ─────────────────────────────────────────
// FUNCTIONS
// ─────────────────────────────────────────

// Must be called once at startup to fill the tables with random numbers
void init_zobrist();

// Returns the piece index (0-11) for a given piece value
int piece_index(int piece);

// Compute hash from scratch for a given board position
Hash compute_hash(const Board& board);

// Incrementally update hash after a move (much faster than recomputing)
Hash update_hash(Hash h, const Board& before, const Move& move,
                 const Board& after);
