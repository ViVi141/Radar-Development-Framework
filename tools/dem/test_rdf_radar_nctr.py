import unittest

from rdf_radar_nctr import (
    NCTR_FAN,
    NCTR_FIXED,
    NCTR_ROTOR,
    NCTR_UNKNOWN,
    SURF_VEGETATION,
    SURF_WATER,
    classify,
    classify_confidence,
    class_to_short,
    glint_elevation_bias_deg,
    rain_water_clutter_scale,
    track_quality,
)


class TestRadarNctr(unittest.TestCase):
    def test_rotor_from_sideband(self):
        cls = classify(80.0, 0.25, 0.0, 0.0, True, 8.0)
        self.assertEqual(cls, NCTR_ROTOR)
        self.assertEqual(class_to_short(cls), "rotor")
        conf = classify_confidence(cls, True, 8.0)
        self.assertGreater(conf, 0.5)

    def test_fan_from_sideband(self):
        cls = classify(0.0, 0.0, 320.0, 0.07, True, 10.0)
        self.assertEqual(cls, NCTR_FAN)
        self.assertEqual(class_to_short(cls), "fan")

    def test_rotor_beats_fan(self):
        cls = classify(90.0, 0.2, 320.0, 0.07, True, 12.0)
        self.assertEqual(cls, NCTR_ROTOR)

    def test_fixed_body_return(self):
        cls = classify(0.0, 0.0, 0.0, 0.0, False, 6.0)
        self.assertEqual(cls, NCTR_FIXED)
        self.assertEqual(class_to_short(cls), "fixed")
        conf = classify_confidence(cls, False, 6.0)
        self.assertLess(conf, classify_confidence(NCTR_ROTOR, False, 6.0))

    def test_unknown_low_snr(self):
        cls = classify(80.0, 0.25, 0.0, 0.0, False, -1.0)
        self.assertEqual(cls, NCTR_UNKNOWN)
        self.assertAlmostEqual(classify_confidence(cls, False, -1.0), 0.08)

    def test_fan_needs_high_snr_without_sideband(self):
        weak = classify(0.0, 0.0, 320.0, 0.07, False, 7.0)
        self.assertEqual(weak, NCTR_FIXED)
        strong = classify(0.0, 0.0, 320.0, 0.07, False, 8.0)
        self.assertEqual(strong, NCTR_FAN)

    def test_track_quality_coasting_damps(self):
        live = track_quality(4, 2, 12.0, 10.0, 400.0, False)
        coast = track_quality(4, 2, 12.0, 10.0, 400.0, True)
        self.assertAlmostEqual(coast, live * 0.6)
        self.assertGreater(live, coast)

    def test_rain_water_clutter_scale(self):
        self.assertAlmostEqual(rain_water_clutter_scale(0.0), 1.0)
        self.assertAlmostEqual(rain_water_clutter_scale(1.0), 0.45)
        self.assertAlmostEqual(rain_water_clutter_scale(2.0), 0.45)
        self.assertLess(rain_water_clutter_scale(0.5), 1.0)

    def test_glint_water_stronger_than_vegetation(self):
        water = glint_elevation_bias_deg(20.0, SURF_WATER, 1.0, 2000.0)
        veg = glint_elevation_bias_deg(20.0, SURF_VEGETATION, 1.0, 2000.0)
        self.assertLess(water, 0.0)
        self.assertLess(veg, 0.0)
        self.assertLess(water, veg)

    def test_glint_skips_short_range(self):
        self.assertEqual(glint_elevation_bias_deg(20.0, SURF_WATER, 1.0, 10.0), 0.0)


if __name__ == "__main__":
    unittest.main()
