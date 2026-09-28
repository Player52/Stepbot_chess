#include "board.h"
#include "movegen.h"
#include "search.h"
#include "zobrist.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Helper function to parse FEN
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

static Board board_from_fen(const std::string& fen) {
    Board board;
    board.squares.fill(EMPTY);

    std::istringstream ss(fen);
    std::string placement, active, castling, ep, halfmove, fullmove;
    ss >> placement >> active >> castling >> ep >> halfmove >> fullmove;

    int rank = 7;
    int file = 0;
    for (char c : placement) {
        if (c == '/') {
            rank--;
            file = 0;
        } else if (std::isdigit((unsigned char)c)) {
            file += c - '0';
        } else {
            board.squares[sq(file++, rank)] = fen_piece(c);
        }
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

struct BenchResult {
    std::string position_name;
    std::string implementation;
    int depth;
    long long elapsed_us = 0;
    long long nodes_searched = 0;
    long long nps = 0;
    long long tt_hits = 0;
    std::string bestmove;
};

// Function to run a search and collect results
// This will be linked with either search_original.cpp or search_ab.cpp
template<typename SearcherType>
static BenchResult run_search(const std::string& name, const std::string& fen, int depth, const std::string& impl_name) {
    Board board = board_from_fen(fen);
    
    SharedTT tt;
    std::atomic<bool> stop{false};
    
    // Redirect cout to suppress UCI output
    std::ostringstream uci_sink;
    std::streambuf* old_cout = std::cout.rdbuf(uci_sink.rdbuf());
    
    SearcherType searcher;
    
    auto start = std::chrono::steady_clock::now();
    Move best = searcher.find_best_move(board, depth, -1.0, -1, 0, -1,
                                        {}, 1, &tt, &stop, 1, nullptr);
    auto end = std::chrono::steady_clock::now();
    
    std::cout.rdbuf(old_cout);
    
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    long long nodes_searched = searcher.nodes_searched;
    long long nps = (long long)(nodes_searched * 1000000.0 / elapsed_us);
    long long tt_hits = searcher.tt_hits;
    
    return {
        name,
        impl_name,
        depth,
        elapsed_us,
        nodes_searched,
        nps,
        tt_hits,
        best.to_uci()
    };
}

static void print_results_table(const std::vector<BenchResult>& results) {
    std::cout << std::string(120, '-') << std::endl;
    std::cout << "BENCHMARK RESULTS" << std::endl;
    std::cout << std::string(120, '-') << std::endl;
    std::cout << std::left << std::setw(12) << "Position"
              << std::setw(8) << "Impl"
              << std::setw(6) << "Depth"
              << std::setw(12) << "Time (ms)"
              << std::setw(12) << "Nodes"
              << std::setw(12) << "Nodes/s"
              << std::setw(12) << "TT Hits"
              << std::setw(10) << "Best Move"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;
    
    for (const auto& result : results) {
        std::cout << std::left << std::setw(12) << result.position_name
                  << std::setw(8) << result.implementation
                  << std::setw(6) << result.depth
                  << std::setw(12) << std::fixed << std::setprecision(2) << (result.elapsed_us / 1000.0)
                  << std::setw(12) << result.nodes_searched
                  << std::setw(12) << result.nps
                  << std::setw(12) << result.tt_hits
                  << std::setw(10) << result.bestmove
                  << std::endl;
    }
    std::cout << std::string(120, '-') << std::endl;
}

static void print_comparison_summary(const std::vector<BenchResult>& original_results, 
                                    const std::vector<BenchResult>& new_results) {
    std::cout << std::endl << std::string(120, '=') << std::endl;
    std::cout << "COMPARISON SUMMARY" << std::endl;
    std::cout << std::string(120, '=') << std::endl;
    
    for (size_t i = 0; i < original_results.size(); i++) {
        const auto& orig = original_results[i];
        const auto& newer = new_results[i];
        
        if (orig.position_name != newer.position_name || orig.depth != newer.depth) {
            continue; // Should not happen if data is properly paired
        }
        
        double time_speedup = (double)orig.elapsed_us / newer.elapsed_us;
        double nps_speedup = (double)newer.nps / orig.nps;
        double time_improvement = 100.0 * (1.0 - 1.0/time_speedup);
        bool same_bestmove = (orig.bestmove == newer.bestmove);
        bool same_nodes = (orig.nodes_searched == newer.nodes_searched);
        bool same_tt_hits = (orig.tt_hits == newer.tt_hits);
        
        std::cout << "Position: " << orig.position_name << " (depth " << orig.depth << ")" << std::endl;
        std::cout << "  Original: " << (orig.elapsed_us / 1000.0) << " ms, " << orig.nodes_searched 
                  << " nodes, " << orig.nps << " nps, " << orig.tt_hits << " TT hits, best: " << orig.bestmove << std::endl;
        std::cout << "  New:     " << (newer.elapsed_us / 1000.0) << " ms, " << newer.nodes_searched 
                  << " nodes, " << newer.nps << " nps, " << newer.tt_hits << " TT hits, best: " << newer.bestmove << std::endl;
        std::cout << "  Time speedup: " << time_speedup << "x (" << time_improvement << "% improvement)" << std::endl;
        std::cout << "  NPS speedup: " << nps_speedup << "x" << std::endl;
        std::cout << "  Same node count: " << (same_nodes ? "YES" : "NO") << std::endl;
        std::cout << "  Same TT hits: " << (same_tt_hits ? "YES" : "NO") << std::endl;
        std::cout << "  Same best move: " << (same_bestmove ? "YES" : "NO") << std::endl;
        
        if (!same_nodes || !same_bestmove || !same_tt_hits) {
            std::cout << "  [WARNING] Results differ!" << std::endl;
        }
        std::cout << std::endl;
    }
}

// Run the existing smoke tests
static bool run_existing_smoke_tests(const std::string& impl_name) {
    std::cout << "Running existing smoke_tests.exe with " << impl_name << " implementation..." << std::endl;
    
    int result = system("./smoke_tests_" + std::string(impl_name == "ORIGINAL" ? "original" : "ab") + ".exe");
    return result == 0;
}

int main() {
    init_zobrist();
    
    std::cout << "A/B Benchmark: Original vs New Legal Move Generation" << std::endl;
    std::cout << "====================================================" << std::endl;
    
    // Test positions for benchmarking
    const std::vector<std::pair<std::string, std::string>> bench_positions = {
        {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/2PPN3/1p2P3/2N2Q1p/PP1PBPPP/R3K2R w KQkq - 0 1"},
        {"tactical", "r5k1/1b3rbp/1p4p1/n7/P4P2/2P1p1P1/1Q2R2P/R1Bq2K1 w - - 1 25"}
    };
    
    const std::vector<int> depths = {5, 6, 7, 8, 9, 10, 11};
    
    std::vector<BenchResult> original_results, new_results;
    
    std::cout << "\n1. Running existing smoke tests with ORIGINAL implementation..." << std::endl;
    bool original_smoke_passed = run_existing_smoke_tests("ORIGINAL");
    std::cout << "   Result: " << (original_smoke_passed ? "PASSED" : "FAILED") << std::endl;
    
    std::cout << "\n2. Running benchmarks with ORIGINAL implementation..." << std::endl;
    // We'll need to compile with search_original.cpp for this
    // For now, note that we can't do this directly in a single program
    
    std::cout << "\n3. Running existing smoke tests with NEW implementation..." << std::endl;
    bool new_smoke_passed = run_existing_smoke_tests("NEW");
    std::cout << "   Result: " << (new_smoke_passed ? "PASSED" : "FAILED") << std::endl;
    
    std::cout << "\n4. Running benchmarks with NEW implementation..." << std::endl;
    // We'll need to compile with search_ab.cpp and set USE_NEW_LEGAL_MOVE_GEN = true
    
    std::cout << "\nNote: This benchmark requires separate compilation of each implementation." << std::endl;
    std::cout << "The results above show the smoke test results." << std::endl;
    
    return (original_smoke_passed && new_smoke_passed) ? 0 : 1;
}