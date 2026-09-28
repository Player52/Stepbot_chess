# validate_bin.py — structural validation of .bin datasets.
# Usage: python validate_bin.py file1.bin [file2.bin ...]

import sys, chess
from nnue_bin import read_records, decode, encode

for path in sys.argv[1:]:
    recs = read_records(path)
    for i, r in enumerate(recs):
        b, s = decode(r)
        # Kings: exactly one per side.
        assert bin(b.kings).count('1') == 2, f'rec {i}: king count'
        assert bin(b.kings & b.occupied_co[chess.WHITE]).count('1') == 1, f'rec {i}: white kings'
        assert bin(b.kings & b.occupied_co[chess.BLACK]).count('1') == 1, f'rec {i}: black kings'
        # Pawns: at most 8 per side, none on back ranks.
        wp = b.pieces(chess.PAWN, chess.WHITE)
        bp = b.pieces(chess.PAWN, chess.BLACK)
        assert bin(wp).count('1') <= 8 and bin(bp).count('1') <= 8, f'rec {i}: pawns'
        assert not (wp | bp) & (chess.BB_RANK_1 | chess.BB_RANK_8), f'rec {i}: pawn on back rank'
        assert len(b.piece_map()) <= 32, f'rec {i}: piece count'
        assert -29000 <= s <= 29000, f'rec {i}: score {s}'
        # Self-consistency: re-encoding reproduces the record exactly.
        assert encode(b, s) == r, f'rec {i}: re-encode mismatch'
    print(f'{path}: {len(recs):,} records OK')