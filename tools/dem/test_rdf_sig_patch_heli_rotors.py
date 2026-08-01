#!/usr/bin/env python3
"""Unit tests for explicit heli rotor conf patcher."""

from __future__ import annotations

import unittest

from rdf_sig_patch_heli_rotors import (
    family_for_key,
    is_airframe_heli,
    patch_conf_text,
)


SAMPLE = """RDF_RadarSignatureTableConf {
 m_iVersion 1
 m_aEntries {
  RDF_RadarSignatureEntryConf "{C8A3F15E00000001}" {
   m_sKey "{GUID}Prefabs/Vehicles/Helicopters/UH1H/UH1H.et"
   m_fSizeX 2.8
   m_fSizeY 5.0
   m_fSizeZ 13.0
   m_fCharLengthM 13.0
   m_fMeanRcsM2 16.0
   m_iSwerling 1
   m_iTypeHint 0
  }
  RDF_RadarSignatureEntryConf "{C8A3F15E00000002}" {
   m_sKey "{GUID}Prefabs/Vehicles/Helicopters/Mi8MT/Mi8MT_base.et"
   m_fSizeX 3.0
   m_fSizeY 5.5
   m_fSizeZ 18.0
   m_fCharLengthM 18.0
   m_fMeanRcsM2 20.0
   m_iSwerling 1
   m_iTypeHint 0
  }
  RDF_RadarSignatureEntryConf "{C8A3F15E00000003}" {
   m_sKey "{GUID}Prefabs/Vehicles/Helicopters/Mi8MT/VehParts/Rotor/VehPart_Mi8_rotor_main.et"
   m_fSizeX 0.2
   m_fSizeY 0.2
   m_fSizeZ 1.0
   m_fCharLengthM 1.0
   m_fMeanRcsM2 0.1
   m_iSwerling 0
   m_iTypeHint 0
  }
 }
}
"""


class TestHeliRotorPatch(unittest.TestCase):
    def test_family_table(self) -> None:
        self.assertEqual(family_for_key(".../Mi8MT/x.et")["blades"], 5)
        self.assertEqual(family_for_key(".../UH1H/x.et")["blades"], 2)

    def test_skip_parts(self) -> None:
        self.assertFalse(
            is_airframe_heli(
                "Prefabs/Vehicles/Helicopters/Mi8MT/VehParts/Rotor/x.et"
            )
        )
        self.assertTrue(
            is_airframe_heli("Prefabs/Vehicles/Helicopters/UH1H/UH1H.et")
        )

    def test_patch_inserts_rotor_columns(self) -> None:
        out, patched, skipped = patch_conf_text(SAMPLE)
        self.assertEqual(patched, 2)
        self.assertEqual(skipped, 0)
        self.assertIn("m_fRotorTipSpeedMs 220.0", out)
        self.assertIn("m_iBladeCount 2", out)
        self.assertIn("m_fRotorTipSpeedMs 230.0", out)
        self.assertIn("m_iBladeCount 5", out)
        # Rotor part entry must remain without rotor tip columns.
        part_block = out.split("VehPart_Mi8_rotor_main.et")[1].split("}")[0]
        self.assertNotIn("m_fRotorTipSpeedMs", part_block)

    def test_idempotent(self) -> None:
        once, p1, _ = patch_conf_text(SAMPLE)
        twice, p2, skipped = patch_conf_text(once)
        self.assertEqual(p1, 2)
        self.assertEqual(p2, 0)
        self.assertEqual(skipped, 2)
        self.assertEqual(once, twice)


if __name__ == "__main__":
    unittest.main()
