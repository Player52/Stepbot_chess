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
    std::memset(history,       0, sizeof(history));
    std::memset(cont_history,  0, sizeof(cont_history));
    std::memset(killer_count,  0, sizeof(killer_count));

    // Initialise countermove table with null moves
    for (int pt = 0; pt < 6; pt++)
        for (int sq = 0; sq < 64; sq++)
            countermove[pt][sq] = Move(0, 0);

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

    // Estimate moves remaining — be conservative to preserve time
    int estimated_moves;
    if      (fullmove_number < 10)  estimated_moves = 45;
    else if (fullmove_number < 20)  estimated_moves = 35;
    else if (fullmove_number < 35)  estimated_moves = 25;
    else if (fullmove_number < 50)  estimated_moves = 18;
    else                            estimated_moves = 12;

    if (moves_to_go > 0) estimated_moves = moves_to_go;

    // Base time per move
    double soft = (remaining_secs / estimated_moves) + inc_secs * 0.7;

    // Hard cap: never spend more than 8% of remaining time on one move
    // (down from 10% — more conservative to avoid flagging)
    double max_soft = remaining_secs * 0.08;
    soft = std::min(soft, max_soft);
    soft = std::max(soft, 0.1);   // At least 100ms

    // Hard limit: 2.5x soft, capped at 15% of remaining
    // (tighter than before — prevents runaway searches)
    double hard = std::min(soft * 2.5, remaining_secs * 0.15);
    hard = std::max(hard, soft + 0.05);

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

    std::memset(history,      0, sizeof(history));
    std::memset(cont_history, 0, sizeof(cont_history));
    std::memset(killer_count, 0, sizeof(killer_count));
    for (int pt = 0; pt < 6; pt++)
        for (int sq = 0; sq < 64; sq++)
            countermove[pt][sq] = Move(0, 0);

    Hash current_hash = compute_hash(board);
    Move best_move(0, 0);
    int  best_score = 0;

    for (int depth = 1; depth <= max_depth; depth++) {
        if (soft_limit > 0 && (now() - start_time) >= soft_limit) break;
        if (time_up()) break;

        Move iter_move(0, 0);
        int  iter_score = 0;

        // ── Aspiration Windows ──
        // From depth 4 upwards, search with a narrow window around the
        // previous score. If the result falls outside, widen and retry.
        // At depth 1-3 just do a full-width search — not worth the overhead.
        if (depth >= ASPIRATION_MIN_DEPTH && best_score != 0) {
            int delta = ASPIRATION_INITIAL_DELTA;
            int alpha = best_score - delta;
            int beta  = best_score + delta;

            while (true) {
                auto [move, score] = search_root(board, current_hash,
                                                 depth, best_score);
                iter_move  = move;
                iter_score = score;

                if (score <= alpha) {
                    // Failed low — result was below our window
                    // Widen the lower bound and retry
                    alpha -= delta;
                    delta *= 2;
                } else if (score >= beta) {
                    // Failed high — result was above our window
                    // Widen the upper bound and retry
                    beta  += delta;
                    delta *= 2;
                } else {
                    // Result fit inside the window — done
                    break;
                }

                // Safety: if window has grown very wide, just go full width
                if (alpha < -CHECKMATE_SCORE / 2) alpha = -CHECKMATE_SCORE - 1;
                if (beta  >  CHECKMATE_SCORE / 2) beta  =  CHECKMATE_SCORE + 1;

                // If we've blown out to a full window, stop resizing
                if (alpha <= -CHECKMATE_SCORE && beta >= CHECKMATE_SCORE) break;

                if (time_up()) break;
            }
        } else {
            // Depth 1-3: full window search
            auto [move, score] = search_root(board, current_hash,
                                             depth, best_score);
            iter_move  = move;
            iter_score = score;
        }

        if (iter_move.from_sq != iter_move.to_sq || iter_move.from_sq != 0) {
            best_move  = iter_move;
            best_score = iter_score;
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

        // Hard stop — never start a new depth if we're already over time
        if (time_up()) break;
    }

    return best_move;
}

std::pair<Move, int> Searcher::search_root(const Board& board,
                                            Hash hash, int depth,
                                            int prev_score) {
    auto moves = generate_legal_moves(board);
    if (moves.empty()) return {Move(0, 0), 0};

    moves = order_moves(board, moves, 0);

    Move best_move  = moves[0];
    int  best_score = -CHECKMATE_SCORE - 1;
    int  alpha      = -CHECKMATE_SCORE - 1;
    int  beta       =  CHECKMATE_SCORE + 1;

    (void)prev_score;

    for (int i = 0; i < (int)moves.size(); i++) {
        const Move& move = moves[i];
        Board new_board = apply_move(board, move);
        Hash  new_hash  = update_hash(hash, board, move, new_board);
        int   score;

        if (i == 0) {
            score = -alphabeta(new_board, new_hash,
                               depth - 1, -beta, -alpha, 1, move);
        } else {
            score = -alphabeta(new_board, new_hash,
                               depth - 1, -alpha - 1, -alpha, 1, move);
            if (score > alpha && score < beta)
                score = -alphabeta(new_board, new_hash,
                                   depth - 1, -beta, -alpha, 1, move);
        }

        if (score > best_score) { best_score = score; best_move = move; }
        alpha = std::max(alpha, score);
        if (time_up()) break;
    }

    tt_store(hash, depth, best_score, TT_EXACT, best_move);
    return {best_move, best_score};
}

int Searcher::alphabeta(const Board& board, Hash hash,
                         int depth, int alpha, int beta, int ply,
                         Move prev_move) {
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

    if (moves.empty()) {
        if (king_in_check(board, board.turn))
            return -(CHECKMATE_SCORE - ply);
        return 0;
    }

    bool in_check = king_in_check(board, board.turn);

    // ── Check Extension ──
    // If we're in check, extend depth by 1 — checks are forcing and
    // tactical sequences involving checks must be fully resolved.
    if (in_check)
        depth += CHECK_EXTENSION;

    // ── Futility Pruning ──
    if (depth >= 1 && depth <= 3
        && !in_check
        && alpha > -CHECKMATE_SCORE / 2
        && beta  <  CHECKMATE_SCORE / 2)
    {
        int static_eval = score_from_perspective(board);
        if (static_eval + FUTILITY_MARGIN[depth] <= alpha)
            return quiescence(board, hash, alpha, beta);
    }

    // ── Null Move Pruning ──
    if (!in_null_move
        && depth >= 4
        && !in_check
        && beta  <  CHECKMATE_SCORE
        && alpha > -CHECKMATE_SCORE)
    {
        Hash null_hash = hash ^ BLACK_TO_MOVE;
        if (board.en_passant_sq != -1)
            null_hash ^= EN_PASSANT_RANDOM[file_of(board.en_passant_sq)];

        Board null_board         = board;
        null_board.turn          = -board.turn;
        null_board.en_passant_sq = -1;

        int R          = (depth >= 6) ? 3 : 2;
        int null_depth = std::max(1, depth - 1 - R);

        in_null_move = true;
        int null_score = -alphabeta(null_board, null_hash,
                                    null_depth,
                                    -beta, -beta + 1, ply + 1,
                                    Move(0, 0));
        in_null_move = false;

        if (null_score >= beta)
            return beta;
    }

    // Get countermove for move ordering (looked up in order_moves via prev_move)
    moves = order_moves(board, moves, ply, tt_move, &prev_move);

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
        bool  gives_check = king_in_check(new_board, new_board.turn);
        int   score;

        if (move_idx == 0) {
            if (move_idx >= LMR_MIN_MOVE_INDEX
                && depth  >= LMR_MIN_DEPTH
                && !in_check && !is_capture && !is_promo && !gives_check)
            {
                int R = 1 + (move_idx >= 6 ? 1 : 0) + (depth >= 6 ? 1 : 0);
                score = -alphabeta(new_board, new_hash,
                                   depth - 1 - R, -alpha - 1, -alpha,
                                   ply + 1, move);
                if (score > alpha)
                    score = -alphabeta(new_board, new_hash,
                                       depth - 1, -beta, -alpha,
                                       ply + 1, move);
            } else {
                score = -alphabeta(new_board, new_hash,
                                   depth - 1, -beta, -alpha,
                                   ply + 1, move);
            }
        } else {
            int search_depth = depth - 1;

            if (move_idx >= LMR_MIN_MOVE_INDEX
                && depth  >= LMR_MIN_DEPTH
                && !in_check && !is_capture && !is_promo && !gives_check)
            {
                int R = 1 + (move_idx >= 6 ? 1 : 0) + (depth >= 6 ? 1 : 0);
                search_depth = depth - 1 - R;
            }

            score = -alphabeta(new_board, new_hash,
                               search_depth, -alpha - 1, -alpha,
                               ply + 1, move);

            if (score > alpha && (search_depth < depth - 1 || score < beta))
                score = -alphabeta(new_board, new_hash,
                                   depth - 1, -beta, -alpha,
                                   ply + 1, move);
        }

        if (score >= beta) {
            update_killers(move, ply);
            update_history(board, move, depth, prev_move);
            update_countermove(prev_move, move, board);
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
                                         const Move* tt_move,
                                         const Move* prev_move) {
    // Get prev move piece type for continuation history lookup
    int prev_pt = 0, prev_to = 0;
    bool has_prev = prev_move
                    && (prev_move->from_sq != prev_move->to_sq
                        || prev_move->from_sq != 0);
    if (has_prev) {
        int pp = board.get_piece(prev_move->to_sq);
        prev_pt = std::abs(pp);
        prev_to = prev_move->to_sq;
    }

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
                else {
                    // History score
                    score = history[move.from_sq][move.to_sq];

                    // Continuation history bonus — how good is this move
                    // given the previous move?
                    if (has_prev && prev_pt >= 1 && prev_pt <= 6) {
                        int curr_pt = std::abs(board.get_piece(move.from_sq));
                        if (curr_pt >= 1 && curr_pt <= 6) {
                            score += cont_history[prev_pt-1][prev_to]
                                                 [curr_pt-1][move.to_sq];
                        }
                    }

                    // Countermove bonus
                    if (has_prev) {
                        int pp = std::abs(board.get_piece(prev_move->to_sq));
                        if (pp >= 1 && pp <= 6) {
                            if (countermove[pp-1][prev_move->to_sq] == move)
                                score += 5000;
                        }
                    }
                }
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

void Searcher::update_history(const Board& board, const Move& move,
                               int depth, const Move& prev_move) {
    if (board.is_empty(move.to_sq)) {
        history[move.from_sq][move.to_sq] += depth * depth;
        update_cont_history(prev_move, move, board, depth);
    }
}

void Searcher::update_cont_history(const Move& prev_move, const Move& move,
                                    const Board& board, int depth) {
    // Only update if we have a valid previous move
    if (prev_move.from_sq == prev_move.to_sq && prev_move.from_sq == 0)
        return;

    int prev_piece = std::abs(board.get_piece(prev_move.to_sq));
    int curr_piece = std::abs(board.get_piece(move.from_sq));

    if (prev_piece < 1 || prev_piece > 6) return;
    if (curr_piece < 1 || curr_piece > 6) return;

    cont_history[prev_piece-1][prev_move.to_sq]
                [curr_piece-1][move.to_sq] += depth * depth;
}

void Searcher::update_countermove(const Move& prev_move, const Move& response,
                                   const Board& board) {
    if (prev_move.from_sq == prev_move.to_sq && prev_move.from_sq == 0)
        return;

    int prev_piece = std::abs(board.get_piece(prev_move.to_sq));
    if (prev_piece < 1 || prev_piece > 6) return;

    countermove[prev_piece-1][prev_move.to_sq] = response;
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
