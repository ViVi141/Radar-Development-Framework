#!/usr/bin/env python3
"""Large-scale air-defense + artillery (WLR) radar battle simulation.

Implementation lives in mass_battle._impl; this module remains the CLI entry
and compatibility import surface for shellfire_offline.
"""

from __future__ import annotations

from mass_battle._impl import *  # noqa: F401,F403
# Underscore names are not re-exported by import *; shellfire_offline needs these.
from mass_battle._impl import (  # noqa: F401
    _default_sig_path,
    _render_wlr_fixes,
)
from mass_battle.cli import main

if __name__ == "__main__":
    main()
