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
    int  nodes_searched;
    int  tt_hits;
    double start_time;
    double time_limit;

    // Constructor — initialises everything
    Searcher();

    // Main search entry point
    Move find_best_move(const Board& board, int max_depth,
                        double time_limit_secs = -1.0);

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
