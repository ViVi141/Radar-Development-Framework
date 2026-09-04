#!/usr/bin/env python3
"""Pack HwCalib + SitePathLut into RadarData/ for workshop (.conf + .json + .meta).

Sources:
  tools/dem/calib/prf_clutter_shorad.json  -> scalars for HwCalib
  tools/dem/calib/SitePathLut.json         -> SitePathLut (identity synthetic OK)

Example:
  python tools/dem/rdf_pack_radar_calib.py
"""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_ROOT = _HERE.parent.parent
_CALIB = _HERE / "calib"
_OUT = _ROOT / "RadarData"

HW_CONF_GUID = "D4E81F2A7C903E01"
HW_JSON_GUID = "D4E81F2A7C903E02"
SITE_CONF_GUID = "D4E81F2A7C903E11"
SITE_JSON_GUID = "D4E81F2A7C903E12"


def write_meta(path: Path, guid: str, rel: str, conf: bool) -> None:
    kind = "CONFResourceClass" if conf else "JSONResourceClass"
    text = (
        "MetaFileClass {\n"
        f' Name "{{{guid}}}{rel}"\n'
        " Configurations {\n"
        f"  {kind} PC {{\n"
        "  }\n"
        f"  {kind} XBOX_ONE : PC {{\n"
        "  }\n"
        f"  {kind} XBOX_SERIES : PC {{\n"
        "  }\n"
        f"  {kind} PS4 : PC {{\n"
        "  }\n"
        f"  {kind} PS5 : PC {{\n"
        "  }\n"
        f"  {kind} HEADLESS : PC {{\n"
        "  }\n"
        " }\n"
        "}\n"
    )
    path.write_text(text, encoding="utf-8", newline="\n")


def pack_hw_calib() -> None:
    src = _CALIB / "prf_clutter_shorad.json"
    raw = json.loads(src.read_text(encoding="utf-8"))
    sigma = float(raw.get("clutter_sigma_vr_m_s") or 0.0)
    if sigma <= 0.0:
        psd = raw.get("clutter_psd") or {}
        sigma = float(psd.get("sigma_vr_m_s") or 0.5)

    slim = {
        "schema": "RDF_HW_CALIB_V1",
        "preset": str(raw.get("preset") or "shorad"),
        "prf_hz": float(raw.get("prf_hz") or 4000.0),
        "mti_clutter_floor": float(raw.get("mti_clutter_floor") or 0.0001),
        "mtd_clutter_leakage": float(raw.get("mtd_clutter_leakage") or 1e-9),
        "doppler_bin_count": int(raw.get("doppler_bin_count") or 16),
        "mti_mode": str(raw.get("mti_mode") or "mtd_bank"),
        "clutter_sigma_vr_m_s": sigma,
        "prf_stagger_ratio": float(raw.get("prf_stagger_ratio") or 1.2),
        "notes": "Packaged SHORAD bake-back; profile HwCalib.json overrides",
    }

    json_path = _OUT / "HwCalib.json"
    json_path.write_text(json.dumps(slim, indent=2) + "\n", encoding="utf-8", newline="\n")
    write_meta(_OUT / "HwCalib.json.meta", HW_JSON_GUID, "RadarData/HwCalib.json", False)

    conf_path = _OUT / "HwCalib.conf"
    conf_path.write_text(
        "RDF_RadarHwCalibConf {\n"
        f' m_sSchema "{slim["schema"]}"\n'
        f' m_sPreset "{slim["preset"]}"\n'
        f' m_fPrfHz {slim["prf_hz"]}\n'
        f' m_fMtiClutterFloor {slim["mti_clutter_floor"]}\n'
        f' m_fMtdClutterLeakage {slim["mtd_clutter_leakage"]}\n'
        f' m_iDopplerBinCount {slim["doppler_bin_count"]}\n'
        f' m_sMtiMode "{slim["mti_mode"]}"\n'
        f' m_fClutterSigmaVrMs {slim["clutter_sigma_vr_m_s"]}\n'
        f' m_fPrfStaggerRatio {slim["prf_stagger_ratio"]}\n'
        "}\n",
        encoding="utf-8",
        newline="\n",
    )
    write_meta(_OUT / "HwCalib.conf.meta", HW_CONF_GUID, "RadarData/HwCalib.conf", True)
    print("wrote", json_path)
    print("wrote", conf_path)


def pack_site_path() -> None:
    src = _CALIB / "SitePathLut.json"
    raw = json.loads(src.read_text(encoding="utf-8"))
    factors = raw.get("factors") or []
    az = int(raw.get("az_count") or 0)
    rng = int(raw.get("range_count") or 0)
    expect = az * rng
    if expect < 16 or len(factors) < expect:
        raise SystemExit("SitePathLut factors too short")

    # Keep packaged JSON identical to calib (workshop + FileIO fallback).
    dst_json = _OUT / "SitePathLut.json"
    shutil.copy2(src, dst_json)
    write_meta(_OUT / "SitePathLut.json.meta", SITE_JSON_GUID, "RadarData/SitePathLut.json", False)

    lines = [
        "RDF_RadarSitePathLutConf {",
        f' m_sSchema "{raw.get("schema") or "RDF_SITE_PATH_LUT_V1"}"',
        f' m_sWorld "{raw.get("world") or "synthetic"}"',
        f' m_fOriginX {float(raw.get("origin_x") or 0.0)}',
        f' m_fOriginY {float(raw.get("origin_y") or 25.0)}',
        f' m_fOriginZ {float(raw.get("origin_z") or 0.0)}',
        f' m_fMaxRangeM {float(raw.get("max_range_m") or 4000.0)}',
        f" m_iAzCount {az}",
        f" m_iRangeCount {rng}",
        f' m_fWavelengthM {float(raw.get("wavelength_m") or 0.032)}',
        " m_aFactors {",
    ]
    for i in range(expect):
        lines.append(f"  {float(factors[i])}")
    lines.append(" }")
    lines.append("}")
    lines.append("")

    conf_path = _OUT / "SitePathLut.conf"
    conf_path.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    write_meta(_OUT / "SitePathLut.conf.meta", SITE_CONF_GUID, "RadarData/SitePathLut.conf", True)
    print("wrote", dst_json)
    print("wrote", conf_path)


def main() -> int:
    parser = argparse.ArgumentParser(description="Pack HwCalib + SitePathLut into RadarData/")
    parser.parse_args()
    _OUT.mkdir(parents=True, exist_ok=True)
    pack_hw_calib()
    pack_site_path()
    print("done. ResourceNames:")
    print(f"  {{{HW_CONF_GUID}}}RadarData/HwCalib.conf")
    print(f"  {{{SITE_CONF_GUID}}}RadarData/SitePathLut.conf")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
