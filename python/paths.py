# paths.py
# Shared project paths for Python tools.

import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCR = os.path.join(ROOT, 'scr')
SELF_PLAY = os.path.join(ROOT, 'Self_play')
TRAINING_DATA = os.path.join(ROOT, 'Training_Data')
OPENING_BOOK = os.path.join(ROOT, 'opening_book.json')


def engine_path():
    """Return the path to the compiled C++ engine binary."""
    for name in ('stepbot.exe', 'stepbot'):
        path = os.path.join(SCR, name)
        if os.path.exists(path):
            return path
    return os.path.join(SCR, 'stepbot.exe')
