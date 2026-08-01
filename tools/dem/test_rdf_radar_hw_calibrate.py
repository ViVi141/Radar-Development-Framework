#!/usr/bin/env python3
"""Unit tests for offline hardware calib (clutter PSD / multi-PRF)."""

from __future__ import annotations

import json
import os
import tempfile
import unittest

from rdf_radar_hw_calibrate import (
    SCHEMA,
    apply_calib_to_hardware,
    build_calib,
    gaussian_clutter_psd,
    suggest_mtd_leakage,
)
from rdf_radar_physics import get_preset
import numpy as np


class TestHwCalibrate(unittest.TestCase):
    def test_psd_peaks_at_dc(self):
        fd = np.linspace(-2000.0, 2000.0, 401)
        psd = gaussian_clutter_psd(fd, sigma_vr_m_s=0.5, wavelength_m=0.03)
        self.assertAlmostEqual(float(np.max(psd)), 1.0, places=6)
        self.assertEqual(int(np.argmax(psd)), 200)

    def test_leakage_positive_small(self):
        fd = np.linspace(-1.0, 1.0, 101)
        psd = np.exp(-0.5 * (fd / 0.05) ** 2)
        psd = psd / np.max(psd)
        leak = suggest_mtd_leakage(psd, fd)
        self.assertGreater(leak, 0.0)
        self.assertLess(leak, 0.5)

    def test_build_calib_schema(self):
        hw = get_preset("shorad")
        hw.mti_mode = "mtd_bank"
        hw.prf_set_hz = [4000.0, 4800.0]
        calib = build_calib(hw, "shorad", 0.5, 65)
        self.assertEqual(calib["schema"], SCHEMA)
        self.assertIn("clutter_psd", calib)
        self.assertIn("ambiguity", calib)
        self.assertGreater(calib["ambiguity"]["coverage_score"], 0.0)
        apply_calib_to_hardware(calib, hw)
        self.assertEqual(hw.mti_mode, "mtd_bank")

    def test_write_json_roundtrip(self):
        hw = get_preset("shorad")
        calib = build_calib(hw, "shorad", 0.4, 33)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "calib.json")
            with open(path, "w", encoding="utf-8") as f:
                json.dump(calib, f)
            with open(path, "r", encoding="utf-8") as f:
                loaded = json.load(f)
        self.assertEqual(loaded["schema"], SCHEMA)


if __name__ == "__main__":
    unittest.main()
