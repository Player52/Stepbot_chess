// search.cpp
// Alpha-beta search implementation.
// C++ equivalent of search.py.

#include "search.h"
#include "stepbot_live_writer.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <chrono>
#include <cstring>
#include <cmath>

static double now() {
    using namespace std::chrono;
    auto t = steady_clock::now().time_since_epoch();
    return duration_cast<duration<double>>(t).count();
}

// Mate scores depend on root distance (ply). TT stores ply-independent encodings.
static constexpr int MATE_VALUE_TT = CHECKMATE_SCORE - MAX_DEPTH - 32;
static constexpr int QCHECK_MAX    = 2;
static constexpr int QS_MAX_PLY    = 8;
static constexpr int HISTORY_LIMIT = 16384;
static constexpr int CAPTURE_HISTORY_LIMIT = 10692;

static inline void update_history_gravity(int& entry, int bonus) {
    bonus = std::clamp(bonus, -HISTORY_LIMIT / 2, HISTORY_LIMIT / 2);
    entry += bonus - entry * std::abs(bonus) / HISTORY_LIMIT;
    entry = std::clamp(entry, -HISTORY_LIMIT, HISTORY_LIMIT);
}

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

static inline bool is_null_move_tt(const Move& move) {
    return move.from_sq == 0 && move.to_sq == 0 && move.promotion == 0;
}

static inline bool slot_tt_pv(const TTSlot& slot) {
    return (slot.flags & TT_SLOT_PV) != 0;
}

static inline bool slot_has_eval(const TTSlot& slot) {
    return (slot.flags & TT_SLOT_HAS_EVAL) != 0;
}

static inline int16_t eval_to_tt(int eval) {
    return (int16_t)std::clamp(eval, -32000, 32000);
}

static inline bool should_replace_tt_slot(const TTSlot& slot, uint16_t gen,
                                          Hash hash, int depth) {
    return slot.gen != gen || slot.hash != hash || depth >= slot.depth;
}

// Pre-computed LMR reduction table [depth][move_index]
// Built once at startup: R = ln(depth) * ln(move) / 1.75
// Avoids floating point in the hot search path.
static int LMR_TABLE[64][64];

static void init_lmr_table() {
    for (int d = 1; d < 64; d++)
        for (int m = 1; m < 64; m++) {
            double r = std::log(d) * std::log(m) / 1.75;
            LMR_TABLE[d][m] = std::max(1, (int)r);
        }
}

static inline bool window_has_mate_bounds(int alpha, int beta) {
    return alpha >= MATE_VALUE_TT || beta <= -MATE_VALUE_TT;
}

static std::string score_to_uci(int score) {
    if (std::abs(score) >= MATE_VALUE_TT) {
        int plies = CHECKMATE_SCORE - std::abs(score);
        int moves = (plies + 1) / 2;
        if (score < 0) moves = -moves;
        return "mate " + std::to_string(moves);
    }
    return "cp " + std::to_string(score);
}

static bool contains_move(const MoveList& moves, const Move& move) {
    for (const Move& candidate : moves)
        if (candidate == move) return true;
    return false;
}

static void write_uci_info(std::mutex* output_mutex,
                           int depth, int multipv,
                           int score, int nodes, int nps,
                           int elapsed_ms, const std::string& pv) {
    std::unique_lock<std::mutex> lock;
    if (output_mutex) lock = std::unique_lock<std::mutex>(*output_mutex);
    std::cout << "info depth "  << depth
              << " multipv "    << multipv
              << " score "      << score_to_uci(score)
              << " nodes "      << nodes
              << " nps "        << nps
              << " time "       << elapsed_ms
              << " pv "         << pv
              << "\n";
    std::cout.flush();
}

Searcher::Searcher()
    : nodes_searched(0), tt_hits(0), start_time(0),
      time_limit(-1), soft_limit(-1),
      in_null_move(false),
      opponent_move_count(0), last_go_time(-1),
      root_pv_move(0, 0), has_root_pv_move(false)
{
    // Heap-allocate large arrays to avoid stack/BSS overflow
    pv_table     = new Move[MAX_DEPTH][MAX_DEPTH]{};
    cont_history = new int[6][64][6][64]{};
    pawn_corr    = new int[2][CORRECTION_HISTORY_SIZE]{};

    std::memset(opponent_move_times, 0, sizeof(opponent_move_times));
    std::memset(history,         0, sizeof(history));
    std::memset(capture_history, 0, sizeof(capture_history));
    std::memset(cont_history[0], 0, sizeof(int) * 6 * 64 * 6 * 64);
    std::memset(killer_count,    0, sizeof(killer_count));
    std::memset(pv_length,       0, sizeof(pv_length));
    std::memset(search_stack,    0, sizeof(search_stack));
    std::memset(cutoff_cnt,      0, sizeof(cutoff_cnt));
    std::memset(pawn_corr[0],    0, sizeof(int) * 2 * CORRECTION_HISTORY_SIZE);
    for (auto& p : ply_stack) p = PlyData{};

    // Initialise countermove table with null moves
    for (int pt = 0; pt < 6; pt++)
        for (int sq = 0; sq < 64; sq++)
            countermove[pt][sq] = Move(0, 0);
    // tt pointer set externally by SMPPool
    init_lmr_table();
}

Searcher::~Searcher() {
    delete[] pv_table;
    delete[] cont_history;
    delete[] pawn_corr;
}

void Searcher::tt_new_game() {
    // TT generation bump is handled by SMPPool::new_game() on the shared TT
}

bool Searcher::time_up() const {
    if (stop_flag && stop_flag->load(std::memory_order_relaxed)) return true;
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
                               const std::vector<Hash>& history,
                               int    /*num_threads*/,
                               SharedTT* shared_tt,
                               std::atomic<bool>* stop,
                               int    multipv,
                               std::mutex* output_lock) {
    // Wire up shared resources
    if (shared_tt) tt = shared_tt;
    stop_flag = stop;
    output_mutex = output_lock;
    multipv = std::max(1, std::min(5, multipv));
    position_history = history;
    nodes_searched = 0;
    tt_hits        = 0;
    start_time     = now();
    time_limit     = -1;
    soft_limit     = -1;
    in_null_move   = false;
    root_pv_move   = Move(0, 0);
    has_root_pv_move = false;

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
    }

    std::memset(this->history,  0, sizeof(this->history));
    std::memset(capture_history, 0, sizeof(capture_history));
    std::memset(cont_history[0], 0, sizeof(int) * 6 * 64 * 6 * 64);
    std::memset(killer_count, 0, sizeof(killer_count));
    std::memset(pv_length,    0, sizeof(pv_length));
    std::memset(cutoff_cnt,   0, sizeof(cutoff_cnt));
    std::memset(pawn_corr[0],    0, sizeof(int) * 2 * CORRECTION_HISTORY_SIZE);
    for (auto& p : ply_stack) p = PlyData{};
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

        if (multipv > 1) {
            auto lines = search_root_multipv(search_board, current_hash, depth, multipv);
            if (time_up()) break;
            if (!lines.empty()) {
                best_move  = lines[0].move;
                best_score = lines[0].score;
                double elapsed    = now() - start_time;
                int    elapsed_ms = (int)(elapsed * 1000);
                int    nps        = (elapsed > 0) ? (int)(nodes_searched / elapsed) : 0;
                for (int i = 0; i < (int)lines.size(); i++) {
                    write_uci_info(output_mutex, depth, i + 1, lines[i].score,
                                   nodes_searched, nps, elapsed_ms, lines[i].pv);
                }
                LiveWriter::update(search_board, depth, best_score, (long long)nps, lines[0].pv);
            }
        } else {
            Move iter_move(0, 0);
            int  iter_score = 0;

            auto [move, score] = search_root(search_board, current_hash, depth);
            iter_move  = move;
            iter_score = score;

            if (time_up()) break;

            if (iter_move.from_sq != iter_move.to_sq || iter_move.from_sq != 0) {
                best_move  = iter_move;
                best_score = iter_score;
            }

            std::string pv_str;
            int pv_len = pv_length[0];
            for (int i = 0; i < pv_len && i < MAX_DEPTH; i++) {
                if (i > 0) pv_str += ' ';
                pv_str += pv_table[0][i].to_uci();
            }
            if (pv_str.empty()) pv_str = best_move.to_uci();

            double elapsed    = now() - start_time;
            int    elapsed_ms = (int)(elapsed * 1000);
            int    nps        = (elapsed > 0) ? (int)(nodes_searched / elapsed) : 0;
            write_uci_info(output_mutex, depth, 1, best_score,
                           nodes_searched, nps, elapsed_ms, pv_str);

            LiveWriter::update(search_board, depth, best_score, (long long)nps, pv_str);
        }

        // Hard stop — never start a new depth if we're already over time
        if (time_up()) break;
    }

    // Signal all helper threads to stop
    if (stop_flag) stop_flag->store(true, std::memory_order_relaxed);

    return best_move;
}

std::pair<Move, int> Searcher::search_root(Board& board,
                                            Hash hash, int depth) {
    MoveList moves;
    generate_legal_moves_into(board, moves);
    if (moves.empty()) return {Move(0, 0), 0};

    pv_length[0] = 0;
    Move root_order_move_storage(0, 0);
    const Move* root_order_move = nullptr;
    if (tt) {
        TTSlot& slot = tt->slots[tt->index(hash)];
        if (slot.gen == tt->generation && slot.hash == hash &&
            contains_move(moves, slot.move)) {
            root_order_move_storage = slot.move;
            root_order_move = &root_order_move_storage;
        }
    }
    if (!root_order_move && has_root_pv_move && contains_move(moves, root_pv_move)) {
        root_order_move_storage = root_pv_move;
        root_order_move = &root_order_move_storage;
    }
    order_moves(board, moves, 0, root_order_move);

    Move best_move  = moves[0];
    int  best_score = -CHECKMATE_SCORE - 1;
    int  alpha      = -CHECKMATE_SCORE - 1;
    int  beta       =  CHECKMATE_SCORE + 1;

    for (int i = 0; i < moves.size(); i++) {
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
        if (alpha >= beta) break;
        if (time_up()) break;
    }

    tt_store(hash, depth, best_score, TT_EXACT, best_move, 0);
    root_pv_move = best_move;
    has_root_pv_move = true;
    return {best_move, best_score};
}

std::vector<RootLine> Searcher::search_root_multipv(Board& board,
                                                     Hash hash, int depth,
                                                     int multipv) {
    MoveList moves;
    generate_legal_moves_into(board, moves);
    if (moves.empty()) return {};

    Move root_order_move_storage(0, 0);
    const Move* root_order_move = nullptr;
    if (tt) {
        TTSlot& slot = tt->slots[tt->index(hash)];
        if (slot.gen == tt->generation && slot.hash == hash &&
            contains_move(moves, slot.move)) {
            root_order_move_storage = slot.move;
            root_order_move = &root_order_move_storage;
        }
    }
    if (!root_order_move && has_root_pv_move && contains_move(moves, root_pv_move)) {
        root_order_move_storage = root_pv_move;
        root_order_move = &root_order_move_storage;
    }
    order_moves(board, moves, 0, root_order_move);
    std::vector<RootLine> lines;
    lines.reserve(moves.size());

    for (const Move& move : moves) {
        if (time_up()) break;

        pv_length[1] = 0;
        UndoInfo undo = make_move(board, move);
        Hash new_hash = update_hash(hash, board, move,
                                    undo.en_passant_sq,
                                    undo.castling_rights,
                                    undo.captured_piece);

        int score = -alphabeta(board, new_hash,
                               depth - 1,
                               -CHECKMATE_SCORE - 1,
                                CHECKMATE_SCORE + 1,
                               1, move);

        std::string pv = move.to_uci();
        int child_len = pv_length[1];
        if (child_len > MAX_DEPTH - 1) child_len = MAX_DEPTH - 1;
        for (int i = 0; i < child_len; i++) {
            pv += ' ';
            pv += pv_table[1][i].to_uci();
        }

        unmake_move(board, move, undo);
        lines.push_back({move, score, pv});
    }

    std::sort(lines.begin(), lines.end(),
              [](const RootLine& a, const RootLine& b) {
                  return a.score > b.score;
              });

    if ((int)lines.size() > multipv)
        lines.resize(multipv);

    if (!lines.empty()) {
        pv_table[0][0] = lines[0].move;
        pv_length[0] = 1;
        tt_store(hash, depth, lines[0].score, TT_EXACT, lines[0].move, 0);
        root_pv_move = lines[0].move;
        has_root_pv_move = true;
    }

    return lines;
}

int Searcher::alphabeta(Board& board, Hash hash,
                         int depth, int alpha, int beta, int ply,
                         Move prev_move, bool cut_node, int prev_static_eval) {
    if (time_up()) return alpha;
    if (ply >= MAX_DEPTH - 1)
        return quiescence(board, hash, alpha, beta, ply);

    nodes_searched++;

    // Initialise PV and ply stack
    if (ply < MAX_DEPTH) pv_length[ply] = 0;
    int safe_ply = std::min(ply, MAX_DEPTH + 3);
    PlyData& ps       = ply_stack[safe_ply];
    PlyData& ps_prev  = ply_stack[safe_ply > 0 ? safe_ply - 1 : 0];
    PlyData& ps_prev2 = ply_stack[safe_ply > 1 ? safe_ply - 2 : 0];
    (void)ps_prev2;
    ps.reduction = 0;
    int prior_reduction = ps_prev.reduction;

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

    // ── Mate Distance Pruning ──
    if (ply > 0 && ply < CHECKMATE_SCORE) {
        int mated_score = -(CHECKMATE_SCORE - ply);
        int mate_score  =  (CHECKMATE_SCORE - ply - 1);
        alpha = std::max(alpha, mated_score);
        beta  = std::min(beta,  mate_score);
        if (alpha >= beta) return alpha;
    }

    // Reset cutoff count for next ply
    if (ply + 2 < MAX_DEPTH) cutoff_cnt[ply + 2] = 0;

    const Move* tt_move = nullptr;
    Move        tt_move_storage(0, 0);

    // TT lookup
    TTSlot*   slot   = nullptr;
    uint16_t  gen    = tt ? tt->generation : 0;
    bool      tt_hit = false;
    if (tt && !tt->slots.empty()) {
        slot   = &tt->slots[tt->index(hash)];
        tt_hit = (slot->gen == gen && slot->hash == hash);
    }
    bool      is_tt_pv = tt_hit && slot_tt_pv(*slot);  // Was this a PV node before?
    if (tt_hit) {
        if (slot->depth >= depth) {
            tt_hits++;
            int v    = value_from_tt(slot->score, ply);
            int flag = slot->flag;
            if (flag == TT_EXACT)       return v;
            if (flag == TT_LOWER_BOUND) alpha = std::max(alpha, v);
            if (flag == TT_UPPER_BOUND) beta  = std::min(beta,  v);
            if (alpha >= beta)          return v;
        }
        tt_move_storage = slot->move;
        tt_move         = &tt_move_storage;
    }

    // ttPv: this node is on a PV path if it was before OR if it's a PV search
    bool node_tt_pv = is_tt_pv || (beta - alpha > 1);  // PV node if wide window

    // Use stored static eval from TT if available (saves a full eval call)
    bool used_tt_eval = tt_hit && slot_has_eval(*slot);

    // In check at the horizon: search one full-width ply of evasions (never
    // drop straight into capture-only quiescence while in check).
    if (depth <= 0) {
        if (!king_in_check(board, board.turn))
            return quiescence(board, hash, alpha, beta, ply);
        depth = 1;
    }

    MoveList moves;
    generate_legal_moves_into(board, moves);

    if (moves.empty()) {
        if (king_in_check(board, board.turn))
            return -(CHECKMATE_SCORE - ply);
        return 0;
    }

    if (tt_move && !contains_move(moves, *tt_move))
        tt_move = nullptr;

    bool in_check = king_in_check(board, board.turn);

    // Cache static eval — use TT stored eval if available, else compute
    // Keep the cached value raw; correction history is applied only once below.
    int raw_static_eval;
    if (in_check)          raw_static_eval = 0;
    else if (used_tt_eval) raw_static_eval = slot->eval;
    else                   raw_static_eval = score_from_perspective(board);

    int static_eval = raw_static_eval;

    // Apply pawn correction history to improve eval accuracy
    if (!in_check) {
        int correction = get_correction(board);
        static_eval = std::clamp(static_eval + correction / 8,
                                 -(CHECKMATE_SCORE / 2),
                                  (CHECKMATE_SCORE / 2));
    }

    // Store the raw eval in TT on first visit.
    if (!in_check && tt_hit && !slot_has_eval(*slot)) {
        slot->eval = eval_to_tt(raw_static_eval);
        slot->flags |= TT_SLOT_HAS_EVAL;
    }

    // Update ply stack with this node's data
    ps.static_eval = static_eval;
    ps.in_check    = in_check;
    ps.tt_pv       = node_tt_pv;

    // ── Improving / OpponentWorsening flags ──
    bool improving         = !in_check && (static_eval > prev_static_eval);
    bool opponent_worsening = !in_check && (static_eval > -prev_static_eval);

    // ── Razoring ──
    // Only at depth 1 to avoid false pruning at higher depths.
    if (!in_check && depth == 1
        && static_eval < alpha - 400)
        return quiescence(board, hash, alpha, beta, ply);

    // ── Internal Iterative Reduction (IIR) ──
    // At depth >= 6 with no TT move, our move ordering is poor.
    // Reduce depth by 1 to search cheaper and get a TT entry for the real search.
    if (depth >= 6 && !tt_move && !in_check)
        depth--;

    // ── Hindsight depth adjustment ──
    if (prior_reduction >= 3 && !opponent_worsening)
        depth = std::min(depth + 1, MAX_DEPTH - 1);
    if (prior_reduction >= 2 && depth >= 2
        && static_eval + prev_static_eval > 173)
        depth = std::max(depth - 1, 1);

    // ── Futility Pruning ──
    // When improving, use a tighter margin (we expect to do better).
    if (depth >= 1 && depth <= 3
        && !in_check
        && alpha > -CHECKMATE_SCORE / 2
        && beta  <  CHECKMATE_SCORE / 2
        && !window_has_mate_bounds(alpha, beta))
    {
        int margin = FUTILITY_MARGIN[depth]
                   + (improving        ? -30 : 30)
                   + (opponent_worsening ? -20 : 20);
        if (static_eval + margin <= alpha)
            return quiescence(board, hash, alpha, beta, ply);
    }

    // ── Null Move Pruning ──
    if (!in_null_move
        && depth >= NULL_MOVE_MIN_DEPTH
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

        int R          = 3 + depth / 3 + (improving ? 0 : 1);
        int null_depth = std::max(1, depth - 1 - R);

        int null_score = -alphabeta(null_board, null_hash,
                                    null_depth,
                                    -beta, -beta + 1, ply + 1,
                                    Move(0, 0), !cut_node, static_eval);
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
            if (static_exchange_eval(board, m) < PROBCUT_SEE_MIN)
                continue;

            UndoInfo pc_undo = make_move(board, m);
            Hash     ph      = update_hash(hash, board, m,
                                           pc_undo.en_passant_sq,
                                           pc_undo.castling_rights,
                                           pc_undo.captured_piece);

            int pc_score = -alphabeta(board, ph, probcut_depth,
                                      -probcut_beta, -probcut_beta + 1,
                                      ply + 1, m, !cut_node, static_eval);
            unmake_move(board, m, pc_undo);

            if (pc_score >= probcut_beta)
                return beta;
        }
    }

    // Get countermove for move ordering (looked up in order_moves via prev_move)
    order_moves(board, moves, ply, tt_move, &prev_move);

    int  original_alpha = alpha;
    Move best_move(0, 0);
    bool found_best = false;
    int  quiet_count = 0;  // LMP: count of quiet moves searched

    // Searched moves lists for batch history penalisation
    std::vector<Move> quiets_searched;    // non-best quiet moves
    std::vector<Move> captures_searched;  // non-best capture moves
    quiets_searched.reserve(32);
    captures_searched.reserve(16);

    for (int move_idx = 0; move_idx < moves.size(); move_idx++) {
        const Move& move = moves[move_idx];
        if (time_up()) break;

        bool is_capture  = !board.is_empty(move.to_sq)
                           || move.to_sq == board.en_passant_sq;
        bool is_promo    = (move.promotion != 0);
        int  capture_see = is_capture ? static_exchange_eval(board, move) : 0;

        // ── Late Move Pruning (LMP) ──
        bool skip_quiets = false;
        if (!in_check && depth <= 8) {
            int lmp_limit = (3 + depth * depth) / (2 - improving);
            if (quiet_count >= lmp_limit)
                skip_quiets = true;
        }
        if (depth >= 1 && depth <= 3
            && !in_check
            && !is_capture && !is_promo
            && move_idx > 0
            && !window_has_mate_bounds(alpha, beta)
            && quiet_count >= LMP_THRESHOLD[depth])
        {
            continue;
        }

        UndoInfo undo     = make_move(board, move);
        Hash     new_hash  = update_hash(hash, board, move,
                                         undo.en_passant_sq,
                                         undo.castling_rights,
                                         undo.captured_piece);
        bool gives_check   = king_in_check(board, board.turn);

        // Re-check LMP after make_move — never prune checking moves
        if (!gives_check) {
            if (skip_quiets && !is_capture && !is_promo) {
                unmake_move(board, move, undo);
                continue;
            }
            if (!in_check
                && is_capture && !is_promo
                && depth <= BAD_CAPTURE_PRUNE_DEPTH
                && move_idx > 0
                && !node_tt_pv
                && capture_see <= BAD_CAPTURE_PRUNE_SEE
                && static_eval + capture_see + BAD_CAPTURE_PRUNE_MARGIN <= alpha
                && !window_has_mate_bounds(alpha, beta))
            {
                unmake_move(board, move, undo);
                continue;
            }
        }

        // Count quiet moves AFTER pruning decisions
        if (!is_capture && !is_promo) quiet_count++;

        // Per-move futility / history pruning (only for non-checking moves)
        if (!gives_check && !in_check && !is_capture && !is_promo
            && !window_has_mate_bounds(alpha, beta)
            && move_idx > 0)
        {
            int d = std::min(depth, 63);
            int m = std::min(move_idx, 63);
            int R = (d >= 2 && m >= 2) ? LMR_TABLE[d][m] : 1;
            int lmr_depth = std::max(0, depth - 1 - R);

            int hist_score = history[move.from_sq][move.to_sq];
            if (hist_score < -4000 * depth) {
                unmake_move(board, move, undo);
                continue;
            }

            int futility_val = static_eval + 200 + 150 * lmr_depth;
            if (lmr_depth < 8 && futility_val <= alpha) {
                unmake_move(board, move, undo);
                continue;
            }
        }

        bool allow_lmr = !window_has_mate_bounds(alpha, beta);

        // ── Singular Extensions ──
        int extension = 0;
        if (move_idx == 0
            && tt_move && move == *tt_move
            && depth >= SE_DEPTH_LIMIT
            && !in_check
            && std::abs(beta) < CHECKMATE_SCORE / 2)
        {
            unmake_move(board, move, undo);
            if (is_singular(board, hash, move, depth, ply, beta))
                extension = 1;
            undo = make_move(board, move);
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
            extension = 1;

        // ── Passed Pawn Extension ──
        if (extension == 0 && !is_capture) {
            int orig_colour = -board.turn;
            int to_rank     = move.to_sq / 8;
            int moved_pt    = std::abs(board.get_piece(move.to_sq));
            if (move.promotion) moved_pt = PAWN;
            if (moved_pt == PAWN) {
                if ((orig_colour == WHITE && to_rank == 6) ||
                    (orig_colour == BLACK && to_rank == 1))
                    extension = 1;
            }
        }

        // ── Check Extension ──
        if (gives_check && extension == 0 && depth >= 2)
            extension = CHECK_EXTENSION;

        int search_depth_ext = std::max(0, depth - 1 + extension);
        int score;

        if (move_idx == 0) {
            score = -alphabeta(board, new_hash,
                               search_depth_ext, -beta, -alpha,
                               ply + 1, move, false, static_eval);
        } else {
            // ── Aggressive LMR (table lookup) ──
            int search_depth = search_depth_ext;
            bool did_lmr = false;
            if (allow_lmr
                && move_idx >= LMR_MIN_MOVE_INDEX
                && depth  >= LMR_MIN_DEPTH
                && !in_check && !is_capture && !is_promo && !gives_check)
            {
                int d = std::min(depth, 63);
                int m = std::min(move_idx, 63);
                int R = LMR_TABLE[d][m];
                // Increase reduction on cut nodes
                if (cut_node) R++;
                // Decrease reduction if improving
                if (improving) R = std::max(1, R - 1);
                // Extra reduction for moves with poor history
                if (history[move.from_sq][move.to_sq] < 0) R++;
                // Increase reduction when child node has many fail-highs
                if (ply + 1 < MAX_DEPTH && cutoff_cnt[ply + 1] > 1) R++;
                // Increase reduction for ttPv nodes
                if (node_tt_pv) R += 1;
                // Scale by statScore (composite history)
                int stat_score = history[move.from_sq][move.to_sq];
                R -= stat_score * 850 / 8192;
                R = std::max(1, R);
                search_depth = std::max(1, search_depth_ext - R);
                search_depth = std::min(search_depth, search_depth_ext);
                ps.reduction = search_depth_ext - search_depth;  // Store for hindsight
                did_lmr = true;
            }
            score = -alphabeta(board, new_hash,
                               search_depth, -alpha - 1, -alpha,
                               ply + 1, move, true, static_eval);
            // Only re-search if the reduced search beat alpha
            if (did_lmr && score > alpha)
                score = -alphabeta(board, new_hash,
                                   search_depth_ext, -alpha - 1, -alpha,
                                   ply + 1, move, !cut_node, static_eval);
            // Full-window re-search only if it genuinely beats alpha at full depth
            if (score > alpha && score < beta)
                score = -alphabeta(board, new_hash,
                                   search_depth_ext, -beta, -alpha,
                                   ply + 1, move, false, static_eval);
        }

        unmake_move(board, move, undo);

        if (score >= beta) {
            if (!is_capture)
                update_killers(move, ply);
            update_history(board, move, depth, prev_move);
            if (is_capture)
                update_capture_history(board, move, depth, true);
            update_countermove(prev_move, move, board);

            // Batch penalise all non-best searched moves
            int malus = std::min(depth * depth, 400);
            for (const Move& qm : quiets_searched) {
                update_history_gravity(history[qm.from_sq][qm.to_sq], -malus);
            }
            for (const Move& cm : captures_searched) {
                update_capture_history(board, cm, depth, false);
            }

            tt_store(hash, depth, beta, TT_LOWER_BOUND, move, ply,
                     raw_static_eval, node_tt_pv, !in_check);
            if (ply < MAX_DEPTH) cutoff_cnt[ply]++;
            return beta;
        }
        if (score > alpha) {
            alpha      = score;
            best_move  = move;
            found_best = true;
            if (ply + 1 < MAX_DEPTH) {
                pv_table[ply][0] = move;
                int child_len = pv_length[ply + 1];
                for (int i = 0; i < child_len; i++)
                    pv_table[ply][i + 1] = pv_table[ply + 1][i];
                pv_length[ply] = child_len + 1;
            }
            (void)0;
        }
        // Track non-best searched moves for batch penalisation
        if (move != best_move) {
            if (is_capture) {
                captures_searched.push_back(move);
            } else {
                quiets_searched.push_back(move);
            }
        }
        // Individual capture history update
        if (is_capture && score < beta)
            update_capture_history(board, move, depth, false);
    }

    if (found_best) {
        int flag = (alpha > original_alpha) ? TT_EXACT : TT_UPPER_BOUND;
        tt_store(hash, depth, alpha, flag, best_move, ply,
                 raw_static_eval, node_tt_pv, !in_check);

        // Update correction history when search result differs from static eval
        if (!in_check && best_move.from_sq != best_move.to_sq)
            update_correction(board, depth, alpha, static_eval);
    }

    return alpha;
}

int Searcher::quiescence(Board& board, Hash hash, int alpha, int beta,
                          int ply, int qcheck_depth) {
    if (time_up()) return alpha;

    nodes_searched++;

    bool in_check = king_in_check(board, board.turn);

    int stand_pat = 0;
    if (qcheck_depth >= QS_MAX_PLY && !in_check) {
        stand_pat = score_from_perspective(board);
        if (stand_pat >= beta) return beta;
        return std::max(alpha, stand_pat);
    }
    if (qcheck_depth >= QS_MAX_PLY)
        return alpha;

    if (ply >= MAX_DEPTH - 1 && !in_check) {
        stand_pat = score_from_perspective(board);
        if (stand_pat >= beta) return beta;
        return std::max(alpha, stand_pat);
    }
    if (ply >= MAX_DEPTH - 1)
        return alpha;

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

    if (in_check) {
        MoveList moves;
        generate_legal_moves_into(board, moves);
        order_moves(board, moves, 0);
        if (moves.empty())
            return -(CHECKMATE_SCORE - ply);
        for (const Move& move : moves) {
            if (time_up()) break;
            UndoInfo undo    = make_move(board, move);
            Hash     new_hash = update_hash(hash, board, move,
                                            undo.en_passant_sq,
                                            undo.castling_rights,
                                            undo.captured_piece);
            int score = -quiescence(board, new_hash, -beta, -alpha,
                                    ply + 1, qcheck_depth + 1);
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
    std::vector<Move> quiet_checks;

    MoveList pseudo_moves;
    generate_pseudo_legal_moves_into(board, pseudo_moves);
    tactical.reserve(pseudo_moves.size());
    if (qcheck_depth < QCHECK_MAX)
        quiet_checks.reserve(8);
    Board legality_board = board;

    for (const Move& m : pseudo_moves) {
        bool capture = !board.is_empty(m.to_sq) || m.to_sq == board.en_passant_sq;
        int  mover   = board.get_piece(m.from_sq);
        bool quiet_queen_promo =
            mover != EMPTY && std::abs(mover) == PAWN &&
            board.is_empty(m.to_sq) && m.promotion == QUEEN;
        bool needs_check_test =
            qcheck_depth < QCHECK_MAX && !capture && !quiet_queen_promo;

        if (!capture && !quiet_queen_promo && !needs_check_test)
            continue;

        UndoInfo undo = make_move(legality_board, m);
        bool legal = !king_in_check(legality_board, board.turn);
        bool gives_check = legal && needs_check_test &&
                           king_in_check(legality_board, legality_board.turn);
        unmake_move(legality_board, m, undo);
        if (!legal) continue;

        if (capture || quiet_queen_promo) {
            int see = static_exchange_eval(board, m);
            tactical.push_back({see, m, capture});
        } else if (gives_check) {
            quiet_checks.push_back(m);
        }
    }

    std::sort(tactical.begin(), tactical.end(),
              [](const Tactical& a, const Tactical& b) {
                  return a.see_score > b.see_score;
              });

    for (const Tactical& t : tactical) {
        if (time_up()) break;
        if (t.is_capture && t.see_score < 0 &&
            stand_pat + t.see_score <= alpha)
            continue;

        UndoInfo undo    = make_move(board, t.m);
        Hash     new_hash = update_hash(hash, board, t.m,
                                        undo.en_passant_sq,
                                        undo.castling_rights,
                                        undo.captured_piece);
        int score = -quiescence(board, new_hash, -beta, -alpha,
                                ply + 1, qcheck_depth + 1);
        unmake_move(board, t.m, undo);
        if (score >= beta) return beta;
        alpha = std::max(alpha, score);
    }

    if (qcheck_depth < QCHECK_MAX) {
        for (const Move& m : quiet_checks) {
            if (time_up()) break;
            UndoInfo undo = make_move(board, m);
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

void Searcher::order_moves(const Board& board,
                           MoveList& moves,
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

    struct ScoredMove {
        int score;
        Move move;
    };
    std::array<ScoredMove, MAX_MOVES> scored;
    int scored_count = 0;

    for (const Move& move : moves) {
        int score = 0;

        int target = board.get_piece(move.to_sq);
        if (tt_move && move == *tt_move) {
            score = 2000000;
        } else {
            if (target != EMPTY) {
                int see      = static_exchange_eval(board, move);
                int mover_t  = std::abs(board.get_piece(move.from_sq));
                int target_t = std::abs(target);
                int cap_hist = (mover_t >= 1 && mover_t <= 6
                                && target_t >= 1 && target_t <= 6)
                             ? capture_history[mover_t][move.to_sq][target_t]
                             : 0;
                score = 100000 + see + cap_hist / 100;
            } else if (move.to_sq == board.en_passant_sq) {
                int see = static_exchange_eval(board, move);
                score = 100000 + see;
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

        if (scored_count < MAX_MOVES)
            scored[scored_count++] = {score, move};
    }

    std::sort(scored.begin(), scored.begin() + scored_count,
              [](const ScoredMove& a, const ScoredMove& b) {
                  return a.score > b.score;
              });

    for (int i = 0; i < scored_count; i++)
        moves[i] = scored[i].move;
}

std::vector<Move> Searcher::order_moves(const Board& board,
                                         std::vector<Move>& moves,
                                         int ply,
                                         const Move* tt_move,
                                         const Move* prev_move) {
    MoveList fixed;
    for (const Move& move : moves)
        fixed.push_back(move);
    order_moves(board, fixed, ply, tt_move, prev_move);
    moves.clear();
    moves.reserve(fixed.size());
    for (const Move& move : fixed)
        moves.push_back(move);
    return moves;
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
        int bonus = std::min(depth * depth, 400);
        update_history_gravity(history[move.from_sq][move.to_sq], bonus);
        update_cont_history(prev_move, move, board, depth);
    }
}

void Searcher::update_capture_history(const Board& board, const Move& move,
                                       int depth, bool caused_cutoff) {
    int piece         = board.get_piece(move.from_sq);
    int piece_type    = std::abs(piece);
    int captured      = board.get_piece(move.to_sq);
    int captured_type = std::abs(captured);

    if (piece_type < 1 || piece_type > 6) return;
    if (captured_type < 0 || captured_type > 6) return;

    int bonus  = caused_cutoff ? depth * depth : -(depth * depth);
    int& entry = capture_history[piece_type][move.to_sq][captured_type];
    // Gravity update: scale towards 0 then add bonus
    entry += bonus - entry * std::abs(bonus) / CAPTURE_HISTORY_LIMIT;
    entry  = std::clamp(entry, -CAPTURE_HISTORY_LIMIT, CAPTURE_HISTORY_LIMIT);
}

void Searcher::update_cont_history(const Move& prev_move, const Move& move,
                                    const Board& board, int depth) {
    int bonus = std::min(depth * depth, 400);
    update_cont_history_bonus(prev_move, move, board, bonus);
}

void Searcher::update_cont_history_bonus(const Move& prev_move, const Move& move,
                                          const Board& board, int bonus) {
    // Only update if we have a valid previous move
    if (prev_move.from_sq == prev_move.to_sq && prev_move.from_sq == 0)
        return;

    int prev_piece = std::abs(board.get_piece(prev_move.to_sq));
    int curr_piece = std::abs(board.get_piece(move.from_sq));

    if (prev_piece < 1 || prev_piece > 6) return;
    if (curr_piece < 1 || curr_piece > 6) return;

    update_history_gravity(cont_history[prev_piece-1][prev_move.to_sq]
                                       [curr_piece-1][move.to_sq],
                           bonus);
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
    if (!tt || tt->slots.empty())
        return false;

    size_t    i    = tt->index(hash);
    TTSlot&   slot  = tt->slots[i];
    uint16_t  gen   = tt->generation;
    if (slot.gen != gen || slot.hash != hash || slot.depth < 0)
        return false;

    int tt_score = value_from_tt(slot.score, ply);
    int s_beta   = tt_score - SE_MARGIN;
    int s_depth  = std::max(1, depth / 2);

    MoveList moves;
    generate_legal_moves_into(board, moves);

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
                         int flag, const Move& move, int ply, int eval,
                         bool tt_pv, bool has_eval) {
    if (!tt || tt->slots.empty())
        return;

    size_t   i    = tt->index(hash);
    TTSlot&  slot = tt->slots[i];
    uint16_t gen  = tt->generation;

    if (!should_replace_tt_slot(slot, gen, hash, depth)) {
        if (slot.gen == gen && slot.hash == hash) {
            if (has_eval && !slot_has_eval(slot)) {
                slot.eval = eval_to_tt(eval);
                slot.flags |= TT_SLOT_HAS_EVAL;
            }
            if (tt_pv)
                slot.flags |= TT_SLOT_PV;
            if (is_null_move_tt(slot.move) && !is_null_move_tt(move))
                slot.move = move;
        }
        return;
    }

    slot.hash  = hash;
    slot.gen   = gen;
    slot.depth = (int16_t)depth;
    slot.score = value_to_tt(score, ply);
    slot.eval  = has_eval ? eval_to_tt(eval) : 0;
    slot.flag  = (int8_t)flag;
    slot.flags = (tt_pv ? TT_SLOT_PV : 0)
               | (has_eval ? TT_SLOT_HAS_EVAL : 0);
    slot.move  = move;
}

int Searcher::get_correction(const Board& board) const {
    // Get pawn hash key from the board (lower bits of Zobrist hash of pawn positions)
    // We approximate by using board hash XOR'd with turn to get a pawn-like key.
    // A proper implementation would hash only pawn positions.
    uint64_t pawn_key = 0;
    for (int sq = 0; sq < 64; sq++) {
        int p = board.get_piece(sq);
        if (std::abs(p) == PAWN)
            pawn_key ^= PIECE_SQUARE_TABLE[piece_index(p)][sq];
    }
    int side = (board.turn == WHITE) ? 0 : 1;
    int idx  = (int)(pawn_key & (CORRECTION_HISTORY_SIZE - 1));
    return pawn_corr[side][idx];
}

void Searcher::update_correction(const Board& board, int depth,
                                  int best_score, int static_eval) {
    int error = best_score - static_eval;
    int bonus = std::clamp(error * depth / 8,
                           -CORRECTION_HISTORY_LIMIT / 4,
                            CORRECTION_HISTORY_LIMIT / 4);
    uint64_t pawn_key = 0;
    for (int sq = 0; sq < 64; sq++) {
        int p = board.get_piece(sq);
        if (std::abs(p) == PAWN)
            pawn_key ^= PIECE_SQUARE_TABLE[piece_index(p)][sq];
    }
    int side = (board.turn == WHITE) ? 0 : 1;
    int idx  = (int)(pawn_key & (CORRECTION_HISTORY_SIZE - 1));
    int& entry = pawn_corr[side][idx];
    entry += bonus - entry * std::abs(bonus) / CORRECTION_HISTORY_LIMIT;
    entry  = std::clamp(entry,
                        -CORRECTION_HISTORY_LIMIT,
                         CORRECTION_HISTORY_LIMIT);
}

int Searcher::score_from_perspective(const Board& board) {
    int score = evaluate(board);
    return (board.turn == WHITE) ? score : -score;
}

// ─────────────────────────────────────────
// HELPER THREAD SEARCH
// Runs iterative deepening independently until stop_flag is set.
// Shares the TT with the main thread — results cross-pollinate.
// Starts at depth 1 and keeps going; helpers intentionally start
// at odd depths to reduce search overlap with the main thread.
// ─────────────────────────────────────────

void Searcher::helper_search(Board board, Hash hash, int max_depth) {
    // Reset per-thread state
    std::memset(this->history,  0, sizeof(this->history));
    std::memset(capture_history, 0, sizeof(capture_history));
    std::memset(cont_history[0], 0, sizeof(int) * 6 * 64 * 6 * 64);
    std::memset(killer_count,   0, sizeof(killer_count));
    std::memset(pv_length,      0, sizeof(pv_length));
    std::memset(search_stack,   0, sizeof(search_stack));
    std::memset(cutoff_cnt,     0, sizeof(cutoff_cnt));
    std::memset(pawn_corr[0],      0, sizeof(int) * 2 * CORRECTION_HISTORY_SIZE);
    for (auto& p : ply_stack) p = PlyData{};
    in_null_move = false;
    nodes_searched = 0;
    tt_hits = 0;

    search_stack[0] = hash;
    Board search_board = board;

    // Helpers start at depth offset by thread_id to spread work
    int start_depth = 1 + (thread_id % 2);

    for (int depth = start_depth; depth <= max_depth; depth++) {
        if (time_up()) break;
        search_root(search_board, hash, depth);
        if (time_up()) break;
    }
}

// ─────────────────────────────────────────
// SMP POOL IMPLEMENTATION
// ─────────────────────────────────────────

void SMPPool::resize(int n) {
    stop_helpers();
    num_threads = std::max(1, n);
    helpers.clear();
    int num_helpers = num_threads - 1;
    helpers.reserve(num_helpers);  // Reserve before emplacing to avoid realloc
    for (int i = 0; i < num_helpers; i++) {
        helpers.emplace_back();     // Default-construct in place
        helpers.back().tt        = &shared_tt;
        helpers.back().thread_id = i + 1;
        helpers.back().stop_flag = &stop;
        helpers.back().time_limit = -1;
        helpers.back().soft_limit = -1;
    }
}

void SMPPool::new_game() {
    shared_tt.new_game();
}

void SMPPool::stop_helpers() {
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : threads)
        if (t.joinable()) t.join();
    threads.clear();
    stop.store(false, std::memory_order_relaxed);
}
