#include "board.h"
#include "movegen.h"
#include "search.h"
#include "zobrist.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

struct BenchCase {
    std::string name;
    std::string fen;
    int depth;
};

struct BenchRun {
    long long elapsed_us = 1;
    int nodes = 0;
    long long nps = 0;
    int reached_depth = 0;
    std::string bestmove;
};

struct Options {
    int runs = 3;
    bool include_multipv3 = false;
    bool timed = false;
    int override_time_ms = 0;
};

static int parse_last_depth(const std::string& uci) {
    std::istringstream lines(uci);
    std::string line;
    int last_depth = 0;

    while (std::getline(lines, line)) {
        std::istringstream ss(line);
        std::string token;
        ss >> token;
        if (token != "info") continue;
        while (ss >> token) {
            if (token == "depth") {
                int depth = 0;
                if (ss >> depth)
                    last_depth = std::max(last_depth, depth);
            }
        }
    }

    return last_depth;
}

static BenchRun run_once(const BenchCase& bench, int multipv,
                         bool timed, int time_ms) {
    Board board = board_from_fen(bench.fen);
    SharedTT tt;
    std::atomic<bool> stop{false};
    Searcher searcher;

    std::ostringstream uci_sink;
    std::streambuf* old_cout = std::cout.rdbuf(uci_sink.rdbuf());

    int max_depth = timed ? MAX_DEPTH - 1 : bench.depth;
    double time_secs = timed ? time_ms / 1000.0 : -1.0;

    auto start = std::chrono::steady_clock::now();
    Move best = searcher.find_best_move(board, max_depth, time_secs, -1, 0, -1,
                                        {}, 1, &tt, &stop, multipv, nullptr);
    auto end = std::chrono::steady_clock::now();

    std::cout.rdbuf(old_cout);

    auto elapsed_us =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    if (elapsed_us <= 0) elapsed_us = 1;

    BenchRun result;
    result.elapsed_us = elapsed_us;
    result.nodes = searcher.nodes_searched;
    result.nps = (long long)(searcher.nodes_searched * 1000000.0 / elapsed_us);
    result.reached_depth = timed ? parse_last_depth(uci_sink.str()) : bench.depth;
    result.bestmove = best.to_uci();
    return result;
}

static BenchRun median_run(std::vector<BenchRun> runs) {
    std::sort(runs.begin(), runs.end(),
              [](const BenchRun& a, const BenchRun& b) {
                  return a.elapsed_us < b.elapsed_us;
              });
    return runs[runs.size() / 2];
}

static void print_result(const std::string& mode, const BenchCase& bench,
                         int multipv, int requested, int runs,
                         const std::vector<BenchRun>& samples) {
    BenchRun med = median_run(samples);
    long long min_us = samples.front().elapsed_us;
    long long max_us = samples.front().elapsed_us;
    for (const BenchRun& sample : samples) {
        min_us = std::min(min_us, sample.elapsed_us);
        max_us = std::max(max_us, sample.elapsed_us);
    }

    std::cout << "BENCH_" << mode
              << " name " << bench.name
              << " multipv " << multipv
              << " runs " << runs;

    if (mode == "FIXED")
        std::cout << " depth " << requested;
    else
        std::cout << " time_ms " << requested
                  << " reached_depth " << med.reached_depth;

    std::cout << " median_nodes " << med.nodes
              << " median_nps " << med.nps
              << " median_time_ms " << (med.elapsed_us / 1000.0)
              << " min_time_ms " << (min_us / 1000.0)
              << " max_time_ms " << (max_us / 1000.0)
              << " bestmove " << med.bestmove
              << "\n";
}

static void run_fixed_suite(const std::vector<BenchCase>& cases,
                            const Options& options) {
    std::vector<int> multipv_values = {1};
    if (options.include_multipv3)
        multipv_values.push_back(3);

    for (int multipv : multipv_values) {
        for (const BenchCase& bench : cases) {
            std::vector<BenchRun> samples;
            samples.reserve(options.runs);
            for (int i = 0; i < options.runs; i++)
                samples.push_back(run_once(bench, multipv, false, 0));
            print_result("FIXED", bench, multipv, bench.depth,
                         options.runs, samples);
        }
    }
}

static void run_timed_suite(const std::vector<BenchCase>& cases,
                            const Options& options) {
    std::vector<int> times_ms = {5000, 10000};
    if (options.override_time_ms > 0)
        times_ms = {options.override_time_ms};

    for (int time_ms : times_ms) {
        for (int multipv : {1, 3}) {
            for (const BenchCase& bench : cases) {
                std::vector<BenchRun> samples;
                samples.reserve(options.runs);
                for (int i = 0; i < options.runs; i++)
                    samples.push_back(run_once(bench, multipv, true, time_ms));
                print_result("TIMED", bench, multipv, time_ms,
                             options.runs, samples);
            }
        }
    }
}

static void print_usage() {
    std::cout
        << "Usage: bench_tests [--runs N] [--multipv] [--timed] [--time-ms N]\n"
        << "  default      fixed-depth median suite, MultiPV=1\n"
        << "  --multipv    include fixed-depth MultiPV=3 cases\n"
        << "  --timed      run timed 5s/10s suite for MultiPV=1 and 3\n"
        << "  --time-ms N  with --timed, run one custom timed duration\n";
}

static Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--runs" && i + 1 < argc) {
            options.runs = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--multipv") {
            options.include_multipv3 = true;
        } else if (arg == "--timed") {
            options.timed = true;
        } else if (arg == "--time-ms" && i + 1 < argc) {
            options.override_time_ms = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            std::exit(0);
        }
    }
    return options;
}

int main(int argc, char** argv) {
    init_zobrist();

    Options options = parse_options(argc, argv);

    const std::vector<BenchCase> fixed_cases = {
        {
            "startpos",
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            7,
        },
        {
            "reported_tactical",
            "r5k1/1b3rbp/1p4p1/n7/P4P2/2P1p1P1/1Q2R2P/R1Bq2K1 w - - 1 25",
            7,
        },
        {
            "kiwipete",
            "r3k2r/p1ppqpb1/bn2pnp1/2PPN3/1p2P3/2N2Q1p/PP1PBPPP/R3K2R w KQkq - 0 1",
            6,
        },
        {
            "queen_pressure",
            "6k1/5ppp/8/8/8/8/5PPP/6KQ w - - 0 1",
            8,
        },
    };

    const std::vector<BenchCase> timed_cases = {
        fixed_cases[0],
        fixed_cases[1],
    };

    if (options.timed)
        run_timed_suite(timed_cases, options);
    else
        run_fixed_suite(fixed_cases, options);

    return 0;
}
