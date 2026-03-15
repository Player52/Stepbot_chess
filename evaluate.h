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

// Phase 8 additions
extern int ROOK_OPEN_FILE_BONUS;      // Rook on fully open file
extern int ROOK_SEMI_OPEN_FILE_BONUS; // Rook on file with only enemy pawns
extern int ROOK_SEVENTH_RANK_BONUS;   // Rook on 7th rank (2nd for Black)
extern int KNIGHT_OUTPOST_BONUS;      // Knight on advanced outpost square

// ─────────────────────────────────────────
// PIECE-SQUARE TABLES — MIDDLEGAME & ENDGAME
// ─────────────────────────────────────────

extern const int PAWN_MG[64];
extern const int PAWN_EG[64];
extern const int KNIGHT_MG[64];
extern const int KNIGHT_EG[64];
extern const int BISHOP_MG[64];
extern const int BISHOP_EG[64];
extern const int ROOK_MG[64];
extern const int ROOK_EG[64];
extern const int QUEEN_MG[64];
extern const int QUEEN_EG[64];
extern const int KING_MG[64];
extern const int KING_EG[64];

// Game phase constants
extern const int PHASE_WEIGHT[7];
extern const int MAX_PHASE;

// ─────────────────────────────────────────
// EVALUATOR FUNCTIONS
// ─────────────────────────────────────────

// Main evaluation — returns centipawns, positive = good for White
int evaluate(const Board& board);

// Game phase (0 = full endgame, MAX_PHASE = full middlegame)
int game_phase(const Board& board);

// Sub-components
bool is_endgame(const Board& board);
int  eval_material_and_placement(const Board& board, bool endgame);
int  eval_pawn_structure(const Board& board, bool endgame);
int  eval_king_safety(const Board& board, bool endgame);
int  eval_mobility(const Board& board);
int  eval_bishop_pair(const Board& board);
int  eval_rooks(const Board& board, bool endgame);
int  eval_knight_outposts(const Board& board);

// Piece-square lookups
void piece_square_scores(int piece_type, int sq_idx, int colour,
                         int& mg_score, int& eg_score);
int  piece_square_bonus(int piece_type, int sq_idx, int colour, bool endgame);
