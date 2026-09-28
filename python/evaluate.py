# evaluate.py
# Position evaluation for Stepbot.
#
# Phase 2 additions:
#   - Pawn structure (doubled, isolated, passed pawns)
#   - King safety (pawn shield, open files near king, attack count)
#   - Piece mobility (bonus for more available squares)
#   - Endgame detection (separate evaluation when few pieces remain)
#
# Returns a score in centipawns.
# Positive = good for White, Negative = good for Black.

from board import (
    Board, WHITE, BLACK, EMPTY,
    PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    square, file_of, rank_of
)

# ─────────────────────────────────────────
# PIECE VALUES
# ─────────────────────────────────────────

PIECE_VALUES = {
    PAWN:   100,
    KNIGHT: 320,
    BISHOP: 330,
    ROOK:   500,
    QUEEN:  900,
    KING:   20000,
}

# ─────────────────────────────────────────
# PIECE-SQUARE TABLES (middlegame)
# ─────────────────────────────────────────

PAWN_TABLE = [
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0,
]

KNIGHT_TABLE = [
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50,
]

BISHOP_TABLE = [
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20,
]

ROOK_TABLE = [
     0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0,
]

QUEEN_TABLE = [
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -10,  5,  5,  5,  5,  5,  0,-10,
      0,  0,  5,  5,  5,  5,  0, -5,
     -5,  0,  5,  5,  5,  5,  0, -5,
    -10,  0,  5,  5,  5,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20,
]

# King middlegame: hide behind pawns
KING_TABLE_MG = [
     20, 30, 10,  0,  0, 10, 30, 20,
     20, 20,  0,  0,  0,  0, 20, 20,
    -10,-20,-20,-20,-20,-20,-20,-10,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
]

# King endgame: become active and centralise
KING_TABLE_EG = [
    -50,-30,-30,-30,-30,-30,-30,-50,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -50,-40,-30,-20,-20,-30,-40,-50,
]

PIECE_SQUARE_TABLES_MG = {
    PAWN:   PAWN_TABLE,
    KNIGHT: KNIGHT_TABLE,
    BISHOP: BISHOP_TABLE,
    ROOK:   ROOK_TABLE,
    QUEEN:  QUEEN_TABLE,
    KING:   KING_TABLE_MG,
}

# ─────────────────────────────────────────
# EVALUATION WEIGHTS
# Easy to tune later (Phase 4 texel tuning)
# ─────────────────────────────────────────

# Pawn structure
DOUBLED_PAWN_PENALTY   = -20   # Per extra pawn on same file
ISOLATED_PAWN_PENALTY  = -20   # Pawn with no friendly pawns on adjacent files
PASSED_PAWN_BONUS      = [0, 10, 20, 40, 60, 80, 120, 0]  # Bonus by rank (0-7)
PASSED_PAWN_BONUS_EG   = [0, 20, 40, 70,100,150, 200, 0]  # Bigger in endgame

# King safety
PAWN_SHIELD_BONUS      =  10   # Per pawn directly shielding the king
OPEN_FILE_NEAR_KING    = -20   # Per open file adjacent to king
SEMI_OPEN_FILE_KING    = -10   # Per half-open file adjacent to king
KING_ATTACKER_WEIGHT   = [-50, -30, -20, -10]  # By number of attackers (1,2,3,4+)

# Mobility
MOBILITY_BONUS = {
    KNIGHT: 4,   # Per available square
    BISHOP: 3,
    ROOK:   2,
    QUEEN:  1,
}

# Bishop pair bonus
BISHOP_PAIR_BONUS = 30

# Endgame threshold (total non-pawn, non-king material per side)
# Below this = endgame
ENDGAME_THRESHOLD = 1300  # Roughly: no queens, or queen + one minor piece gone


# ─────────────────────────────────────────
# EVALUATOR
# ─────────────────────────────────────────

class Evaluator:

    def evaluate(self, board):
        """
        Return a score for the current position.
        Positive = good for White, Negative = good for Black.
        """
        # Detect game phase
        is_endgame = self._is_endgame(board)

        score = 0
        score += self._material_and_placement(board, is_endgame)
        score += self._pawn_structure(board, is_endgame)
        score += self._king_safety(board, is_endgame)
        score += self._mobility(board)
        score += self._bishop_pair(board)

        return score

    # ─────────────────────────────────────
    # GAME PHASE DETECTION
    # ─────────────────────────────────────

    def _is_endgame(self, board):
        """
        Return True if the position is in the endgame.
        Defined as: total non-pawn, non-king material for either side
        falling below the threshold.
        """
        for colour in (WHITE, BLACK):
            material = 0
            for sq in range(64):
                piece = board.get_piece(sq)
                if piece == EMPTY:
                    continue
                if (piece > 0) == (colour == WHITE):
                    pt = abs(piece)
                    if pt not in (PAWN, KING):
                        material += PIECE_VALUES[pt]
            if material <= ENDGAME_THRESHOLD:
                return True
        return False

    # ─────────────────────────────────────
    # MATERIAL AND PIECE-SQUARE TABLES
    # ─────────────────────────────────────

    def _material_and_placement(self, board, is_endgame):
        """Score material and piece placement."""
        score = 0

        for sq in range(64):
            piece = board.get_piece(sq)
            if piece == EMPTY:
                continue

            colour     = WHITE if piece > 0 else BLACK
            piece_type = abs(piece)

            # Material
            score += colour * PIECE_VALUES[piece_type]

            # Piece-square table
            if piece_type == KING:
                table = KING_TABLE_EG if is_endgame else KING_TABLE_MG
            else:
                table = PIECE_SQUARE_TABLES_MG[piece_type]

            if colour == WHITE:
                ps_bonus = table[sq]
            else:
                mirrored = (7 - rank_of(sq)) * 8 + file_of(sq)
                ps_bonus = table[mirrored]

            score += colour * ps_bonus

        return score

    # ─────────────────────────────────────
    # PAWN STRUCTURE
    # ─────────────────────────────────────

    def _pawn_structure(self, board, is_endgame):
        """
        Evaluate pawn structure for both sides.
        Penalties for doubled and isolated pawns.
        Bonuses for passed pawns.
        """
        score = 0

        for colour in (WHITE, BLACK):
            sign       = colour  # WHITE=+1, BLACK=-1
            direction  = 1 if colour == WHITE else -1
            enemy      = -colour

            # Build a set of files with friendly pawns, and enemy pawn positions
            friendly_pawn_files = set()
            enemy_pawns_by_file = {}   # file -> list of ranks

            for sq in range(64):
                piece = board.get_piece(sq)
                if piece == colour * PAWN:
                    friendly_pawn_files.add(file_of(sq))
                if piece == enemy * PAWN:
                    f = file_of(sq)
                    if f not in enemy_pawns_by_file:
                        enemy_pawns_by_file[f] = []
                    enemy_pawns_by_file[f].append(rank_of(sq))

            # Now evaluate each friendly pawn
            pawns_per_file = {}
            for sq in range(64):
                piece = board.get_piece(sq)
                if piece != colour * PAWN:
                    continue

                f = file_of(sq)
                r = rank_of(sq)

                # Count pawns per file (for doubled detection)
                pawns_per_file[f] = pawns_per_file.get(f, 0) + 1

                # ── Isolated pawn ──
                # No friendly pawn on adjacent files
                adjacent_files = set()
                if f > 0: adjacent_files.add(f - 1)
                if f < 7: adjacent_files.add(f + 1)
                if not adjacent_files & friendly_pawn_files:
                    score += sign * ISOLATED_PAWN_PENALTY

                # ── Passed pawn ──
                # No enemy pawn on same file or adjacent files ahead of this pawn
                is_passed = True
                check_files = {f} | adjacent_files
                for cf in check_files:
                    enemy_ranks = enemy_pawns_by_file.get(cf, [])
                    for er in enemy_ranks:
                        # Is this enemy pawn ahead of (or blocking) our pawn?
                        if colour == WHITE and er >= r:
                            is_passed = False
                            break
                        if colour == BLACK and er <= r:
                            is_passed = False
                            break
                    if not is_passed:
                        break

                if is_passed:
                    bonus_table = PASSED_PAWN_BONUS_EG if is_endgame else PASSED_PAWN_BONUS
                    # Use rank from White's perspective for the bonus table
                    bonus_rank = r if colour == WHITE else (7 - r)
                    score += sign * bonus_table[bonus_rank]

            # ── Doubled pawns ──
            for f, count in pawns_per_file.items():
                if count > 1:
                    score += sign * DOUBLED_PAWN_PENALTY * (count - 1)

        return score

    # ─────────────────────────────────────
    # KING SAFETY
    # ─────────────────────────────────────

    def _king_safety(self, board, is_endgame):
        """
        Evaluate king safety for both sides.
        Less important in endgames (kings become active pieces).
        """
        if is_endgame:
            return 0  # King safety handled by endgame piece-square table

        score = 0

        for colour in (WHITE, BLACK):
            sign  = colour
            enemy = -colour

            # Find the king
            king_sq = None
            for sq in range(64):
                if board.get_piece(sq) == colour * KING:
                    king_sq = sq
                    break
            if king_sq is None:
                continue

            king_file = file_of(king_sq)
            king_rank = rank_of(king_sq)

            # ── Pawn shield ──
            # Reward pawns on the two ranks directly in front of the king
            shield_ranks = [king_rank + (1 if colour == WHITE else -1),
                            king_rank + (2 if colour == WHITE else -2)]

            for shield_rank in shield_ranks:
                if not (0 <= shield_rank <= 7):
                    continue
                for df in (-1, 0, 1):
                    sf = king_file + df
                    if not (0 <= sf <= 7):
                        continue
                    sq = square(sf, shield_rank)
                    if board.get_piece(sq) == colour * PAWN:
                        score += sign * PAWN_SHIELD_BONUS

            # ── Open files near king ──
            for df in (-1, 0, 1):
                f = king_file + df
                if not (0 <= f <= 7):
                    continue

                has_friendly_pawn = False
                has_enemy_pawn    = False

                for r in range(8):
                    piece = board.get_piece(square(f, r))
                    if piece == colour * PAWN:
                        has_friendly_pawn = True
                    if piece == enemy * PAWN:
                        has_enemy_pawn = True

                if not has_friendly_pawn and not has_enemy_pawn:
                    score += sign * OPEN_FILE_NEAR_KING      # Fully open
                elif not has_friendly_pawn:
                    score += sign * SEMI_OPEN_FILE_KING       # Half-open

            # ── Enemy attackers near king ──
            # Count enemy pieces attacking squares adjacent to the king
            attacker_count = 0
            for r in range(max(0, king_rank - 1), min(8, king_rank + 2)):
                for f in range(max(0, king_file - 1), min(8, king_file + 2)):
                    sq = square(f, r)
                    # Check if any enemy piece attacks this square
                    # (simplified: just count enemy pieces in the vicinity)
                    piece = board.get_piece(sq)
                    if piece != EMPTY and (piece > 0) != (colour == WHITE):
                        pt = abs(piece)
                        if pt in (KNIGHT, BISHOP, ROOK, QUEEN):
                            attacker_count += 1

            if attacker_count > 0:
                idx = min(attacker_count - 1, len(KING_ATTACKER_WEIGHT) - 1)
                score += sign * KING_ATTACKER_WEIGHT[idx]

        return score

    # ─────────────────────────────────────
    # PIECE MOBILITY
    # ─────────────────────────────────────

    def _mobility(self, board):
        """
        Bonus for pieces that have more squares available to move to.
        Uses pseudo-legal move counts for speed.
        """
        from movegen import MoveGenerator
        score = 0

        # We need move counts for both sides — temporarily check each
        for colour in (WHITE, BLACK):
            sign = colour

            # Count pseudo-legal moves for each piece type
            for sq in range(64):
                piece = board.get_piece(sq)
                if piece == EMPTY:
                    continue
                if (piece > 0) != (colour == WHITE):
                    continue

                piece_type = abs(piece)
                if piece_type not in MOBILITY_BONUS:
                    continue

                # Count reachable squares (simplified: just count empty/capturable)
                mobility = self._count_mobility(board, sq, piece_type, colour)
                score += sign * MOBILITY_BONUS[piece_type] * mobility

        return score

    def _count_mobility(self, board, sq, piece_type, colour):
        """Count the number of squares a piece can reach (pseudo-legal)."""
        from board import DIAGONAL_DIRS, STRAIGHT_DIRS, ALL_DIRS, KNIGHT_OFFSETS
        count = 0

        if piece_type == KNIGHT:
            for offset in KNIGHT_OFFSETS:
                target = sq + offset
                if not (0 <= target < 64):
                    continue
                if abs(file_of(sq) - file_of(target)) not in (1, 2):
                    continue
                if board.colour_at(target) != colour:
                    count += 1

        elif piece_type in (BISHOP, ROOK, QUEEN):
            dirs = (DIAGONAL_DIRS if piece_type == BISHOP else
                    STRAIGHT_DIRS if piece_type == ROOK else
                    ALL_DIRS)
            for direction in dirs:
                target = sq
                while True:
                    prev_file = file_of(target)
                    target += direction
                    if not (0 <= target < 64):
                        break
                    new_file = file_of(target)
                    if direction in (1, -1) and abs(prev_file - new_file) != 1:
                        break
                    if direction in (9, -7) and new_file <= prev_file:
                        break
                    if direction in (7, -9) and new_file >= prev_file:
                        break
                    if board.colour_at(target) == colour:
                        break
                    count += 1
                    if board.colour_at(target) == -colour:
                        break

        return count

    # ─────────────────────────────────────
    # BISHOP PAIR
    # ─────────────────────────────────────

    def _bishop_pair(self, board):
        """Bonus for having both bishops (very strong in open positions)."""
        score = 0
        for colour in (WHITE, BLACK):
            bishop_count = sum(
                1 for sq in range(64)
                if board.get_piece(sq) == colour * BISHOP
            )
            if bishop_count >= 2:
                score += colour * BISHOP_PAIR_BONUS
        return score


# ─────────────────────────────────────────
# QUICK TEST
# ─────────────────────────────────────────

if __name__ == "__main__":
    board = Board()
    ev    = Evaluator()

    score = ev.evaluate(board)
    print(f"Starting position score: {score}")
    print("(Should be 0 — perfectly symmetric)\n")

    print(f"Is endgame: {ev._is_endgame(board)}")
    print("(Should be False — full pieces on the board)\n")

    # Pawn structure test: remove a white pawn to create an isolated pawn
    from board import square
    board.set_piece(square(1, 1), EMPTY)  # Remove b2 pawn
    score2 = ev.evaluate(board)
    print(f"Score after removing b2 pawn: {score2}")
    print("(Should be negative — White is down material and has structural issues)")
