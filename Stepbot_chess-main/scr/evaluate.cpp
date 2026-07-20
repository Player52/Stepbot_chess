// evaluate.cpp
// Position evaluation implementation.
// C++ equivalent of evaluate.py.

#include "evaluate.h"
#include "movegen.h"
#include <cstdlib>
#include <algorithm>

// ─────────────────────────────────────────
// PIECE VALUES
// ─────────────────────────────────────────

const int PIECE_VALUES[7] = {
    0,      // EMPTY (unused)
    100,    // PAWN
    320,    // KNIGHT
    330,    // BISHOP
    500,    // ROOK
    900,    // QUEEN
    20000,  // KING
};

// ─────────────────────────────────────────
// EVALUATION WEIGHTS
// Defined here — declared extern in evaluate.h
// so other files (tune.cpp) can modify them
// ─────────────────────────────────────────

int DOUBLED_PAWN_PENALTY  = -20;
int ISOLATED_PAWN_PENALTY = -20;
int PASSED_PAWN_BONUS[8]    = {0, 10, 20,  40,  60,  80, 120,   0};
int PASSED_PAWN_BONUS_EG[8] = {0, 20, 40,  70, 100, 150, 200,   0};
int PAWN_SHIELD_BONUS       =  10;
int OPEN_FILE_NEAR_KING     = -20;
int SEMI_OPEN_FILE_KING     = -10;
int KING_ATTACKER_WEIGHT[4] = {-50, -30, -20, -10};
int MOBILITY_BONUS[7]       = {0, 0, 4, 3, 2, 1, 0};  // indexed by piece type
int BISHOP_PAIR_BONUS       =  30;
int ENDGAME_THRESHOLD       = 1300;

// Phase 8 weights
int ROOK_OPEN_FILE_BONUS      =  25;   // Rook on fully open file
int ROOK_SEMI_OPEN_FILE_BONUS =  15;   // Rook on semi-open file
int ROOK_SEVENTH_RANK_BONUS   =  30;   // Rook on 7th rank
int KNIGHT_OUTPOST_BONUS      =  20;   // Knight on protected outpost

int TEMPO_MG                  =  18;   // Side to move (scaled by game phase)
int CONNECTED_PASSED_BONUS    =  22;   // Adjacent-file passed pawn pairs
int BACKWARD_PAWN_PENALTY     = -15;   // Pawn that can't advance safely
int CONNECTED_ROOKS_BONUS     =  20;   // Two rooks with clear line between them

// ─────────────────────────────────────────
// PIECE-SQUARE TABLES — MIDDLEGAME & ENDGAME
// Each piece now has two tables. The score is blended
// between them based on the current game phase.
// ─────────────────────────────────────────

// ── Pawns ──
const int PAWN_MG[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0,
};
const int PAWN_EG[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    -5, -5, -5, -5, -5, -5, -5, -5,
     0,  0,  0,  0,  0,  0,  0,  0,
     5,  5,  5,  5,  5,  5,  5,  5,
    15, 15, 15, 15, 15, 15, 15, 15,
    25, 25, 25, 25, 25, 25, 25, 25,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0,
};

// ── Knights ──
const int KNIGHT_MG[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,  0, 15, 20, 20, 15,  0,-40,
    -40,  5, 15, 20, 20, 15,  5,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50,
};
const int KNIGHT_EG[64] = {
    -60,-45,-35,-35,-35,-35,-45,-60,
    -45,-25, -5,  0,  0, -5,-25,-45,
    -35,  0, 10, 15, 15, 10,  0,-35,
    -35,  0, 15, 20, 20, 15,  0,-35,
    -35,  0, 15, 20, 20, 15,  0,-35,
    -35,  0, 10, 15, 15, 10,  0,-35,
    -45,-25, -5,  0,  0, -5,-25,-45,
    -60,-45,-35,-35,-35,-35,-45,-60,
};

// ── Bishops ──
const int BISHOP_MG[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -20,  0,  5, 10, 10,  5,  0,-20,
    -20,  0,  5, 10, 10,  5,  0,-20,
    -20,  0,  0,  0,  0,  0,  0,-20,
    -30,-20,-20,-20,-20,-20,-20,-30,
};
const int BISHOP_EG[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0, 10, 15, 15, 10,  0,-10,
    -10,  0, 10, 15, 15, 10,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20,
};

// ── Rooks ──
const int ROOK_MG[64] = {
     0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0,
};
const int ROOK_EG[64] = {
     5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,
     0,  0,  5,  5,  5,  5,  0,  0,
     0,  0,  5,  5,  5,  5,  0,  0,
     0,  0,  5,  5,  5,  5,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5, -5,  0,  0,  0,  0, -5, -5,
};

// ── Queens ──
const int QUEEN_MG[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -10,  5,  5,  5,  5,  5,  0,-10,
      0,  0,  5,  5,  5,  5,  0, -5,
     -5,  0,  5,  5,  5,  5,  0, -5,
    -10,  0,  5,  5,  5,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20,
};
const int QUEEN_EG[64] = {
    -30,-20,-15,-10,-10,-15,-20,-30,
    -20,-10, -5,  0,  0, -5,-10,-20,
    -15, -5,  5, 10, 10,  5, -5,-15,
    -10,  0, 10, 15, 15, 10,  0,-10,
    -10,  0, 10, 15, 15, 10,  0,-10,
    -15, -5,  5, 10, 10,  5, -5,-15,
    -20,-10, -5,  0,  0, -5,-10,-20,
    -30,-20,-15,-10,-10,-15,-20,-30,
};

// ── King ──
const int KING_MG[64] = {
     20, 30, 10,  0,  0, 10, 30, 20,
     20, 20,  0,  0,  0,  0, 20, 20,
    -10,-20,-20,-20,-20,-20,-20,-10,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
};
const int KING_EG[64] = {
    -50,-30,-30,-30,-30,-30,-30,-50,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -50,-40,-30,-20,-20,-30,-40,-50,
};

// ─────────────────────────────────────────
// GAME PHASE
// Phase weights per piece type — how much each piece
// contributes to the middlegame phase value.
// Knights/bishops = 1, rooks = 2, queens = 4.
// Maximum phase (all pieces) = 4*1 + 4*1 + 4*2 + 2*4 = 24
// ─────────────────────────────────────────

const int PHASE_WEIGHT[7] = {0, 0, 1, 1, 2, 4, 0};
const int MAX_PHASE        = 24;

// Returns a phase value 0 (endgame) to MAX_PHASE (full middlegame)
int game_phase(const Board& board) {
    return board.incremental_eval.phase;
}

// ─────────────────────────────────────────
// PIECE-SQUARE BONUS LOOKUP
// Returns the tapered (blended) bonus for a piece.
// mg_score and eg_score are set by reference.
// ─────────────────────────────────────────

void piece_square_scores(int piece_type, int sq_idx, int colour,
                         int& mg_score, int& eg_score) {
    // Mirror square for Black
    int table_sq = (colour == WHITE) ? sq_idx
                                     : (7 - rank_of(sq_idx)) * 8 + file_of(sq_idx);
    switch (piece_type) {
        case PAWN:
            mg_score = PAWN_MG[table_sq];
            eg_score = PAWN_EG[table_sq];
            break;
        case KNIGHT:
            mg_score = KNIGHT_MG[table_sq];
            eg_score = KNIGHT_EG[table_sq];
            break;
        case BISHOP:
            mg_score = BISHOP_MG[table_sq];
            eg_score = BISHOP_EG[table_sq];
            break;
        case ROOK:
            mg_score = ROOK_MG[table_sq];
            eg_score = ROOK_EG[table_sq];
            break;
        case QUEEN:
            mg_score = QUEEN_MG[table_sq];
            eg_score = QUEEN_EG[table_sq];
            break;
        case KING:
            mg_score = KING_MG[table_sq];
            eg_score = KING_EG[table_sq];
            break;
        default:
            mg_score = eg_score = 0;
    }
}

static void apply_incremental_eval_piece(IncrementalEvalState& state,
                                         int sq_idx, int piece, int delta) {
    if (piece == EMPTY)
        return;

    int colour = (piece > 0) ? WHITE : BLACK;
    int piece_type = std::abs(piece);
    int pst_mg = 0;
    int pst_eg = 0;
    piece_square_scores(piece_type, sq_idx, colour, pst_mg, pst_eg);

    state.mg_score += delta * colour * (PIECE_VALUES[piece_type] + pst_mg);
    state.eg_score += delta * colour * (PIECE_VALUES[piece_type] + pst_eg);
    state.phase = std::clamp(state.phase + delta * PHASE_WEIGHT[piece_type],
                             0, MAX_PHASE);
}

void incremental_eval_add_piece(IncrementalEvalState& state,
                                int sq_idx, int piece) {
    apply_incremental_eval_piece(state, sq_idx, piece, 1);
}

void incremental_eval_remove_piece(IncrementalEvalState& state,
                                   int sq_idx, int piece) {
    apply_incremental_eval_piece(state, sq_idx, piece, -1);
}

void refresh_incremental_eval_state(Board& board) {
    board.incremental_eval = IncrementalEvalState{};
    for (int sq_idx = 0; sq_idx < 64; sq_idx++)
        incremental_eval_add_piece(board.incremental_eval,
                                   sq_idx, board.squares[sq_idx]);
}

// Legacy single-value lookup (used by king safety and other callers)
int piece_square_bonus(int piece_type, int sq_idx, int colour, bool endgame) {
    int mg, eg;
    piece_square_scores(piece_type, sq_idx, colour, mg, eg);
    return endgame ? eg : mg;
}

// ─────────────────────────────────────────
// ENDGAME DETECTION (kept for king safety)
// ─────────────────────────────────────────

bool is_endgame(const Board& board) {
    return game_phase(board) <= MAX_PHASE / 2;
}

// ─────────────────────────────────────────
// MATERIAL AND PLACEMENT — TAPERED
// Accumulates separate MG and EG scores, then blends them
// ─────────────────────────────────────────

static int eval_material_and_placement_phase(const Board& board, int phase) {
    const IncrementalEvalState& state = board.incremental_eval;
    return (state.mg_score * phase
            + state.eg_score * (MAX_PHASE - phase)) / MAX_PHASE;
}

static int eval_material_phase_and_placement(const Board& board, int& phase) {
    phase = board.incremental_eval.phase;
    return eval_material_and_placement_phase(board, phase);
}

int eval_material_and_placement(const Board& board, bool /*endgame*/) {
    return eval_material_and_placement_phase(board, game_phase(board));
}

// ─────────────────────────────────────────
// PAWN STRUCTURE
// ─────────────────────────────────────────

int eval_pawn_structure(const Board& board, bool endgame) {
    int score = 0;

    for (int colour : {WHITE, BLACK}) {
        int sign  = colour;
        int enemy = -colour;

        // Build friendly pawn file set and enemy pawn file->ranks map
        // We use plain arrays for speed instead of std::set/std::map
        bool friendly_pawn_files[8] = {};
        int  enemy_pawn_ranks[8][8] = {};
        int  enemy_pawn_count[8]    = {};

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            int piece = board.squares[sq_idx];
            if (piece == colour * PAWN)
                friendly_pawn_files[file_of(sq_idx)] = true;
            if (piece == enemy * PAWN) {
                int f = file_of(sq_idx);
                if (enemy_pawn_count[f] < 8)
                    enemy_pawn_ranks[f][enemy_pawn_count[f]++] = rank_of(sq_idx);
            }
        }

        int pawns_per_file[8] = {};
        bool passed_sq[64]    = {};

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            int piece = board.squares[sq_idx];
            if (piece != colour * PAWN) continue;

            int f = file_of(sq_idx);
            int r = rank_of(sq_idx);

            pawns_per_file[f]++;

            // Isolated pawn check
            bool has_adjacent = false;
            if (f > 0 && friendly_pawn_files[f - 1]) has_adjacent = true;
            if (f < 7 && friendly_pawn_files[f + 1]) has_adjacent = true;
            if (!has_adjacent)
                score += sign * ISOLATED_PAWN_PENALTY;

            // Passed pawn check
            bool is_passed = true;
            for (int cf = std::max(0, f - 1); cf <= std::min(7, f + 1); cf++) {
                for (int ei = 0; ei < enemy_pawn_count[cf]; ei++) {
                    int er = enemy_pawn_ranks[cf][ei];
                    if (colour == WHITE && er >= r) { is_passed = false; break; }
                    if (colour == BLACK && er <= r) { is_passed = false; break; }
                }
                if (!is_passed) break;
            }

            if (is_passed) {
                passed_sq[sq_idx] = true;
                int bonus_rank = (colour == WHITE) ? r : (7 - r);
                score += sign * (endgame ? PASSED_PAWN_BONUS_EG[bonus_rank]
                                         : PASSED_PAWN_BONUS[bonus_rank]);
            }
        }

        // Connected passed pawns (adjacent files)
        int conn = endgame ? (CONNECTED_PASSED_BONUS * 3 + 1) / 2
                           : CONNECTED_PASSED_BONUS;
        auto any_passed_on_file = [&](int file) -> bool {
            if (file < 0 || file > 7) return false;
            for (int r = 0; r < 8; r++)
                if (passed_sq[sq(file, r)]) return true;
            return false;
        };
        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            if (!passed_sq[sq_idx]) continue;
            int f = file_of(sq_idx);
            if (any_passed_on_file(f - 1) || any_passed_on_file(f + 1))
                score += sign * conn;
        }

        // Doubled pawns
        for (int f = 0; f < 8; f++) {
            if (pawns_per_file[f] > 1)
                score += sign * DOUBLED_PAWN_PENALTY * (pawns_per_file[f] - 1);
        }
    }

    return score;
}

// ─────────────────────────────────────────
// KING SAFETY
// ─────────────────────────────────────────

// Attack weight per piece type — how dangerous each attacker is
static const int ATTACK_WEIGHT[7] = {0, 0, 20, 20, 40, 80, 0};

// Danger score -> penalty table (like Stockfish's king safety table)
// Index is the raw danger score, value is the centipawn penalty
static int danger_to_penalty(int danger) {
    if (danger <= 0)   return 0;
    if (danger >= 400) return 600;   // Maximum penalty
    // Quadratic scaling — danger compounds quickly
    return (danger * danger) / 256;
}

static int direct_slider_danger(const Board& board, int king_sq, int enemy) {
    int danger = 0;
    static const int dirs[] = {8, -8, 1, -1, 9, 7, -7, -9};
    for (int direction : dirs) {
        int cur = king_sq;
        int blockers = 0;
        while (true) {
            int prev_file = file_of(cur);
            cur += direction;
            if (cur < 0 || cur >= 64) break;
            int new_file = file_of(cur);
            if ((direction == 1 || direction == -1) &&
                std::abs(new_file - prev_file) != 1) break;
            if (direction ==  9 && new_file <= prev_file) break;
            if (direction == -7 && new_file <= prev_file) break;
            if (direction ==  7 && new_file >= prev_file) break;
            if (direction == -9 && new_file >= prev_file) break;

            int piece = board.get_piece(cur);
            if (piece == EMPTY) continue;

            int pt = std::abs(piece);
            bool enemy_piece = piece == enemy * pt;
            bool straight = direction == 8 || direction == -8 ||
                            direction == 1 || direction == -1;
            bool diagonal = !straight;
            bool slider = enemy_piece &&
                          ((straight && (pt == ROOK || pt == QUEEN)) ||
                           (diagonal && (pt == BISHOP || pt == QUEEN)));
            if (slider) {
                int base = (pt == QUEEN) ? 70 : (pt == ROOK ? 55 : 40);
                danger += blockers == 0 ? base : base / 2;
            }

            blockers++;
            if (blockers >= 2) break;
        }
    }
    return danger;
}

int eval_king_safety(const Board& board, bool endgame) {
    if (endgame) return 0;

    int score = 0;

    for (int colour : {WHITE, BLACK}) {
        int sign  = colour;
        int enemy = -colour;

        int king_sq = board.king_square(colour);
        if (king_sq == -1) continue;

        int kf = file_of(king_sq);
        int kr = rank_of(king_sq);

        int danger = 0;   // Accumulates threat level against this king

        // ── Pawn shield ──
        // Friendly pawns directly in front of the king are protective
        for (int shield_rank_offset : {1, 2}) {
            int shield_rank = kr + (colour == WHITE ?  shield_rank_offset
                                                    : -shield_rank_offset);
            if (shield_rank < 0 || shield_rank > 7) continue;
            for (int df = -1; df <= 1; df++) {
                int sf = kf + df;
                if (sf < 0 || sf > 7) continue;
                if (board.get_piece(sq(sf, shield_rank)) == colour * PAWN)
                    score += sign * PAWN_SHIELD_BONUS;
                else if (shield_rank_offset == 1)
                    // Missing pawn on first shield rank is dangerous
                    danger += 15;
            }
        }

        // ── Open files near king ──
        for (int df = -1; df <= 1; df++) {
            int f = kf + df;
            if (f < 0 || f > 7) continue;
            bool has_friendly = false, has_enemy = false;
            for (int r = 0; r < 8; r++) {
                int piece = board.get_piece(sq(f, r));
                if (piece == colour * PAWN) has_friendly = true;
                if (piece == enemy  * PAWN) has_enemy    = true;
            }
            if (!has_friendly && !has_enemy) {
                score += sign * OPEN_FILE_NEAR_KING;
                danger += 25;   // Fully open file = rook/queen highway
            } else if (!has_friendly) {
                score += sign * SEMI_OPEN_FILE_KING;
                danger += 12;
            }
        }

        // ── King attack zone ──
        // Count enemy pieces attacking squares near the king.
        // We look at a 5x5 zone centred on the king for long-range pieces.
        int attacked_zone_squares = 0;
        int front_rank = kr + (colour == WHITE ? 1 : -1);

        for (int zr = std::max(0, kr - 1); zr <= std::min(7, kr + 1); zr++) {
            for (int zf = std::max(0, kf - 1); zf <= std::min(7, kf + 1); zf++) {
                if (square_attacked_by(board, sq(zf, zr), enemy))
                    attacked_zone_squares++;
            }
        }

        if (front_rank >= 0 && front_rank <= 7) {
            for (int zf = std::max(0, kf - 2); zf <= std::min(7, kf + 2); zf++) {
                if (square_attacked_by(board, sq(zf, front_rank), enemy))
                    attacked_zone_squares++;
            }
        }

        if (attacked_zone_squares >= 5)
            danger += attacked_zone_squares * 18;
        else if (attacked_zone_squares >= 2)
            danger += attacked_zone_squares * 10;
        else
            danger += attacked_zone_squares * 4;

        // ── Escape squares ──
        // If the king has very few safe squares to move to, penalise heavily
        int escape_squares = 0;
        for (int direction : ALL_DIRS) {
            int target = king_sq + direction;
            if (target < 0 || target >= 64) continue;
            if (std::abs(file_of(king_sq) - file_of(target)) > 1) continue;
            if (std::abs(rank_of(king_sq) - rank_of(target)) > 1) continue;
            int occupant = board.get_piece(target);
            if ((occupant > 0) == (colour == WHITE) && occupant != EMPTY) continue;
            if (!square_attacked_by(board, target, enemy))
                escape_squares++;
        }
        if (escape_squares == 0) danger += 120;
        else if (escape_squares == 1) danger += 70;
        else if (escape_squares == 2) danger += 25;

        danger += direct_slider_danger(board, king_sq, enemy);

        // ── Queen proximity bonus ──
        // Enemy queen near our king is especially dangerous
        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            if (board.get_piece(sq_idx) != enemy * QUEEN) continue;
            int qf   = file_of(sq_idx);
            int qr   = rank_of(sq_idx);
            int dist = std::max(std::abs(qf - kf), std::abs(qr - kr));
            if (dist <= 2) danger += (3 - dist) * 30;
        }

        // Convert danger to a score penalty
        score -= sign * danger_to_penalty(danger);
    }

    return score;
}

int eval_hanging_pieces(const Board& board) {
    int score = 0;

    for (int colour : {WHITE, BLACK}) {
        int enemy = -colour;
        int penalty = 0;

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            int victim = board.squares[sq_idx];
            if (victim == EMPTY || (victim > 0) != (colour == WHITE)) continue;

            int victim_type = std::abs(victim);
            if (victim_type < KNIGHT || victim_type > QUEEN) continue;
            if (!square_attacked_by(board, sq_idx, enemy)) continue;

            bool defended = square_attacked_by(board, sq_idx, colour);
            int base = PIECE_VALUES[victim_type] / (defended ? 12 : 6);
            if (victim_type == QUEEN) base += 90;
            else if (victim_type == ROOK) base += 55;
            else base += 25;

            if (!defended) base += 45;
            penalty += std::min(base, 260);
        }

        penalty = std::min(penalty, 450);
        score -= colour * penalty;
    }

    return score;
}

// ─────────────────────────────────────────
// MOBILITY
// ─────────────────────────────────────────

int eval_mobility(const Board& board) {
    int score = 0;

    for (int colour : {WHITE, BLACK}) {
        int sign = colour;

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            int piece = board.squares[sq_idx];
            if (piece == EMPTY) continue;
            if ((piece > 0) != (colour == WHITE)) continue;

            int piece_type = std::abs(piece);
            if (MOBILITY_BONUS[piece_type] == 0) continue;

            // Count reachable squares
            int mobility = 0;
            static const int diag[]     = {9, 7, -7, -9};
            static const int straight[] = {8, -8, 1, -1};
            static const int all[]      = {9, 7, -7, -9, 8, -8, 1, -1};

            if (piece_type == KNIGHT) {
                for (int offset : KNIGHT_OFFSETS) {
                    int target = sq_idx + offset;
                    if (target < 0 || target >= 64) continue;
                    if (std::abs(file_of(sq_idx) - file_of(target)) > 2) continue;
                    int target_piece = board.squares[target];
                    if ((target_piece > 0 ? WHITE : (target_piece < 0 ? BLACK : 0)) != colour)
                        mobility++;
                }
            } else {
                const int* dirs;
                int num_dirs;
                if      (piece_type == BISHOP) { dirs = diag;     num_dirs = 4; }
                else if (piece_type == ROOK)   { dirs = straight; num_dirs = 4; }
                else if (piece_type == QUEEN)  { dirs = all;      num_dirs = 8; }
                else continue;

                for (int d = 0; d < num_dirs; d++) {
                    int target = sq_idx;
                    while (true) {
                        int pf = file_of(target);
                        target += dirs[d];
                        if (target < 0 || target >= 64) break;
                        int nf = file_of(target);
                        if (dirs[d] ==  1 && std::abs(pf-nf) != 1) break;
                        if (dirs[d] == -1 && std::abs(pf-nf) != 1) break;
                        if (dirs[d] ==  9 && nf <= pf) break;
                        if (dirs[d] == -7 && nf <= pf) break;
                        if (dirs[d] ==  7 && nf >= pf) break;
                        if (dirs[d] == -9 && nf >= pf) break;
                        int target_piece = board.squares[target];
                        int target_colour = target_piece > 0 ? WHITE :
                                            (target_piece < 0 ? BLACK : 0);
                        if (target_colour == colour) break;
                        mobility++;
                        if (target_colour == -colour) break;
                    }
                }
            }

            score += sign * MOBILITY_BONUS[piece_type] * mobility;
        }
    }

    return score;
}

// ─────────────────────────────────────────
// BISHOP PAIR
// ─────────────────────────────────────────

int eval_bishop_pair(const Board& board) {
    int score = 0;
    for (int colour : {WHITE, BLACK}) {
        int bishop_count = 0;
        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            if (board.squares[sq_idx] == colour * BISHOP)
                bishop_count++;
        }
        if (bishop_count >= 2)
            score += colour * BISHOP_PAIR_BONUS;
    }
    return score;
}

// ─────────────────────────────────────────
// ROOK EVALUATION
// ─────────────────────────────────────────

int eval_rooks(const Board& board, bool /*endgame*/) {
    int score = 0;

    for (int colour : {WHITE, BLACK}) {
        int sign  = colour;
        int enemy = -colour;

        int seventh_rank = (colour == WHITE) ? 6 : 1;

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            if (board.squares[sq_idx] != colour * ROOK) continue;

            int f = file_of(sq_idx);
            int r = rank_of(sq_idx);

            bool has_friendly_pawn = false;
            bool has_enemy_pawn    = false;
            for (int rank = 0; rank < 8; rank++) {
                int piece = board.squares[sq(f, rank)];
                if (piece == colour * PAWN) has_friendly_pawn = true;
                if (piece == enemy  * PAWN) has_enemy_pawn    = true;
            }

            if (!has_friendly_pawn && !has_enemy_pawn)
                score += sign * ROOK_OPEN_FILE_BONUS;
            else if (!has_friendly_pawn)
                score += sign * ROOK_SEMI_OPEN_FILE_BONUS;

            if (r == seventh_rank) {
                int enemy_back_rank = (colour == WHITE) ? 7 : 0;
                int enemy_king_sq = board.king_square(enemy);
                bool king_on_back = enemy_king_sq != -1 &&
                                    rank_of(enemy_king_sq) == enemy_back_rank;
                int bonus = ROOK_SEVENTH_RANK_BONUS;
                if (king_on_back) bonus += ROOK_SEVENTH_RANK_BONUS / 2;
                score += sign * bonus;
            }
        }
    }

    return score;
}

// ─────────────────────────────────────────
// KNIGHT OUTPOSTS
// ─────────────────────────────────────────

int eval_knight_outposts(const Board& board) {
    int score = 0;

    for (int colour : {WHITE, BLACK}) {
        int sign  = colour;
        int enemy = -colour;

        int min_rank = (colour == WHITE) ? 3 : 2;
        int max_rank = (colour == WHITE) ? 5 : 4;

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            if (board.squares[sq_idx] != colour * KNIGHT) continue;

            int f = file_of(sq_idx);
            int r = rank_of(sq_idx);

            if (r < min_rank || r > max_rank) continue;

            int pawn_rank      = r - (colour == WHITE ? 1 : -1);
            bool pawn_protected = false;
            if (pawn_rank >= 0 && pawn_rank <= 7) {
                if (f > 0 && board.squares[sq(f-1, pawn_rank)] == colour * PAWN)
                    pawn_protected = true;
                if (f < 7 && board.squares[sq(f+1, pawn_rank)] == colour * PAWN)
                    pawn_protected = true;
            }
            if (!pawn_protected) continue;

            bool safe = true;
            int  enemy_pawn_direction = (colour == WHITE) ? -1 : 1;
            int  attack_rank          = r + enemy_pawn_direction;
            if (attack_rank >= 0 && attack_rank <= 7) {
                if (f > 0 && board.squares[sq(f-1, attack_rank)] == enemy * PAWN)
                    safe = false;
                if (f < 7 && board.squares[sq(f+1, attack_rank)] == enemy * PAWN)
                    safe = false;
            }
            if (!safe) continue;

            int bonus = KNIGHT_OUTPOST_BONUS;
            if (f >= 2 && f <= 5) bonus += KNIGHT_OUTPOST_BONUS / 2;
            score += sign * bonus;
        }
    }

    return score;
}

// ─────────────────────────────────────────
// BACKWARD PAWNS
// A pawn is backward if:
//   1. It has no friendly pawns behind it on adjacent files to support it
//   2. The square directly in front of it is controlled by an enemy pawn
// These pawns are weak because they can never be supported by other pawns.
// ─────────────────────────────────────────

int eval_backward_pawns(const Board& board) {
    int score = 0;

    for (int colour : {WHITE, BLACK}) {
        int sign    = colour;
        int enemy   = -colour;
        int forward = (colour == WHITE) ? 1 : -1;  // rank direction of advance

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            if (board.squares[sq_idx] != colour * PAWN) continue;

            int f = file_of(sq_idx);
            int r = rank_of(sq_idx);

            // Condition 1: no friendly pawn on adjacent files at or behind this rank
            // (i.e. nothing that could advance to support it)
            bool has_support = false;
            for (int df : {-1, 1}) {
                int af = f + df;
                if (af < 0 || af > 7) continue;
                // Look for a friendly pawn on the adjacent file at or behind this rank
                for (int br = r; br != (colour == WHITE ? 0 : 7); br -= forward) {
                    if (board.squares[sq(af, br)] == colour * PAWN) {
                        has_support = true;
                        break;
                    }
                }
                if (has_support) break;
            }
            if (has_support) continue;

            // Condition 2: the square in front is controlled by an enemy pawn
            int front_rank = r + forward;
            if (front_rank < 0 || front_rank > 7) continue;

            bool front_controlled = false;
            for (int df : {-1, 1}) {
                int ef = f + df;
                if (ef < 0 || ef > 7) continue;
                // Enemy pawn attacks our front square diagonally
                int enemy_pawn_rank = front_rank + forward; // one more step
                if (enemy_pawn_rank < 0 || enemy_pawn_rank > 7) continue;
                if (board.squares[sq(ef, enemy_pawn_rank)] == enemy * PAWN) {
                    front_controlled = true;
                    break;
                }
            }
            if (!front_controlled) continue;

            score += sign * BACKWARD_PAWN_PENALTY;
        }
    }

    return score;
}

// ─────────────────────────────────────────
// CONNECTED ROOKS
// Two rooks of the same colour are connected if they share a rank or file
// with no pieces between them. Connected rooks coordinate far better
// and are much more dangerous as an attacking or defensive unit.
// ─────────────────────────────────────────

static int eval_pawn_features_combined(const Board& board, bool endgame) {
    int score = 0;

    for (int colour : {WHITE, BLACK}) {
        int sign = colour;
        int enemy = -colour;
        int forward = (colour == WHITE) ? 1 : -1;

        bool friendly_pawn_files[8] = {};
        int enemy_pawn_ranks[8][8] = {};
        int enemy_pawn_count[8] = {};

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            int piece = board.squares[sq_idx];
            if (piece == colour * PAWN)
                friendly_pawn_files[file_of(sq_idx)] = true;
            if (piece == enemy * PAWN) {
                int f = file_of(sq_idx);
                if (enemy_pawn_count[f] < 8)
                    enemy_pawn_ranks[f][enemy_pawn_count[f]++] = rank_of(sq_idx);
            }
        }

        int pawns_per_file[8] = {};
        bool passed_sq[64] = {};

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            int piece = board.squares[sq_idx];
            if (piece != colour * PAWN) continue;

            int f = file_of(sq_idx);
            int r = rank_of(sq_idx);
            pawns_per_file[f]++;

            bool has_adjacent = false;
            if (f > 0 && friendly_pawn_files[f - 1]) has_adjacent = true;
            if (f < 7 && friendly_pawn_files[f + 1]) has_adjacent = true;
            if (!has_adjacent)
                score += sign * ISOLATED_PAWN_PENALTY;

            bool is_passed = true;
            for (int cf = std::max(0, f - 1); cf <= std::min(7, f + 1); cf++) {
                for (int ei = 0; ei < enemy_pawn_count[cf]; ei++) {
                    int er = enemy_pawn_ranks[cf][ei];
                    if (colour == WHITE && er >= r) { is_passed = false; break; }
                    if (colour == BLACK && er <= r) { is_passed = false; break; }
                }
                if (!is_passed) break;
            }

            if (is_passed) {
                passed_sq[sq_idx] = true;
                int bonus_rank = (colour == WHITE) ? r : (7 - r);
                score += sign * (endgame ? PASSED_PAWN_BONUS_EG[bonus_rank]
                                         : PASSED_PAWN_BONUS[bonus_rank]);
            }

            bool has_support = false;
            for (int df : {-1, 1}) {
                int af = f + df;
                if (af < 0 || af > 7) continue;
                for (int br = r; br != (colour == WHITE ? 0 : 7); br -= forward) {
                    if (board.squares[sq(af, br)] == colour * PAWN) {
                        has_support = true;
                        break;
                    }
                }
                if (has_support) break;
            }

            if (!has_support) {
                int front_rank = r + forward;
                if (front_rank >= 0 && front_rank <= 7) {
                    bool front_controlled = false;
                    for (int df : {-1, 1}) {
                        int ef = f + df;
                        if (ef < 0 || ef > 7) continue;
                        int enemy_pawn_rank = front_rank + forward;
                        if (enemy_pawn_rank < 0 || enemy_pawn_rank > 7) continue;
                        if (board.squares[sq(ef, enemy_pawn_rank)] == enemy * PAWN) {
                            front_controlled = true;
                            break;
                        }
                    }
                    if (front_controlled)
                        score += sign * BACKWARD_PAWN_PENALTY;
                }
            }
        }

        int conn = endgame ? (CONNECTED_PASSED_BONUS * 3 + 1) / 2
                           : CONNECTED_PASSED_BONUS;
        auto any_passed_on_file = [&](int file) -> bool {
            if (file < 0 || file > 7) return false;
            for (int r = 0; r < 8; r++)
                if (passed_sq[sq(file, r)]) return true;
            return false;
        };
        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            if (!passed_sq[sq_idx]) continue;
            int f = file_of(sq_idx);
            if (any_passed_on_file(f - 1) || any_passed_on_file(f + 1))
                score += sign * conn;
        }

        for (int f = 0; f < 8; f++) {
            if (pawns_per_file[f] > 1)
                score += sign * DOUBLED_PAWN_PENALTY * (pawns_per_file[f] - 1);
        }
    }

    return score;
}

int eval_connected_rooks(const Board& board) {
    int score = 0;

    for (int colour : {WHITE, BLACK}) {
        int sign = colour;

        // Collect rook squares
        int rook_sqs[10];
        int rook_count = 0;
        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            if (board.squares[sq_idx] == colour * ROOK && rook_count < 10)
                rook_sqs[rook_count++] = sq_idx;
        }

        // Check each pair of rooks
        for (int i = 0; i < rook_count; i++) {
            for (int j = i + 1; j < rook_count; j++) {
                int sq_a = rook_sqs[i];
                int sq_b = rook_sqs[j];
                int fa = file_of(sq_a), ra = rank_of(sq_a);
                int fb = file_of(sq_b), rb = rank_of(sq_b);

                bool connected = false;

                if (ra == rb) {
                    // Same rank — check for clear path between files
                    int f_min = std::min(fa, fb);
                    int f_max = std::max(fa, fb);
                    bool clear = true;
                    for (int f = f_min + 1; f < f_max; f++)
                        if (board.squares[sq(f, ra)] != EMPTY) { clear = false; break; }
                    if (clear) connected = true;
                } else if (fa == fb) {
                    // Same file — check for clear path between ranks
                    int r_min = std::min(ra, rb);
                    int r_max = std::max(ra, rb);
                    bool clear = true;
                    for (int r = r_min + 1; r < r_max; r++)
                        if (board.squares[sq(fa, r)] != EMPTY) { clear = false; break; }
                    if (clear) connected = true;
                }

                if (connected)
                    score += sign * CONNECTED_ROOKS_BONUS;
            }
        }
    }

    return score;
}

// ─────────────────────────────────────────
// MAIN EVALUATE
// ─────────────────────────────────────────

static int eval_piece_activity_combined(const Board& board) {
    int score = 0;
    int bishop_count[2] = {};
    int rook_sqs[2][10] = {};
    int rook_count[2] = {};

    for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
        int piece = board.squares[sq_idx];
        if (piece == EMPTY) continue;

        int colour = piece > 0 ? WHITE : BLACK;
        int idx = colour == WHITE ? 0 : 1;
        int type = std::abs(piece);

        if (type == BISHOP) {
            bishop_count[idx]++;
            continue;
        }

        if (type == ROOK) {
            if (rook_count[idx] < 10)
                rook_sqs[idx][rook_count[idx]++] = sq_idx;

            int enemy = -colour;
            int f = file_of(sq_idx);
            int r = rank_of(sq_idx);

            bool has_friendly_pawn = false;
            bool has_enemy_pawn = false;
            for (int rank = 0; rank < 8; rank++) {
                int file_piece = board.squares[sq(f, rank)];
                if (file_piece == colour * PAWN) has_friendly_pawn = true;
                if (file_piece == enemy * PAWN) has_enemy_pawn = true;
            }

            if (!has_friendly_pawn && !has_enemy_pawn)
                score += colour * ROOK_OPEN_FILE_BONUS;
            else if (!has_friendly_pawn)
                score += colour * ROOK_SEMI_OPEN_FILE_BONUS;

            int seventh_rank = (colour == WHITE) ? 6 : 1;
            if (r == seventh_rank) {
                int enemy_back_rank = (colour == WHITE) ? 7 : 0;
                int enemy_king_sq = board.king_square(enemy);
                bool king_on_back = enemy_king_sq != -1 &&
                                    rank_of(enemy_king_sq) == enemy_back_rank;
                int bonus = ROOK_SEVENTH_RANK_BONUS;
                if (king_on_back) bonus += ROOK_SEVENTH_RANK_BONUS / 2;
                score += colour * bonus;
            }
            continue;
        }

        if (type == KNIGHT) {
            int enemy = -colour;
            int f = file_of(sq_idx);
            int r = rank_of(sq_idx);
            int min_rank = (colour == WHITE) ? 3 : 2;
            int max_rank = (colour == WHITE) ? 5 : 4;

            if (r < min_rank || r > max_rank)
                continue;

            int pawn_rank = r - (colour == WHITE ? 1 : -1);
            bool pawn_protected = false;
            if (pawn_rank >= 0 && pawn_rank <= 7) {
                if (f > 0 && board.squares[sq(f - 1, pawn_rank)] == colour * PAWN)
                    pawn_protected = true;
                if (f < 7 && board.squares[sq(f + 1, pawn_rank)] == colour * PAWN)
                    pawn_protected = true;
            }
            if (!pawn_protected)
                continue;

            bool safe = true;
            int enemy_pawn_direction = (colour == WHITE) ? -1 : 1;
            int attack_rank = r + enemy_pawn_direction;
            if (attack_rank >= 0 && attack_rank <= 7) {
                if (f > 0 && board.squares[sq(f - 1, attack_rank)] == enemy * PAWN)
                    safe = false;
                if (f < 7 && board.squares[sq(f + 1, attack_rank)] == enemy * PAWN)
                    safe = false;
            }
            if (!safe)
                continue;

            int bonus = KNIGHT_OUTPOST_BONUS;
            if (f >= 2 && f <= 5) bonus += KNIGHT_OUTPOST_BONUS / 2;
            score += colour * bonus;
        }
    }

    for (int colour : {WHITE, BLACK}) {
        int idx = colour == WHITE ? 0 : 1;
        if (bishop_count[idx] >= 2)
            score += colour * BISHOP_PAIR_BONUS;

        for (int i = 0; i < rook_count[idx]; i++) {
            for (int j = i + 1; j < rook_count[idx]; j++) {
                int sq_a = rook_sqs[idx][i];
                int sq_b = rook_sqs[idx][j];
                int fa = file_of(sq_a), ra = rank_of(sq_a);
                int fb = file_of(sq_b), rb = rank_of(sq_b);

                bool connected = false;
                if (ra == rb) {
                    int f_min = std::min(fa, fb);
                    int f_max = std::max(fa, fb);
                    bool clear = true;
                    for (int f = f_min + 1; f < f_max; f++) {
                        if (board.squares[sq(f, ra)] != EMPTY) {
                            clear = false;
                            break;
                        }
                    }
                    if (clear) connected = true;
                } else if (fa == fb) {
                    int r_min = std::min(ra, rb);
                    int r_max = std::max(ra, rb);
                    bool clear = true;
                    for (int r = r_min + 1; r < r_max; r++) {
                        if (board.squares[sq(fa, r)] != EMPTY) {
                            clear = false;
                            break;
                        }
                    }
                    if (clear) connected = true;
                }

                if (connected)
                    score += colour * CONNECTED_ROOKS_BONUS;
            }
        }
    }

    return score;
}

static int eval_tempo_phase(const Board& board, int phase) {
    int t     = (TEMPO_MG * phase) / MAX_PHASE;
    return (board.turn == WHITE) ? t : -t;
}

int evaluate_handcrafted(const Board& board) {
    int phase = 0;
    int score = eval_material_phase_and_placement(board, phase);
    bool endgame = phase <= MAX_PHASE / 2;
    score += eval_pawn_features_combined(board, endgame);
    score += eval_king_safety(board, endgame);
    score += eval_mobility(board);
    score += eval_piece_activity_combined(board);
    score += eval_hanging_pieces(board);
    score += eval_tempo_phase(board, phase);
    return score;
}

const EvaluatorBackend& active_evaluator() {
    static const EvaluatorBackend handcrafted = {
        EvaluatorKind::Handcrafted,
        "handcrafted",
        evaluate_handcrafted,
    };
    return handcrafted;
}

int evaluate(const Board& board) {
    return active_evaluator().evaluate_position(board);
}
