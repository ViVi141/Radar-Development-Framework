"""Mass-battle offline simulation package.

Thematic modules under this package are **facade re-exports**; the full
implementation remains in ``mass_battle._impl``. See ``README.md``.

``__init__`` stays import-light so ``unittest discover`` can recurse into
this package without requiring matplotlib (only ``_impl`` / CLI need it).
"""
from __future__ import annotations

import importlib
from typing import Any

__all__ = ["main"]


def __getattr__(name: str) -> Any:
    if name == "main":
        from mass_battle.cli import main as _main

        return _main
    # unittest discover probes packages for load_tests via getattr — must not
    # pull in _impl (matplotlib) during discovery.
    if name == "load_tests" or name.startswith("_"):
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    impl = importlib.import_module("mass_battle._impl")
    if hasattr(impl, name):
        return getattr(impl, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
