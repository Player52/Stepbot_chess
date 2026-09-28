// smoke_tests.cpp for new implementation
// Same as smoke_tests.cpp but with USE_NEW_LEGAL_MOVE_GEN = true

#include "board.h"
#include "movegen.h"
#include "search.h"
#include "zobrist.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

// We need to set the flag before including search.h
// But since USE_NEW_LEGAL_MOVE_GEN is defined in movegen.cpp, we can set it here

int main() {
    init_zobrist();
    
    // Set the flag to use new implementation
    USE_NEW_LEGAL_MOVE_GEN = true;
    
    // Run the same tests as smoke_tests.cpp
    // Test 1: basic move generation
    Board startpos;
    startpos.setup_starting_position();
    
    MoveList moves;
    generate_legal_moves_into(startpos, moves);
    if (moves.size() != 20) {
        std::cerr << "FAIL: startpos has " << moves.size() << " moves, expected 20" << std::endl;
        return 1;
    }
    
    // Test with new implementation
    MoveList new_moves;
    generate_legal_moves_into_new(startpos, new_moves);
    if (new_moves.size() != 20) {
        std::cerr << "FAIL: new implementation gives " << new_moves.size() << " moves, expected 20" << std::endl;
        return 1;
    }
    
    // Compare the move lists
    if (!std::equal(moves.begin(), moves.end(), new_moves.begin(), new_moves.end(), 
        [](const Move& a, const Move& b) { return a.from_sq == b.from_sq && a.to_sq == b.to_sq && a.promotion == b.promotion; })) {
        std::cerr << "FAIL: move lists differ" << std::endl;
        return 1;
    }
    
    // Test 2: kiwipete position
    Board kiwipete = Board("r3k2r/p1ppqpb1/bn2pnp1/2PPN3/1p2P3/2N2Q1p/PP1PBPPP/R3K2R w KQkq - 0 1");
    generate_legal_moves_into(kiwipete, moves);
    if (moves.size() != 44) {
        std::cerr << "FAIL: kiwipete has " << moves.size() << " moves, expected 44" << std::endl;
        return 1;
    }
    
    generate_legal_moves_into_new(kiwipete, new_moves);
    if (new_moves.size() != 44) {
        std::cerr << "FAIL: new implementation on kiwipete gives " << new_moves.size() << " moves, expected 44" << std::endl;
        return 1;
    }
    
    // Test 3: in-check position
    Board in_check = Board("rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1");
    generate_legal_moves_into(in_check, moves);
    generate_legal_moves_into_new(in_check, new_moves);
    if (moves.size() != new_moves.size()) {
        std::cerr << "FAIL: in-check position has " << moves.size() << " vs " << new_moves.size() << " moves" << std::endl;
        return 1;
    }
    
    // Test 4: search at depth 1
    SharedTT tt;
    std::atomic<bool> stop{false};
    Searcher searcher;
    
    // Redirect cout to suppress output
    std::streambuf* old_cout = std::cout.rdbuf();
    std::cout.rdbuf(nullptr);
    
    Move best = searcher.find_best_move(startpos, 1, -1.0, -1, 0, -1, {}, 1, &tt, &stop, 1, nullptr);
    
    std::cout.rdbuf(old_cout);
    
    if (best.to_sq == 0 && best.from_sq == 0) {
        std::cerr << "FAIL: no move found at depth 1" << std::endl;
        return 1;
    }
    
    std::cout << "Smoke tests passed" << std::endl;
    return 0;
}
