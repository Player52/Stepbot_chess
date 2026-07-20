/*
 * stepbot_live_writer.h
 *
 * Drop this into your Stepbot source folder alongside board.h / search.h.
 * Tailored exactly to Stepbot's board representation — no guesswork needed.
 *
 * ── HOW TO INTEGRATE ─────────────────────────────────────────────────────────
 *
 * In main.cpp, add near the other #includes:
 *
 *   #include "stepbot_live_writer.h"
 *
 * Inside UCIEngine::cmd_go(), add ONE line before the book lookup so even
 * early book-move returns clean up the file automatically:
 *
 *   void cmd_go(const std::vector<std::string>& tokens) {
 *       LiveWriter::Guard liveGuard;   // <── ADD THIS
 *       // ... rest of cmd_go unchanged ...
 *   }
 *
 * In search.cpp, add near the other #includes:
 *
 *   #include "stepbot_live_writer.h"
 *
 * Inside find_best_move(), after the existing UCI "info" cout block:
 *
 *   std::cout << "info depth " << depth << ...;
 *   std::cout.flush();
 *
 *   // ADD THIS LINE — all these variables already exist at this point:
 *   LiveWriter::update(search_board, depth, best_score, (long long)nps, pv_str);
 *
 * ── FILE LIFECYCLE ────────────────────────────────────────────────────────────
 *
 * Created/overwritten:  once per depth iteration — always ~350 bytes, never grows
 * Deleted:              when cmd_go() returns (Guard destructor), covering:
 *                         - normal game end
 *                         - book move early return
 *                         - Stepbot crash (screensaver just falls back to branding)
 */

#pragma once

#include "board.h"    // Board, piece constants, piece_symbol(), sq()
#include <string>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <windows.h>  // For CreateDirectoryA

#ifndef STEPBOT_LIVE_FILE
  #define STEPBOT_LIVE_FILE "C:\\temp\\stepbot_live.txt"
#endif

namespace LiveWriter {

inline std::string formatScore(int cp) {
    if (cp >  29000) return "+M" + std::to_string(30000 - cp);
    if (cp < -29000) return "-M" + std::to_string(30000 + cp);
    char buf[16]; snprintf(buf, sizeof(buf), "%+.2f", cp / 100.0);
    return buf;
}

inline std::string formatNps(long long nps) {
    char buf[32];
    if      (nps >= 1'000'000) snprintf(buf, sizeof(buf), "%.1fM", nps / 1e6);
    else if (nps >=     1'000) snprintf(buf, sizeof(buf), "%.1fK", nps / 1e3);
    else                        snprintf(buf, sizeof(buf), "%lld",  nps);
    return buf;
}

// Uses Stepbot's own board.squares array and piece_symbol() from board.h.
// Squares: 0=a1, 63=h8 (little-endian). Printed rank 8 down to rank 1.
inline std::string buildBoardString(const Board& board) {
    std::ostringstream oss;
    oss << "BOARD_START\n";
    for (int rank = 7; rank >= 0; rank--) {
        oss << (rank + 1) << ' ';
        for (int file = 0; file < 8; file++)
            oss << piece_symbol(board.squares[sq(file, rank)]) << ' ';
        oss << '\n';
    }
    oss << "BOARD_END\n";
    return oss.str();
}

// Overwrites the live file — never appends, always ~350 bytes.
inline void update(const Board& board,
                   int depth,
                   int score,
                   long long nps,
                   const std::string& bestMove)
{
    CreateDirectoryA("C:\\temp", nullptr);  // no-op if already exists
    std::ofstream f(STEPBOT_LIVE_FILE, std::ios::out | std::ios::trunc);
    if (!f.is_open()) return;
    f << buildBoardString(board);
    f << "DEPTH:"    << depth              << '\n';
    f << "SCORE:"    << formatScore(score) << '\n';
    f << "NODES:"    << formatNps(nps)     << '\n';
    f << "BESTMOVE:" << bestMove           << '\n';
}

// Delete the file → screensaver reverts to branding mode.
inline void cleanup() {
    std::remove(STEPBOT_LIVE_FILE);
}

// RAII: create at the top of cmd_go() — destructor calls cleanup() no matter
// how go() exits (normal return, book early return, exception, etc.)
struct Guard {
    ~Guard() { cleanup(); }
};

} // namespace LiveWriter
