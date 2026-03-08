// search.h
// Alpha-beta search for Stepbot.
// C++ equivalent of search.py.

#pragma once

#include "board.h"
#include "movegen.h"
#include "evaluate.h"
#include "zobrist.h"
#include <unordered_map>
#include <vector>
#include <cstdint>

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
// Start each iterative deepening iteration with a narrow window around
// the previous score. If the result falls outside, widen and re-search.
// Much cheaper than always searching with a full -inf/+inf window.
const int ASPIRATION_INITIAL_DELTA = 50;   // ±50cp starting window
const int ASPIRATION_MIN_DEPTH     = 4;    // Only use from depth 4 up

// ── Futility Pruning ──
// Near leaf nodes, if the static eval is so far below alpha that no
// realistic move could catch up, skip searching that node entirely.
// Indexed by remaining depth (1, 2, 3).
const int FUTILITY_MARGIN[4] = {
    0,     // depth 0 — unused (quiescence handles this)
    150,   // depth 1 — one move can gain at most ~150cp
    300,   // depth 2 — two moves ~300cp
    500,   // depth 3 — three moves ~500cp
};

struct TTEntry {
    Hash hash;
    int  depth;
    int  score;
    int  flag;
    Move move;

    TTEntry() : hash(0), depth(0), score(0), flag(TT_EXACT), move(0, 0) {}
    TTEntry(Hash h, int d, int s, int f, Move m)
        : hash(h), depth(d), score(s), flag(f), move(m) {}
};

struct Searcher {
    std::unordered_map<Hash, TTEntry> tt;

    Move killers[MAX_DEPTH][2];
    int  killer_count[MAX_DEPTH];

    int history[64][64];

    int    nodes_searched;
    int    tt_hits;
    double start_time;
    double time_limit;
    double soft_limit;

    // Prevents two null moves in a row (would be unsound)
    bool   in_null_move;

    double opponent_move_times[200];
    int    opponent_move_count;
    double last_go_time;

    Searcher();

    Move find_best_move(const Board& board, int max_depth,
                        double time_limit_secs  = -1.0,
                        int    time_budget_ms   = -1,
                        int    inc_ms           = 0,
                        int    moves_to_go      = -1);

    void record_opponent_move_time(double seconds);

    std::pair<double,double> allocate_time(int time_budget_ms,
                                           int inc_ms,
                                           int moves_to_go,
                                           int fullmove_number) const;

    std::pair<Move, int> search_root(const Board& board, Hash hash,
                                     int depth, int prev_score);

    int alphabeta(const Board& board, Hash hash,
                  int depth, int alpha, int beta, int ply);

    int quiescence(const Board& board, Hash hash, int alpha, int beta);

    std::vector<Move> order_moves(const Board& board,
                                  std::vector<Move>& moves,
                                  int ply,
                                  const Move* tt_move = nullptr);

    void update_killers(const Move& move, int ply);
    void update_history(const Board& board, const Move& move, int depth);

    void tt_store(Hash hash, int depth, int score, int flag, const Move& move);

    int score_from_perspective(const Board& board);

    bool time_up() const;
};
