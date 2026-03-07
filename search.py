# search.py
# The search algorithm — finds the best move for the side to move.
#
# Implements:
#   - Minimax with Alpha-Beta pruning
#   - Iterative deepening
#   - Basic move ordering (captures first)
#
# ── How Minimax works ──
# We imagine a tree of positions. The root is the current position.
# Each branch is a possible move. Each node's children are the opponent's replies.
# We alternate between MAXIMISING (our turn) and MINIMISING (opponent's turn).
# The best move is whichever leads to the highest guaranteed score.
#
# ── How Alpha-Beta improves it ──
# Alpha = the best score the maximiser has found so far
# Beta  = the best score the minimiser has found so far
# If we find a position the opponent would never allow (score >= beta),
# we stop searching that branch entirely — it's a "cutoff".
# This can reduce search time enormously without missing any good moves.
#
# ── How Iterative Deepening works ──
# Instead of searching straight to depth N, we search depth 1, then 2, then 3...
# This means we always have a best move ready, and move ordering improves each pass.

import time
from board import WHITE, BLACK
from movegen import MoveGenerator
from evaluate import Evaluator

# Score used to represent checkmate
CHECKMATE_SCORE = 100000


class Searcher:

    def __init__(self):
        self.gen = MoveGenerator()
        self.ev  = Evaluator()

        # Stats (useful for debugging and understanding the engine)
        self.nodes_searched = 0
        self.start_time     = 0

    def find_best_move(self, board, max_depth=4, time_limit=None):
        """
        Find the best move using iterative deepening alpha-beta search.

        max_depth  : maximum depth to search (4 = looks 4 moves ahead)
        time_limit : optional time limit in seconds

        Returns the best Move found, or None if no legal moves exist.
        """
        self.nodes_searched = 0
        self.start_time     = time.time()
        self.time_limit     = time_limit

        best_move  = None
        best_score = None

        # Iterative deepening: search depth 1, 2, 3... up to max_depth
        for depth in range(1, max_depth + 1):

            # Stop if we've run out of time
            if time_limit and (time.time() - self.start_time) >= time_limit:
                break

            move, score = self._search_root(board, depth)

            if move is not None:
                best_move  = move
                best_score = score

            elapsed = time.time() - self.start_time
            print(f"  Depth {depth}: best={best_move} score={best_score:+d} "
                  f"nodes={self.nodes_searched} time={elapsed:.2f}s")

        return best_move

    def _search_root(self, board, depth):
        """
        Search all legal moves at the root and return the best (move, score).
        Separated from _alphabeta so we can track which move gave the best score.
        """
        moves = self.gen.generate_legal_moves(board)

        if not moves:
            return None, 0

        # Order moves: search captures first (improves alpha-beta efficiency)
        moves = self._order_moves(board, moves)

        best_move  = moves[0]
        best_score = -CHECKMATE_SCORE - 1

        alpha = -CHECKMATE_SCORE - 1
        beta  =  CHECKMATE_SCORE + 1

        for move in moves:
            new_board = self.gen._apply_move(board, move)
            score = -self._alphabeta(new_board, depth - 1, -beta, -alpha)

            if score > best_score:
                best_score = score
                best_move  = move

            alpha = max(alpha, score)

        return best_move, best_score

    def _alphabeta(self, board, depth, alpha, beta):
        """
        Recursive alpha-beta search.

        Returns the best score achievable from this position,
        assuming both sides play optimally.

        Uses "negamax" framing: the score is always from the perspective
        of the SIDE TO MOVE. We negate the score at each level because
        what's good for one side is bad for the other.

        alpha : the best score the current player can guarantee
        beta  : the best score the opponent can guarantee (our ceiling)
        """
        self.nodes_searched += 1

        # ── Base case: reached maximum depth ──
        if depth == 0:
            return self._quiescence(board, alpha, beta)

        moves = self.gen.generate_legal_moves(board)

        # ── No legal moves: checkmate or stalemate ──
        if not moves:
            if self.gen._king_in_check(board, board.turn):
                # Checkmate — penalise by depth so we prefer faster mates
                return -(CHECKMATE_SCORE - (10 - depth))
            else:
                # Stalemate — draw
                return 0

        # Order moves for better cutoffs
        moves = self._order_moves(board, moves)

        for move in moves:
            new_board = self.gen._apply_move(board, move)
            score = -self._alphabeta(new_board, depth - 1, -beta, -alpha)

            if score >= beta:
                # Beta cutoff — opponent won't allow this
                return beta

            alpha = max(alpha, score)

        return alpha

    def _quiescence(self, board, alpha, beta):
        """
        Quiescence search: at the end of normal search, keep searching
        captures until the position is "quiet" (no more captures available).

        This prevents the "horizon effect" — where the engine doesn't see
        that its last move hangs a piece.
        """
        self.nodes_searched += 1

        # Evaluate the current position (the "stand pat" score)
        stand_pat = self._score_from_perspective(board)

        if stand_pat >= beta:
            return beta

        alpha = max(alpha, stand_pat)

        # Generate only capture moves
        moves = self.gen.generate_legal_moves(board)
        captures = [m for m in moves if not board.is_empty(m.to_sq)
                    or m.to_sq == board.en_passant_sq]

        for move in captures:
            new_board = self.gen._apply_move(board, move)
            score = -self._quiescence(new_board, -beta, -alpha)

            if score >= beta:
                return beta

            alpha = max(alpha, score)

        return alpha

    def _score_from_perspective(self, board):
        """
        Return the evaluation score from the perspective of the side to move.
        The Evaluator always returns positive=good for White,
        so we flip it if it's Black's turn.
        """
        score = self.ev.evaluate(board)
        return score if board.turn == WHITE else -score

    def _order_moves(self, board, moves):
        """
        Order moves to improve alpha-beta efficiency.
        Better moves searched first = more cutoffs = faster search.

        Current ordering:
          1. Captures (sorted by value of captured piece)
          2. Non-captures
        """
        from evaluate import PIECE_VALUES  # local import to avoid circular issues

        def move_score(move):
            target = board.get_piece(move.to_sq)
            if target != 0:
                # MVV-LVA: Most Valuable Victim, Least Valuable Attacker
                # Capture a queen with a pawn = very good
                victim_value   = abs(target) * 10
                attacker_value = abs(board.get_piece(move.from_sq))
                return victim_value - attacker_value
            return 0  # Non-capture

        return sorted(moves, key=move_score, reverse=True)


# ─────────────────────────────────────────
# QUICK TEST
# ─────────────────────────────────────────

if __name__ == "__main__":
    from board import Board

    board = Board()
    searcher = Searcher()

    print("Searching starting position to depth 4...\n")
    best = searcher.find_best_move(board, max_depth=4)
    print(f"\nBest move: {best}")
    print(f"Total nodes searched: {searcher.nodes_searched}")
