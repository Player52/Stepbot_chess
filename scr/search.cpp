// search.cpp
// Alpha-beta search implementation.
// C++ equivalent of search.py.

#include "search.h"
#include "stepbot_live_writer.h"
#include <algorithm>
#include <iostream>
#include <chrono>
#include <cstring>

static double now() {
    using namespace std::chrono;
    auto t = steady_clock::now().time_since_epoch();
    return duration_cast<duration<double>>(t).count();
}

// Mate scores depend on root distance (ply). TT stores ply-independent encodings.
static constexpr int MATE_VALUE_TT = CHECKMATE_SCORE - MAX_DEPTH - 32;
static constexpr int QCHECK_MAX    = 2;

static inline int value_to_tt(int v, int ply) {
    if (v >= MATE_VALUE_TT)  return v + ply;
    if (v <= -MATE_VALUE_TT) return v - ply;
    return v;
}

static inline int value_from_tt(int v, int ply) {
    if (v >= MATE_VALUE_TT)  return v - ply;
    if (v <= -MATE_VALUE_TT) return v + ply;
    return v;
}

static inline bool window_has_mate_bounds(int alpha, int beta) {
    return alpha >= MATE_VALUE_TT || beta <= -MATE_VALUE_TT;
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
    std::memset(pv_length,     0, sizeof(pv_length));
    std::memset(search_stack,  0, sizeof(search_stack));

    // Initialise countermove table with null moves
    for (int pt = 0; pt < 6; pt++)
        for (int sq = 0; sq < 64; sq++)
            countermove[pt][sq] = Move(0, 0);

    tt.resize(TT_NUM_SLOTS);
}

void Searcher::tt_new_game() {
    ++tt_generation;
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
                               int    moves_to_go,
                               const std::vector<Hash>& history) {
    position_history = history;
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

    std::memset(this->history,  0, sizeof(this->history));
    std::memset(cont_history, 0, sizeof(cont_history));
    std::memset(killer_count, 0, sizeof(killer_count));
    std::memset(pv_length,    0, sizeof(pv_length));
    for (int pt = 0; pt < 6; pt++)
        for (int sq = 0; sq < 64; sq++)
            countermove[pt][sq] = Move(0, 0);

    Hash current_hash = compute_hash(board);
    search_stack[0]  = current_hash;
    Move best_move(0, 0);
    int  best_score = 0;

    // Make a mutable copy of the board for the search
    Board search_board = board;

    for (int depth = 1; depth <= max_depth; depth++) {
        if (soft_limit > 0 && (now() - start_time) >= soft_limit) break;
        if (time_up()) break;

        Move iter_move(0, 0);
        int  iter_score = 0;

        if (depth >= ASPIRATION_MIN_DEPTH && best_score != 0) {
            int delta = ASPIRATION_INITIAL_DELTA;
            int alpha = best_score - delta;
            int beta  = best_score + delta;

            while (true) {
                auto [move, score] = search_root(search_board, current_hash,
                                                 depth, best_score);
                iter_move  = move;
                iter_score = score;

                if (score <= alpha) {
                    alpha -= delta;
                    delta *= 2;
                } else if (score >= beta) {
                    beta  += delta;
                    delta *= 2;
                } else {
                    break;
                }

                if (alpha < -CHECKMATE_SCORE / 2) alpha = -CHECKMATE_SCORE - 1;
                if (beta  >  CHECKMATE_SCORE / 2) beta  =  CHECKMATE_SCORE + 1;
                if (alpha <= -CHECKMATE_SCORE && beta >= CHECKMATE_SCORE) break;
                if (time_up()) break;
            }
        } else {
            auto [move, score] = search_root(search_board, current_hash,
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

        // Build PV string from pv_table
        std::string pv_str;
        int pv_len = pv_length[0];
        for (int i = 0; i < pv_len && i < MAX_DEPTH; i++) {
            if (i > 0) pv_str += ' ';
            pv_str += pv_table[0][i].to_uci();
        }
        if (pv_str.empty()) pv_str = best_move.to_uci();

        std::cout << "info depth "  << depth
                  << " score cp "   << best_score
                  << " nodes "      << nodes_searched
                  << " nps "        << nps
                  << " time "       << elapsed_ms
                  << " pv "         << pv_str
                  << "\n";
        std::cout.flush();

        // Update screensaver live file
        LiveWriter::update(search_board, depth, best_score, (long long)nps, pv_str);

        // Hard stop — never start a new depth if we're already over time
        if (time_up()) break;
    }

    return best_move;
}

std::pair<Move, int> Searcher::search_root(Board& board,
                                            Hash hash, int depth,
                                            int prev_score) {
    auto moves = generate_legal_moves(board);
    if (moves.empty()) return {Move(0, 0), 0};

    pv_length[0] = 0;
    moves = order_moves(board, moves, 0);

    Move best_move  = moves[0];
    int  best_score = -CHECKMATE_SCORE - 1;
    int  alpha      = -CHECKMATE_SCORE - 1;
    int  beta       =  CHECKMATE_SCORE + 1;

    (void)prev_score;

    for (int i = 0; i < (int)moves.size(); i++) {
        const Move& move = moves[i];
        pv_length[1] = 0;

        UndoInfo undo = make_move(board, move);
        Hash     new_hash = update_hash(hash, board, move,
                                        undo.en_passant_sq,
                                        undo.castling_rights,
                                        undo.captured_piece);
        int score;

        if (i == 0) {
            score = -alphabeta(board, new_hash,
                               depth - 1, -beta, -alpha, 1, move);
        } else {
            score = -alphabeta(board, new_hash,
                               depth - 1, -alpha - 1, -alpha, 1, move);
            if (score > alpha && score < beta)
                score = -alphabeta(board, new_hash,
                                   depth - 1, -beta, -alpha, 1, move);
        }

        unmake_move(board, move, undo);

        if (score > best_score) {
            best_score = score;
            best_move  = move;
            pv_table[0][0] = move;
            int child_len = pv_length[1];
            if (child_len > MAX_DEPTH - 1) child_len = MAX_DEPTH - 1;
            for (int j = 0; j < child_len; j++)
                pv_table[0][j + 1] = pv_table[1][j];
            pv_length[0] = child_len + 1;
        }
        alpha = std::max(alpha, score);
        if (time_up()) break;
    }

    tt_store(hash, depth, best_score, TT_EXACT, best_move, 0);
    return {best_move, best_score};
}

int Searcher::alphabeta(Board& board, Hash hash,
                         int depth, int alpha, int beta, int ply,
                         Move prev_move) {
    nodes_searched++;

    // Initialise PV length for this ply to 0 moves beyond this point
    if (ply < MAX_DEPTH) pv_length[ply] = 0;

    // ── Repetition Detection ──
    // Check game history AND the current search path.
    // If this hash appears anywhere in either, this visit is a repetition.
    if (ply > 0 && ply < MAX_DEPTH) {
        int rep_count = 0;
        for (Hash h : position_history)
            if (h == hash) rep_count++;
        for (int i = 0; i < ply; i++)
            if (search_stack[i] == hash) rep_count++;
        if (rep_count >= 1)
            return REPETITION_SCORE;  // Never TT-cache this
    }

    // Record this position on the search path (cleared on return)
    if (ply < MAX_DEPTH) search_stack[ply] = hash;
    struct StackGuard {
        Hash* slot;
        ~StackGuard() { if (slot) *slot = 0; }
    } guard{ ply < MAX_DEPTH ? &search_stack[ply] : nullptr };

    const Move* tt_move = nullptr;
    Move        tt_move_storage(0, 0);

    // TT lookup — skipped for positions already flagged as repetitions above
    size_t      tt_idx = hash & (TT_NUM_SLOTS - 1);
    const TTSlot& slot = tt[tt_idx];
    if (slot.gen == tt_generation && slot.hash == hash) {
        if (slot.depth >= depth) {
            tt_hits++;
            int v = value_from_tt(slot.score, ply);
            if (slot.flag == TT_EXACT)       return v;
            if (slot.flag == TT_LOWER_BOUND) alpha = std::max(alpha, v);
            if (slot.flag == TT_UPPER_BOUND) beta  = std::min(beta,  v);
            if (alpha >= beta)               return v;
        }
        tt_move_storage = slot.move;
        tt_move         = &tt_move_storage;
    }

    // In check at the horizon: search one full-width ply of evasions (never
    // drop straight into capture-only quiescence while in check).
    if (depth <= 0) {
        if (!king_in_check(board, board.turn))
            return quiescence(board, hash, alpha, beta, ply);
        depth = 1;
    }

    auto moves = generate_legal_moves(board);

    if (moves.empty()) {
        if (king_in_check(board, board.turn))
            return -(CHECKMATE_SCORE - ply);
        return 0;
    }

    bool in_check = king_in_check(board, board.turn);

    // ── Futility Pruning ──
    if (depth >= 1 && depth <= 3
        && !in_check
        && alpha > -CHECKMATE_SCORE / 2
        && beta  <  CHECKMATE_SCORE / 2
        && !window_has_mate_bounds(alpha, beta))
    {
        int static_eval = score_from_perspective(board);
        if (static_eval + FUTILITY_MARGIN[depth] <= alpha)
            return quiescence(board, hash, alpha, beta, ply);
    }

    // ── Null Move Pruning ──
    if (!in_null_move
        && depth >= 4
        && !in_check
        && beta  <  CHECKMATE_SCORE
        && alpha > -CHECKMATE_SCORE
        && !window_has_mate_bounds(alpha, beta))
    {
        in_null_move = true;
        Board null_board         = board;
        null_board.turn          = -board.turn;
        null_board.en_passant_sq = -1;
        Hash null_hash = hash ^ BLACK_TO_MOVE;
        if (board.en_passant_sq != -1)
            null_hash ^= EN_PASSANT_RANDOM[file_of(board.en_passant_sq)];

        int R          = (depth >= 6) ? 3 : 2;
        int null_depth = std::max(1, depth - 1 - R);

        int null_score = -alphabeta(null_board, null_hash,
                                    null_depth,
                                    -beta, -beta + 1, ply + 1,
                                    Move(0, 0));
        in_null_move = false;

        if (null_score >= beta)
            return beta;
    }

    // ── Probcut ──
    // If a capture is very likely to beat beta even at reduced depth,
    // prune the rest of the search early.
    // Only applies at higher depths where the overhead is worth it.
    if (depth >= PROBCUT_DEPTH
        && !in_check
        && std::abs(beta) < CHECKMATE_SCORE / 2)
    {
        int probcut_beta  = beta + PROBCUT_MARGIN;
        int probcut_depth = std::max(1, depth - 4);

        for (const Move& m : moves) {
            if (board.is_empty(m.to_sq) && m.to_sq != board.en_passant_sq)
                continue;

            UndoInfo pc_undo = make_move(board, m);
            Hash     ph      = update_hash(hash, board, m,
                                           pc_undo.en_passant_sq,
                                           pc_undo.castling_rights,
                                           pc_undo.captured_piece);

            int pc_score = -alphabeta(board, ph, probcut_depth,
                                      -probcut_beta, -probcut_beta + 1,
                                      ply + 1, m);
            unmake_move(board, m, pc_undo);

            if (pc_score >= probcut_beta)
                return beta;
        }
    }

    // Get countermove for move ordering (looked up in order_moves via prev_move)
    moves = order_moves(board, moves, ply, tt_move, &prev_move);

    int  original_alpha = alpha;
    Move best_move(0, 0);
    bool found_best = false;
    int  quiet_count = 0;  // LMP: count of quiet moves searched

    for (int move_idx = 0; move_idx < (int)moves.size(); move_idx++) {
        const Move& move = moves[move_idx];
        if (time_up()) break;

        bool is_capture  = !board.is_empty(move.to_sq)
                           || move.to_sq == board.en_passant_sq;
        bool is_promo    = (move.promotion != 0);

        // ── Late Move Pruning (LMP) ──
        if (depth >= 1 && depth <= 3
            && !in_check
            && !is_capture && !is_promo
            && move_idx > 0
            && !window_has_mate_bounds(alpha, beta)
            && quiet_count >= LMP_THRESHOLD[depth])
        {
            continue;
        }

        UndoInfo undo    = make_move(board, move);
        Hash     new_hash = update_hash(hash, board, move,
                                        undo.en_passant_sq,
                                        undo.castling_rights,
                                        undo.captured_piece);

        bool gives_check = king_in_check(board, board.turn);

        // ── Singular Extensions ──
        int extension = 0;
        if (move_idx == 0
            && tt_move && move == *tt_move
            && depth >= SE_DEPTH_LIMIT
            && !in_check
            && std::abs(beta) < CHECKMATE_SCORE / 2)
        {
            unmake_move(board, move, undo);  // temporarily restore for is_singular
            if (is_singular(board, hash, move, depth, ply, beta))
                extension = 1;
            undo = make_move(board, move);   // re-apply
            new_hash = update_hash(hash, board, move,
                                   undo.en_passant_sq,
                                   undo.castling_rights,
                                   undo.captured_piece);
        }

        // ── Recapture Extension ──
        if (extension == 0
            && is_capture
            && prev_move.from_sq != prev_move.to_sq
            && move.to_sq == prev_move.to_sq)
        {
            extension = 1;
        }

        // ── Passed Pawn Extension ──
        if (extension == 0 && !is_capture) {
            // Check the piece on from_sq before the move — use undo to recover
            // The pawn was there before make_move; board.turn is now flipped
            int orig_colour = -board.turn;
            int to_rank = move.to_sq / 8;
            // We need to know if a pawn moved — check promotion or pawn type
            // Since the piece is now on to_sq, check it directly
            int moved_pt = std::abs(board.get_piece(move.to_sq));
            if (move.promotion) moved_pt = PAWN;  // was a pawn before promo
            if (moved_pt == PAWN) {
                if ((orig_colour == WHITE && to_rank == 6) ||
                    (orig_colour == BLACK && to_rank == 1))
                    extension = 1;
            }
        }

        // Check extension
        if (gives_check && extension == 0)
            extension = CHECK_EXTENSION;

        int search_depth_ext = depth - 1 + extension;
        int score;

        bool is_bad_capture = is_capture &&
                              static_exchange_eval(board, move) < -50;
        if (!is_capture && !is_promo) quiet_count++;

        bool allow_lmr = !window_has_mate_bounds(alpha, beta)
                         && !is_bad_capture;

        if (move_idx == 0) {
            if (allow_lmr
                && move_idx >= LMR_MIN_MOVE_INDEX
                && depth  >= LMR_MIN_DEPTH
                && !in_check && !is_capture && !is_promo && !gives_check)
            {
                int R = 1 + (move_idx >= 6 ? 1 : 0) + (depth >= 6 ? 1 : 0);
                score = -alphabeta(board, new_hash,
                                   search_depth_ext - R, -alpha - 1, -alpha,
                                   ply + 1, move);
                if (score > alpha)
                    score = -alphabeta(board, new_hash,
                                       search_depth_ext, -beta, -alpha,
                                       ply + 1, move);
            } else {
                score = -alphabeta(board, new_hash,
                                   search_depth_ext, -beta, -alpha,
                                   ply + 1, move);
            }
        } else {
            int search_depth = search_depth_ext;
            if (allow_lmr
                && move_idx >= LMR_MIN_MOVE_INDEX
                && depth  >= LMR_MIN_DEPTH
                && !in_check && !is_capture && !is_promo && !gives_check)
            {
                int R = 1 + (move_idx >= 6 ? 1 : 0) + (depth >= 6 ? 1 : 0);
                search_depth = search_depth_ext - R;
            }
            score = -alphabeta(board, new_hash,
                               search_depth, -alpha - 1, -alpha,
                               ply + 1, move);
            if (score > alpha && (search_depth < search_depth_ext || score < beta))
                score = -alphabeta(board, new_hash,
                                   search_depth_ext, -beta, -alpha,
                                   ply + 1, move);
        }

        unmake_move(board, move, undo);

        if (score >= beta) {
            if (!is_capture)
                update_killers(move, ply);
            update_history(board, move, depth, prev_move);
            update_countermove(prev_move, move, board);
            tt_store(hash, depth, beta, TT_LOWER_BOUND, move, ply);
            return beta;
        }
        if (score > alpha) {
            alpha      = score;
            best_move  = move;
            found_best = true;
            if (ply < MAX_DEPTH) {
                pv_table[ply][0] = move;
                int child_len = pv_length[ply + 1];
                for (int i = 0; i < child_len; i++)
                    pv_table[ply][i + 1] = pv_table[ply + 1][i];
                pv_length[ply] = child_len + 1;
            }
        }
    }

    if (found_best) {
        int flag = (alpha > original_alpha) ? TT_EXACT : TT_UPPER_BOUND;
        tt_store(hash, depth, alpha, flag, best_move, ply);
    }

    return alpha;
}

int Searcher::quiescence(Board& board, Hash hash, int alpha, int beta,
                          int ply, int qcheck_depth) {
    nodes_searched++;

    bool in_check = king_in_check(board, board.turn);

    int stand_pat = 0;
    if (!in_check) {
        stand_pat = score_from_perspective(board);
        if (stand_pat >= beta) return beta;
        alpha = std::max(alpha, stand_pat);

        if (stand_pat + DELTA_MAX_GAIN + DELTA_MARGIN <= alpha
            && alpha < CHECKMATE_SCORE / 2)
        {
            return alpha;
        }
    }

    auto moves = generate_legal_moves(board);

    if (in_check) {
        moves = order_moves(board, moves, 0);
        if (moves.empty())
            return -(CHECKMATE_SCORE - ply);
        for (const Move& move : moves) {
            UndoInfo undo    = make_move(board, move);
            Hash     new_hash = update_hash(hash, board, move,
                                            undo.en_passant_sq,
                                            undo.castling_rights,
                                            undo.captured_piece);
            int score = -quiescence(board, new_hash, -beta, -alpha,
                                    ply + 1, qcheck_depth);
            unmake_move(board, move, undo);
            if (score >= beta) return beta;
            alpha = std::max(alpha, score);
        }
        return alpha;
    }

    struct Tactical {
        int  see_score;
        Move m;
        bool is_capture;
    };
    std::vector<Tactical> tactical;
    tactical.reserve(moves.size());

    for (const Move& m : moves) {
        bool capture = !board.is_empty(m.to_sq) || m.to_sq == board.en_passant_sq;
        int  mover   = board.get_piece(m.from_sq);
        bool quiet_queen_promo =
            mover != EMPTY && std::abs(mover) == PAWN &&
            board.is_empty(m.to_sq) && m.promotion == QUEEN;
        if (!capture && !quiet_queen_promo) continue;
        int see = static_exchange_eval(board, m);
        tactical.push_back({see, m, capture});
    }

    std::sort(tactical.begin(), tactical.end(),
              [](const Tactical& a, const Tactical& b) {
                  return a.see_score > b.see_score;
              });

    for (const Tactical& t : tactical) {
        if (t.is_capture && t.see_score < 0 &&
            stand_pat + t.see_score <= alpha)
            continue;

        UndoInfo undo    = make_move(board, t.m);
        Hash     new_hash = update_hash(hash, board, t.m,
                                        undo.en_passant_sq,
                                        undo.castling_rights,
                                        undo.captured_piece);
        int score = -quiescence(board, new_hash, -beta, -alpha,
                                ply + 1, qcheck_depth);
        unmake_move(board, t.m, undo);
        if (score >= beta) return beta;
        alpha = std::max(alpha, score);
    }

    if (qcheck_depth < QCHECK_MAX) {
        for (const Move& m : moves) {
            bool capture = !board.is_empty(m.to_sq) || m.to_sq == board.en_passant_sq;
            if (capture) continue;
            int mover_pc = board.get_piece(m.from_sq);
            bool in_tactical =
                (mover_pc != EMPTY && std::abs(mover_pc) == PAWN &&
                 board.is_empty(m.to_sq) && m.promotion == QUEEN);
            if (in_tactical) continue;

            UndoInfo undo = make_move(board, m);
            bool gives_check = king_in_check(board, board.turn);
            if (!gives_check) {
                unmake_move(board, m, undo);
                continue;
            }
            Hash nh = update_hash(hash, board, m,
                                  undo.en_passant_sq,
                                  undo.castling_rights,
                                  undo.captured_piece);
            int sc = -quiescence(board, nh, -beta, -alpha, ply + 1, qcheck_depth + 1);
            unmake_move(board, m, undo);
            if (sc >= beta) return beta;
            alpha = std::max(alpha, sc);
        }
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
            score = 2000000;
        } else {
            int target = board.get_piece(move.to_sq);
            if (target != EMPTY) {
                int see = static_exchange_eval(board, move);
                score   = 100000 + see;
            } else if (move.to_sq == board.en_passant_sq) {
                score = 100000 + static_exchange_eval(board, move);
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

// ─────────────────────────────────────────
// SINGULAR EXTENSION CHECK
// Search all moves EXCEPT the TT move at reduced depth with a
// narrowed beta window. If none of them beat (tt_score - SE_MARGIN),
// the TT move is singular and deserves an extension.
// ─────────────────────────────────────────

bool Searcher::is_singular(const Board& board, Hash hash,
                            const Move& tt_move, int depth,
                            int ply, int /*beta*/) {
    size_t i = hash & (TT_NUM_SLOTS - 1);
    const TTSlot& slot = tt[i];
    if (slot.gen != tt_generation || slot.hash != hash || slot.depth < 0)
        return false;

    int tt_score = value_from_tt(slot.score, ply);
    int s_beta   = tt_score - SE_MARGIN;
    int s_depth  = std::max(1, depth / 2);

    auto moves = generate_legal_moves(board);

    // is_singular is called with the board in the pre-move state
    // (we unmake before calling it, then re-make after).
    // We need a mutable board copy for the sub-search.
    Board search_board = board;

    for (const Move& m : moves) {
        if (m == tt_move) continue;

        UndoInfo undo    = make_move(search_board, m);
        Hash     new_hash = update_hash(hash, search_board, m,
                                        undo.en_passant_sq,
                                        undo.castling_rights,
                                        undo.captured_piece);

        int score = -alphabeta(search_board, new_hash,
                               s_depth, -s_beta - 1, -s_beta,
                               ply + 1, m);
        unmake_move(search_board, m, undo);

        if (score >= s_beta)
            return false;

        if (time_up()) return false;
    }

    return true;
}

void Searcher::tt_store(Hash hash, int depth, int score,
                         int flag, const Move& move, int ply) {
    size_t   i = hash & (TT_NUM_SLOTS - 1);
    TTSlot& slot = tt[i];
    if (slot.gen != tt_generation || slot.hash != hash || depth >= slot.depth) {
        slot.hash  = hash;
        slot.gen   = tt_generation;
        slot.depth = depth;
        slot.score = value_to_tt(score, ply);
        slot.flag  = flag;
        slot.move  = move;
    }
}

int Searcher::score_from_perspective(const Board& board) {
    int score = evaluate(board);
    return (board.turn == WHITE) ? score : -score;
}
