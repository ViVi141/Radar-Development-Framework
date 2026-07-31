#!/usr/bin/env python3
"""Signature pack tools preserve optional rotor / micro-Doppler columns."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from rdf_sig_pack_conf import pack_csv as pack_conf
from rdf_sig_pack_json import pack_csv as pack_json


class TestSigPackRotorColumns(unittest.TestCase):
    def _write_csv(self, path: Path, with_rotor: bool) -> None:
        lines = [
            "RDF_RADAR_SIG_V2",
            "key,size_x_m,size_y_m,size_z_m,char_length_m,mean_rcs_m2,swerling,type_hint"
            ",rotor_tip_ms,blade_count,rotor_rcs_frac,hub_width_ms",
        ]
        if with_rotor:
            lines.append(
                '"{GUID}Prefabs/Vehicles/Helicopters/UH1H/UH1H.et",'
                "2.8,5.0,13.0,13.0,16.0,1,0,220.0,2,0.35,40.0"
            )
        else:
            lines.append(
                '"{GUID}Prefabs/Vehicles/Wheeled/Truck.et",'
                "2.0,2.0,6.0,6.0,8.0,1,0"
            )
            # Legacy 8-column row still packs.
            lines[1] = (
                "key,size_x_m,size_y_m,size_z_m,char_length_m,"
                "mean_rcs_m2,swerling,type_hint"
            )
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    def test_conf_keeps_rotor_fields(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "sig.csv"
            out = Path(tmp) / "sig.conf"
            self._write_csv(src, with_rotor=True)
            pack_conf(src, out)
            text = out.read_text(encoding="utf-8")
            self.assertIn("m_fRotorTipSpeedMs 220.0", text)
            self.assertIn("m_iBladeCount 2", text)
            self.assertIn("m_fRotorRcsFraction 0.35", text)
            self.assertIn("m_fHubWidthMs 40.0", text)

    def test_conf_omits_rotor_for_legacy_rows(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "sig.csv"
            out = Path(tmp) / "sig.conf"
            self._write_csv(src, with_rotor=False)
            pack_conf(src, out)
            text = out.read_text(encoding="utf-8")
            self.assertNotIn("m_fRotorTipSpeedMs", text)
            self.assertIn("Truck.et", text)

    def test_json_keeps_rotor_fields(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "sig.csv"
            out = Path(tmp) / "sig.json"
            self._write_csv(src, with_rotor=True)
            pack_json(src, out)
            text = out.read_text(encoding="utf-8")
            self.assertIn('"rotor_tip_ms": 220.0', text)
            self.assertIn('"blade_count": 2', text)


if __name__ == "__main__":
    unittest.main()
