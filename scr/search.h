// search.h
// Alpha-beta search for Stepbot.
// C++ equivalent of search.py.

#pragma once

#include "board.h"
#include "movegen.h"
#include "evaluate.h"
#include "zobrist.h"
#include <vector>
#include <cstdint>
#include <cstring>

const int CHECKMATE_SCORE = 100000;
const int MAX_DEPTH       = 64;

const int TT_EXACT       = 0;
const int TT_LOWER_BOUND = 1;
const int TT_UPPER_BOUND = 2;

// ── Null Move Pruning ──
const int NULL_MOVE_REDUCTION = 3;
const int NULL_MOVE_MIN_DEPTH = 3;

// ── Late Move Reductions (LMR) ──
const int LMR_MIN_DEPTH      = 3;
const int LMR_MIN_MOVE_INDEX = 3;

// ── Aspiration Windows ──
const int ASPIRATION_INITIAL_DELTA = 50;
const int ASPIRATION_MIN_DEPTH     = 4;

// ── Futility Pruning ──
const int FUTILITY_MARGIN[4] = {
    0, 150, 300, 500,
};

// ── Check Extensions ──
// When a move gives check, extend search by this many plies.
// Keeps tactical sequences involving checks fully visible.
const int CHECK_EXTENSION = 1;

// ── Singular Extensions ──
// If the TT move is much better than all alternatives, extend it.
const int SE_DEPTH_LIMIT = 4;    // Only try at depth >= 4
const int SE_MARGIN      = 150;  // TT move must beat alternatives by this much

// ── Probcut ──
// If a capture looks strong even at reduced depth, prune early.
const int PROBCUT_DEPTH  = 5;    // Only try at depth >= 5
const int PROBCUT_MARGIN = 200;  // Must beat beta by this much to trigger

// ── Delta Pruning (qsearch) ──
// If even capturing the most valuable piece + margin can't raise alpha,
// bail out of quiescence search early.
const int DELTA_MARGIN        = 200;  // Safety buffer (cp)
const int DELTA_MAX_GAIN      = 900;  // Queen value — best plausible single gain

// ── Late Move Pruning (LMP) ──
// At low depths, skip quiet moves beyond this count threshold.
// Indexed by depth (0 unused, 1-3 active).
const int LMP_THRESHOLD[4] = { 0, 3, 6, 10 };

// ── Repetition / Contempt ──
// Score returned when a position is a two-fold repetition during search.
// Negative = engine tries to avoid repeating when it has a choice.
const int REPETITION_SCORE = -10;  // cp penalty for repeating

// Power-of-two bucket count; mask with (TT_NUM_SLOTS - 1) for indexing.
constexpr size_t TT_NUM_SLOTS = 1 << 20;

struct TTSlot {
    Hash     hash  = 0;
    uint32_t gen   = 0;
    int      depth = -1;
    int      score = 0;
    int      flag  = TT_EXACT;
    Move     move;
};

struct Searcher {
    std::vector<TTSlot> tt;
    uint32_t              tt_generation = 1;

    // ── PV Table ──
    // Stores the principal variation — the sequence of best moves
    // found during search. pv_table[ply][depth] = move at that ply.
    // pv_length[ply] = how many moves are stored from that ply.
    Move pv_table[MAX_DEPTH][MAX_DEPTH];
    int  pv_length[MAX_DEPTH];

    // Killer moves
    Move killers[MAX_DEPTH][2];
    int  killer_count[MAX_DEPTH];

    // ── History heuristic ──
    // history[from][to] — how often this move caused a beta cutoff
    int history[64][64];

    // ── Continuation History ──
    // cont_history[piece_type][to_sq][piece_type][to_sq]
    // Tracks how good a move is given the previous move.
    // Indexed as [prev_piece_type-1][prev_to][curr_piece_type-1][curr_to]
    // piece_type 1-6, so we use [6][64][6][64]
    int cont_history[6][64][6][64];

    // ── Countermove Heuristic ──
    // countermove[piece_type-1][to_sq] = best response to that move
    // When the opponent plays piece X to square Y, try this move first
    Move countermove[6][64];

    int    nodes_searched;
    int    tt_hits;
    double start_time;
    double time_limit;
    double soft_limit;

    bool   in_null_move;

    // Position history: hashes of all positions from the start of the game.
    // Used for repetition detection during search.
    std::vector<Hash> position_history;

    // Search path stack: hashes of positions on the current search path.
    // Used alongside position_history for repetition detection.
    Hash search_stack[MAX_DEPTH];

    double opponent_move_times[200];
    int    opponent_move_count;
    double last_go_time;

    Searcher();

    // Invalidate transposition table entries from previous games (cheap age bump).
    void tt_new_game();

    Move find_best_move(const Board& board, int max_depth,
                        double time_limit_secs  = -1.0,
                        int    time_budget_ms   = -1,
                        int    inc_ms           = 0,
                        int    moves_to_go      = -1,
                        const std::vector<Hash>& history = {});

    void record_opponent_move_time(double seconds);

    std::pair<double,double> allocate_time(int time_budget_ms,
                                           int inc_ms,
                                           int moves_to_go,
                                           int fullmove_number) const;

    std::pair<Move, int> search_root(Board& board, Hash hash,
                                     int depth, int prev_score);

    // prev_move: the move that led to this position (for cont_history)
    int alphabeta(Board& board, Hash hash,
                  int depth, int alpha, int beta, int ply,
                  Move prev_move = Move(0, 0));

    // ply: distance from root (mate scores); qcheck_depth: quiet-check plies in q-search
    int quiescence(Board& board, Hash hash, int alpha, int beta,
                   int ply, int qcheck_depth = 0);

    // order_moves now takes the previous move for countermove lookup
    std::vector<Move> order_moves(const Board& board,
                                  std::vector<Move>& moves,
                                  int ply,
                                  const Move* tt_move  = nullptr,
                                  const Move* prev_move = nullptr);

    void update_killers(const Move& move, int ply);
    void update_history(const Board& board, const Move& move,
                        int depth, const Move& prev_move);
    void update_cont_history(const Move& prev_move, const Move& move,
                             const Board& board, int depth);
    void update_countermove(const Move& prev_move, const Move& response,
                            const Board& board);

    void tt_store(Hash hash, int depth, int score, int flag, const Move& move,
                  int ply);
    int  score_from_perspective(const Board& board);
    bool time_up() const;

    // Returns true if the TT move is singular (much better than all others)
    bool is_singular(const Board& board, Hash hash, const Move& tt_move,
                     int depth, int ply, int beta);
};
