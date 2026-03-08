// search.cpp
// Alpha-beta search implementation.
// C++ equivalent of search.py.

#include "search.h"
#include <algorithm>
#include <iostream>
#include <chrono>
#include <cstring>

static double now() {
    using namespace std::chrono;
    auto t = steady_clock::now().time_since_epoch();
    return duration_cast<duration<double>>(t).count();
}

Searcher::Searcher()
    : nodes_searched(0), tt_hits(0), start_time(0),
      time_limit(-1), soft_limit(-1),
      in_null_move(false),
      opponent_move_count(0), last_go_time(-1)
{
    std::memset(opponent_move_times, 0, sizeof(opponent_move_times));
    std::memset(history, 0, sizeof(history));
    std::memset(killer_count, 0, sizeof(killer_count));
    tt.reserve(1 << 20);
}

bool Searcher::time_up() const {
    if (time_limit < 0) return false;
    return (now() - start_time) >= time_limit;
}

void Searcher::record_opponent_move_time(double seconds) {
    if (seconds <= 0) return;
    int idx = opponent_move_count % 200;
    opponent_move_times[idx] = seconds;
    opponent_move_count++;
}

std::pair<double,double> Searcher::allocate_time(int time_budget_ms,
                                                   int inc_ms,
                                                   int moves_to_go,
                                                   int fullmove_number) const {
    if (time_budget_ms <= 0) return {-1.0, -1.0};

    double remaining_secs = time_budget_ms / 1000.0;
    double inc_secs       = inc_ms / 1000.0;

    int estimated_moves;
    if      (fullmove_number < 10)  estimated_moves = 40;
    else if (fullmove_number < 30)  estimated_moves = 30;
    else if (fullmove_number < 50)  estimated_moves = 20;
    else                            estimated_moves = 15;

    if (moves_to_go > 0) estimated_moves = moves_to_go;

    double soft     = (remaining_secs / estimated_moves) + inc_secs * 0.8;
    double max_soft = remaining_secs * 0.10;
    soft = std::min(soft, max_soft);
    soft = std::max(soft, 0.1);

    double hard = std::min(soft * 3.0, remaining_secs * 0.20);
    hard = std::max(hard, soft + 0.1);

    return {soft, hard};
}

Move Searcher::find_best_move(const Board& board, int max_depth,
                               double time_limit_secs,
                               int    time_budget_ms,
                               int    inc_ms,
                               int    moves_to_go) {
    nodes_searched = 0;
    tt_hits        = 0;
    start_time     = now();
    time_limit     = -1;
    soft_limit     = -1;
    in_null_move   = false;

    if (last_go_time > 0) {
        double opponent_time = start_time - last_go_time;
        record_opponent_move_time(opponent_time);
    }
    last_go_time = start_time;

    if (time_limit_secs > 0) {
        time_limit = time_limit_secs;
        soft_limit = time_limit_secs;
    } else if (time_budget_ms > 0) {
        auto [soft, hard] = allocate_time(
            time_budget_ms, inc_ms, moves_to_go, board.fullmove_number
        );
        soft_limit = soft;
        time_limit = hard;
        std::cerr << "  [Time] Budget=" << time_budget_ms
                  << "ms  Soft=" << soft << "s  Hard=" << hard << "s\n";
    }

    std::memset(history, 0, sizeof(history));
    std::memset(killer_count, 0, sizeof(killer_count));

    Hash current_hash = compute_hash(board);
    Move best_move(0, 0);
    int  best_score = 0;

    for (int depth = 1; depth <= max_depth; depth++) {
        if (soft_limit > 0 && (now() - start_time) >= soft_limit) break;
        if (time_up()) break;

        auto [move, score] = search_root(board, current_hash, depth);

        if (move.from_sq != move.to_sq || move.from_sq != 0) {
            best_move  = move;
            best_score = score;
        }

        double elapsed    = now() - start_time;
        int    elapsed_ms = (int)(elapsed * 1000);
        int    nps        = (elapsed > 0) ? (int)(nodes_searched / elapsed) : 0;

        std::cout << "info depth "  << depth
                  << " score cp "   << best_score
                  << " nodes "      << nodes_searched
                  << " nps "        << nps
                  << " time "       << elapsed_ms
                  << " pv "         << best_move.to_uci()
                  << "\n";
        std::cout.flush();

        if (time_limit > 0 && elapsed > time_limit * 0.5) break;
    }

    return best_move;
}

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
        Board new_board = apply_move(board, move);
        Hash  new_hash  = update_hash(hash, board, move, new_board);
        int   score     = -alphabeta(new_board, new_hash,
                                     depth - 1, -beta, -alpha, 1);
        if (score > best_score) { best_score = score; best_move = move; }
        alpha = std::max(alpha, score);
    }

    tt_store(hash, depth, best_score, TT_EXACT, best_move);
    return {best_move, best_score};
}

int Searcher::alphabeta(const Board& board, Hash hash,
                         int depth, int alpha, int beta, int ply) {
    nodes_searched++;

    const Move* tt_move = nullptr;
    Move        tt_move_storage(0, 0);

    auto it = tt.find(hash);
    if (it != tt.end()) {
        const TTEntry& entry = it->second;
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

    if (depth == 0)
        return quiescence(board, hash, alpha, beta);

    auto moves = generate_legal_moves(board);

    // ── No legal moves: checkmate or stalemate ──
    if (moves.empty()) {
        if (king_in_check(board, board.turn))
            return -(CHECKMATE_SCORE - ply);
        return 0;
    }

    bool in_check = king_in_check(board, board.turn);

    // ── Null Move Pruning ──
    // Skip our turn and search at reduced depth. If the result is still
    // >= beta even when we do nothing, prune — the position is too good
    // for us for the opponent to do anything about it.
    // Skipped when: already in a null search, in check, or depth too low.
    if (!in_null_move
        && depth >= NULL_MOVE_MIN_DEPTH
        && !in_check)
    {
        Board null_board         = board;
        null_board.turn          = -board.turn;
        null_board.en_passant_sq = -1;
        Hash null_hash           = compute_hash(null_board);

        in_null_move = true;
        int null_score = -alphabeta(null_board, null_hash,
                                    depth - 1 - NULL_MOVE_REDUCTION,
                                    -beta, -beta + 1, ply + 1);
        in_null_move = false;

        if (null_score >= beta)
            return beta;
    }

    moves = order_moves(board, moves, ply, tt_move);

    int  original_alpha = alpha;
    Move best_move(0, 0);
    bool found_best = false;

    for (int move_idx = 0; move_idx < (int)moves.size(); move_idx++) {
        const Move& move = moves[move_idx];
        if (time_up()) break;

        Board new_board  = apply_move(board, move);
        Hash  new_hash   = update_hash(hash, board, move, new_board);
        bool  is_capture = !board.is_empty(move.to_sq)
                           || move.to_sq == board.en_passant_sq;
        bool  is_promo   = (move.promotion != 0);
        int   score;

        // ── Late Move Reductions (LMR) ──
        // Later moves in the ordered list are probably weaker.
        // Search them at reduced depth — if they still look interesting,
        // re-search at full depth to confirm.
        // Skipped for: early moves, captures, promotions, and checks.
        if (move_idx >= LMR_MIN_MOVE_INDEX
            && depth  >= LMR_MIN_DEPTH
            && !in_check
            && !is_capture
            && !is_promo)
        {
            // Reduction grows for later moves and deeper searches
            int R = 1 + (move_idx >= 6 ? 1 : 0)
                      + (depth    >= 6 ? 1 : 0);

            // Reduced-depth search with a zero window
            score = -alphabeta(new_board, new_hash,
                               depth - 1 - R, -alpha - 1, -alpha, ply + 1);

            // If it beat alpha, confirm with full depth
            if (score > alpha)
                score = -alphabeta(new_board, new_hash,
                                   depth - 1, -beta, -alpha, ply + 1);
        } else {
            score = -alphabeta(new_board, new_hash,
                               depth - 1, -beta, -alpha, ply + 1);
        }

        if (score >= beta) {
            update_killers(move, ply);
            update_history(board, move, depth);
            tt_store(hash, depth, beta, TT_LOWER_BOUND, move);
            return beta;
        }
        if (score > alpha) { alpha = score; best_move = move; found_best = true; }
    }

    if (found_best) {
        int flag = (alpha > original_alpha) ? TT_EXACT : TT_UPPER_BOUND;
        tt_store(hash, depth, alpha, flag, best_move);
    }

    return alpha;
}

int Searcher::quiescence(const Board& board, Hash hash, int alpha, int beta) {
    nodes_searched++;

    int stand_pat = score_from_perspective(board);
    if (stand_pat >= beta) return beta;
    alpha = std::max(alpha, stand_pat);

    auto moves = generate_legal_moves(board);
    std::vector<Move> captures;
    captures.reserve(8);
    for (const Move& m : moves)
        if (!board.is_empty(m.to_sq) || m.to_sq == board.en_passant_sq)
            captures.push_back(m);

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

std::vector<Move> Searcher::order_moves(const Board& board,
                                         std::vector<Move>& moves,
                                         int ply,
                                         const Move* tt_move) {
    std::vector<std::pair<int, Move>> scored;
    scored.reserve(moves.size());

    for (const Move& move : moves) {
        int score = 0;
        if (tt_move && move == *tt_move) {
            score = 20000;
        } else {
            int target = board.get_piece(move.to_sq);
            if (target != EMPTY) {
                int victim_val   = std::abs(target) * 10;
                int attacker_val = std::abs(board.get_piece(move.from_sq));
                score = 10000 + victim_val - attacker_val;
            } else if (move.to_sq == board.en_passant_sq) {
                score = 10000;
            } else if (ply < MAX_DEPTH) {
                if (killer_count[ply] > 0 && move == killers[ply][0])
                    score = 9000;
                else if (killer_count[ply] > 1 && move == killers[ply][1])
                    score = 8999;
                else
                    score = history[move.from_sq][move.to_sq];
            }
        }
        scored.push_back({score, move});
    }

    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<Move> ordered;
    ordered.reserve(scored.size());
    for (auto& [s, m] : scored) ordered.push_back(m);
    return ordered;
}

void Searcher::update_killers(const Move& move, int ply) {
    if (ply >= MAX_DEPTH) return;
    if (killer_count[ply] > 0 && killers[ply][0] == move) return;
    if (killer_count[ply] > 1 && killers[ply][1] == move) return;
    killers[ply][1] = killers[ply][0];
    killers[ply][0] = move;
    if (killer_count[ply] < 2) killer_count[ply]++;
}

void Searcher::update_history(const Board& board, const Move& move, int depth) {
    if (board.is_empty(move.to_sq))
        history[move.from_sq][move.to_sq] += depth * depth;
}

void Searcher::tt_store(Hash hash, int depth, int score,
                         int flag, const Move& move) {
    if (tt.size() >= 1000000) tt.clear();
    tt[hash] = TTEntry(hash, depth, score, flag, move);
}

int Searcher::score_from_perspective(const Board& board) {
    int score = evaluate(board);
    return (board.turn == WHITE) ? score : -score;
}
