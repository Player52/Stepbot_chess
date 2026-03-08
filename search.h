// search.h
// Alpha-beta search for Stepbot.
// C++ equivalent of search.py.

#pragma once

#include "board.h"
#include "movegen.h"
#include "evaluate.h"
#include "zobrist.h"
#include <unordered_map>  // Like Python's dict, but keys must be hashable
#include <vector>
#include <cstdint>

// ─────────────────────────────────────────
// CONSTANTS
// ─────────────────────────────────────────

const int CHECKMATE_SCORE = 100000;
const int MAX_DEPTH       = 64;

// Transposition table flags — same as search.py
const int TT_EXACT       = 0;
const int TT_LOWER_BOUND = 1;
const int TT_UPPER_BOUND = 2;

// ─────────────────────────────────────────
// TRANSPOSITION TABLE ENTRY
// Equivalent of Python's TTEntry class.
// We use a struct — plain data, no methods needed.
// ─────────────────────────────────────────

struct TTEntry {
    Hash hash;    // Full hash for collision detection
    int  depth;
    int  score;
    int  flag;    // TT_EXACT / TT_LOWER_BOUND / TT_UPPER_BOUND
    Move move;    // Best move found

    // Default constructor — needed because Move has no default constructor
    // 'Move(0,0)' creates a dummy move with from=0, to=0
    TTEntry() : hash(0), depth(0), score(0), flag(TT_EXACT), move(0, 0) {}

    TTEntry(Hash h, int d, int s, int f, Move m)
        : hash(h), depth(d), score(s), flag(f), move(m) {}
};

// ─────────────────────────────────────────
// SEARCHER
// Holds all search state: TT, killers, history, stats.
// ─────────────────────────────────────────

struct Searcher {
    // Transposition table: maps hash -> TTEntry
    // std::unordered_map is like Python's dict
    // It uses the hash directly as the key (Hash = uint64_t)
    std::unordered_map<Hash, TTEntry> tt;

    // Killer moves: two per ply
    // We store up to 2 killers per depth level
    // 'Move killers[MAX_DEPTH][2]' is a 2D array of Move objects
    Move killers[MAX_DEPTH][2];
    int  killer_count[MAX_DEPTH];

    // History heuristic: history[from][to]
    int history[64][64];

    // Stats
    int    nodes_searched;
    int    tt_hits;
    double start_time;
    double time_limit;     // Hard cutoff — stop immediately when reached
    double soft_limit;     // Soft cutoff — don't start a new depth iteration

    // Time management state
    // Tracks how long the opponent spends per move so we can
    // estimate a sensible budget for ourselves
    double opponent_move_times[200];   // Rolling log of opponent move durations
    int    opponent_move_count;
    double last_go_time;               // Timestamp when we last received 'go'

    // Constructor — initialises everything
    Searcher();

    // Main search entry point.
    // time_budget_ms: total time remaining for our side in milliseconds (-1 = none)
    // inc_ms:         increment per move in milliseconds (0 = none)
    // moves_to_go:    moves until next time control (-1 = unknown)
    Move find_best_move(const Board& board, int max_depth,
                        double time_limit_secs  = -1.0,
                        int    time_budget_ms   = -1,
                        int    inc_ms           = 0,
                        int    moves_to_go      = -1);

    // Called by UCI handler when opponent makes a move,
    // so we can record how long they took
    void record_opponent_move_time(double seconds);

    // Compute a time allocation for this move given the clock state
    // Returns (soft_limit_secs, hard_limit_secs)
    std::pair<double,double> allocate_time(int time_budget_ms,
                                           int inc_ms,
                                           int moves_to_go,
                                           int fullmove_number) const;

    // Internal search functions
    std::pair<Move, int> search_root(const Board& board, Hash hash, int depth);

    int alphabeta(const Board& board, Hash hash,
                  int depth, int alpha, int beta, int ply);

    int quiescence(const Board& board, Hash hash, int alpha, int beta);

    // Move ordering
    std::vector<Move> order_moves(const Board& board,
                                  std::vector<Move>& moves,
                                  int ply,
                                  const Move* tt_move = nullptr);

    // Killer and history updates
    void update_killers(const Move& move, int ply);
    void update_history(const Board& board, const Move& move, int depth);

    // Transposition table
    void tt_store(Hash hash, int depth, int score, int flag, const Move& move);

    // Score from the perspective of the side to move
    int score_from_perspective(const Board& board);

    // Time check
    bool time_up() const;
};
