"""Mass-battle offline simulation package."""
from __future__ import annotations

from mass_battle._impl import *  # noqa: F401,F403
from mass_battle.cli import main

__all__ = ["main"]
