# movegen.py
# Move generation for our chess engine.
# Given a board state, produces a list of every legal move available.
#
# A "legal move" is one that:
#   1. Follows the movement rules of the piece
#   2. Does not leave our own king in check
#
# Strategy:
#   - Generate all "pseudo-legal" moves (correct piece movement, ignoring check)
#   - Filter out any that leave our king in check
#
# This is simple and correct. Later we can optimise it for speed.

from board import (
    Board, WHITE, BLACK, EMPTY,
    PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    square, file_of, rank_of, square_name
)

# ─────────────────────────────────────────
# MOVE CLASS
# ─────────────────────────────────────────

class Move:
    def __init__(self, from_sq, to_sq, promotion=None):
        """
        from_sq   : index of the square the piece moves FROM
        to_sq     : index of the square the piece moves TO
        promotion : piece type to promote to (KNIGHT/BISHOP/ROOK/QUEEN), or None
        """
        self.from_sq   = from_sq
        self.to_sq     = to_sq
        self.promotion = promotion

    def __repr__(self):
        promo = f"={self.promotion}" if self.promotion else ""
        return f"{square_name(self.from_sq)}{square_name(self.to_sq)}{promo}"

    def __eq__(self, other):
        return (self.from_sq == other.from_sq and
                self.to_sq   == other.to_sq   and
                self.promotion == other.promotion)


# ─────────────────────────────────────────
# DIRECTION VECTORS
# ─────────────────────────────────────────
# Chess squares are indexed 0-63 in rows of 8.
# Moving one rank up   = +8
# Moving one rank down = -8
# Moving one file right = +1 (but only if we don't wrap around!)
# Moving one file left  = -1

# Sliding piece directions (bishop, rook, queen)
DIAGONAL_DIRS   = [9, 7, -7, -9]   # Diagonals
STRAIGHT_DIRS   = [8, -8, 1, -1]   # Ranks and files
ALL_DIRS        = DIAGONAL_DIRS + STRAIGHT_DIRS

# Knight move offsets
KNIGHT_OFFSETS  = [17, 15, 10, 6, -6, -10, -15, -17]


# ─────────────────────────────────────────
# MOVE GENERATOR
# ─────────────────────────────────────────

class MoveGenerator:

    def generate_legal_moves(self, board):
        """
        Return a list of all legal Move objects for the side to move.
        This is the main function the rest of the engine will call.
        """
        pseudo_legal = self._generate_pseudo_legal_moves(board)
        legal = []

        for move in pseudo_legal:
            # Make the move on a copy of the board
            test_board = self._apply_move(board, move)
            # Only keep it if our king is not in check afterwards
            if not self._king_in_check(test_board, board.turn):
                legal.append(move)

        return legal

    # ─────────────────────────────────────
    # PSEUDO-LEGAL MOVE GENERATION
    # ─────────────────────────────────────

    def _generate_pseudo_legal_moves(self, board):
        """Generate all moves that follow piece movement rules, ignoring check."""
        moves = []

        for sq in range(64):
            piece = board.get_piece(sq)

            # Skip empty squares and opponent pieces
            if piece == EMPTY:
                continue
            if board.colour_at(sq) != board.turn:
                continue

            piece_type = abs(piece)

            if piece_type == PAWN:
                moves.extend(self._pawn_moves(board, sq))
            elif piece_type == KNIGHT:
                moves.extend(self._knight_moves(board, sq))
            elif piece_type == BISHOP:
                moves.extend(self._sliding_moves(board, sq, DIAGONAL_DIRS))
            elif piece_type == ROOK:
                moves.extend(self._sliding_moves(board, sq, STRAIGHT_DIRS))
            elif piece_type == QUEEN:
                moves.extend(self._sliding_moves(board, sq, ALL_DIRS))
            elif piece_type == KING:
                moves.extend(self._king_moves(board, sq))

        return moves

    # ─────────────────────────────────────
    # PIECE MOVE GENERATORS
    # ─────────────────────────────────────

    def _pawn_moves(self, board, sq):
        """Generate all pseudo-legal pawn moves from sq."""
        moves = []
        colour = board.turn
        direction = 8 if colour == WHITE else -8  # White moves up, Black moves down
        start_rank = 1 if colour == WHITE else 6  # Rank where pawns can move 2 squares
        promo_rank = 7 if colour == WHITE else 0  # Rank where pawns promote

        # ── Single push ──
        target = sq + direction
        if 0 <= target < 64 and board.is_empty(target):
            if rank_of(target) == promo_rank:
                # Promotion: add a move for each possible promotion piece
                for promo in [QUEEN, ROOK, BISHOP, KNIGHT]:
                    moves.append(Move(sq, target, promotion=promo))
            else:
                moves.append(Move(sq, target))

            # ── Double push (only from starting rank, only if single push was clear) ──
            if rank_of(sq) == start_rank:
                target2 = sq + direction * 2
                if board.is_empty(target2):
                    moves.append(Move(sq, target2))

        # ── Captures (diagonal) ──
        for capture_offset in [direction + 1, direction - 1]:
            target = sq + capture_offset
            if not (0 <= target < 64):
                continue

            # Prevent wrap-around (e.g. h-file capturing to a-file)
            if abs(file_of(sq) - file_of(target)) != 1:
                continue

            # Normal capture
            if board.colour_at(target) == -colour:
                if rank_of(target) == promo_rank:
                    for promo in [QUEEN, ROOK, BISHOP, KNIGHT]:
                        moves.append(Move(sq, target, promotion=promo))
                else:
                    moves.append(Move(sq, target))

            # En passant capture
            elif target == board.en_passant_sq:
                moves.append(Move(sq, target))

        return moves

    def _knight_moves(self, board, sq):
        """Generate all pseudo-legal knight moves from sq."""
        moves = []
        colour = board.turn

        for offset in KNIGHT_OFFSETS:
            target = sq + offset
            if not (0 <= target < 64):
                continue

            # Knights can wrap around the board without this check
            # A valid knight move changes file by 1 or 2
            file_diff = abs(file_of(sq) - file_of(target))
            rank_diff = abs(rank_of(sq) - rank_of(target))
            if not ({file_diff, rank_diff} == {1, 2}):
                continue

            # Can move to empty square or capture opponent
            if board.colour_at(target) != colour:
                moves.append(Move(sq, target))

        return moves

    def _sliding_moves(self, board, sq, directions):
        """
        Generate pseudo-legal moves for sliding pieces (bishop, rook, queen).
        Slides in each direction until hitting a piece or the board edge.
        """
        moves = []
        colour = board.turn

        for direction in directions:
            target = sq

            while True:
                prev_file = file_of(target)
                target += direction

                # Off the board
                if not (0 <= target < 64):
                    break

                # Prevent horizontal wrap-around
                new_file = file_of(target)
                if direction in (1, -1) and abs(prev_file - new_file) != 1:
                    break
                if direction in (9, -7) and new_file <= prev_file:
                    break
                if direction in (7, -9) and new_file >= prev_file:
                    break

                target_colour = board.colour_at(target)

                if target_colour == colour:
                    # Blocked by our own piece — stop
                    break
                elif target_colour == -colour:
                    # Opponent piece — capture and stop
                    moves.append(Move(sq, target))
                    break
                else:
                    # Empty square — move here and keep going
                    moves.append(Move(sq, target))

        return moves

    def _king_moves(self, board, sq):
        """Generate pseudo-legal king moves, including castling."""
        moves = []
        colour = board.turn

        # ── Normal king moves (one square in any direction) ──
        for direction in ALL_DIRS:
            target = sq + direction
            if not (0 <= target < 64):
                continue

            # Prevent wrap-around
            file_diff = abs(file_of(sq) - file_of(target))
            rank_diff = abs(rank_of(sq) - rank_of(target))
            if file_diff > 1 or rank_diff > 1:
                continue

            if board.colour_at(target) != colour:
                moves.append(Move(sq, target))

        # ── Castling ──
        # Conditions: castling right exists, squares between are empty,
        # king is not in check, king does not pass through check.
        # (The "not passing through check" is handled in legal move filtering.)

        if colour == WHITE:
            if board.castling_rights['K']:  # White kingside
                if board.is_empty(5) and board.is_empty(6):
                    moves.append(Move(sq, 6))  # e1 -> g1
            if board.castling_rights['Q']:  # White queenside
                if board.is_empty(3) and board.is_empty(2) and board.is_empty(1):
                    moves.append(Move(sq, 2))  # e1 -> c1
        else:
            if board.castling_rights['k']:  # Black kingside
                if board.is_empty(61) and board.is_empty(62):
                    moves.append(Move(sq, 62))  # e8 -> g8
            if board.castling_rights['q']:  # Black queenside
                if board.is_empty(59) and board.is_empty(58) and board.is_empty(57):
                    moves.append(Move(sq, 58))  # e8 -> c8

        return moves

    # ─────────────────────────────────────
    # APPLYING MOVES
    # ─────────────────────────────────────

    def _apply_move(self, board, move):
        """
        Return a new Board with the move applied.
        Does not modify the original board.
        """
        import copy
        new_board = copy.deepcopy(board)

        piece = new_board.get_piece(move.from_sq)
        piece_type = abs(piece)
        colour = 1 if piece > 0 else -1

        # ── Move the piece ──
        new_board.set_piece(move.to_sq, piece)
        new_board.set_piece(move.from_sq, EMPTY)

        # ── Promotion ──
        if move.promotion:
            new_board.set_piece(move.to_sq, colour * move.promotion)

        # ── En passant capture ──
        if piece_type == PAWN and move.to_sq == board.en_passant_sq:
            # Remove the captured pawn (it's one rank behind the target)
            captured_pawn_sq = move.to_sq - (8 if colour == WHITE else -8)
            new_board.set_piece(captured_pawn_sq, EMPTY)

        # ── Update en passant square ──
        if piece_type == PAWN and abs(move.to_sq - move.from_sq) == 16:
            # Pawn moved two squares — set en passant target
            new_board.en_passant_sq = (move.from_sq + move.to_sq) // 2
        else:
            new_board.en_passant_sq = -1

        # ── Castling: move the rook too ──
        if piece_type == KING:
            if move.to_sq - move.from_sq == 2:  # Kingside
                rook_from = move.from_sq + 3
                rook_to   = move.from_sq + 1
                new_board.set_piece(rook_to,   colour * ROOK)
                new_board.set_piece(rook_from, EMPTY)
            elif move.from_sq - move.to_sq == 2:  # Queenside
                rook_from = move.from_sq - 4
                rook_to   = move.from_sq - 1
                new_board.set_piece(rook_to,   colour * ROOK)
                new_board.set_piece(rook_from, EMPTY)

        # ── Update castling rights ──
        if piece_type == KING:
            if colour == WHITE:
                new_board.castling_rights['K'] = False
                new_board.castling_rights['Q'] = False
            else:
                new_board.castling_rights['k'] = False
                new_board.castling_rights['q'] = False

        if piece_type == ROOK:
            if move.from_sq == 0:  new_board.castling_rights['Q'] = False
            if move.from_sq == 7:  new_board.castling_rights['K'] = False
            if move.from_sq == 56: new_board.castling_rights['q'] = False
            if move.from_sq == 63: new_board.castling_rights['k'] = False

        # ── Update halfmove clock ──
        if piece_type == PAWN or board.colour_at(move.to_sq) == -colour:
            new_board.halfmove_clock = 0
        else:
            new_board.halfmove_clock += 1

        # ── Switch turn ──
        new_board.turn = -board.turn
        if new_board.turn == WHITE:
            new_board.fullmove_number += 1

        return new_board

    # ─────────────────────────────────────
    # CHECK DETECTION
    # ─────────────────────────────────────

    def _king_in_check(self, board, colour):
        """
        Return True if the king of the given colour is in check on this board.
        Strategy: find the king, then see if any opponent piece can attack it.
        """
        # Find the king
        king_sq = None
        for sq in range(64):
            if board.get_piece(sq) == colour * KING:
                king_sq = sq
                break

        if king_sq is None:
            return False  # Shouldn't happen in a legal game

        return self._square_attacked_by(board, king_sq, -colour)

    def _square_attacked_by(self, board, sq, attacker_colour):
        """
        Return True if the given square is attacked by any piece of attacker_colour.
        """
        # Check knight attacks
        for offset in KNIGHT_OFFSETS:
            target = sq + offset
            if not (0 <= target < 64):
                continue
            if abs(file_of(sq) - file_of(target)) not in (1, 2):
                continue
            if board.get_piece(target) == attacker_colour * KNIGHT:
                return True

        # Check sliding attacks (rook/queen on straights, bishop/queen on diagonals)
        for direction in STRAIGHT_DIRS:
            target = sq
            while True:
                prev_file = file_of(target)
                target += direction
                if not (0 <= target < 64):
                    break
                new_file = file_of(target)
                if direction in (1, -1) and abs(prev_file - new_file) != 1:
                    break
                piece = board.get_piece(target)
                if piece != EMPTY:
                    if piece == attacker_colour * ROOK or piece == attacker_colour * QUEEN:
                        return True
                    break

        for direction in DIAGONAL_DIRS:
            target = sq
            while True:
                prev_file = file_of(target)
                target += direction
                if not (0 <= target < 64):
                    break
                new_file = file_of(target)
                if direction in (9, -7) and new_file <= prev_file:
                    break
                if direction in (7, -9) and new_file >= prev_file:
                    break
                piece = board.get_piece(target)
                if piece != EMPTY:
                    if piece == attacker_colour * BISHOP or piece == attacker_colour * QUEEN:
                        return True
                    break

        # Check pawn attacks
        pawn_attack_dirs = [9, 7] if attacker_colour == BLACK else [-9, -7]
        for direction in pawn_attack_dirs:
            target = sq + direction
            if not (0 <= target < 64):
                continue
            if abs(file_of(sq) - file_of(target)) != 1:
                continue
            if board.get_piece(target) == attacker_colour * PAWN:
                return True

        # Check king attacks (to avoid kings walking next to each other)
        for direction in ALL_DIRS:
            target = sq + direction
            if not (0 <= target < 64):
                continue
            if abs(file_of(sq) - file_of(target)) > 1:
                continue
            if abs(rank_of(sq) - rank_of(target)) > 1:
                continue
            if board.get_piece(target) == attacker_colour * KING:
                return True

        return False


# ─────────────────────────────────────────
# QUICK TEST
# ─────────────────────────────────────────

if __name__ == "__main__":
    board = Board()
    gen = MoveGenerator()

    moves = gen.generate_legal_moves(board)
    print(f"Legal moves from starting position: {len(moves)}")
    print("(Should be 20 — 16 pawn moves + 4 knight moves)\n")
    print("Moves:", [str(m) for m in moves])
