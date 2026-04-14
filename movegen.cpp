// movegen.cpp
// Move generation implementation.
// C++ equivalent of movegen.py.

#include "movegen.h"
#include "evaluate.h"
#include <cstdlib>    // For std::abs
#include <algorithm>  // For std::copy (used when deep-copying the board)

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
        if (move.from_sq == 0)  new_board.castling_rights.Q = false;
        if (move.from_sq == 7)  new_board.castling_rights.K = false;
        if (move.from_sq == 56) new_board.castling_rights.q = false;
        if (move.from_sq == 63) new_board.castling_rights.k = false;
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
        if (move.from_sq == 0)  board.castling_rights.Q = false;
        if (move.from_sq == 7)  board.castling_rights.K = false;
        if (move.from_sq == 56) board.castling_rights.q = false;
        if (move.from_sq == 63) board.castling_rights.k = false;
    }
    // Capturing the rook also removes castling rights
    if (move.to_sq == 0)  board.castling_rights.Q = false;
    if (move.to_sq == 7)  board.castling_rights.K = false;
    if (move.to_sq == 56) board.castling_rights.q = false;
    if (move.to_sq == 63) board.castling_rights.k = false;

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

void pawn_moves(const Board& board, int from_sq, std::vector<Move>& moves) {
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

        if (board.colour_at(target) == -colour) {
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

void knight_moves(const Board& board, int from_sq, std::vector<Move>& moves) {
    int colour = board.turn;

    for (int offset : KNIGHT_OFFSETS) {
        int target = from_sq + offset;
        if (target < 0 || target >= 64) continue;

        int file_diff = std::abs(file_of(from_sq) - file_of(target));
        int rank_diff = std::abs(rank_of(from_sq) - rank_of(target));

        // Valid knight move: one axis moves 1, the other moves 2
        if (!((file_diff == 1 && rank_diff == 2) ||
              (file_diff == 2 && rank_diff == 1))) continue;

        if (board.colour_at(target) != colour) {
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
                   std::vector<Move>& moves) {
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

            moves.push_back(Move(from_sq, target));

            if (target_colour == -colour) break;  // Capture — stop sliding
        }
    }
}

// ─────────────────────────────────────────
// KING MOVES
// ─────────────────────────────────────────

void king_moves(const Board& board, int from_sq, std::vector<Move>& moves) {
    int colour = board.turn;

    // Normal moves
    for (int direction : ALL_DIRS) {
        int target = from_sq + direction;
        if (target < 0 || target >= 64) continue;
        if (std::abs(file_of(from_sq) - file_of(target)) > 1) continue;
        if (std::abs(rank_of(from_sq) - rank_of(target)) > 1) continue;
        if (board.colour_at(target) != colour) {
            moves.push_back(Move(from_sq, target));
        }
    }

    // Castling
    if (colour == WHITE) {
        if (board.castling_rights.K && board.is_empty(5) && board.is_empty(6))
            moves.push_back(Move(from_sq, 6));
        if (board.castling_rights.Q && board.is_empty(3) &&
            board.is_empty(2) && board.is_empty(1))
            moves.push_back(Move(from_sq, 2));
    } else {
        if (board.castling_rights.k && board.is_empty(61) && board.is_empty(62))
            moves.push_back(Move(from_sq, 62));
        if (board.castling_rights.q && board.is_empty(59) &&
            board.is_empty(58) && board.is_empty(57))
            moves.push_back(Move(from_sq, 58));
    }
}

// ─────────────────────────────────────────
// PSEUDO-LEGAL MOVE GENERATION
// ─────────────────────────────────────────

std::vector<Move> generate_pseudo_legal_moves(const Board& board) {
    // Declare an empty vector — like Python's moves = []
    std::vector<Move> moves;

    // Reserve space upfront — avoids repeated memory allocation
    // A typical chess position has ~30 legal moves
    moves.reserve(50);

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

    return moves;
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
    // Find the king
    int king_sq = -1;
    for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
        if (board.get_piece(sq_idx) == colour * KING) {
            king_sq = sq_idx;
            break;
        }
    }
    if (king_sq == -1) return false;
    return square_attacked_by(board, king_sq, -colour);
}

// ─────────────────────────────────────────
// LEGAL MOVE GENERATION
// Filters pseudo-legal moves that leave the king in check
// ─────────────────────────────────────────

std::vector<Move> generate_legal_moves(const Board& board) {
    std::vector<Move> pseudo = generate_pseudo_legal_moves(board);
    std::vector<Move> legal;
    legal.reserve(pseudo.size());

    for (const Move& move : pseudo) {
        // Apply the move and check if our king is still safe
        Board test_board = apply_move(board, move);
        if (!king_in_check(test_board, board.turn)) {
            // '.push_back()' appends to the vector — like Python's .append()
            legal.push_back(move);
        }
    }

    return legal;
}

// ─────────────────────────────────────────
// STATIC EXCHANGE EVALUATION (SEE)
// ─────────────────────────────────────────

static bool pawn_attacks_square(int from_sq, int to_sq, int colour) {
    int dr = rank_of(to_sq) - rank_of(from_sq);
    int df = file_of(to_sq) - file_of(from_sq);
    if (colour == WHITE)
        return dr == 1 && std::abs(df) == 1;
    return dr == -1 && std::abs(df) == 1;
}

static bool knight_attacks_square(int from_sq, int to_sq) {
    int file_diff = std::abs(file_of(from_sq) - file_of(to_sq));
    int rank_diff = std::abs(rank_of(from_sq) - rank_of(to_sq));
    return (file_diff == 1 && rank_diff == 2) ||
           (file_diff == 2 && rank_diff == 1);
}

static bool king_attacks_square(int from_sq, int to_sq) {
    return std::abs(file_of(from_sq) - file_of(to_sq)) <= 1 &&
           std::abs(rank_of(from_sq) - rank_of(to_sq)) <= 1 &&
           from_sq != to_sq;
}

static bool sliding_attacks_square(const Board& board, int from_sq, int to_sq,
                                   bool diagonal, bool orthogonal) {
    int f0 = file_of(from_sq), r0 = rank_of(from_sq);
    int f1 = file_of(to_sq),   r1 = rank_of(to_sq);
    int df = f1 - f0, dr = r1 - r0;
    if (df == 0 && dr == 0) return false;

    int adf = std::abs(df), adr = std::abs(dr);
    int step_f = (df == 0) ? 0 : (df > 0 ? 1 : -1);
    int step_r = (dr == 0) ? 0 : (dr > 0 ? 1 : -1);
    if (step_f != 0 && step_r != 0) {
        if (adf != adr) return false;
        if (!diagonal) return false;
    } else {
        if (!orthogonal) return false;
        if (adf != 0 && adr != 0) return false;
    }

    int dir = step_r * 8 + step_f;
    int cur = from_sq;
    while (cur != to_sq) {
        int prev_file = file_of(cur);
        cur += dir;
        if (cur < 0 || cur >= 64) return false;
        int new_file = file_of(cur);
        if ((dir == 1 || dir == -1) && std::abs(new_file - prev_file) != 1)
            return false;
        if (dir == 9 && new_file <= prev_file) return false;
        if (dir == -7 && new_file <= prev_file) return false;
        if (dir == 7 && new_file >= prev_file) return false;
        if (dir == -9 && new_file >= prev_file) return false;
        if (cur == to_sq) return true;
        if (board.get_piece(cur) != EMPTY) return false;
    }
    return false;
}

static bool piece_attacks_square(const Board& board, int from_sq, int to_sq) {
    int piece = board.get_piece(from_sq);
    if (piece == EMPTY) return false;
    int colour   = (piece > 0) ? WHITE : BLACK;
    int piece_ty = std::abs(piece);

    switch (piece_ty) {
        case PAWN:
            return pawn_attacks_square(from_sq, to_sq, colour);
        case KNIGHT:
            return knight_attacks_square(from_sq, to_sq);
        case BISHOP:
            return sliding_attacks_square(board, from_sq, to_sq, true, false);
        case ROOK:
            return sliding_attacks_square(board, from_sq, to_sq, false, true);
        case QUEEN:
            return sliding_attacks_square(board, from_sq, to_sq, true, true);
        case KING:
            return king_attacks_square(from_sq, to_sq);
        default:
            return false;
    }
}

// Least-valuable attacker of target_sq for side `stm` (WHITE or BLACK).
// Uses queen promotion when a pawn capture reaches the back rank.
// King is omitted — king swaps in SEE are rarely sound and risk illegal positions.
static Move find_least_valuable_attacker(const Board& board, int target_sq, int stm) {
    static const int ORDER[] = {PAWN, KNIGHT, BISHOP, ROOK, QUEEN};
    for (int pt : ORDER) {
        for (int sq_idx = 0; sq_idx < 64; sq_idx++) {
            if (board.get_piece(sq_idx) != stm * pt) continue;
            if (!piece_attacks_square(board, sq_idx, target_sq)) continue;
            int promo = 0;
            if (pt == PAWN) {
                int tr = rank_of(target_sq);
                if ((stm == WHITE && tr == 7) || (stm == BLACK && tr == 0))
                    promo = QUEEN;
            }
            return Move(sq_idx, target_sq, promo);
        }
    }
    return Move(0, 0);
}

static int see_recursive(Board board, int target_sq, int depth) {
    if (depth <= 0) return 0;

    int stm = board.turn;
    Move cap = find_least_valuable_attacker(board, target_sq, stm);
    if (cap.from_sq == cap.to_sq && cap.from_sq == 0)
        return 0;

    int captured_ty = std::abs(board.get_piece(target_sq));
    if (captured_ty < PAWN || captured_ty > KING)
        return 0;
    int gain = PIECE_VALUES[captured_ty];

    Board nb = apply_move(board, cap);
    int sub  = see_recursive(nb, target_sq, depth - 1);
    return std::max(0, gain - sub);
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

    int promo_delta = 0;
    if (move.promotion)
        promo_delta = PIECE_VALUES[move.promotion] - PIECE_VALUES[PAWN];
    else if (mover_ty == PAWN &&
             (rank_of(move.to_sq) == 0 || rank_of(move.to_sq) == 7))
        promo_delta = PIECE_VALUES[QUEEN] - PIECE_VALUES[PAWN];

    int first_gain = victim_val + promo_delta;
    Board nb       = apply_move(board, move);
    int opp_see    = see_recursive(nb, move.to_sq, 32);
    return first_gain - opp_see;
}
