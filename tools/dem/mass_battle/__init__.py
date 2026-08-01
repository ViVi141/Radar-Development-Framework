"""Mass-battle offline simulation package.

Thematic modules under this package are **facade re-exports**; the full
implementation remains in ``mass_battle._impl``. See ``README.md``.
"""
from __future__ import annotations

from mass_battle._impl import *  # noqa: F401,F403
from mass_battle.cli import main

__all__ = ["main"]
