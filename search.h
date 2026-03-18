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

    // ── Killer moves ──
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

    // prev_move: the move that led to this position (for cont_history)
    int alphabeta(const Board& board, Hash hash,
                  int depth, int alpha, int beta, int ply,
                  Move prev_move = Move(0, 0));

    int quiescence(const Board& board, Hash hash, int alpha, int beta);

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

    void tt_store(Hash hash, int depth, int score, int flag, const Move& move);
    int  score_from_perspective(const Board& board);
    bool time_up() const;
};
