// search.cpp
// Alpha-beta search implementation.
// C++ equivalent of search.py.

#include "search.h"
#include <algorithm>   // std::sort, std::max, std::min
#include <iostream>    // std::cout
#include <chrono>      // For timing
#include <cstring>     // std::memset — fast array zeroing

// ─────────────────────────────────────────
// TIMING HELPER
// C++ doesn't have Python's time.time() — we use std::chrono instead.
// ─────────────────────────────────────────

static double now() {
    // chrono::steady_clock is a monotonic clock — always moves forward
    // duration_cast converts to seconds as a double
    using namespace std::chrono;
    auto t = steady_clock::now().time_since_epoch();
    return duration_cast<duration<double>>(t).count();
}

// ─────────────────────────────────────────
// SEARCHER CONSTRUCTOR
// Initialises all arrays to zero
// ─────────────────────────────────────────

Searcher::Searcher()
    : nodes_searched(0), tt_hits(0), start_time(0), time_limit(-1)
{
    // std::memset fills a block of memory with a value — much faster
    // than a loop for zeroing arrays
    std::memset(history, 0, sizeof(history));
    std::memset(killer_count, 0, sizeof(killer_count));
    // killers array initialises automatically via Move's default constructor

    // Reserve space in the transposition table
    tt.reserve(1 << 20);   // 1M buckets — '1 << 20' is 2^20 = 1,048,576
}

// ─────────────────────────────────────────
// TIME CHECK
// ─────────────────────────────────────────

bool Searcher::time_up() const {
    if (time_limit < 0) return false;
    return (now() - start_time) >= time_limit;
}

// ─────────────────────────────────────────
// FIND BEST MOVE
// Iterative deepening entry point
// ─────────────────────────────────────────

Move Searcher::find_best_move(const Board& board, int max_depth,
                               double time_limit_secs) {
    nodes_searched = 0;
    tt_hits        = 0;
    start_time     = now();
    time_limit     = time_limit_secs;

    // Reset killers and history
    std::memset(history, 0, sizeof(history));
    std::memset(killer_count, 0, sizeof(killer_count));
    // killers reset automatically — Move default constructor gives null moves

    Hash current_hash = compute_hash(board);
    Move best_move(0, 0);
    int  best_score = 0;

    for (int depth = 1; depth <= max_depth; depth++) {
        if (time_up()) break;

        // std::pair holds two values — like Python's tuple
        // .first and .second access them
        auto [move, score] = search_root(board, current_hash, depth);

        if (move.from_sq != move.to_sq || move.from_sq != 0) {
            best_move  = move;
            best_score = score;
        }

        double elapsed = now() - start_time;
        std::cout << "  Depth " << depth
                  << ": best=" << best_move.to_uci()
                  << " score=" << (best_score >= 0 ? "+" : "") << best_score
                  << " nodes=" << nodes_searched
                  << " tt_hits=" << tt_hits
                  << " time=" << elapsed << "s\n";
    }

    return best_move;
}

// ─────────────────────────────────────────
// SEARCH ROOT
// Searches all root moves and returns the best
// ─────────────────────────────────────────

std::pair<Move, int> Searcher::search_root(const Board& board,
                                            Hash hash, int depth) {
    auto moves = generate_legal_moves(board);
    if (moves.empty()) return {Move(0, 0), 0};

    moves = order_moves(board, moves, 0);

    Move best_move  = moves[0];
    int  best_score = -CHECKMATE_SCORE - 1;
    int  alpha      = -CHECKMATE_SCORE - 1;
    int  beta       =  CHECKMATE_SCORE + 1;

    for (const Move& move : moves) {
        Board new_board  = apply_move(board, move);
        Hash  new_hash   = update_hash(hash, board, move, new_board);
        int   score      = -alphabeta(new_board, new_hash,
                                      depth - 1, -beta, -alpha, 1);

        if (score > best_score) {
            best_score = score;
            best_move  = move;
        }
        alpha = std::max(alpha, score);
    }

    tt_store(hash, depth, best_score, TT_EXACT, best_move);
    return {best_move, best_score};
}

// ─────────────────────────────────────────
// ALPHA-BETA
// Recursive negamax alpha-beta search
// ─────────────────────────────────────────

int Searcher::alphabeta(const Board& board, Hash hash,
                         int depth, int alpha, int beta, int ply) {
    nodes_searched++;

    // ── Transposition table lookup ──
    const Move* tt_move = nullptr;
    Move        tt_move_storage(0, 0);

    // .find() returns an iterator — like Python's dict.get()
    // If not found, it returns tt.end() (a sentinel value)
    auto it = tt.find(hash);
    if (it != tt.end()) {
        const TTEntry& entry = it->second;   // .second = the value (TTEntry)
        if (entry.hash == hash && entry.depth >= depth) {
            tt_hits++;
            if (entry.flag == TT_EXACT)       return entry.score;
            if (entry.flag == TT_LOWER_BOUND) alpha = std::max(alpha, entry.score);
            if (entry.flag == TT_UPPER_BOUND) beta  = std::min(beta,  entry.score);
            if (alpha >= beta)                return entry.score;
        }
        tt_move_storage = it->second.move;
        tt_move         = &tt_move_storage;
    }

    // ── Base case ──
    if (depth == 0)
        return quiescence(board, hash, alpha, beta);

    auto moves = generate_legal_moves(board);

    // ── No legal moves: checkmate or stalemate ──
    if (moves.empty()) {
        if (king_in_check(board, board.turn))
            return -(CHECKMATE_SCORE - ply);
        return 0;
    }

    moves = order_moves(board, moves, ply, tt_move);

    int  original_alpha = alpha;
    Move best_move(0, 0);
    bool found_best = false;

    for (const Move& move : moves) {
        if (time_up()) break;

        Board new_board = apply_move(board, move);
        Hash  new_hash  = update_hash(hash, board, move, new_board);
        int   score     = -alphabeta(new_board, new_hash,
                                     depth - 1, -beta, -alpha, ply + 1);

        if (score >= beta) {
            update_killers(move, ply);
            update_history(board, move, depth);
            tt_store(hash, depth, beta, TT_LOWER_BOUND, move);
            return beta;
        }

        if (score > alpha) {
            alpha     = score;
            best_move = move;
            found_best = true;
        }
    }

    if (found_best) {
        int flag = (alpha > original_alpha) ? TT_EXACT : TT_UPPER_BOUND;
        tt_store(hash, depth, alpha, flag, best_move);
    }

    return alpha;
}

// ─────────────────────────────────────────
// QUIESCENCE SEARCH
// ─────────────────────────────────────────

int Searcher::quiescence(const Board& board, Hash hash,
                          int alpha, int beta) {
    nodes_searched++;

    int stand_pat = score_from_perspective(board);
    if (stand_pat >= beta) return beta;
    alpha = std::max(alpha, stand_pat);

    auto moves = generate_legal_moves(board);

    // Filter to captures only
    std::vector<Move> captures;
    captures.reserve(8);
    for (const Move& m : moves) {
        if (!board.is_empty(m.to_sq) || m.to_sq == board.en_passant_sq)
            captures.push_back(m);
    }

    captures = order_moves(board, captures, 0);

    for (const Move& move : captures) {
        Board new_board = apply_move(board, move);
        Hash  new_hash  = update_hash(hash, board, move, new_board);
        int   score     = -quiescence(new_board, new_hash, -beta, -alpha);

        if (score >= beta) return beta;
        alpha = std::max(alpha, score);
    }

    return alpha;
}

// ─────────────────────────────────────────
// MOVE ORDERING
// ─────────────────────────────────────────

std::vector<Move> Searcher::order_moves(const Board& board,
                                         std::vector<Move>& moves,
                                         int ply,
                                         const Move* tt_move) {
    // Assign a score to each move, then sort descending
    // We use a lambda — an anonymous function defined inline
    // Like Python's key= argument to sorted()
    std::vector<std::pair<int, Move>> scored;
    scored.reserve(moves.size());

    for (const Move& move : moves) {
        int score = 0;

        // TT move — highest priority
        if (tt_move && move == *tt_move) {
            score = 20000;
        }
        // Captures: MVV-LVA
        else {
            int target = board.get_piece(move.to_sq);
            if (target != EMPTY) {
                int victim_val   = std::abs(target) * 10;
                int attacker_val = std::abs(board.get_piece(move.from_sq));
                score = 10000 + victim_val - attacker_val;
            }
            // En passant capture
            else if (move.to_sq == board.en_passant_sq) {
                score = 10000;
            }
            // Killer moves
            else if (ply < MAX_DEPTH) {
                if (killer_count[ply] > 0 && move == killers[ply][0])
                    score = 9000;
                else if (killer_count[ply] > 1 && move == killers[ply][1])
                    score = 8999;
                // History heuristic
                else
                    score = history[move.from_sq][move.to_sq];
            }
        }

        scored.push_back({score, move});
    }

    // Sort by score descending
    // Lambda syntax: [](const auto& a, const auto& b) { return ...; }
    // 'auto' lets the compiler infer the type
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) {
                  return a.first > b.first;
              });

    // Extract sorted moves
    std::vector<Move> ordered;
    ordered.reserve(scored.size());
    for (auto& [s, m] : scored)
        ordered.push_back(m);

    return ordered;
}

// ─────────────────────────────────────────
// KILLER MOVES
// ─────────────────────────────────────────

void Searcher::update_killers(const Move& move, int ply) {
    if (ply >= MAX_DEPTH) return;

    // Don't store captures as killers
    // (We check killer moves in move ordering — captures are handled by MVV-LVA)

    // Check if already stored
    if (killer_count[ply] > 0 && killers[ply][0] == move) return;
    if (killer_count[ply] > 1 && killers[ply][1] == move) return;

    // Shift: new killer goes in slot 0, old slot 0 goes to slot 1
    killers[ply][1] = killers[ply][0];
    killers[ply][0] = move;
    if (killer_count[ply] < 2) killer_count[ply]++;
}

// ─────────────────────────────────────────
// HISTORY HEURISTIC
// ─────────────────────────────────────────

void Searcher::update_history(const Board& board, const Move& move, int depth) {
    if (board.is_empty(move.to_sq)) {
        history[move.from_sq][move.to_sq] += depth * depth;
    }
}

// ─────────────────────────────────────────
// TRANSPOSITION TABLE
// ─────────────────────────────────────────

void Searcher::tt_store(Hash hash, int depth, int score,
                         int flag, const Move& move) {
    // If table is getting large, clear it
    // (Simple replacement strategy — good enough for now)
    if (tt.size() >= 1000000) {
        tt.clear();
    }
    tt[hash] = TTEntry(hash, depth, score, flag, move);
}

// ─────────────────────────────────────────
// SCORE FROM PERSPECTIVE
// ─────────────────────────────────────────

int Searcher::score_from_perspective(const Board& board) {
    int score = evaluate(board);
    return (board.turn == WHITE) ? score : -score;
}
