// board.cpp
// Implementation of the Board struct and helper functions.
// This is the C++ equivalent of board.py.

#include "board.h"    // Our own header
#include <iostream>   // For std::cout (printing)
#include <cctype>     // For std::tolower

// ─────────────────────────────────────────
// BOARD CONSTRUCTOR
// Called automatically when we write: Board b;
// Like Python's __init__
// ─────────────────────────────────────────

Board::Board() {
    // Initialise all squares to empty
    // squares.fill(x) sets every element to x
    squares.fill(EMPTY);

    turn             = WHITE;
    en_passant_sq    = -1;
    halfmove_clock   = 0;
    fullmove_number  = 1;

    // Castling rights default to all true (set in CastlingRights struct)

    setup_starting_position();
}

// ─────────────────────────────────────────
// STARTING POSITION
// ─────────────────────────────────────────

void Board::setup_starting_position() {
    squares.fill(EMPTY);

    // Back rank pieces — same order as board.py
    // In C++ we use a plain array here: int arr[] = {...}
    int back_rank[] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};

    for (int file = 0; file < 8; file++) {
        squares[sq(file, 0)] =  WHITE * back_rank[file];   // Rank 1
        squares[sq(file, 7)] =  BLACK * back_rank[file];   // Rank 8
        squares[sq(file, 1)] =  WHITE * PAWN;              // Rank 2
        squares[sq(file, 6)] =  BLACK * PAWN;              // Rank 7
    }
}

// ─────────────────────────────────────────
// BOARD METHODS
// ─────────────────────────────────────────

// 'const' after the method name means "this method doesn't modify the board"
// Like a Python method that only reads self but never writes to it
int Board::get_piece(int sq) const {
    return squares[sq];
}

void Board::set_piece(int sq, int piece) {
    squares[sq] = piece;
}

bool Board::is_empty(int sq) const {
    return squares[sq] == EMPTY;
}

int Board::colour_at(int sq) const {
    int piece = squares[sq];
    if (piece > 0) return WHITE;
    if (piece < 0) return BLACK;
    return 0;   // Empty
}

void Board::print_board() const {
    // std::cout is like Python's print()
    // << is the "stream" operator — chains output together
    // std::endl flushes and adds a newline (like '\n')
    std::cout << "\n  a b c d e f g h\n";
    std::cout << "  -----------------\n";

    for (int rank = 7; rank >= 0; rank--) {
        std::cout << (rank + 1) << "| ";
        for (int file = 0; file < 8; file++) {
            int piece = squares[sq(file, rank)];
            std::cout << piece_symbol(piece) << ' ';
        }
        std::cout << '\n';
    }

    std::cout << "\n  Turn: " << (turn == WHITE ? "White" : "Black") << '\n';
    std::cout << "  Castling: "
              << (castling_rights.K ? "K" : "-")
              << (castling_rights.Q ? "Q" : "-")
              << (castling_rights.k ? "k" : "-")
              << (castling_rights.q ? "q" : "-") << '\n';

    if (en_passant_sq != -1)
        std::cout << "  En passant: " << square_name(en_passant_sq) << '\n';
    else
        std::cout << "  En passant: -\n";

    std::cout << "  Move: " << fullmove_number << "\n\n";
}

// ─────────────────────────────────────────
// HELPER FUNCTIONS
// ─────────────────────────────────────────

std::string square_name(int sq) {
    // std::string is like Python's str
    // We build it character by character
    std::string name;
    name += (char)('a' + file_of(sq));    // File letter: a-h
    name += (char)('1' + rank_of(sq));    // Rank number: 1-8
    return name;
}

int name_to_square(const std::string& name) {
    // 'const std::string&' means we receive a reference to a string
    // without copying it — more efficient than passing by value
    int file = name[0] - 'a';   // 'a'=0, 'b'=1, etc.
    int rank = name[1] - '1';   // '1'=0, '2'=1, etc.
    return sq(file, rank);
}

char piece_symbol(int piece) {
    // A switch statement is like Python's match/case or a chain of if/elif
    switch (piece) {
        case  PAWN:    return 'P';
        case -PAWN:    return 'p';
        case  KNIGHT:  return 'N';
        case -KNIGHT:  return 'n';
        case  BISHOP:  return 'B';
        case -BISHOP:  return 'b';
        case  ROOK:    return 'R';
        case -ROOK:    return 'r';
        case  QUEEN:   return 'Q';
        case -QUEEN:   return 'q';
        case  KING:    return 'K';
        case -KING:    return 'k';
        default:       return '.';
    }
}
