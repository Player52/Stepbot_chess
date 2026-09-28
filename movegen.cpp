// movegen.cpp
// Move generation implementation.
// C++ equivalent of movegen.py.

#include "movegen.h"
#include "evaluate.h"
#include <cstdlib>    // For std::abs
#include <algorithm>  // For std::copy (used when deep-copying the board)

// A/B Testing: Global flag definition
// When true, generate_legal_moves_into_ab() uses the optimized implementation
// When false, it uses the original implementation
bool USE_NEW_LEGAL_MOVE_GEN = false;

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

    // Pawn attacks. These offsets search backward from target_sq to the
    // possible attacker squares.
    int pawn_dirs[2];
    if (attacker_colour == BLACK) {
        pawn_dirs[0] =  9; pawn_dirs[1] =  7;
    } else {
        pawn_dirs[0] = -9; pawn_dirs[1] = -7;
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

// ─────────────────────────────────────────────────────────────
// DIRECT LEGALITY CHECK APPROACH (Optimization)
// Uses direct board analysis instead of make/unmake to check move legality
// ─────────────────────────────────────────────────────────────

// Helper function: get direction from one square to another
// Returns direction index (0-7 for ALL_DIRS) or -1 if not on a straight line
// ALL_DIRS = {9, 7, -7, -9, 8, -8, 1, -1}
static inline int get_direction_index(int from_sq, int to_sq) {
    int df = file_of(to_sq) - file_of(from_sq);
    int dr = rank_of(to_sq) - rank_of(from_sq);
    
    if (df == 0 && dr == 0) return -1;
    
    // Straight directions
    if (df == 0 && dr > 0) return 4;   // Up (8) -> ALL_DIRS[4]
    if (df == 0 && dr < 0) return 5;   // Down (-8) -> ALL_DIRS[5]
    if (dr == 0 && df > 0) return 6;   // Right (1) -> ALL_DIRS[6]
    if (dr == 0 && df < 0) return 7;   // Left (-1) -> ALL_DIRS[7]
    
    // Diagonal directions
    if (std::abs(df) == std::abs(dr)) {
        if (df > 0 && dr > 0) return 0;   // Up-Right (9) -> ALL_DIRS[0]
        if (df > 0 && dr < 0) return 2;   // Down-Right (-7) -> ALL_DIRS[2]
        if (df < 0 && dr > 0) return 1;   // Up-Left (7) -> ALL_DIRS[1]
        if (df < 0 && dr < 0) return 3;   // Down-Left (-9) -> ALL_DIRS[3]
    }
    
    return -1;  // Not on a straight line
}

// Helper function: check if a piece type can slide in a given direction
// ALL_DIRS = {9, 7, -7, -9, 8, -8, 1, -1}
// Diagonals: indices 0-3 (9, 7, -7, -9)
// Straights:  indices 4-7 (8, -8, 1, -1)
static inline bool can_slide(int piece_type, int direction_index) {
    if (direction_index == -1) return false;
    
    if (piece_type == ROOK) {
        // Rook: straight directions only (indices 4-7: 8, -8, 1, -1)
        return direction_index >= 4 && direction_index <= 7;
    }
    if (piece_type == BISHOP) {
        // Bishop: diagonal directions only (indices 0-3: 9, 7, -7, -9)
        return direction_index >= 0 && direction_index <= 3;
    }
    if (piece_type == QUEEN) {
        // Queen: all sliding directions
        return direction_index >= 0 && direction_index <= 7;
    }
    return false;  // Non-sliding piece
}

// Check if a piece is pinned to its king
// A piece is pinned if:
// 1. It's on a straight line from the king
// 2. There's an enemy sliding piece beyond it that attacks through it
// 3. There are no pieces between the king and the pinned piece (except the pinned piece itself)
//    that would block the attacker's path to the king
static bool is_pinned(const Board& board, int sq, int colour) {
    int king_sq = board.king_square(colour);
    if (sq == king_sq) return false;  // King is never pinned to itself
    
    int direction = get_direction_index(king_sq, sq);
    if (direction == -1) return false;  // Not on a straight line from king
    
    // The ALL_DIRS array from board.h
    static const int all_dirs[] = {9, 7, -7, -9, 8, -8, 1, -1};
    int dir = all_dirs[direction];
    
    // First, check if the path from king to sq is clear (no pieces between king and sq)
    // If there's a piece between king and sq, then even if there's an attacker beyond sq,
    // that attacker couldn't see the king through sq (the intermediate piece blocks)
    int check_sq = king_sq + dir;
    while (check_sq != sq) {
        if (check_sq < 0 || check_sq >= 64) break;  // Shouldn't happen if king and sq are on same line
        
        if (board.get_piece(check_sq) != EMPTY) {
            // There's a piece between king and sq, so sq cannot be pinned
            // (the attacker beyond sq would be blocked by this intermediate piece)
            return false;
        }
        
        // Check board boundaries - detect if we wrapped
        int prev_file = file_of(check_sq - dir);
        int new_file = file_of(check_sq);
        if (dir == 1 || dir == -1) {
            if (std::abs(prev_file - new_file) != 1) break;
        } else if (dir == 9 || dir == -9) {
            if ((dir == 9 && new_file <= prev_file) || (dir == -9 && new_file >= prev_file)) break;
        } else if (dir == 7 || dir == -7) {
            if ((dir == 7 && new_file >= prev_file) || (dir == -7 && new_file <= prev_file)) break;
        }
        
        check_sq += dir;
        if (check_sq == sq) break;  // Reached the target square
    }
    
    // Now check if there's an enemy sliding piece beyond sq in the same direction
    // Use proper boundary checking: check before processing each square
    int search_sq = sq + dir;
    while (true) {
        if (search_sq < 0 || search_sq >= 64) break;
        
        // Check if the step from previous square wrapped
        int prev_file = file_of(search_sq - dir);
        int new_file = file_of(search_sq);
        if (dir == 1 || dir == -1) {
            if (std::abs(prev_file - new_file) != 1) break;
        } else if (dir == 9 || dir == -9) {
            if ((dir == 9 && new_file <= prev_file) || (dir == -9 && new_file >= prev_file)) break;
        } else if (dir == 7 || dir == -7) {
            if ((dir == 7 && new_file >= prev_file) || (dir == -7 && new_file <= prev_file)) break;
        }
        
        // Now process the square
        int piece_here = board.get_piece(search_sq);
        if (piece_here != EMPTY) {
            if (board.colour_at(search_sq) == -colour) {
                int attacker_type = std::abs(piece_here);
                // The attacker direction from sq to search_sq should be the same as dir
                // Since we're moving in dir from sq, search_sq = sq + dir
                // So the direction from sq to search_sq is dir
                if (can_slide(attacker_type, direction)) {
                    return true;  // Piece is pinned
                }
            }
            break;  // Blocked by a piece
        }
        
        search_sq += dir;
    }
    
    return false;
}

// Check if a pinned piece move is legal
// A pinned piece can move:
// 1. Along the pin line (same direction from king)
// 2. To capture the attacker (if the attacker is on the pin line)
static bool is_pinned_move_legal(const Board& board, const Move& move, int colour) {
    int king_sq = board.king_square(colour);
    int from_sq = move.from_sq;
    int to_sq = move.to_sq;
    
    int from_dir = get_direction_index(king_sq, from_sq);
    if (from_dir == -1) return true;  // Shouldn't happen for pinned piece
    
    int to_dir = get_direction_index(king_sq, to_sq);
    
    // Must stay on the same line (same direction from king)
    if (from_dir != to_dir) {
        // Unless capturing the attacker - check if destination is the attacker
        int captured = board.get_piece(to_sq);
        if (captured != EMPTY && board.colour_at(to_sq) == -colour) {
            // Check if this piece is the attacker causing the pin
            static const int all_dirs[] = {9, 7, -7, -9, 8, -8, 1, -1};
            int dir = all_dirs[from_dir];
            
            int search_sq = from_sq + dir;
            while (search_sq >= 0 && search_sq < 64) {
                int piece_here = board.get_piece(search_sq);
                if (piece_here != EMPTY) {
                    if (search_sq == to_sq) {
                        int attacker_type = std::abs(piece_here);
                        int attacker_dir = get_direction_index(from_sq, search_sq);
                        if (can_slide(attacker_type, attacker_dir)) {
                            return true;  // Capturing the attacker - legal
                        }
                    }
                    break;  // Blocked by a piece
                }
                
                // Check board boundaries
                int prev_file = file_of(search_sq - dir);
                int new_file = file_of(search_sq);
                if (dir == 1 || dir == -1) {
                    if (std::abs(prev_file - new_file) != 1) break;
                } else if (dir == 9 || dir == -9) {
                    if ((dir == 9 && new_file <= prev_file) || (dir == -9 && new_file >= prev_file)) break;
                } else if (dir == 7 || dir == -7) {
                    if ((dir == 7 && new_file >= prev_file) || (dir == -7 && new_file <= prev_file)) break;
                }
                
                search_sq += dir;
            }
        }
        return false;  // Moves off the pin line without capturing attacker
    }
    
    return true;  // Stays on pin line
}

// Check if moving a piece reveals a discovered check
// This happens when:
// 1. The piece being moved is a sliding piece
// 2. There's an enemy sliding piece behind it (relative to king)
// 3. That enemy piece would attack the king if the moving piece moves away
// 4. There are no pieces between the king and the moving piece
static bool move_reveals_discovered_check(const Board& board, const Move& move, int colour) {
    int piece = board.get_piece(move.from_sq);
    int piece_type = std::abs(piece);
    
    // Only sliding pieces can reveal discovered checks
    if (piece_type != ROOK && piece_type != BISHOP && piece_type != QUEEN) {
        return false;
    }
    
    int king_sq = board.king_square(colour);
    int from_sq = move.from_sq;
    
    int king_to_piece_dir = get_direction_index(king_sq, from_sq);
    if (king_to_piece_dir == -1) return false;  // Piece not on straight line from king
    
    // The ALL_DIRS array from board.h
    static const int all_dirs[] = {9, 7, -7, -9, 8, -8, 1, -1};
    int dir = all_dirs[king_to_piece_dir];
    
    // First, check if the path from king to from_sq is clear (no pieces between king and moving piece)
    // If there's a piece between king and from_sq, then moving from_sq won't reveal an attack
    // because that intermediate piece still blocks
    int check_sq = king_sq + dir;
    while (check_sq != from_sq) {
        if (check_sq < 0 || check_sq >= 64) break;
        
        if (board.get_piece(check_sq) != EMPTY) {
            // There's a piece between king and from_sq, so moving from_sq won't reveal discovered check
            return false;
        }
        
        // Check board boundaries
        int prev_file = file_of(check_sq - dir);
        int new_file = file_of(check_sq);
        if (dir == 1 || dir == -1) {
            if (std::abs(prev_file - new_file) != 1) break;
        } else if (dir == 9 || dir == -9) {
            if ((dir == 9 && new_file <= prev_file) || (dir == -9 && new_file >= prev_file)) break;
        } else if (dir == 7 || dir == -7) {
            if ((dir == 7 && new_file >= prev_file) || (dir == -7 && new_file <= prev_file)) break;
        }
        
        check_sq += dir;
        if (check_sq == from_sq) break;
    }
    
    // The piece is between king and some squares - check behind the piece
    // (away from the king)
    // Use proper boundary checking: check before processing each square
    int search_sq = from_sq + dir;  // Start looking behind the piece (away from king)
    while (true) {
        if (search_sq < 0 || search_sq >= 64) break;
        
        // Check if the step from previous square wrapped
        int prev_file = file_of(search_sq - dir);
        int new_file = file_of(search_sq);
        if (dir == 1 || dir == -1) {
            if (std::abs(prev_file - new_file) != 1) break;
        } else if (dir == 9 || dir == -9) {
            if ((dir == 9 && new_file <= prev_file) || (dir == -9 && new_file >= prev_file)) break;
        } else if (dir == 7 || dir == -7) {
            if ((dir == 7 && new_file >= prev_file) || (dir == -7 && new_file <= prev_file)) break;
        }
        
        // Now process the square
        int piece_here = board.get_piece(search_sq);
        if (piece_here != EMPTY) {
            if (board.colour_at(search_sq) == -colour) {
                int attacker_type = std::abs(piece_here);
                // Check if attacker can slide along the line (using king_to_piece_dir)
                if (can_slide(attacker_type, king_to_piece_dir)) {
                    // Found an attacker that was blocked by the moving piece
                    // The move reveals discovered check unless it captures this attacker
                    if (move.to_sq != search_sq) {
                        return true;  // Reveals discovered check
                    }
                    // Capturing the attacker - this is fine, not a discovered check
                    return false;
                }
            }
            break;  // Blocked by another piece
        }
        
        search_sq += dir;
    }
    
    return false;
}

// Check if a king move is legal (for Direct Legality Check)
// Handles regular king moves and castling
static bool is_king_move_legal(const Board& board, const Move& move, int colour) {
    int from_sq = move.from_sq;
    int to_sq = move.to_sq;
    int enemy = -colour;
    
    // Check if it's castling (king moves 2 squares)
    if (std::abs(to_sq - from_sq) == 2) {
        // For castling, we need to check:
        // 1. King doesn't move through check
        // 2. King doesn't end in check
        // 3. The intermediate square is not attacked
        
        int step = (to_sq > from_sq) ? 1 : -1;
        int king_step1 = from_sq + step;
        
        // Check if king moves through or into check
        if (square_attacked_by(board, king_step1, enemy)) return false;
        if (square_attacked_by(board, to_sq, enemy)) return false;
        
        return true;
    }
    
    // Regular king move: destination must not be attacked
    return !square_attacked_by(board, to_sq, enemy);
}

// Main legality check without make/unmake
// Returns true if the pseudo-legal move is actually legal
// This is only used when the king is NOT currently in check
static bool is_move_legal_no_make(const Board& board, const Move& move, int colour) {
    int piece = board.get_piece(move.from_sq);
    if (piece == EMPTY) return false;
    
    int piece_type = std::abs(piece);
    int from_sq = move.from_sq;

    if (piece_type == PAWN && move.to_sq == board.en_passant_sq) {
        Board test_board = board;
        UndoInfo undo = make_move(test_board, move);
        bool legal = !king_in_check(test_board, colour);
        unmake_move(test_board, move, undo);
        return legal;
    }
    
    // King move
    if (piece_type == KING) {
        return is_king_move_legal(board, move, colour);
    }
    
    // Not in check - check for normal legality issues
    // Check if piece is pinned
    if (is_pinned(board, from_sq, colour)) {
        return is_pinned_move_legal(board, move, colour);
    }
    
    // Check if move reveals discovered check
    if (move_reveals_discovered_check(board, move, colour)) {
        return false;
    }
    
    // Move is legal
    return true;
}

// New optimized implementation using Direct Legality Check
void generate_legal_moves_into_new(const Board& board, MoveList& legal) {
    MoveList pseudo;
    generate_pseudo_legal_moves_into(board, pseudo);
    legal.clear();
    
    int colour = board.turn;
    int king_sq = board.king_square(colour);
    
    // If king square is invalid, fall back to original implementation
    if (king_sq == -1) {
        Board test_board = board;
        for (const Move& move : pseudo) {
            UndoInfo undo = make_move(test_board, move);
            if (!king_in_check(test_board, colour)) {
                legal.push_back(move);
            }
            unmake_move(test_board, move, undo);
        }
        return;
    }
    
    // Check if the moving side is currently in check
    bool in_check = square_attacked_by(board, king_sq, -colour);
    
    if (in_check) {
        // When in check, use a single board copy for all moves
        // This is the same as the original for in-check positions,
        // but we save performance for non-check positions
        Board test_board = board;
        for (const Move& move : pseudo) {
            UndoInfo undo = make_move(test_board, move);
            if (!king_in_check(test_board, colour)) {
                legal.push_back(move);
            }
            unmake_move(test_board, move, undo);
        }
    } else {
        // Not in check - use the optimized direct legality checking
        for (const Move& move : pseudo) {
            if (is_move_legal_no_make(board, move, colour)) {
                legal.push_back(move);
            }
        }
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
