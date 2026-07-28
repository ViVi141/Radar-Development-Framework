#!/usr/bin/env python3
"""Calibratable surface_class → σ⁰ model for RDF DEM radar prototypes.

Constant-gamma form at reference grazing θ_ref:

  σ⁰(θ) = σ⁰_ref * (sin(θ) / sin(θ_ref))^k

Defaults are X-band engineering tables for tactical AD / artillery radar.
Override via Sigma0Table / JSON calibration without rebaking DEM.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

# Must match ERDF_DemSurfaceClass in RDF_DemMaterialTable.c
SURF_UNKNOWN = 0
SURF_WATER = 1
SURF_VEGETATION = 2
SURF_SOIL = 3
SURF_SAND = 4
SURF_GRAVEL = 5
SURF_ASPHALT = 6
SURF_HARD = 7
SURF_WOOD = 8
SURF_METAL = 9
SURF_SNOW_ICE = 10
SURF_FABRIC = 11
SURF_COUNT = 12

SURF_NAMES = [
    "unknown",
    "water",
    "vegetation",
    "soil",
    "sand",
    "gravel",
    "asphalt",
    "hard",
    "wood",
    "metal",
    "snow_ice",
    "fabric",
]

# σ⁰_ref [dB] at 30° grazing, keyed by radar band tag.
_BAND_SIGMA0_DB = {
    "X": {
        "unknown": -18.0,
        "water": -22.0,
        "vegetation": -14.0,
        "soil": -18.0,
        "sand": -20.0,
        "gravel": -16.0,
        "asphalt": -12.0,
        "hard": -10.0,
        "wood": -18.0,
        "metal": -5.0,
        "snow_ice": -20.0,
        "fabric": -24.0,
    },
    "C": {
        "unknown": -19.0,
        "water": -24.0,
        "vegetation": -15.0,
        "soil": -19.0,
        "sand": -21.0,
        "gravel": -17.0,
        "asphalt": -13.0,
        "hard": -11.0,
        "wood": -19.0,
        "metal": -5.5,
        "snow_ice": -21.0,
        "fabric": -25.0,
    },
    "S": {
        "unknown": -20.0,
        "water": -26.0,
        "vegetation": -16.0,
        "soil": -20.0,
        "sand": -22.0,
        "gravel": -18.0,
        "asphalt": -14.0,
        "hard": -12.0,
        "wood": -20.0,
        "metal": -6.0,
        "snow_ice": -22.0,
        "fabric": -26.0,
    },
    "L": {
        "unknown": -22.0,
        "water": -28.0,
        "vegetation": -12.0,
        "soil": -22.0,
        "sand": -24.0,
        "gravel": -20.0,
        "asphalt": -16.0,
        "hard": -14.0,
        "wood": -22.0,
        "metal": -7.0,
        "snow_ice": -18.0,
        "fabric": -28.0,
    },
    # VHF (P-18 class): volume / resonance clutter — higher veg, lower smooth surfaces.
    "VHF": {
        "unknown": -24.0,
        "water": -30.0,
        "vegetation": -10.0,
        "soil": -24.0,
        "sand": -26.0,
        "gravel": -22.0,
        "asphalt": -18.0,
        "hard": -16.0,
        "wood": -14.0,
        "metal": -8.0,
        "snow_ice": -20.0,
        "fabric": -28.0,
    },
}

_DEFAULT_EXPONENT = {
    "unknown": 1.0,
    "water": 1.4,
    "vegetation": 0.8,
    "soil": 1.0,
    "sand": 1.1,
    "gravel": 1.0,
    "asphalt": 1.0,
    "hard": 1.0,
    "wood": 1.0,
    "metal": 0.5,
    "snow_ice": 1.0,
    "fabric": 1.0,
}

# Sea-state offset applied to water σ⁰_ref [dB] (Douglas / Beaufort-ish).
_SEA_STATE_WATER_DB = {
    0: -8.0,  # calm
    1: -4.0,
    2: -1.0,
    3: 0.0,  # reference (moderate)
    4: 2.0,
    5: 4.0,
    6: 6.0,
}

THETA_REF_RAD = math.radians(30.0)
MIN_GRAZING_RAD = math.radians(0.5)
MAX_SIGMA0 = 10.0


def _db_to_lin(db: float) -> float:
    return 10.0 ** (db / 10.0)


def _lin_to_db(linear: float) -> float:
    if linear <= 1e-12:
        return -120.0
    return 10.0 * math.log10(linear)


@dataclass
class Sigma0Table:
    """Per-class σ⁰_ref (linear) and grazing exponent. Calibratable."""

    band: str = "X"
    sea_state: int = 3
    theta_ref_rad: float = THETA_REF_RAD
    min_grazing_rad: float = MIN_GRAZING_RAD
    max_sigma0: float = MAX_SIGMA0
    sigma0_ref_linear: np.ndarray = field(default_factory=lambda: np.zeros(SURF_COUNT))
    exponent: np.ndarray = field(default_factory=lambda: np.ones(SURF_COUNT))
    source: str = "builtin"

    @staticmethod
    def builtin(band: str = "X", sea_state: int = 3) -> "Sigma0Table":
        band_key = band.upper()
        if band_key not in _BAND_SIGMA0_DB:
            band_key = "X"
        table_db = _BAND_SIGMA0_DB[band_key]
        sea = int(sea_state)
        if sea < 0:
            sea = 0
        if sea > 6:
            sea = 6
        water_offset = _SEA_STATE_WATER_DB[sea]

        refs = np.zeros(SURF_COUNT, dtype=np.float64)
        exps = np.ones(SURF_COUNT, dtype=np.float64)
        for i, name in enumerate(SURF_NAMES):
            db = float(table_db[name])
            if i == SURF_WATER:
                db = db + water_offset
            refs[i] = _db_to_lin(db)
            exps[i] = float(_DEFAULT_EXPONENT[name])

        return Sigma0Table(
            band=band_key,
            sea_state=sea,
            sigma0_ref_linear=refs,
            exponent=exps,
            source=f"builtin:{band_key}:ss{sea}",
        )

    @staticmethod
    def from_json(path: str | Path) -> "Sigma0Table":
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)

        # RDF_SURF_TABLE_V1 (game + workshop SurfaceTable.json)
        if str(data.get("magic", "")) == "RDF_SURF_TABLE_V1":
            band = str(data.get("band", "X")).upper()
            sea_state = int(data.get("sea_state", 3))
            base = Sigma0Table.builtin(band, sea_state)
            if "theta_ref_deg" in data:
                base.theta_ref_rad = math.radians(float(data["theta_ref_deg"]))
            for entry in data.get("entries", []):
                if not isinstance(entry, dict):
                    continue
                sid = int(entry.get("id", -1))
                name = str(entry.get("name", ""))
                if sid < 0 or sid >= SURF_COUNT:
                    if name in SURF_NAMES:
                        sid = SURF_NAMES.index(name)
                    else:
                        continue
                if "sigma0_ref_db" in entry:
                    base.sigma0_ref_linear[sid] = _db_to_lin(
                        float(entry["sigma0_ref_db"])
                    )
                if "gamma_k" in entry:
                    base.exponent[sid] = float(entry["gamma_k"])
                scale = float(entry.get("clutter_scale", 1.0))
                if scale > 0.0 and abs(scale - 1.0) > 1e-9:
                    base.sigma0_ref_linear[sid] *= scale
            base.source = f"surf_table:{path}"
            return base

        # Legacy calibration dict (sigma0_ref_db / exponent maps)
        band = str(data.get("band", "X")).upper()
        sea_state = int(data.get("sea_state", 3))
        base = Sigma0Table.builtin(band, sea_state)
        overrides = data.get("sigma0_ref_db", {})
        exponents = data.get("exponent", {})
        for i, name in enumerate(SURF_NAMES):
            if name in overrides:
                base.sigma0_ref_linear[i] = _db_to_lin(float(overrides[name]))
            if name in exponents:
                base.exponent[i] = float(exponents[name])
        if "theta_ref_deg" in data:
            base.theta_ref_rad = math.radians(float(data["theta_ref_deg"]))
        base.source = f"json:{path}"
        return base

    @staticmethod
    def from_surface_table_default() -> "Sigma0Table":
        """Load addon RadarData/SurfaceTable.json when present."""
        root = Path(__file__).resolve().parents[2]
        path = root / "RadarData" / "SurfaceTable.json"
        if path.is_file():
            return Sigma0Table.from_json(path)
        return Sigma0Table.builtin("X", 3)
    def to_calibration_dict(self) -> dict:
        refs_db = {}
        exps = {}
        for i, name in enumerate(SURF_NAMES):
            refs_db[name] = _lin_to_db(float(self.sigma0_ref_linear[i]))
            exps[name] = float(self.exponent[i])
        return {
            "band": self.band,
            "sea_state": self.sea_state,
            "theta_ref_deg": math.degrees(self.theta_ref_rad),
            "sigma0_ref_db": refs_db,
            "exponent": exps,
            "notes": "Edit sigma0_ref_db / exponent then pass --sigma0-calib <file>",
        }

    def write_calibration_template(self, path: str | Path) -> None:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(self.to_calibration_dict(), handle, indent=2)
            handle.write("\n")


_DEFAULT_TABLE = Sigma0Table.builtin("X", 3)


def set_default_table(table: Sigma0Table) -> None:
    global _DEFAULT_TABLE
    _DEFAULT_TABLE = table


def get_default_table() -> Sigma0Table:
    return _DEFAULT_TABLE


def sigma0_ref_linear(surface_class: int, table: Sigma0Table | None = None) -> float:
    if table is None:
        table = _DEFAULT_TABLE
    if surface_class < 0 or surface_class >= SURF_COUNT:
        return float(table.sigma0_ref_linear[SURF_UNKNOWN])
    return float(table.sigma0_ref_linear[surface_class])


def sigma0_linear(
    surface_class: int,
    grazing_rad: float,
    table: Sigma0Table | None = None,
) -> float:
    if table is None:
        table = _DEFAULT_TABLE
    theta = grazing_rad
    if theta < table.min_grazing_rad:
        theta = table.min_grazing_rad
    if theta > math.pi * 0.5:
        theta = math.pi * 0.5

    ref = sigma0_ref_linear(surface_class, table)
    if surface_class < 0 or surface_class >= SURF_COUNT:
        exponent = 1.0
    else:
        exponent = float(table.exponent[surface_class])

    ratio = math.sin(theta) / math.sin(table.theta_ref_rad)
    if ratio < 1e-6:
        ratio = 1e-6
    value = ref * (ratio**exponent)
    if value > table.max_sigma0:
        value = table.max_sigma0
    return value


def sigma0_map(
    surface: np.ndarray,
    grazing_rad: np.ndarray,
    table: Sigma0Table | None = None,
) -> np.ndarray:
    if table is None:
        table = _DEFAULT_TABLE
    surface_i = np.asarray(surface, dtype=np.int32)
    grazing = np.clip(
        np.asarray(grazing_rad, dtype=np.float64),
        table.min_grazing_rad,
        math.pi * 0.5,
    )
    classes = np.clip(surface_i, 0, SURF_COUNT - 1)
    ref = table.sigma0_ref_linear[classes]
    exponent = table.exponent[classes]
    ratio = np.sin(grazing) / math.sin(table.theta_ref_rad)
    ratio = np.maximum(ratio, 1e-6)
    out = ref * np.power(ratio, exponent)
    return np.minimum(out, table.max_sigma0).astype(np.float64)


def sigma0_to_db(sigma0: float) -> float:
    return _lin_to_db(sigma0)


def canopy_sigma0_linear(
    grazing_rad: float,
    table: Sigma0Table | None = None,
) -> float:
    return sigma0_linear(SURF_VEGETATION, grazing_rad, table)
