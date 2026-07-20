# engine.py
# The UCI (Universal Chess Interface) protocol implementation.
#
# UCI is the standard way chess engines communicate with GUIs.
# The GUI sends text commands to the engine via stdin,
# the engine replies via stdout.
#
# Key UCI commands we implement:
#   uci          → engine identifies itself
#   isready      → engine confirms it's ready
#   ucinewgame   → reset for a new game
#   position     → set up a board position (startpos or FEN + moves)
#   go           → start searching, return best move
#   quit         → exit
#
# To use with a GUI (e.g. Arena, CuteChess):
#   Point the GUI at this file: python engine.py
#
# To test manually:
#   Run: python engine.py
#   Type UCI commands and press Enter.

import sys
from board import Board, WHITE, BLACK, EMPTY, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING
from board import square, file_of, rank_of, square_name, name_to_square
from movegen import MoveGenerator, Move
from search import Searcher

# ─────────────────────────────────────────
# FEN PARSER
# FEN is the standard notation for describing a chess position.
# Example starting position FEN:
#   rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
# ─────────────────────────────────────────

FEN_PIECE_MAP = {
    'P':  1, 'N':  2, 'B':  3, 'R':  4, 'Q':  5, 'K':  6,
    'p': -1, 'n': -2, 'b': -3, 'r': -4, 'q': -5, 'k': -6,
}

STARTING_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"


def board_from_fen(fen):
    """Parse a FEN string and return a Board object."""
    board = Board()
    board.squares = [EMPTY] * 64

    parts = fen.strip().split()
    piece_placement = parts[0]
    active_colour   = parts[1] if len(parts) > 1 else 'w'
    castling        = parts[2] if len(parts) > 2 else 'KQkq'
    en_passant      = parts[3] if len(parts) > 3 else '-'
    halfmove        = parts[4] if len(parts) > 4 else '0'
    fullmove        = parts[5] if len(parts) > 5 else '1'

    # ── Piece placement ──
    # FEN ranks go from rank 8 (top) to rank 1 (bottom)
    rank = 7
    file = 0
    for char in piece_placement:
        if char == '/':
            rank -= 1
            file = 0
        elif char.isdigit():
            file += int(char)  # Empty squares
        else:
            sq = square(file, rank)
            board.squares[sq] = FEN_PIECE_MAP[char]
            file += 1

    # ── Active colour ──
    board.turn = WHITE if active_colour == 'w' else BLACK

    # ── Castling rights ──
    board.castling_rights = {
        'K': 'K' in castling,
        'Q': 'Q' in castling,
        'k': 'k' in castling,
        'q': 'q' in castling,
    }

    # ── En passant ──
    if en_passant != '-':
        board.en_passant_sq = name_to_square(en_passant)
    else:
        board.en_passant_sq = -1

    # ── Clocks ──
    board.halfmove_clock    = int(halfmove)
    board.fullmove_number   = int(fullmove)

    return board


def move_from_uci(board, uci_str, gen):
    """
    Parse a UCI move string (e.g. 'e2e4', 'e7e8q') and return a Move object.
    Matches against legal moves on the board.
    """
    from_sq = name_to_square(uci_str[0:2])
    to_sq   = name_to_square(uci_str[2:4])

    promotion = None
    if len(uci_str) == 5:
        promo_char = uci_str[4].lower()
        promo_map  = {'n': KNIGHT, 'b': BISHOP, 'r': ROOK, 'q': QUEEN}
        promotion  = promo_map.get(promo_char)

    return Move(from_sq, to_sq, promotion)


def board_to_fen(board):
    """Convert a Board object to a FEN string (useful for debugging)."""
    PIECE_FEN = {
         1: 'P',  2: 'N',  3: 'B',  4: 'R',  5: 'Q',  6: 'K',
        -1: 'p', -2: 'n', -3: 'b', -4: 'r', -5: 'q', -6: 'k',
    }

    rows = []
    for rank in range(7, -1, -1):
        row   = ''
        empty = 0
        for file in range(8):
            piece = board.squares[square(file, rank)]
            if piece == EMPTY:
                empty += 1
            else:
                if empty:
                    row  += str(empty)
                    empty = 0
                row += PIECE_FEN[piece]
        if empty:
            row += str(empty)
        rows.append(row)

    placement = '/'.join(rows)
    turn      = 'w' if board.turn == WHITE else 'b'

    castling = ''
    for key in ['K', 'Q', 'k', 'q']:
        if board.castling_rights[key]:
            castling += key
    if not castling:
        castling = '-'

    ep = square_name(board.en_passant_sq) if board.en_passant_sq != -1 else '-'

    return f"{placement} {turn} {castling} {ep} {board.halfmove_clock} {board.fullmove_number}"


# ─────────────────────────────────────────
# UCI ENGINE LOOP
# ─────────────────────────────────────────

class UCIEngine:

    ENGINE_NAME   = "PyChess"
    ENGINE_AUTHOR = "James"

    def __init__(self):
        self.board    = board_from_fen(STARTING_FEN)
        self.gen      = MoveGenerator()
        self.searcher = Searcher()
        self.depth    = 4  # Default search depth

        # Opening book — loaded on startup
        from book import OpeningBook
        self.book = OpeningBook()

    def run(self):
        """Main loop — reads UCI commands from stdin."""
        while True:
            try:
                line = input().strip()
            except EOFError:
                break

            if not line:
                continue

            self._handle_command(line)

    def _handle_command(self, line):
        """Dispatch a UCI command to the appropriate handler."""
        tokens = line.split()
        cmd    = tokens[0]

        if cmd == 'uci':
            self._cmd_uci()
        elif cmd == 'isready':
            self._cmd_isready()
        elif cmd == 'ucinewgame':
            self._cmd_ucinewgame()
        elif cmd == 'position':
            self._cmd_position(tokens[1:])
        elif cmd == 'go':
            self._cmd_go(tokens[1:])
        elif cmd == 'stop':
            pass  # We don't do time management yet
        elif cmd == 'quit':
            sys.exit(0)
        elif cmd == 'print':
            # Non-standard — handy for debugging
            self.board.print_board()
        elif cmd == 'fen':
            # Non-standard — print current FEN
            print(board_to_fen(self.board))
        elif cmd == 'moves':
            # Non-standard — list all legal moves
            moves = self.gen.generate_legal_moves(self.board)
            print(f"Legal moves ({len(moves)}): {' '.join(str(m) for m in moves)}")
        else:
            # Unknown command — ignore (UCI spec says to do this)
            pass

    def _cmd_uci(self):
        """Respond to 'uci' — identify the engine."""
        print(f"id name {self.ENGINE_NAME}")
        print(f"id author {self.ENGINE_AUTHOR}")
        print(f"option name Depth type spin default 4 min 1 max 10")
        print("uciok")

    def _cmd_isready(self):
        """Respond to 'isready' — confirm engine is initialised."""
        print("readyok")

    def _cmd_ucinewgame(self):
        """Reset for a new game."""
        self.board = board_from_fen(STARTING_FEN)

    def _cmd_position(self, tokens):
        """
        Set up the board from a position command.

        Formats:
          position startpos
          position startpos moves e2e4 e7e5 ...
          position fen <fen_string>
          position fen <fen_string> moves e2e4 ...
        """
        if not tokens:
            return

        if tokens[0] == 'startpos':
            self.board = board_from_fen(STARTING_FEN)
            move_start = 2 if len(tokens) > 1 and tokens[1] == 'moves' else None

        elif tokens[0] == 'fen':
            # FEN is the next 6 tokens
            fen_parts  = tokens[1:7]
            fen_string = ' '.join(fen_parts)
            self.board = board_from_fen(fen_string)
            move_start = 8 if len(tokens) > 7 and tokens[7] == 'moves' else None

        else:
            return

        # Apply any moves listed after 'moves'
        if move_start and len(tokens) >= move_start:
            for uci_str in tokens[move_start:]:
                move = move_from_uci(self.board, uci_str, self.gen)
                self.board = self.gen._apply_move(self.board, move)

    def _cmd_go(self, tokens):
        """
        Search for the best move and return it.

        Supports:
          go depth <n>       — search to fixed depth
          go movetime <ms>   — search for a fixed time
          go wtime/btime     — time controls (we use a simple fraction)
        """
        depth      = self.depth
        time_limit = None

        i = 0
        while i < len(tokens):
            if tokens[i] == 'depth' and i + 1 < len(tokens):
                depth = int(tokens[i + 1])
            elif tokens[i] == 'movetime' and i + 1 < len(tokens):
                time_limit = int(tokens[i + 1]) / 1000.0  # ms -> seconds
            elif tokens[i] in ('wtime', 'btime') and i + 1 < len(tokens):
                # Very simple time management: use 5% of remaining time
                ms = int(tokens[i + 1])
                if (tokens[i] == 'wtime' and self.board.turn == WHITE) or \
                   (tokens[i] == 'btime' and self.board.turn == BLACK):
                    time_limit = (ms / 1000.0) * 0.05
            i += 2

        # ── Check opening book first ──
        book_move = self.book.lookup(self.board)
        if book_move:
            promo = ''
            if book_move.promotion:
                promo_map = {KNIGHT: 'n', BISHOP: 'b', ROOK: 'r', QUEEN: 'q'}
                promo = promo_map.get(book_move.promotion, '')
            print(f"  [Book] Playing book move: {book_move}{promo}")
            print(f"bestmove {book_move}{promo}")
            return

        best_move = self.searcher.find_best_move(
            self.board, max_depth=depth, time_limit=time_limit
        )

        if best_move:
            # Format promotion piece for UCI
            promo = ''
            if best_move.promotion:
                promo_map = {KNIGHT: 'n', BISHOP: 'b', ROOK: 'r', QUEEN: 'q'}
                promo = promo_map.get(best_move.promotion, '')
            print(f"bestmove {best_move}{promo}")
        else:
            print("bestmove 0000")  # No legal moves (game over)


# ─────────────────────────────────────────
# ENTRY POINT
# ─────────────────────────────────────────

if __name__ == "__main__":
    engine = UCIEngine()
    engine.run()
