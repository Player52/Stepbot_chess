import csv, sys, chess
from nnue_bin import encode, decode, HEADER, MAGIC, VERSION

src, dst = sys.argv[1], sys.argv[2]
records = []
with open(src, newline='', encoding='utf-8') as f:
    for row in csv.reader(f):
        if len(row) < 2 or row[0] == 'fen':
            continue
        fen, score = row[0], int(row[1])
        board = chess.Board(fen)
        rec = encode(board, score)
        # Round-trip validation on every record — this is the corruption check.
        b2, s2 = decode(rec)
        b2.fullmove_number = board.fullmove_number
        assert b2.fen() == board.fen(), f'round-trip mismatch: {fen}'
        assert s2 == score
        records.append(rec)
with open(dst, 'wb') as f:
    f.write(HEADER.pack(MAGIC, VERSION, 0, len(records)))
    f.write(b''.join(records))
print(f'{len(records)} records -> {dst} ({len(records) * 40:,} bytes)')