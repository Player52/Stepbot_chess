// board.h
// Board representation for Stepbot.
//
// Same logic as board.py — mailbox array of 64 integers.
// Positive = White, Negative = Black, Zero = Empty.
//
// In C++ we use a header file to declare everything,
// and board.cpp to implement it.

#pragma once   // Only include this file once per compile (like Python's import guard)

#include <string>
#include <array>

// ─────────────────────────────────────────
// PIECE CONSTANTS
// Same values as board.py
// ─────────────────────────────────────────

// 'const int' means these values never change — like Python constants
const int EMPTY  = 0;
const int PAWN   = 1;
const int KNIGHT = 2;
const int BISHOP = 3;
const int ROOK   = 4;
const int QUEEN  = 5;
const int KING   = 6;

const int WHITE =  1;
const int BLACK = -1;

// ─────────────────────────────────────────
// DIRECTION CONSTANTS
// Used by move generation and evaluation
// ─────────────────────────────────────────

// 'constexpr' means computed at compile time — slightly faster than const
// std::array<int, N> is a fixed-size array — like a Python list but the
// size must be known at compile time
constexpr std::array<int, 4> DIAGONAL_DIRS  = {9, 7, -7, -9};
constexpr std::array<int, 4> STRAIGHT_DIRS  = {8, -8, 1, -1};
constexpr std::array<int, 8> ALL_DIRS       = {9, 7, -7, -9, 8, -8, 1, -1};
constexpr std::array<int, 8> KNIGHT_OFFSETS = {17, 15, 10, 6, -6, -10, -15, -17};

// ─────────────────────────────────────────
// CASTLING RIGHTS
// We use a simple struct to hold the four castling flags.
// A 'struct' groups related data together — like a Python dataclass.
// ─────────────────────────────────────────

struct CastlingRights {
    bool K = true;   // White kingside
    bool Q = true;   // White queenside
    bool k = true;   // Black kingside
    bool q = true;   // Black queenside
};

// ─────────────────────────────────────────
// UNDO INFO
// Captures everything needed to reverse a make_move call.
// Stored on the search stack — no heap allocation.
// ─────────────────────────────────────────

struct UndoInfo {
    int            captured_piece;   // Piece on to_sq before the move (EMPTY if none)
    int            en_passant_sq;    // Previous en passant square
    CastlingRights castling_rights;  // Previous castling rights
    int            halfmove_clock;   // Previous halfmove clock
};

// ─────────────────────────────────────────
// BOARD STRUCT
// Holds the complete game state.
// In Python this was a class — in C++ we use a struct.
// (In C++ structs and classes are nearly identical;
//  we use struct here to keep things simple.)
// ─────────────────────────────────────────

struct Board {
    // The mailbox: 64 squares
    // std::array<int, 64> is like a Python list of 64 ints
    std::array<int, 64> squares;

    int  turn;             // WHITE or BLACK
    CastlingRights castling_rights;
    int  en_passant_sq;    // -1 if none
    int  halfmove_clock;
    int  fullmove_number;

    // Constructor — called when we create a Board
    // Like Python's __init__
    Board();

    // Methods — like Python methods but declared here, defined in board.cpp
    int  get_piece(int sq) const;
    void set_piece(int sq, int piece);
    bool is_empty(int sq) const;
    int  colour_at(int sq) const;   // Returns WHITE, BLACK, or 0
    void print_board() const;
    void setup_starting_position();
};

// ─────────────────────────────────────────
// HELPER FUNCTIONS
// Declared here, defined in board.cpp
// ─────────────────────────────────────────

// 'inline' means the compiler can paste the function body directly
// at the call site for speed — good for tiny functions called millions of times
inline int  file_of(int sq)            { return sq % 8; }
inline int  rank_of(int sq)            { return sq / 8; }
inline int  sq(int file, int rank)     { return rank * 8 + file; }

std::string square_name(int sq);
int         name_to_square(const std::string& name);

// piece_symbol: returns the character for a piece (e.g. 'P', 'n', '.')
char piece_symbol(int piece);
