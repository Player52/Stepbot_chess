// movegen.h
// Move generation for Stepbot.
// C++ equivalent of movegen.py.

#pragma once

#include "board.h"
#include <vector>    // std::vector — like a Python list (resizable array)
#include <string>

// ─────────────────────────────────────────
// MOVE STRUCT
// Equivalent of Python's Move class.
// Holds from_sq, to_sq, and optional promotion piece.
// ─────────────────────────────────────────

struct Move {
    int from_sq;
    int to_sq;
    int promotion;   // 0 = no promotion, otherwise KNIGHT/BISHOP/ROOK/QUEEN

    // Default constructor — creates a null move (a1a1)
    // Needed so we can declare arrays of Move objects e.g. Move killers[64][2]
    Move() : from_sq(0), to_sq(0), promotion(0) {}

    // Constructor with default promotion of 0
    // In C++ you can give parameters default values like Python's def f(x=0)
    Move(int from, int to, int promo = 0)
        : from_sq(from), to_sq(to), promotion(promo) {}

    // Equality operator — lets us write (move1 == move2)
    // Like Python's __eq__
    bool operator==(const Move& other) const {
        return from_sq   == other.from_sq &&
               to_sq     == other.to_sq   &&
               promotion == other.promotion;
    }

    // Convert to UCI string e.g. "e2e4", "e7e8q"
    std::string to_uci() const;
};

// ─────────────────────────────────────────
// MOVE GENERATOR
// All functions take a Board by const reference —
// they read the board but never modify it.
// Returns moves as std::vector<Move> — like Python's list of Move objects.
// ─────────────────────────────────────────

// Generate all legal moves for the side to move
std::vector<Move> generate_legal_moves(const Board& board);

// Generate pseudo-legal moves (correct piece movement, ignoring check)
std::vector<Move> generate_pseudo_legal_moves(const Board& board);

// Apply a move and return the resulting board (does not modify original)
Board apply_move(const Board& board, const Move& move);

// Make/unmake: mutate the board in-place for search performance.
// make_move returns an UndoInfo; pass it to unmake_move to restore.
UndoInfo make_move(Board& board, const Move& move);
void     unmake_move(Board& board, const Move& move, const UndoInfo& undo);

// Check detection
bool king_in_check(const Board& board, int colour);
bool square_attacked_by(const Board& board, int sq, int attacker_colour);

// Static exchange evaluation: material balance of captures on move.to_sq
// starting with this move (positive = good for the side to move before the move).
// Returns 0 for non-captures that are not quiet queen promotions.
int static_exchange_eval(const Board& board, const Move& move);

// Individual piece move generators (used internally)
void pawn_moves  (const Board& board, int sq, std::vector<Move>& moves);
void knight_moves(const Board& board, int sq, std::vector<Move>& moves);
void sliding_moves(const Board& board, int sq,
                   const int* dirs, int num_dirs,
                   std::vector<Move>& moves);
void king_moves  (const Board& board, int sq, std::vector<Move>& moves);
