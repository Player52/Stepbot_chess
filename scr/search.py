# search.py
# Alpha-beta search with iterative deepening.
#
# Phase 2 additions:
#   - Transposition table (avoid re-searching known positions)
#   - Killer move heuristic (try moves that caused cutoffs recently)
#   - History heuristic (try historically good quiet moves earlier)

import time
from board import WHITE, BLACK, EMPTY, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING
from movegen import MoveGenerator
from evaluate import Evaluator
from zobrist import compute_hash, update_hash

CHECKMATE_SCORE = 100000

# ─────────────────────────────────────────
# TRANSPOSITION TABLE FLAGS
# ─────────────────────────────────────────
# When alpha-beta cuts off, we don't always get an exact score.
# We record what kind of score it is so we use it correctly.

EXACT       = 0   # Score is exact
LOWER_BOUND = 1   # Score is at least this (beta cutoff — we stopped searching)
UPPER_BOUND = 2   # Score is at most this (no move improved alpha)


# ─────────────────────────────────────────
# TRANSPOSITION TABLE ENTRY
# ─────────────────────────────────────────

class TTEntry:
    __slots__ = ['hash', 'depth', 'score', 'flag', 'move']

    def __init__(self, hash, depth, score, flag, move):
        self.hash  = hash   # Full hash (to verify — avoid collisions)
        self.depth = depth  # How deep we searched
        self.score = score  # Score found
        self.flag  = flag   # EXACT / LOWER_BOUND / UPPER_BOUND
        self.move  = move   # Best move found (can be None)


# ─────────────────────────────────────────
# SEARCHER
# ─────────────────────────────────────────

class Searcher:

    # Maximum number of entries in the transposition table.
    # Each entry is ~100 bytes, so 1M entries ≈ 100MB.
    TT_SIZE = 1_000_000

    # Maximum depth for killer moves table
    MAX_DEPTH = 64

    def __init__(self):
        self.gen      = MoveGenerator()
        self.ev       = Evaluator()

        # Transposition table: maps hash -> TTEntry
        self.tt = {}

        # Killer moves: two quiet moves per depth that recently caused cutoffs
        # killers[depth] = [move1, move2]
        self.killers = [[] for _ in range(self.MAX_DEPTH)]

        # History heuristic: history[from_sq][to_sq] = score
        # Incremented when a quiet move causes a beta cutoff
        self.history = [[0] * 64 for _ in range(64)]

        # Stats
        self.nodes_searched = 0
        self.tt_hits        = 0
        self.start_time     = 0
        self.time_limit     = None

    def find_best_move(self, board, max_depth=4, time_limit=None):
        """
        Find the best move using iterative deepening alpha-beta search.
        Returns the best Move found, or None if no legal moves exist.
        """
        self.nodes_searched = 0
        self.tt_hits        = 0
        self.start_time     = time.time()
        self.time_limit     = time_limit

        # Reset killer moves and history for a new search
        self.killers = [[] for _ in range(self.MAX_DEPTH)]
        self.history = [[0] * 64 for _ in range(64)]

        # Compute the starting hash
        current_hash = compute_hash(board)

        best_move  = None
        best_score = None

        for depth in range(1, max_depth + 1):
            if time_limit and (time.time() - self.start_time) >= time_limit:
                break

            move, score = self._search_root(board, current_hash, depth)

            if move is not None:
                best_move  = move
                best_score = score

            elapsed = time.time() - self.start_time
            print(f"  Depth {depth}: best={best_move} score={best_score:+d} "
                  f"nodes={self.nodes_searched} tt_hits={self.tt_hits} "
                  f"time={elapsed:.2f}s")

        return best_move

    def _search_root(self, board, current_hash, depth):
        """Search all root moves and return (best_move, best_score)."""
        moves = self.gen.generate_legal_moves(board)
        if not moves:
            return None, 0

        moves = self._order_moves(board, moves, depth=0)

        best_move  = moves[0]
        best_score = -CHECKMATE_SCORE - 1
        alpha      = -CHECKMATE_SCORE - 1
        beta       =  CHECKMATE_SCORE + 1

        for move in moves:
            new_board = self.gen._apply_move(board, move)
            new_hash  = update_hash(current_hash, board, move, new_board)
            score     = -self._alphabeta(new_board, new_hash, depth - 1, -beta, -alpha, 1)

            if score > best_score:
                best_score = score
                best_move  = move

            alpha = max(alpha, score)

        # Store root result in transposition table
        self._tt_store(current_hash, depth, best_score, EXACT, best_move)

        return best_move, best_score

    def _alphabeta(self, board, current_hash, depth, alpha, beta, ply):
        """
        Recursive negamax alpha-beta search.

        board        : current position
        current_hash : Zobrist hash of current position
        depth        : remaining depth to search
        alpha        : best score current player can guarantee
        beta         : best score opponent can guarantee (our ceiling)
        ply          : distance from root (used for killer moves)
        """
        self.nodes_searched += 1

        # ── Transposition table lookup ──
        tt_move = None
        entry   = self.tt.get(current_hash)

        if entry and entry.hash == current_hash and entry.depth >= depth:
            self.tt_hits += 1
            if entry.flag == EXACT:
                return entry.score
            elif entry.flag == LOWER_BOUND:
                alpha = max(alpha, entry.score)
            elif entry.flag == UPPER_BOUND:
                beta = min(beta, entry.score)

            if alpha >= beta:
                return entry.score

            tt_move = entry.move  # Try this move first

        # ── Base case ──
        if depth == 0:
            return self._quiescence(board, current_hash, alpha, beta)

        moves = self.gen.generate_legal_moves(board)

        # ── No legal moves: checkmate or stalemate ──
        if not moves:
            if self.gen._king_in_check(board, board.turn):
                return -(CHECKMATE_SCORE - ply)
            return 0

        # ── Order moves ──
        moves = self._order_moves(board, moves, depth=ply, tt_move=tt_move)

        original_alpha = alpha
        best_move      = None

        for move in moves:
            new_board = self.gen._apply_move(board, move)
            new_hash  = update_hash(current_hash, board, move, new_board)
            score     = -self._alphabeta(new_board, new_hash, depth - 1,
                                         -beta, -alpha, ply + 1)

            if score >= beta:
                # Beta cutoff — update killer moves and history
                self._update_killers(move, ply)
                self._update_history(board, move, depth)
                self._tt_store(current_hash, depth, beta, LOWER_BOUND, move)
                return beta

            if score > alpha:
                alpha     = score
                best_move = move

        # ── Store result in transposition table ──
        if best_move:
            flag = EXACT if alpha > original_alpha else UPPER_BOUND
            self._tt_store(current_hash, depth, alpha, flag, best_move)

        return alpha

    def _quiescence(self, board, current_hash, alpha, beta):
        """
        Search captures only until the position is quiet.
        Prevents the horizon effect.
        """
        self.nodes_searched += 1

        stand_pat = self._score_from_perspective(board)

        if stand_pat >= beta:
            return beta

        alpha = max(alpha, stand_pat)

        moves    = self.gen.generate_legal_moves(board)
        captures = [m for m in moves
                    if not board.is_empty(m.to_sq)
                    or m.to_sq == board.en_passant_sq]

        captures = self._order_moves(board, captures, depth=0)

        for move in captures:
            new_board = self.gen._apply_move(board, move)
            new_hash  = update_hash(current_hash, board, move, new_board)
            score     = -self._quiescence(new_board, new_hash, -beta, -alpha)

            if score >= beta:
                return beta

            alpha = max(alpha, score)

        return alpha

    # ─────────────────────────────────────
    # MOVE ORDERING
    # ─────────────────────────────────────

    def _order_moves(self, board, moves, depth=0, tt_move=None):
        """
        Order moves for better alpha-beta efficiency.

        Priority (highest first):
          1. Transposition table move (best move from previous search)
          2. Captures — sorted by MVV-LVA (best capture first)
          3. Killer moves (quiet moves that caused cutoffs at this depth)
          4. History heuristic moves (quiet moves that historically cause cutoffs)
          5. Everything else
        """
        def move_score(move):
            # TT move gets highest priority
            if tt_move and move == tt_move:
                return 20000

            target = board.get_piece(move.to_sq)

            # Captures: MVV-LVA
            if target != EMPTY:
                victim_value   = abs(target) * 10
                attacker_value = abs(board.get_piece(move.from_sq))
                return 10000 + victim_value - attacker_value

            # En passant capture
            if move.to_sq == board.en_passant_sq:
                return 10000

            # Killer moves
            killers = self.killers[depth] if depth < self.MAX_DEPTH else []
            if move in killers:
                return 9000 - killers.index(move)  # First killer > second killer

            # History heuristic
            return self.history[move.from_sq][move.to_sq]

        return sorted(moves, key=move_score, reverse=True)

    # ─────────────────────────────────────
    # KILLER MOVES
    # ─────────────────────────────────────

    def _update_killers(self, move, ply):
        """
        Store a quiet move that caused a beta cutoff as a killer move.
        We keep two killers per ply. Only quiet moves (non-captures) are killers.
        """
        if ply >= self.MAX_DEPTH:
            return

        killers = self.killers[ply]

        if move in killers:
            return

        killers.insert(0, move)  # New killer goes first
        if len(killers) > 2:
            killers.pop()        # Keep only two killers per ply

    # ─────────────────────────────────────
    # HISTORY HEURISTIC
    # ─────────────────────────────────────

    def _update_history(self, board, move, depth):
        """
        Increment the history score for a quiet move that caused a cutoff.
        Deeper cutoffs are worth more (depth * depth weighting).
        """
        if board.is_empty(move.to_sq):  # Only quiet moves
            self.history[move.from_sq][move.to_sq] += depth * depth

    # ─────────────────────────────────────
    # TRANSPOSITION TABLE
    # ─────────────────────────────────────

    def _tt_store(self, hash_val, depth, score, flag, move):
        """Store an entry in the transposition table."""
        if len(self.tt) >= self.TT_SIZE:
            # Remove ~10% of entries when full
            keys = list(self.tt.keys())[:self.TT_SIZE // 10]
            for k in keys:
                del self.tt[k]

        self.tt[hash_val] = TTEntry(hash_val, depth, score, flag, move)

    # ─────────────────────────────────────
    # HELPERS
    # ─────────────────────────────────────

    def _score_from_perspective(self, board):
        """Return eval score from the perspective of the side to move."""
        score = self.ev.evaluate(board)
        return score if board.turn == WHITE else -score


# ─────────────────────────────────────────
# QUICK TEST
# ─────────────────────────────────────────

if __name__ == "__main__":
    from board import Board

    board    = Board()
    searcher = Searcher()

    print("Searching starting position to depth 5...\n")
    best = searcher.find_best_move(board, max_depth=5)
    print(f"\nBest move: {best}")
    print(f"Nodes searched : {searcher.nodes_searched}")
    print(f"TT hits        : {searcher.tt_hits}")
