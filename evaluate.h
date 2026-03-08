// evaluate.h
// Position evaluation for Stepbot.
// C++ equivalent of evaluate.py.

#pragma once

#include "board.h"

// ─────────────────────────────────────────
// PIECE VALUES
// Indexed by piece type (1-6 = PAWN..KING)
// We use a plain array indexed by piece type.
// Index 0 is unused (EMPTY=0).
// ─────────────────────────────────────────

extern const int PIECE_VALUES[7];   // [0]=unused, [1]=PAWN ... [6]=KING

// ─────────────────────────────────────────
// EVALUATION WEIGHTS
// Declared extern so tune.cpp can modify them
// ─────────────────────────────────────────

extern int DOUBLED_PAWN_PENALTY;
extern int ISOLATED_PAWN_PENALTY;
extern int PASSED_PAWN_BONUS[8];
extern int PASSED_PAWN_BONUS_EG[8];
extern int PAWN_SHIELD_BONUS;
extern int OPEN_FILE_NEAR_KING;
extern int SEMI_OPEN_FILE_KING;
extern int KING_ATTACKER_WEIGHT[4];
extern int MOBILITY_BONUS[7];       // Indexed by piece type
extern int BISHOP_PAIR_BONUS;
extern int ENDGAME_THRESHOLD;

// ─────────────────────────────────────────
// PIECE-SQUARE TABLES
// ─────────────────────────────────────────

extern const int PAWN_TABLE[64];
extern const int KNIGHT_TABLE[64];
extern const int BISHOP_TABLE[64];
extern const int ROOK_TABLE[64];
extern const int QUEEN_TABLE[64];
extern const int KING_TABLE_MG[64];
extern const int KING_TABLE_EG[64];

// ─────────────────────────────────────────
// EVALUATOR FUNCTIONS
// ─────────────────────────────────────────

// Main evaluation — returns centipawns, positive = good for White
int evaluate(const Board& board);

// Sub-components (used by tune.cpp and tests)
bool is_endgame(const Board& board);
int  eval_material_and_placement(const Board& board, bool endgame);
int  eval_pawn_structure(const Board& board, bool endgame);
int  eval_king_safety(const Board& board, bool endgame);
int  eval_mobility(const Board& board);
int  eval_bishop_pair(const Board& board);

// Helper: piece-square table lookup for a given piece and square
int piece_square_bonus(int piece_type, int sq, int colour, bool endgame);
