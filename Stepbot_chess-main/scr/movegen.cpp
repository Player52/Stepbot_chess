// movegen.cpp
// Move generation implementation.
// C++ equivalent of movegen.py.

#include "movegen.h"
#include "evaluate.h"
#include <cstdlib>    // For std::abs
#include <algorithm>  // For std::copy (used when deep-copying the board)

static void clear_rook_castling_right(CastlingRights& rights, int rook_sq) {
    if (rook_sq == 0)  rights.Q = false;
    if (rook_sq == 7)  rights.K = false;
    if (rook_sq == 56) rights.q = false;
    if (rook_sq == 63) rights.k = false;
}

// ─────────────────────────────────────────
// MOVE::TO_UCI
// Convert a move to a UCI string e.g. "e2e4", "e7e8q"
// ─────────────────────────────────────────

std::string Move::to_uci() const {
    std::string uci = square_name(from_sq) + square_name(to_sq);
    if (promotion) {
        // Promotion piece as lowercase letter
        switch (promotion) {
            case KNIGHT: uci += 'n'; break;
            case BISHOP: uci += 'b'; break;
            case ROOK:   uci += 'r'; break;
            case QUEEN:  uci += 'q'; break;
        }
    }
    return uci;
}

// ─────────────────────────────────────────
// APPLY MOVE
// Returns a new board with the move applied.
// Does not modify the original.
// ─────────────────────────────────────────

Board apply_move(const Board& board, const Move& move) {
    // Copy the board — in C++ assigning a struct copies it by value
    Board new_board = board;

    int piece      = new_board.get_piece(move.from_sq);
    int piece_type = std::abs(piece);
    int colour     = (piece > 0) ? WHITE : BLACK;
    int captured_piece = board.get_piece(move.to_sq);

    // Move the piece
    new_board.set_piece(move.to_sq,   piece);
    new_board.set_piece(move.from_sq, EMPTY);

    // Promotion
    if (move.promotion) {
        new_board.set_piece(move.to_sq, colour * move.promotion);
    }

    // En passant capture
    if (piece_type == PAWN && move.to_sq == board.en_passant_sq) {
        int captured_pawn_sq = move.to_sq - (colour == WHITE ? 8 : -8);
        new_board.set_piece(captured_pawn_sq, EMPTY);
    }

    // Update en passant square
    if (piece_type == PAWN && std::abs(move.to_sq - move.from_sq) == 16) {
        new_board.en_passant_sq = (move.from_sq + move.to_sq) / 2;
    } else {
        new_board.en_passant_sq = -1;
    }

    // Castling: move the rook too
    if (piece_type == KING) {
        int diff = move.to_sq - move.from_sq;
        if (diff == 2) {   // Kingside
            new_board.set_piece(move.from_sq + 1, colour * ROOK);
            new_board.set_piece(move.from_sq + 3, EMPTY);
        } else if (diff == -2) {   // Queenside
            new_board.set_piece(move.from_sq - 1, colour * ROOK);
            new_board.set_piece(move.from_sq - 4, EMPTY);
        }
    }

    // Update castling rights
    if (piece_type == KING) {
        if (colour == WHITE) {
            new_board.castling_rights.K = false;
            new_board.castling_rights.Q = false;
        } else {
            new_board.castling_rights.k = false;
            new_board.castling_rights.q = false;
        }
    }
    if (piece_type == ROOK) {
        clear_rook_castling_right(new_board.castling_rights, move.from_sq);
    }
    if (std::abs(captured_piece) == ROOK) {
        clear_rook_castling_right(new_board.castling_rights, move.to_sq);
    }

    // Update halfmove clock
    if (piece_type == PAWN || board.colour_at(move.to_sq) == -colour) {
        new_board.halfmove_clock = 0;
    } else {
        new_board.halfmove_clock++;
    }

    // Switch turn
    new_board.turn = -board.turn;
    if (new_board.turn == WHITE) {
        new_board.fullmove_number++;
    }

    return new_board;
}

// ─────────────────────────────────────────
// MAKE MOVE
// Mutates the board in-place and returns an UndoInfo
// so the move can be reversed with unmake_move.
// ─────────────────────────────────────────

UndoInfo make_move(Board& board, const Move& move) {
    UndoInfo undo;
    undo.captured_piece  = board.get_piece(move.to_sq);
    undo.en_passant_sq   = board.en_passant_sq;
    undo.castling_rights = board.castling_rights;
    undo.halfmove_clock  = board.halfmove_clock;

    int piece      = board.get_piece(move.from_sq);
    int piece_type = std::abs(piece);
    int colour     = (piece > 0) ? WHITE : BLACK;

    // Move the piece
    board.set_piece(move.to_sq,   piece);
    board.set_piece(move.from_sq, EMPTY);

    // Promotion
    if (move.promotion)
        board.set_piece(move.to_sq, colour * move.promotion);

    // En passant capture
    if (piece_type == PAWN && move.to_sq == undo.en_passant_sq) {
        int captured_pawn_sq = move.to_sq - (colour == WHITE ? 8 : -8);
        board.set_piece(captured_pawn_sq, EMPTY);
    }

    // Update en passant square
    if (piece_type == PAWN && std::abs(move.to_sq - move.from_sq) == 16)
        board.en_passant_sq = (move.from_sq + move.to_sq) / 2;
    else
        board.en_passant_sq = -1;

    // Castling: move the rook
    if (piece_type == KING) {
        int diff = move.to_sq - move.from_sq;
        if (diff == 2) {   // Kingside
            board.set_piece(move.from_sq + 1, colour * ROOK);
            board.set_piece(move.from_sq + 3, EMPTY);
        } else if (diff == -2) {   // Queenside
            board.set_piece(move.from_sq - 1, colour * ROOK);
            board.set_piece(move.from_sq - 4, EMPTY);
        }
    }

    // Update castling rights
    if (piece_type == KING) {
        if (colour == WHITE) {
            board.castling_rights.K = false;
            board.castling_rights.Q = false;
        } else {
            board.castling_rights.k = false;
            board.castling_rights.q = false;
        }
    }
    if (piece_type == ROOK) {
        clear_rook_castling_right(board.castling_rights, move.from_sq);
    }
    // Capturing the rook also removes castling rights
    if (std::abs(undo.captured_piece) == ROOK)
        clear_rook_castling_right(board.castling_rights, move.to_sq);

    // Halfmove clock
    if (piece_type == PAWN || undo.captured_piece != EMPTY)
        board.halfmove_clock = 0;
    else
        board.halfmove_clock++;

    // Switch turn
    board.turn = -board.turn;
    if (board.turn == WHITE)
        board.fullmove_number++;

    return undo;
}

// ─────────────────────────────────────────
// UNMAKE MOVE
// Restores the board to exactly the state before make_move.
// ─────────────────────────────────────────

void unmake_move(Board& board, const Move& move, const UndoInfo& undo) {
    // Restore turn first (we need the original colour for piece logic)
    board.turn = -board.turn;
    if (board.turn == BLACK)
        board.fullmove_number--;

    int colour     = board.turn;  // Now restored to the side that made the move
    int piece_type = std::abs(board.get_piece(move.to_sq));

    // If promotion, the piece on to_sq is the promoted piece — restore pawn
    int moved_piece = board.get_piece(move.to_sq);
    if (move.promotion) {
        moved_piece = colour * PAWN;
        piece_type  = PAWN;
    }

    // Restore the moving piece to from_sq
    board.set_piece(move.from_sq, moved_piece);

    // Restore to_sq (captured piece or empty)
    board.set_piece(move.to_sq, undo.captured_piece);

    // Restore en passant captured pawn
    if (piece_type == PAWN && move.to_sq == undo.en_passant_sq) {
        int captured_pawn_sq = move.to_sq - (colour == WHITE ? 8 : -8);
        board.set_piece(captured_pawn_sq, -colour * PAWN);
    }

    // Restore rook from castling
    if (piece_type == KING) {
        int diff = move.to_sq - move.from_sq;
        if (diff == 2) {   // Kingside
            board.set_piece(move.from_sq + 3, colour * ROOK);
            board.set_piece(move.from_sq + 1, EMPTY);
        } else if (diff == -2) {   // Queenside
            board.set_piece(move.from_sq - 4, colour * ROOK);
            board.set_piece(move.from_sq - 1, EMPTY);
        }
    }

    // Restore state
    board.en_passant_sq   = undo.en_passant_sq;
    board.castling_rights = undo.castling_rights;
    board.halfmove_clock  = undo.halfmove_clock;
}

// ─────────────────────────────────────────
// PAWN MOVES
// Appends pawn moves to the moves vector.
// 'std::vector<Move>&' means we pass by reference — we modify the
// vector directly rather than returning a new one.
// ─────────────────────────────────────────

void pawn_moves(const Board& board, int from_sq, MoveList& moves) {
    int colour     = board.turn;
    int direction  = (colour == WHITE) ? 8 : -8;
    int start_rank = (colour == WHITE) ? 1 : 6;
    int promo_rank = (colour == WHITE) ? 7 : 0;

    // Single push
    int target = from_sq + direction;
    if (target >= 0 && target < 64 && board.is_empty(target)) {
        if (rank_of(target) == promo_rank) {
            // Promotion — add one move per promotion piece
            for (int promo : {QUEEN, ROOK, BISHOP, KNIGHT}) {
                moves.push_back(Move(from_sq, target, promo));
            }
        } else {
            moves.push_back(Move(from_sq, target));
        }

        // Double push from starting rank
        if (rank_of(from_sq) == start_rank) {
            int target2 = from_sq + direction * 2;
            if (board.is_empty(target2)) {
                moves.push_back(Move(from_sq, target2));
            }
        }
    }

    // Captures
    for (int offset : {direction + 1, direction - 1}) {
        target = from_sq + offset;
        if (target < 0 || target >= 64) continue;
        if (std::abs(file_of(from_sq) - file_of(target)) != 1) continue;

        if (board.colour_at(target) == -colour &&
            std::abs(board.get_piece(target)) != KING) {
            // Normal capture
            if (rank_of(target) == promo_rank) {
                for (int promo : {QUEEN, ROOK, BISHOP, KNIGHT}) {
                    moves.push_back(Move(from_sq, target, promo));
                }
            } else {
                moves.push_back(Move(from_sq, target));
            }
        } else if (target == board.en_passant_sq) {
            // En passant
            moves.push_back(Move(from_sq, target));
        }
    }
}

// ─────────────────────────────────────────
// KNIGHT MOVES
// ─────────────────────────────────────────

void knight_moves(const Board& board, int from_sq, MoveList& moves) {
    int colour = board.turn;

    for (int offset : KNIGHT_OFFSETS) {
        int target = from_sq + offset;
        if (target < 0 || target >= 64) continue;

        int file_diff = std::abs(file_of(from_sq) - file_of(target));
        int rank_diff = std::abs(rank_of(from_sq) - rank_of(target));

        // Valid knight move: one axis moves 1, the other moves 2
        if (!((file_diff == 1 && rank_diff == 2) ||
              (file_diff == 2 && rank_diff == 1))) continue;

        if (board.colour_at(target) != colour &&
            std::abs(board.get_piece(target)) != KING) {
            moves.push_back(Move(from_sq, target));
        }
    }
}

// ─────────────────────────────────────────
// SLIDING MOVES (bishop, rook, queen)
// dirs and num_dirs let us pass different direction arrays
// without code duplication.
// ─────────────────────────────────────────

void sliding_moves(const Board& board, int from_sq,
                   const int* dirs, int num_dirs,
                   MoveList& moves) {
    int colour = board.turn;

    // Loop over each direction
    // 'int d = 0; d < num_dirs; d++' is like Python's range(num_dirs)
    for (int d = 0; d < num_dirs; d++) {
        int direction = dirs[d];
        int target    = from_sq;

        while (true) {
            int prev_file = file_of(target);
            target += direction;

            if (target < 0 || target >= 64) break;

            int new_file = file_of(target);

            // Prevent wrap-around
            if (direction ==  1 && std::abs(prev_file - new_file) != 1) break;
            if (direction == -1 && std::abs(prev_file - new_file) != 1) break;
            if (direction ==  9 && new_file <= prev_file) break;
            if (direction == -7 && new_file <= prev_file) break;
            if (direction ==  7 && new_file >= prev_file) break;
            if (direction == -9 && new_file >= prev_file) break;

            int target_colour = board.colour_at(target);

            if (target_colour == colour) break;

            if (std::abs(board.get_piece(target)) != KING)
                moves.push_back(Move(from_sq, target));

            if (target_colour == -colour) break;  // Capture — stop sliding
        }
    }
}

// ─────────────────────────────────────────
// KING MOVES
// ─────────────────────────────────────────

void king_moves(const Board& board, int from_sq, MoveList& moves) {
    int colour = board.turn;

    // Normal moves
    for (int direction : ALL_DIRS) {
        int target = from_sq + direction;
        if (target < 0 || target >= 64) continue;
        if (std::abs(file_of(from_sq) - file_of(target)) > 1) continue;
        if (std::abs(rank_of(from_sq) - rank_of(target)) > 1) continue;
        if (board.colour_at(target) != colour &&
            std::abs(board.get_piece(target)) != KING) {
            moves.push_back(Move(from_sq, target));
        }
    }

    // Castling
    // Must check: (1) squares between king and rook are empty,
    // (2) king is not currently in check,
    // (3) king does not pass through an attacked square.
    int enemy = -colour;
    bool in_check_now = square_attacked_by(board, from_sq, enemy);
    if (!in_check_now) {
        if (colour == WHITE) {
            if (board.castling_rights.K
                && board.is_empty(5) && board.is_empty(6)
                && !square_attacked_by(board, 5, enemy)
                && !square_attacked_by(board, 6, enemy))
                moves.push_back(Move(from_sq, 6));
            if (board.castling_rights.Q
                && board.is_empty(3) && board.is_empty(2) && board.is_empty(1)
                && !square_attacked_by(board, 3, enemy)
                && !square_attacked_by(board, 2, enemy))
                moves.push_back(Move(from_sq, 2));
        } else {
            if (board.castling_rights.k
                && board.is_empty(61) && board.is_empty(62)
                && !square_attacked_by(board, 61, enemy)
                && !square_attacked_by(board, 62, enemy))
                moves.push_back(Move(from_sq, 62));
            if (board.castling_rights.q
                && board.is_empty(59) && board.is_empty(58) && board.is_empty(57)
                && !square_attacked_by(board, 59, enemy)
                && !square_attacked_by(board, 58, enemy))
                moves.push_back(Move(from_sq, 58));
        }
    }
}

// ─────────────────────────────────────────
// PSEUDO-LEGAL MOVE GENERATION
// ─────────────────────────────────────────

void generate_pseudo_legal_moves_into(const Board& board, MoveList& moves) {
    // Declare an empty vector — like Python's moves = []
    moves.clear();

    // Reserve space upfront — avoids repeated memory allocation
    // A typical chess position has ~30 legal moves

    for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
        int piece = board.get_piece(sq_idx);
        if (piece == EMPTY) continue;
        if (board.colour_at(sq_idx) != board.turn) continue;

        int piece_type = std::abs(piece);

        // Arrays for sliding directions — we pass a pointer and a count
        // because C++ doesn't have Python's len() for raw arrays
        static const int diag[]     = {9, 7, -7, -9};
        static const int straight[] = {8, -8, 1, -1};
        static const int all[]      = {9, 7, -7, -9, 8, -8, 1, -1};

        switch (piece_type) {
            case PAWN:   pawn_moves  (board, sq_idx, moves); break;
            case KNIGHT: knight_moves(board, sq_idx, moves); break;
            case BISHOP: sliding_moves(board, sq_idx, diag,     4, moves); break;
            case ROOK:   sliding_moves(board, sq_idx, straight, 4, moves); break;
            case QUEEN:  sliding_moves(board, sq_idx, all,      8, moves); break;
            case KING:   king_moves  (board, sq_idx, moves); break;
        }
    }

}

std::vector<Move> generate_pseudo_legal_moves(const Board& board) {
    MoveList moves;
    generate_pseudo_legal_moves_into(board, moves);
    return moves.to_vector();
}

// ─────────────────────────────────────────
// CHECK DETECTION
// ─────────────────────────────────────────

bool square_attacked_by(const Board& board, int target_sq, int attacker_colour) {
    // Knight attacks
    for (int offset : KNIGHT_OFFSETS) {
        int sq_idx = target_sq + offset;
        if (sq_idx < 0 || sq_idx >= 64) continue;
        if (std::abs(file_of(target_sq) - file_of(sq_idx)) > 2) continue;
        if (board.get_piece(sq_idx) == attacker_colour * KNIGHT) return true;
    }

    // Straight attacks (rook / queen)
    static const int straight[] = {8, -8, 1, -1};
    for (int direction : straight) {
        int curr = target_sq;
        while (true) {
            int prev_file = file_of(curr);
            curr += direction;
            if (curr < 0 || curr >= 64) break;
            int new_file = file_of(curr);
            if ((direction == 1 || direction == -1) &&
                std::abs(prev_file - new_file) != 1) break;
            int piece = board.get_piece(curr);
            if (piece != EMPTY) {
                if (piece == attacker_colour * ROOK ||
                    piece == attacker_colour * QUEEN) return true;
                break;
            }
        }
    }

    // Diagonal attacks (bishop / queen)
    static const int diag[] = {9, 7, -7, -9};
    for (int direction : diag) {
        int curr = target_sq;
        while (true) {
            int prev_file = file_of(curr);
            curr += direction;
            if (curr < 0 || curr >= 64) break;
            int new_file = file_of(curr);
            if (direction ==  9 && new_file <= prev_file) break;
            if (direction == -7 && new_file <= prev_file) break;
            if (direction ==  7 && new_file >= prev_file) break;
            if (direction == -9 && new_file >= prev_file) break;
            int piece = board.get_piece(curr);
            if (piece != EMPTY) {
                if (piece == attacker_colour * BISHOP ||
                    piece == attacker_colour * QUEEN) return true;
                break;
            }
        }
    }

    // Pawn attacks
    // If attacker is BLACK, black pawns attack downward (directions -9, -7)
    // If attacker is WHITE, white pawns attack upward (directions 9, 7)
    int pawn_dirs[2];
    if (attacker_colour == BLACK) {
        pawn_dirs[0] = -9; pawn_dirs[1] = -7;
    } else {
        pawn_dirs[0] =  9; pawn_dirs[1] =  7;
    }
    for (int direction : pawn_dirs) {
        int sq_idx = target_sq + direction;
        if (sq_idx < 0 || sq_idx >= 64) continue;
        if (std::abs(file_of(target_sq) - file_of(sq_idx)) != 1) continue;
        if (board.get_piece(sq_idx) == attacker_colour * PAWN) return true;
    }

    // King attacks
    for (int direction : ALL_DIRS) {
        int sq_idx = target_sq + direction;
        if (sq_idx < 0 || sq_idx >= 64) continue;
        if (std::abs(file_of(target_sq) - file_of(sq_idx)) > 1) continue;
        if (std::abs(rank_of(target_sq) - rank_of(sq_idx)) > 1) continue;
        if (board.get_piece(sq_idx) == attacker_colour * KING) return true;
    }

    return false;
}

bool king_in_check(const Board& board, int colour) {
    int king_sq = board.king_square(colour);
    if (king_sq == -1) return true;
    return square_attacked_by(board, king_sq, -colour);
}

// ─────────────────────────────────────────
// LEGAL MOVE GENERATION
// Filters pseudo-legal moves that leave the king in check
// ─────────────────────────────────────────

void generate_legal_moves_into(const Board& board, MoveList& legal) {
    MoveList pseudo;
    generate_pseudo_legal_moves_into(board, pseudo);
    legal.clear();
    Board test_board = board;

    for (const Move& move : pseudo) {
        // Apply the move and check if our king is still safe
        UndoInfo undo = make_move(test_board, move);
        if (!king_in_check(test_board, board.turn)) {
            // '.push_back()' appends to the vector — like Python's .append()
            legal.push_back(move);
        }
        unmake_move(test_board, move, undo);
    }

}

std::vector<Move> generate_legal_moves(const Board& board) {
    MoveList legal;
    generate_legal_moves_into(board, legal);
    return legal.to_vector();
}

// ─────────────────────────────────────────
// STATIC EXCHANGE EVALUATION (SEE)
// ─────────────────────────────────────────

static bool knight_attacks_square(int from_sq, int to_sq) {
    int file_diff = std::abs(file_of(from_sq) - file_of(to_sq));
    int rank_diff = std::abs(rank_of(from_sq) - rank_of(to_sq));
    return (file_diff == 1 && rank_diff == 2) ||
           (file_diff == 2 && rank_diff == 1);
}

static int promotion_piece_for_capture(int piece_type, int colour,
                                       int target_sq, int requested = 0) {
    if (piece_type != PAWN)
        return 0;
    int target_rank = rank_of(target_sq);
    if ((colour == WHITE && target_rank == 7) ||
        (colour == BLACK && target_rank == 0))
        return requested ? requested : QUEEN;
    return 0;
}

static int promotion_delta(int piece_type, int promo_piece) {
    if (piece_type == PAWN && promo_piece)
        return PIECE_VALUES[promo_piece] - PIECE_VALUES[PAWN];
    return 0;
}

// Least-valuable attacker of target_sq for side `stm` (WHITE or BLACK).
// Uses queen promotion when a pawn capture reaches the back rank.
// King is omitted — king swaps in SEE are rarely sound and risk illegal positions.
static bool ray_step_valid(int prev_sq, int next_sq, int direction) {
    if (next_sq < 0 || next_sq >= 64) return false;
    int prev_file = file_of(prev_sq);
    int next_file = file_of(next_sq);
    if ((direction == 1 || direction == -1) &&
        std::abs(next_file - prev_file) != 1)
        return false;
    if (direction == 9  && next_file <= prev_file) return false;
    if (direction == -7 && next_file <= prev_file) return false;
    if (direction == 7  && next_file >= prev_file) return false;
    if (direction == -9 && next_file >= prev_file) return false;
    return true;
}

static int find_pawn_attacker(const std::array<int, 64>& occ,
                              int target_sq, int stm) {
    const int offsets[2] = {
        stm == WHITE ? -7 : 7,
        stm == WHITE ? -9 : 9,
    };
    for (int offset : offsets) {
        int from_sq = target_sq + offset;
        if (from_sq < 0 || from_sq >= 64)
            continue;
        if (std::abs(file_of(from_sq) - file_of(target_sq)) != 1)
            continue;
        if (occ[from_sq] == stm * PAWN)
            return from_sq;
    }
    return -1;
}

static int find_knight_attacker(const std::array<int, 64>& occ,
                                int target_sq, int stm) {
    for (int offset : KNIGHT_OFFSETS) {
        int from_sq = target_sq + offset;
        if (from_sq < 0 || from_sq >= 64)
            continue;
        if (occ[from_sq] == stm * KNIGHT &&
            knight_attacks_square(from_sq, target_sq))
            return from_sq;
    }
    return -1;
}

static int find_slider_attacker(const std::array<int, 64>& occ,
                                int target_sq, int stm, int piece_type,
                                const int* dirs, int num_dirs) {
    for (int i = 0; i < num_dirs; i++) {
        int direction = dirs[i];
        int curr = target_sq;
        while (true) {
            int next = curr + direction;
            if (!ray_step_valid(curr, next, direction))
                break;
            curr = next;

            int piece = occ[curr];
            if (piece == EMPTY)
                continue;
            if (piece == stm * piece_type)
                return curr;
            break;
        }
    }
    return -1;
}

static int find_least_valuable_attacker(const std::array<int, 64>& occ,
                                        int target_sq, int stm,
                                        int& attacker_type,
                                        int& promo_piece) {
    static const int diag[] = {9, 7, -7, -9};
    static const int straight[] = {8, -8, 1, -1};
    static const int all[] = {9, 7, -7, -9, 8, -8, 1, -1};

    int from_sq = find_pawn_attacker(occ, target_sq, stm);
    if (from_sq != -1) {
        attacker_type = PAWN;
        promo_piece = promotion_piece_for_capture(PAWN, stm, target_sq);
        return from_sq;
    }

    from_sq = find_knight_attacker(occ, target_sq, stm);
    if (from_sq != -1) {
        attacker_type = KNIGHT;
        promo_piece = 0;
        return from_sq;
    }

    from_sq = find_slider_attacker(occ, target_sq, stm, BISHOP, diag, 4);
    if (from_sq != -1) {
        attacker_type = BISHOP;
        promo_piece = 0;
        return from_sq;
    }

    from_sq = find_slider_attacker(occ, target_sq, stm, ROOK, straight, 4);
    if (from_sq != -1) {
        attacker_type = ROOK;
        promo_piece = 0;
        return from_sq;
    }

    from_sq = find_slider_attacker(occ, target_sq, stm, QUEEN, all, 8);
    if (from_sq != -1) {
        attacker_type = QUEEN;
        promo_piece = 0;
        return from_sq;
    }

    attacker_type = EMPTY;
    promo_piece = 0;
    return -1;
}

int static_exchange_eval(const Board& board, const Move& move) {
    int from_piece = board.get_piece(move.from_sq);
    if (from_piece == EMPTY) return 0;
    int mover_ty = std::abs(from_piece);

    bool en_passant = (mover_ty == PAWN && move.to_sq == board.en_passant_sq);
    bool capture    = !board.is_empty(move.to_sq) || en_passant;
    bool quiet_q_promo =
        mover_ty == PAWN &&
        board.is_empty(move.to_sq) &&
        (rank_of(move.to_sq) == 0 || rank_of(move.to_sq) == 7) &&
        (move.promotion == QUEEN || move.promotion == 0);

    if (!capture && !quiet_q_promo)
        return 0;

    int victim_val = 0;
    if (en_passant)
        victim_val = PIECE_VALUES[PAWN];
    else if (!board.is_empty(move.to_sq))
        victim_val = PIECE_VALUES[std::abs(board.get_piece(move.to_sq))];

    int colour = from_piece > 0 ? WHITE : BLACK;
    int promo_piece = promotion_piece_for_capture(mover_ty, colour,
                                                  move.to_sq,
                                                  move.promotion);
    int result_ty = promo_piece ? promo_piece : mover_ty;

    int gain[32];
    int depth = 0;
    gain[0] = victim_val + promotion_delta(mover_ty, promo_piece);

    std::array<int, 64> occ = board.squares;
    occ[move.from_sq] = EMPTY;
    if (en_passant) {
        int captured_pawn_sq = move.to_sq - (colour == WHITE ? 8 : -8);
        occ[captured_pawn_sq] = EMPTY;
    }
    occ[move.to_sq] = colour * result_ty;

    int target_value = PIECE_VALUES[result_ty];
    int stm = -colour;

    while (depth < 31) {
        int attacker_ty = EMPTY;
        int attacker_promo = 0;
        int from_sq = find_least_valuable_attacker(occ, move.to_sq, stm,
                                                   attacker_ty,
                                                   attacker_promo);
        if (from_sq == -1)
            break;

        ++depth;
        gain[depth] = target_value
                    + promotion_delta(attacker_ty, attacker_promo)
                    - gain[depth - 1];

        int new_target_ty = attacker_promo ? attacker_promo : attacker_ty;
        occ[from_sq] = EMPTY;
        occ[move.to_sq] = stm * new_target_ty;
        target_value = PIECE_VALUES[new_target_ty];
        stm = -stm;
    }

    while (depth > 0) {
        gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
        --depth;
    }

    return gain[0];
}
