#include "board.h"
#include "evaluate.h"
#include "movegen.h"
#include "search.h"
#include "zobrist.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

static Board empty_board() {
    Board board;
    board.squares.fill(EMPTY);
    board.refresh_king_squares();
    board.turn = WHITE;
    board.castling_rights = CastlingRights{};
    board.castling_rights.K = false;
    board.castling_rights.Q = false;
    board.castling_rights.k = false;
    board.castling_rights.q = false;
    board.en_passant_sq = -1;
    board.halfmove_clock = 0;
    board.fullmove_number = 1;
    return board;
}

static int fen_piece(char c) {
    switch (c) {
        case 'P': return  PAWN;   case 'p': return -PAWN;
        case 'N': return  KNIGHT; case 'n': return -KNIGHT;
        case 'B': return  BISHOP; case 'b': return -BISHOP;
        case 'R': return  ROOK;   case 'r': return -ROOK;
        case 'Q': return  QUEEN;  case 'q': return -QUEEN;
        case 'K': return  KING;   case 'k': return -KING;
        default:  return  EMPTY;
    }
}

static Board board_from_fen_for_test(const std::string& fen) {
    Board board;
    board.squares.fill(EMPTY);
    std::istringstream ss(fen);
    std::string placement, active, castling, ep, halfmove, fullmove;
    ss >> placement >> active >> castling >> ep >> halfmove >> fullmove;

    int rank = 7, file = 0;
    for (char c : placement) {
        if (c == '/') { rank--; file = 0; }
        else if (std::isdigit((unsigned char)c)) file += c - '0';
        else board.squares[sq(file++, rank)] = fen_piece(c);
    }

    board.turn = (active == "w") ? WHITE : BLACK;
    board.castling_rights.K = castling.find('K') != std::string::npos;
    board.castling_rights.Q = castling.find('Q') != std::string::npos;
    board.castling_rights.k = castling.find('k') != std::string::npos;
    board.castling_rights.q = castling.find('q') != std::string::npos;
    board.en_passant_sq = (ep == "-") ? -1 : name_to_square(ep);
    board.halfmove_clock = halfmove.empty() ? 0 : std::stoi(halfmove);
    board.fullmove_number = fullmove.empty() ? 1 : std::stoi(fullmove);
    board.refresh_king_squares();
    return board;
}

static bool contains_move(const std::vector<Move>& moves, const std::string& uci) {
    return std::any_of(moves.begin(), moves.end(),
                       [&](const Move& move) { return move.to_uci() == uci; });
}

static long long perft(Board& board, int depth) {
    if (depth == 0) return 1;

    auto moves = generate_legal_moves(board);
    if (depth == 1) return (long long)moves.size();

    long long nodes = 0;
    for (const Move& move : moves) {
        UndoInfo undo = make_move(board, move);
        nodes += perft(board, depth - 1);
        unmake_move(board, move, undo);
    }
    return nodes;
}

static void test_rook_capture_clears_castling_rights() {
    Board board = empty_board();
    board.set_piece(4, WHITE * KING);
    board.set_piece(60, BLACK * KING);
    board.set_piece(7, WHITE * ROOK);
    board.set_piece(63, BLACK * ROOK);
    board.turn = BLACK;
    board.castling_rights.K = true;
    board.castling_rights.k = true;

    Move capture(63, 7);
    Board copied = apply_move(board, capture);
    require(!copied.castling_rights.K,
            "apply_move clears white kingside castling after h1 rook capture");

    UndoInfo undo = make_move(board, capture);
    require(!board.castling_rights.K,
            "make_move clears white kingside castling after h1 rook capture");
    unmake_move(board, capture, undo);
    require(board.castling_rights.K,
            "unmake_move restores previous castling rights");
}

static void test_missing_king_is_invalid() {
    Board board = empty_board();
    board.set_piece(60, BLACK * KING);
    require(king_in_check(board, WHITE),
            "missing white king is treated as an invalid/in-check position");
}

static void test_king_capture_is_not_legal() {
    Board board = empty_board();
    board.set_piece(4, WHITE * KING);
    board.set_piece(60, BLACK * KING);
    board.set_piece(52, WHITE * QUEEN);
    board.turn = WHITE;

    auto moves = generate_legal_moves(board);
    for (const Move& move : moves) {
        require(move.to_sq != 60, "legal moves never capture the opponent king");
    }
}

static void test_start_position_move_count() {
    Board board;
    auto moves = generate_legal_moves(board);
    require(moves.size() == 20, "start position has 20 legal moves");
}

static void test_start_position_perft_depth_2() {
    Board board;
    require(perft(board, 2) == 400,
            "start position perft depth 2 remains 400");
}

static void test_movelist_matches_vector_wrapper() {
    Board board = board_from_fen_for_test(
        "r3k2r/p1ppqpb1/bn2pnp1/2PPN3/1p2P3/2N2Q1p/PP1PBPPP/R3K2R w KQkq - 0 1"
    );

    auto vector_moves = generate_legal_moves(board);
    MoveList list_moves;
    generate_legal_moves_into(board, list_moves);

    require((int)vector_moves.size() == list_moves.size(),
            "MoveList legal generation matches vector wrapper count");
    for (const Move& move : list_moves) {
        require(contains_move(vector_moves, move.to_uci()),
                "MoveList legal generation matches vector wrapper moves");
    }
}

static void test_king_square_tracking_make_unmake() {
    Board board = empty_board();
    board.set_piece(4, WHITE * KING);
    board.set_piece(60, BLACK * KING);
    board.turn = WHITE;

    Move move(4, 12);
    UndoInfo undo = make_move(board, move);
    require(board.king_square(WHITE) == 12,
            "make_move updates cached white king square");
    unmake_move(board, move, undo);
    require(board.king_square(WHITE) == 4,
            "unmake_move restores cached white king square");
    require(board.king_square(BLACK) == 60,
            "unmake_move preserves cached black king square");
}

static void test_search_respects_preexisting_stop_request() {
    Board board;
    SharedTT tt;
    std::atomic<bool> stop{true};
    Searcher searcher;

    Move best = searcher.find_best_move(board, 3, -1.0, -1, 0, -1, {},
                                        1, &tt, &stop);

    require(searcher.nodes_searched == 0,
            "search does not clear an existing external stop request");
    require(best.from_sq == 0 && best.to_sq == 0,
            "cancelled search returns the null best move");
}

static void test_reported_fen_king_move_legality() {
    Board board = board_from_fen_for_test(
        "r5k1/1b3rbp/1p4p1/n7/P4P2/2P1p1P1/1Q2R2P/R1Bq2K1 w - - 1 25"
    );

    auto moves = generate_legal_moves(board);
    require(contains_move(moves, "g1f2"), "reported FEN allows Kg1-f2");
    require(contains_move(moves, "e2e1"), "reported FEN allows Re2-e1");
    require(!contains_move(moves, "g1f1"), "reported FEN rejects illegal Kg1-f1");
}

static void test_reported_fen_search_returns_legal_move() {
    Board board = board_from_fen_for_test(
        "r5k1/1b3rbp/1p4p1/n7/P4P2/2P1p1P1/1Q2R2P/R1Bq2K1 w - - 1 25"
    );
    auto legal = generate_legal_moves(board);

    for (int depth = 1; depth <= 3; depth++) {
        SharedTT tt;
        std::atomic<bool> stop{false};
        Searcher searcher;
        Move best = searcher.find_best_move(board, depth, -1.0, -1, 0, -1, {},
                                            1, &tt, &stop);
        require(contains_move(legal, best.to_uci()),
                "reported FEN search returns only legal moves");
    }
}

static void test_hanging_queen_is_penalised() {
    Board hanging = empty_board();
    hanging.set_piece(6, WHITE * KING);
    hanging.set_piece(62, BLACK * KING);
    hanging.set_piece(27, WHITE * QUEEN); // d4
    hanging.set_piece(37, BLACK * KNIGHT); // f5 attacks d4
    hanging.turn = WHITE;

    Board safe = hanging;
    safe.set_piece(27, EMPTY);
    safe.set_piece(28, WHITE * QUEEN); // e4

    require(evaluate(safe) > evaluate(hanging) + 100,
            "evaluation prefers queen out of a concrete rook attack");
}

static int legacy_eval_sum_for_test(const Board& board) {
    int phase = game_phase(board);
    bool endgame = phase <= MAX_PHASE / 2;
    int tempo = (TEMPO_MG * phase) / MAX_PHASE;
    if (board.turn == BLACK)
        tempo = -tempo;

    int score = 0;
    score += eval_material_and_placement(board, endgame);
    score += eval_pawn_structure(board, endgame);
    score += eval_king_safety(board, endgame);
    score += eval_mobility(board);
    score += eval_bishop_pair(board);
    score += eval_rooks(board, endgame);
    score += eval_knight_outposts(board);
    score += eval_backward_pawns(board);
    score += eval_connected_rooks(board);
    score += eval_hanging_pieces(board);
    score += tempo;
    return score;
}

static void test_combined_eval_matches_legacy_components() {
    const std::vector<std::string> fens = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r5k1/1b3rbp/1p4p1/n7/P4P2/2P1p1P1/1Q2R2P/R1Bq2K1 w - - 1 25",
        "r3k2r/p1ppqpb1/bn2pnp1/2PPN3/1p2P3/2N2Q1p/PP1PBPPP/R3K2R w KQkq - 0 1",
        "6k1/5ppp/8/8/8/8/5PPP/6KQ w - - 0 1",
    };

    for (const std::string& fen : fens) {
        Board board = board_from_fen_for_test(fen);
        require(evaluate_handcrafted(board) == legacy_eval_sum_for_test(board),
                "combined eval activity pass matches legacy component sum");
    }
}

static void test_static_exchange_eval_sanity() {
    Board safe_capture = empty_board();
    safe_capture.set_piece(6, WHITE * KING);
    safe_capture.set_piece(62, BLACK * KING);
    safe_capture.set_piece(18, WHITE * KNIGHT); // c3
    safe_capture.set_piece(27, BLACK * PAWN);   // d4
    safe_capture.turn = WHITE;
    require(static_exchange_eval(safe_capture, Move(18, 27)) == PIECE_VALUES[PAWN],
            "SEE scores an undefended pawn capture as a pawn win");

    Board bad_capture = empty_board();
    bad_capture.set_piece(6, WHITE * KING);
    bad_capture.set_piece(62, BLACK * KING);
    bad_capture.set_piece(3, WHITE * QUEEN);    // d1
    bad_capture.set_piece(51, BLACK * PAWN);    // d7
    bad_capture.set_piece(59, BLACK * ROOK);    // d8 recaptures on d7
    bad_capture.turn = WHITE;
    require(static_exchange_eval(bad_capture, Move(3, 51)) < -700,
            "SEE detects a queen capture that loses to a rook recapture");

    Board xray_capture = empty_board();
    xray_capture.set_piece(6, WHITE * KING);
    xray_capture.set_piece(62, BLACK * KING);
    xray_capture.set_piece(3, WHITE * ROOK);    // d1
    xray_capture.set_piece(27, BLACK * PAWN);   // d4
    xray_capture.set_piece(59, BLACK * ROOK);   // d8 recaptures down the file
    xray_capture.turn = WHITE;
    require(static_exchange_eval(xray_capture, Move(3, 27)) == -400,
            "SEE sees x-ray rook recapture after the target changes hands");

    Board hanging_promotion = empty_board();
    hanging_promotion.set_piece(6, WHITE * KING);
    hanging_promotion.set_piece(46, BLACK * KING);
    hanging_promotion.set_piece(48, WHITE * PAWN); // a7
    hanging_promotion.set_piece(63, BLACK * ROOK); // h8 attacks a8
    hanging_promotion.turn = WHITE;
    require(static_exchange_eval(hanging_promotion, Move(48, 56, QUEEN)) < 0,
            "SEE includes quiet promotion gain and the hanging promoted queen");
}

static void test_shared_tt_resize() {
    SharedTT tt;
    size_t default_size = tt.slots.size();
    require(sizeof(TTSlot) <= 32, "TT slot stays compact");
    require(default_size > 0 && (default_size & (default_size - 1)) == 0,
            "default TT size is a power of two");

    tt.resize_mb(1);
    require(tt.hash_mb == 1, "TT records requested hash size");
    require(tt.slots.size() > 0 && (tt.slots.size() & (tt.slots.size() - 1)) == 0,
            "resized TT size is a power of two");
    require(tt.index(0x12345678ULL) < tt.slots.size(),
            "TT index stays inside resized table");

    uint16_t gen = tt.generation;
    tt.new_game();
    require(tt.generation == gen + 1, "TT generation advances on new game");
}

static void test_tt_score_eval_and_replacement_policy() {
    SharedTT tt;
    tt.resize_mb(1);
    Searcher searcher;
    searcher.tt = &tt;

    Hash hash_a = 0x1234ULL;
    Hash hash_b = hash_a + (Hash)tt.slots.size();
    require(tt.index(hash_a) == tt.index(hash_b),
            "TT test hashes collide in the resized table");

    searcher.tt_store(hash_a, 8, CHECKMATE_SCORE - 3, TT_EXACT,
                      Move(1, 2), 2, 0, true, true);
    TTSlot& slot = tt.slots[tt.index(hash_a)];
    require(slot.hash == hash_a, "TT stores the first colliding hash");
    require(slot.score == CHECKMATE_SCORE - 1,
            "TT stores mate-range scores without int16 truncation");
    require((slot.flags & TT_SLOT_HAS_EVAL) && slot.eval == 0,
            "TT treats a zero static eval as a valid cached eval");

    searcher.tt_store(hash_b, 2, 40, TT_UPPER_BOUND,
                      Move(3, 4), 0, 25, false, true);
    require(slot.hash == hash_b,
            "TT uses the active one-slot collision replacement policy");

    searcher.tt_store(hash_b, 1, 30, TT_UPPER_BOUND,
                      Move(4, 5), 0, 30, false, true);
    require(slot.hash == hash_b && slot.depth == 2,
            "TT keeps a deeper same-position entry over a shallow update");
}

static void test_tt_eval_applies_correction_once() {
    Board board;
    SharedTT tt;
    Searcher searcher;
    searcher.tt = &tt;

    Hash hash = compute_hash(board);
    int raw_eval = searcher.score_from_perspective(board);
    searcher.update_correction(board, 8, raw_eval + 800, raw_eval);
    int correction = searcher.get_correction(board);
    require(correction != 0, "TT correction test seeds correction history");

    searcher.tt_store(hash, 0, 0, TT_UPPER_BOUND, Move(0, 0), 0,
                      raw_eval, false, true);
    searcher.alphabeta(board, hash, 1,
                       -CHECKMATE_SCORE + 1, CHECKMATE_SCORE - 1, 0);

    int expected = std::clamp(raw_eval + correction / 8,
                              -(CHECKMATE_SCORE / 2),
                               CHECKMATE_SCORE / 2);
    require(searcher.ply_stack[0].static_eval == expected,
            "TT cached eval receives correction history exactly once");
}

int main() {
    init_zobrist();
    test_start_position_move_count();
    test_start_position_perft_depth_2();
    test_movelist_matches_vector_wrapper();
    test_king_square_tracking_make_unmake();
    test_rook_capture_clears_castling_rights();
    test_missing_king_is_invalid();
    test_king_capture_is_not_legal();
    test_search_respects_preexisting_stop_request();
    test_reported_fen_king_move_legality();
    test_reported_fen_search_returns_legal_move();
    test_hanging_queen_is_penalised();
    test_combined_eval_matches_legacy_components();
    test_static_exchange_eval_sanity();
    test_shared_tt_resize();
    test_tt_score_eval_and_replacement_policy();
    test_tt_eval_applies_correction_once();
    std::cout << "Smoke tests passed\n";
    return 0;
}
