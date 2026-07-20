// search.h
// Alpha-beta search for Stepbot.
// C++ equivalent of search.py.

#pragma once

#include "board.h"
#include "movegen.h"
#include "evaluate.h"
#include "zobrist.h"
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <thread>
#include <mutex>

const int CHECKMATE_SCORE = 100000;
const int MAX_DEPTH       = 64;

const int TT_EXACT       = 0;
const int TT_LOWER_BOUND = 1;
const int TT_UPPER_BOUND = 2;
constexpr uint8_t TT_SLOT_PV       = 1u << 0;
constexpr uint8_t TT_SLOT_HAS_EVAL = 1u << 1;

// ── Null Move Pruning ──
const int NULL_MOVE_REDUCTION = 3;
const int NULL_MOVE_MIN_DEPTH = 4;

// ── Late Move Reductions (LMR) ──
const int LMR_MIN_DEPTH      = 3;
const int LMR_MIN_MOVE_INDEX = 3;

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
const int PROBCUT_SEE_MIN = 0;   // Ignore captures that are losing by SEE
const int BAD_CAPTURE_PRUNE_DEPTH  = 3;
const int BAD_CAPTURE_PRUNE_SEE    = -100;
const int BAD_CAPTURE_PRUNE_MARGIN = 150;

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

// UCI-configurable transposition table size, rounded to power-of-two slots.
constexpr int TT_DEFAULT_HASH_MB = 32;
constexpr int TT_MIN_HASH_MB     = 1;
constexpr int TT_MAX_HASH_MB     = 1024;

struct TTSlot {
    Hash     hash  = 0;
    Move     move;
    int      score = 0;
    int16_t  eval  = 0;   // Static eval stored separately from search score
    int16_t  depth = -1;
    uint16_t gen   = 0;
    int8_t   flag  = TT_EXACT;
    uint8_t  flags = 0;   // TT_SLOT_PV / TT_SLOT_HAS_EVAL
    // Note: fields are plain (not atomic). Benign data races in SMP are
    // acceptable — a corrupted entry is simply ignored or overwritten.
};

// ─────────────────────────────────────────
// SHARED TRANSPOSITION TABLE
// One instance shared across all threads.
// Atomic fields ensure no torn reads/writes.
// ─────────────────────────────────────────

struct SharedTT {
    std::vector<TTSlot> slots;
    size_t   mask       = 0;
    int      hash_mb    = TT_DEFAULT_HASH_MB;
    uint16_t generation = 1;  // plain — only written by main thread

    SharedTT() { resize_mb(TT_DEFAULT_HASH_MB); }

    size_t index(Hash hash) const {
        return (size_t)hash & mask;
    }

    void resize_mb(int mb) {
        hash_mb = std::max(TT_MIN_HASH_MB, std::min(TT_MAX_HASH_MB, mb));
        size_t max_slots = ((size_t)hash_mb * 1024 * 1024) / sizeof(TTSlot);
        size_t count = 1;
        while ((count << 1) <= max_slots && (count << 1) > count)
            count <<= 1;
        if (count < 1024)
            count = 1024;

        slots.assign(count, TTSlot{});
        mask = count - 1;
        generation = 1;
    }

    void new_game() {
        ++generation;
        if (generation == 0) {
            generation = 1;
            std::fill(slots.begin(), slots.end(), TTSlot{});
        }
    }
};

// ── Correction History ──
// Corrects static eval based on pawn structure patterns.
// Indexed by pawn hash key (lower 16 bits) and side to move.
constexpr int CORRECTION_HISTORY_SIZE  = 16384;
constexpr int CORRECTION_HISTORY_LIMIT = 1024;

// ── Per-ply search stack data ──
// Tracks state needed across recursive calls.
struct PlyData {
    int   static_eval   = 0;   // Cached static eval at this ply
    int   stat_score    = 0;   // Composite history score for LMR scaling
    int   reduction     = 0;   // LMR reduction applied at this ply
    bool  in_check      = false;
    bool  tt_pv         = false;  // Was this node on a TT PV path?
};

struct RootLine {
    Move        move;
    int         score = 0;
    std::string pv;
};

struct Searcher {
    // Shared transposition table — pointer set by the thread pool
    SharedTT* tt = nullptr;

    // Thread identity (0 = main thread)
    int thread_id = 0;

    // ── PV Table ──
    // Heap-allocated to avoid stack overflow (32KB)
    Move (*pv_table)[MAX_DEPTH];  // pointer to [MAX_DEPTH][MAX_DEPTH]
    int  pv_length[MAX_DEPTH];

    // Killer moves
    Move killers[MAX_DEPTH][2];
    int  killer_count[MAX_DEPTH];

    // ── History heuristic ──
    // history[from][to] — how often this move caused a beta cutoff
    int history[64][64];

    // ── Capture History ──
    // capture_history[piece_type][to_sq][captured_type]
    // Tracks how good captures are based on what is captured and where.
    // piece_type and captured_type are 1-6 (PAWN to KING).
    int capture_history[7][64][7];

    // ── Continuation History ──
    // Heap-allocated to avoid stack overflow (589KB)
    // cont_history[piece_type][to_sq][piece_type][to_sq]
    int (*cont_history)[64][6][64];  // pointer to [6][64][6][64]

    // ── Countermove Heuristic ──
    // countermove[piece_type-1][to_sq] = best response to that move
    // When the opponent plays piece X to square Y, try this move first
    Move countermove[6][64];

    // ── Correction History ──
    // Heap-allocated (128KB) — pawn-structure-keyed correction table
    // [side][pawn_hash & mask]. Adjusts static_eval for pawn patterns.
    int (*pawn_corr)[CORRECTION_HISTORY_SIZE];  // pointer to [2][16384]

    // ── Per-ply stack ──
    // Tracks static_eval, stat_score, reduction, in_check, tt_pv
    // across recursive alphabeta calls.
    PlyData ply_stack[MAX_DEPTH + 4];

    int    nodes_searched;
    int    tt_hits;
    double start_time;
    double time_limit;
    double soft_limit;

    bool   in_null_move;

    // Global stop flag — shared across all threads via pointer
    // Set to true by the main thread when time is up
    std::atomic<bool>* stop_flag = nullptr;
    std::mutex* output_mutex = nullptr;

    // Position history: hashes of all positions from the start of the game.
    // Used for repetition detection during search.
    std::vector<Hash> position_history;

    // Search path stack: hashes of positions on the current search path.
    // Used alongside position_history for repetition detection.
    Hash search_stack[MAX_DEPTH];

    // Per-ply cutoff count: how many fail-highs occurred at ss+1.
    // Used to increase LMR when the child node is failing high a lot.
    int  cutoff_cnt[MAX_DEPTH];

    double opponent_move_times[200];
    int    opponent_move_count;
    double last_go_time;
    Move   root_pv_move;
    bool   has_root_pv_move;

    Searcher();
    ~Searcher();

    // Disable copying — Searcher owns heap memory (pv_table, cont_history)
    Searcher(const Searcher&) = delete;
    Searcher& operator=(const Searcher&) = delete;

    // Allow moving
    Searcher(Searcher&& other) noexcept
        : pv_table(other.pv_table),
          cont_history(other.cont_history),
          pawn_corr(other.pawn_corr)
    {
        other.pv_table    = nullptr;
        other.cont_history = nullptr;
        other.pawn_corr   = nullptr;
        // Copy all other fields
        nodes_searched = other.nodes_searched;
        tt_hits        = other.tt_hits;
        start_time     = other.start_time;
        time_limit     = other.time_limit;
        soft_limit     = other.soft_limit;
        in_null_move   = other.in_null_move;
        stop_flag      = other.stop_flag;
        output_mutex   = other.output_mutex;
        tt             = other.tt;
        thread_id      = other.thread_id;
        opponent_move_count = other.opponent_move_count;
        last_go_time   = other.last_go_time;
        root_pv_move   = other.root_pv_move;
        has_root_pv_move = other.has_root_pv_move;
        std::memcpy(history,         other.history,         sizeof(history));
        std::memcpy(capture_history, other.capture_history, sizeof(capture_history));
        std::memcpy(killers,         other.killers,         sizeof(killers));
        std::memcpy(killer_count,    other.killer_count,    sizeof(killer_count));
        std::memcpy(pv_length,       other.pv_length,       sizeof(pv_length));
        std::memcpy(search_stack,    other.search_stack,    sizeof(search_stack));
        std::memcpy(cutoff_cnt,      other.cutoff_cnt,      sizeof(cutoff_cnt));
        std::memcpy(opponent_move_times, other.opponent_move_times, sizeof(opponent_move_times));
        for (int pt = 0; pt < 6; pt++)
            for (int sq = 0; sq < 64; sq++)
                countermove[pt][sq] = other.countermove[pt][sq];
        position_history = other.position_history;
    }

    // Invalidate transposition table entries from previous games (cheap age bump).
    void tt_new_game();

    Move find_best_move(const Board& board, int max_depth,
                        double time_limit_secs  = -1.0,
                        int    time_budget_ms   = -1,
                        int    inc_ms           = 0,
                        int    moves_to_go      = -1,
                        const std::vector<Hash>& history = {},
                        int    num_threads      = 1,
                        SharedTT* shared_tt     = nullptr,
                        std::atomic<bool>* stop = nullptr,
                        int    multipv          = 1,
                        std::mutex* output_lock = nullptr);

    void record_opponent_move_time(double seconds);

    std::pair<double,double> allocate_time(int time_budget_ms,
                                           int inc_ms,
                                           int moves_to_go,
                                           int fullmove_number) const;

    std::pair<Move, int> search_root(Board& board, Hash hash, int depth);
    std::vector<RootLine> search_root_multipv(Board& board, Hash hash,
                                              int depth, int multipv);

    // prev_move: the move that led to this position (for cont_history)
    int alphabeta(Board& board, Hash hash,
                  int depth, int alpha, int beta, int ply,
                  Move prev_move = Move(0, 0),
                  bool cut_node  = false,
                  int  prev_static_eval = 0);

    // ply: distance from root (mate scores); qcheck_depth: quiet-check plies in q-search
    int quiescence(Board& board, Hash hash, int alpha, int beta,
                   int ply, int qcheck_depth = 0);

    // order_moves now takes the previous move for countermove lookup
    void order_moves(const Board& board,
                     MoveList& moves,
                     int ply,
                     const Move* tt_move  = nullptr,
                     const Move* prev_move = nullptr);
    std::vector<Move> order_moves(const Board& board,
                                  std::vector<Move>& moves,
                                  int ply,
                                  const Move* tt_move  = nullptr,
                                  const Move* prev_move = nullptr);

    void update_killers(const Move& move, int ply);
    void update_history(const Board& board, const Move& move,
                        int depth, const Move& prev_move);
    void update_capture_history(const Board& board, const Move& move,
                                int depth, bool caused_cutoff);
    void update_cont_history(const Move& prev_move, const Move& move,
                             const Board& board, int depth);
    void update_cont_history_bonus(const Move& prev_move, const Move& move,
                                   const Board& board, int bonus);
    void update_countermove(const Move& prev_move, const Move& response,
                            const Board& board);

    void tt_store(Hash hash, int depth, int score, int flag, const Move& move,
                  int ply, int eval = 0, bool tt_pv = false,
                  bool has_eval = false);
    int  get_correction(const Board& board) const;
    void update_correction(const Board& board, int depth, int best_score,
                           int static_eval);
    int  score_from_perspective(const Board& board);
    bool time_up() const;

    // Returns true if the TT move is singular (much better than all others)
    bool is_singular(const Board& board, Hash hash, const Move& tt_move,
                     int depth, int ply, int beta);

    // Helper thread entry point: runs iterative deepening until stop_flag is set
    void helper_search(Board board, Hash hash, int max_depth);
};

// ─────────────────────────────────────────
// SMP POOL
// Owns the shared TT and helper Searcher instances.
// The main UCIEngine holds one of these.
// ─────────────────────────────────────────

struct SMPPool {
    SharedTT               shared_tt;
    std::atomic<bool>      stop{false};
    std::vector<Searcher>  helpers;    // helper threads (not the main searcher)
    std::vector<std::thread> threads;
    int num_threads = 1;

    void resize(int n);        // Set number of threads (1 = single-threaded)
    void new_game();           // Clear TT generation
    void stop_helpers();       // Signal helpers to stop and join threads
};
