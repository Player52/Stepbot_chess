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
    preferred = 'stepbot.exe' if os.name == 'nt' else 'stepbot'
    fallback = 'stepbot' if os.name == 'nt' else 'stepbot.exe'

    for name in (preferred, fallback):
        path = os.path.join(SCR, name)
        if os.path.isfile(path):
            return path

    raise FileNotFoundError(
        f"Could not find Stepbot engine in {SCR}. "
        "Expected 'stepbot.exe' (Windows) or 'stepbot' (Linux/macOS)."
    )
