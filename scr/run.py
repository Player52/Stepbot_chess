# run.py
# Launcher for Stepbot chess engine.
# Run this file to start the engine in UCI mode.
#
# Usage:
#   python run.py
#
# Or to test individual components:
#   python run.py --test

import sys
import os

# Make sure all engine files are in the path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

def run_tests():
    """Run quick sanity checks on all components."""
    print("=" * 50)
    print("  Stepbot — Running tests")
    print("=" * 50)

    # ── Board test ──
    print("\n[1/3] Board...")
    from board import Board, square, square_name, name_to_square
    board = Board()
    assert square(0, 0) == 0,  "square(0,0) should be 0"
    assert square(4, 7) == 60, "square(4,7) should be 60"
    assert square_name(0)  == 'a1', "square_name(0) should be a1"
    assert square_name(60) == 'e8', "square_name(60) should be e8"
    assert name_to_square('e4') == 28, "e4 should be square 28"
    print("  ✓ Board representation OK")

    # ── Move generation test ──
    print("\n[2/3] Move generation...")
    from movegen import MoveGenerator
    gen = MoveGenerator()
    moves = gen.generate_legal_moves(board)
    assert len(moves) == 20, f"Expected 20 moves from start, got {len(moves)}"
    print(f"  ✓ Move generation OK ({len(moves)} moves from starting position)")

    # ── Evaluation test ──
    print("\n[3/3] Evaluation...")
    from evaluate import Evaluator
    ev = Evaluator()
    score = ev.evaluate(board)
    assert score == 0, f"Starting position should score 0, got {score}"
    print(f"  ✓ Evaluation OK (starting position score: {score})")

    print("\n" + "=" * 50)
    print("  All tests passed! Stepbot is ready.")
    print("=" * 50)
    print("\nStarting engine in UCI mode...\n")

def run_engine():
    """Start the UCI engine."""
    from engine import UCIEngine
    engine = UCIEngine()
    engine.run()

if __name__ == "__main__":
    if "--test" in sys.argv:
        run_tests()
    else:
        # Always run a quick silent check before starting
        try:
            from board import Board
            from movegen import MoveGenerator
            from evaluate import Evaluator
            from search import Searcher
            from engine import UCIEngine
        except ImportError as e:
            print(f"Error: missing file — {e}")
            print("Make sure all engine files are in the same folder as run.py")
            sys.exit(1)

    run_engine()
