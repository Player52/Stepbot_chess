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

// ─────────────────────────────────────────
// PIECE-SQUARE TABLES
// Same values as evaluate.py
// ─────────────────────────────────────────

const int PAWN_TABLE[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0,
};

const int KNIGHT_TABLE[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50,
};

const int BISHOP_TABLE[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20,
};

const int ROOK_TABLE[64] = {
     0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0,
};

const int QUEEN_TABLE[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -10,  5,  5,  5,  5,  5,  0,-10,
      0,  0,  5,  5,  5,  5,  0, -5,
     -5,  0,  5,  5,  5,  5,  0, -5,
    -10,  0,  5,  5,  5,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20,
};

const int KING_TABLE_MG[64] = {
     20, 30, 10,  0,  0, 10, 30, 20,
     20, 20,  0,  0,  0,  0, 20, 20,
    -10,-20,-20,-20,-20,-20,-20,-10,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
};

const int KING_TABLE_EG[64] = {
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
// PIECE-SQUARE BONUS LOOKUP
// ─────────────────────────────────────────

int piece_square_bonus(int piece_type, int sq_idx, int colour, bool endgame) {
    // Mirror the square for Black (same as Python's mirrored index)
    int table_sq = (colour == WHITE) ? sq_idx
                                     : (7 - rank_of(sq_idx)) * 8 + file_of(sq_idx);

    switch (piece_type) {
        case PAWN:   return PAWN_TABLE[table_sq];
        case KNIGHT: return KNIGHT_TABLE[table_sq];
        case BISHOP: return BISHOP_TABLE[table_sq];
        case ROOK:   return ROOK_TABLE[table_sq];
        case QUEEN:  return QUEEN_TABLE[table_sq];
        case KING:   return endgame ? KING_TABLE_EG[table_sq]
                                    : KING_TABLE_MG[table_sq];
        default:     return 0;
    }
}

// ─────────────────────────────────────────
// ENDGAME DETECTION
// ─────────────────────────────────────────

bool is_endgame(const Board& board) {
    for (int colour : {WHITE, BLACK}) {
        int material = 0;
        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            int piece = board.get_piece(sq_idx);
            if (piece == EMPTY) continue;
            if ((piece > 0) == (colour == WHITE)) {
                int pt = std::abs(piece);
                if (pt != PAWN && pt != KING) {
                    material += PIECE_VALUES[pt];
                }
            }
        }
        if (material <= ENDGAME_THRESHOLD) return true;
    }
    return false;
}

// ─────────────────────────────────────────
// MATERIAL AND PLACEMENT
// ─────────────────────────────────────────

int eval_material_and_placement(const Board& board, bool endgame) {
    int score = 0;
    for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
        int piece = board.get_piece(sq_idx);
        if (piece == EMPTY) continue;
        int colour     = (piece > 0) ? WHITE : BLACK;
        int piece_type = std::abs(piece);
        score += colour * PIECE_VALUES[piece_type];
        score += colour * piece_square_bonus(piece_type, sq_idx, colour, endgame);
    }
    return score;
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
            int piece = board.get_piece(sq_idx);
            if (piece == colour * PAWN)
                friendly_pawn_files[file_of(sq_idx)] = true;
            if (piece == enemy * PAWN) {
                int f = file_of(sq_idx);
                if (enemy_pawn_count[f] < 8)
                    enemy_pawn_ranks[f][enemy_pawn_count[f]++] = rank_of(sq_idx);
            }
        }

        int pawns_per_file[8] = {};

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            int piece = board.get_piece(sq_idx);
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
                int bonus_rank = (colour == WHITE) ? r : (7 - r);
                score += sign * (endgame ? PASSED_PAWN_BONUS_EG[bonus_rank]
                                         : PASSED_PAWN_BONUS[bonus_rank]);
            }
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

int eval_king_safety(const Board& board, bool endgame) {
    if (endgame) return 0;

    int score = 0;

    for (int colour : {WHITE, BLACK}) {
        int sign  = colour;
        int enemy = -colour;

        // Find the king
        int king_sq = -1;
        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            if (board.get_piece(sq_idx) == colour * KING) {
                king_sq = sq_idx;
                break;
            }
        }
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
        int num_attackers  = 0;
        int attack_units   = 0;

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            int piece = board.get_piece(sq_idx);
            if (piece == EMPTY) continue;
            if ((piece > 0) == (colour == WHITE)) continue;   // Only enemy pieces

            int pt = std::abs(piece);
            if (pt == PAWN || pt == KING) continue;

            // Check if this piece attacks any square in the king zone
            bool attacks_zone = false;

            // King zone: 3x3 around king plus one rank in front
            int front_rank = kr + (colour == WHITE ? 1 : -1);

            for (int zr = std::max(0, kr - 1); zr <= std::min(7, kr + 1); zr++) {
                for (int zf = std::max(0, kf - 1); zf <= std::min(7, kf + 1); zf++) {
                    int zone_sq = sq(zf, zr);
                    if (square_attacked_by(board, zone_sq, enemy)) {
                        attacks_zone = true;
                    }
                }
            }
            // Also check extended front rank
            if (front_rank >= 0 && front_rank <= 7) {
                for (int zf = std::max(0, kf - 2); zf <= std::min(7, kf + 2); zf++) {
                    if (square_attacked_by(board, sq(zf, front_rank), enemy))
                        attacks_zone = true;
                }
            }

            if (attacks_zone) {
                num_attackers++;
                attack_units += ATTACK_WEIGHT[pt];
            }
        }

        // More attackers = danger compounds exponentially
        if (num_attackers >= 2) {
            danger += attack_units * num_attackers / 2;
        } else if (num_attackers == 1) {
            danger += attack_units / 4;   // Single attacker is much less dangerous
        }

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
        if (escape_squares == 0) danger += 80;
        else if (escape_squares == 1) danger += 40;
        else if (escape_squares == 2) danger += 15;

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

// ─────────────────────────────────────────
// MOBILITY
// ─────────────────────────────────────────

int eval_mobility(const Board& board) {
    int score = 0;

    for (int colour : {WHITE, BLACK}) {
        int sign = colour;

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            int piece = board.get_piece(sq_idx);
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
                    if (board.colour_at(target) != colour) mobility++;
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
                        if (board.colour_at(target) == colour) break;
                        mobility++;
                        if (board.colour_at(target) == -colour) break;
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
            if (board.get_piece(sq_idx) == colour * BISHOP)
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

int eval_rooks(const Board& board, bool endgame) {
    int score = 0;

    for (int colour : {WHITE, BLACK}) {
        int sign  = colour;
        int enemy = -colour;

        // The "seventh rank" from each side's perspective
        // White's 7th rank = rank index 6, Black's 7th = rank index 1
        int seventh_rank = (colour == WHITE) ? 6 : 1;

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            if (board.get_piece(sq_idx) != colour * ROOK) continue;

            int f = file_of(sq_idx);
            int r = rank_of(sq_idx);

            // ── Open / semi-open file bonus ──
            // Scan the file for pawns
            bool has_friendly_pawn = false;
            bool has_enemy_pawn    = false;
            for (int rank = 0; rank < 8; rank++) {
                int piece = board.get_piece(sq(f, rank));
                if (piece == colour * PAWN) has_friendly_pawn = true;
                if (piece == enemy  * PAWN) has_enemy_pawn    = true;
            }

            if (!has_friendly_pawn && !has_enemy_pawn)
                score += sign * ROOK_OPEN_FILE_BONUS;
            else if (!has_friendly_pawn)
                score += sign * ROOK_SEMI_OPEN_FILE_BONUS;

            // ── Seventh rank bonus ──
            // A rook on the 7th rank attacks the enemy's pawns on their
            // starting rank and restricts the enemy king. Especially
            // powerful when the enemy king is still on the back rank.
            if (r == seventh_rank) {
                // Extra bonus if the enemy king is also on the back rank
                int enemy_back_rank = (colour == WHITE) ? 7 : 0;
                bool king_on_back   = false;
                for (int file = 0; file < 8; file++) {
                    if (board.get_piece(sq(file, enemy_back_rank)) == enemy * KING)
                        king_on_back = true;
                }
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

        // Outpost ranks: ranks 4-6 for White (indices 3-5), ranks 2-4 for Black
        int min_rank = (colour == WHITE) ? 3 : 2;
        int max_rank = (colour == WHITE) ? 5 : 4;

        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            if (board.get_piece(sq_idx) != colour * KNIGHT) continue;

            int f = file_of(sq_idx);
            int r = rank_of(sq_idx);

            // Must be on an outpost rank
            if (r < min_rank || r > max_rank) continue;

            // Must be protected by a friendly pawn
            int pawn_rank   = r - (colour == WHITE ? 1 : -1);
            bool pawn_protected = false;
            if (pawn_rank >= 0 && pawn_rank <= 7) {
                if (f > 0 && board.get_piece(sq(f - 1, pawn_rank)) == colour * PAWN)
                    pawn_protected = true;
                if (f < 7 && board.get_piece(sq(f + 1, pawn_rank)) == colour * PAWN)
                    pawn_protected = true;
            }
            if (!pawn_protected) continue;

            // Cannot be attacked by an enemy pawn
            // (if it can be chased away it's not a true outpost)
            bool safe = true;
            int  enemy_pawn_direction = (colour == WHITE) ? -1 : 1;
            int  attack_rank          = r + enemy_pawn_direction;
            if (attack_rank >= 0 && attack_rank <= 7) {
                if (f > 0 && board.get_piece(sq(f - 1, attack_rank)) == enemy * PAWN)
                    safe = false;
                if (f < 7 && board.get_piece(sq(f + 1, attack_rank)) == enemy * PAWN)
                    safe = false;
            }
            if (!safe) continue;

            // It's a true outpost — award bonus
            // Extra bonus for central files (c, d, e, f = files 2-5)
            int bonus = KNIGHT_OUTPOST_BONUS;
            if (f >= 2 && f <= 5) bonus += KNIGHT_OUTPOST_BONUS / 2;
            score += sign * bonus;
        }
    }

    return score;
}

// ─────────────────────────────────────────
// MAIN EVALUATE
// ─────────────────────────────────────────

int evaluate(const Board& board) {
    bool endgame = is_endgame(board);
    int score = 0;
    score += eval_material_and_placement(board, endgame);
    score += eval_pawn_structure(board, endgame);
    score += eval_king_safety(board, endgame);
    score += eval_mobility(board);
    score += eval_bishop_pair(board);
    score += eval_rooks(board, endgame);
    score += eval_knight_outposts(board);
    return score;
}
