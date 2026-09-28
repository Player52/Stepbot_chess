# nnue_bin.py — Stepbot NNUE training data binary format, v1
#
#   Header (16 bytes): b'SBNN' | uint16 version | uint16 flags | uint64 record count
#   Record  (40 bytes), little-endian:
#     0-31  : board    64 squares × 4 bits (sq 0 = a1; byte sq>>1, low nibble = even sq)
#             0 empty; 1..6 white PNBRQK; 9..14 black pnbrqk
#     32    : stm      0 = white to move, 1 = black
#     33    : castling bit0=K bit1=Q bit2=k bit3=q
#     34    : ep       0..63 or 255 = none (only if a legal e.p. capture exists,
#                                             matching chess.Board.fen() semantics)
#     35    : halfmove clamped 0..100 (fullmove number intentionally NOT stored)
#     36-37 : reserved
#     38-39 : score    int16, centipawns, FROM THE SIDE-TO-MOVE'S PERSPECTIVE.
#                     ±29000 = mate sentinel. DO NOT change this convention silently.

import os
import struct
import chess

MAGIC       = b'SBNN'
VERSION     = 1
HEADER      = struct.Struct('<4sHHQ')          # 16 bytes
REC         = struct.Struct('<32sBBBBBBh')     # exactly 40 bytes — no padding needed
RECORD_SIZE = REC.size                         # 40

PIECE_NIBBLE = {'P': 1, 'N': 2, 'B': 3, 'R': 4, 'Q': 5, 'K': 6,
                'p': 9, 'n': 10, 'b': 11, 'r': 12, 'q': 13, 'k': 14}
NIBBLE_PIECE = {v: k for k, v in PIECE_NIBBLE.items()}
CASTLE_BITS = {'K': 1, 'Q': 2, 'k': 4, 'q': 8}


def encode(board: chess.Board, score: int) -> bytes:
    occ = bytearray(REC.size - 8)              # 32 board bytes
    for sq, pc in board.piece_map().items():
        nib = PIECE_NIBBLE[pc.symbol()]
        occ[sq >> 1] |= nib if not (sq & 1) else nib << 4
        cast = sum(CASTLE_BITS.get(c, 0) for c in board.castling_xfen())
    ep = 255
    if board.ep_square is not None and board.has_legal_en_passant():
        ep = board.ep_square
    return REC.pack(bytes(occ), board.turn == chess.BLACK, cast, ep,
                    min(board.halfmove_clock, 100), 0, 0, score)


def decode(data: bytes):
    board_b, stm, cast, ep, hm, _, _, score = REC.unpack(data[:REC.size])
    board = chess.Board(None)
    for sq in range(64):
        nib = (board_b[sq >> 1] >> (4 if sq & 1 else 0)) & 0xF
        if nib:
            board.set_piece_at(sq, chess.Piece.from_symbol(NIBBLE_PIECE[nib]))
    board.turn = chess.BLACK if stm else chess.WHITE
    board.set_castling_fen(''.join(c for c, b in zip('KQkq', (1, 2, 4, 8)) if cast & b))
    if ep != 255:
        board.ep_square = ep
    board.halfmove_clock = hm
    return board, score


def read_records(path):
    """Read a .bin file, returning a list of 40-byte record blobs.
    Tolerates a missing header (treats the file as raw records)."""
    if not path or not os.path.exists(path):
        return []
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) >= HEADER.size and data[:4] == MAGIC:
        data = data[HEADER.size:]
    extra = len(data) % RECORD_SIZE
    if extra:
        print(f'  Warning: {path}: {extra} trailing bytes ignored')
    n = len(data) - extra
    return [data[i:i + RECORD_SIZE] for i in range(0, n, RECORD_SIZE)]