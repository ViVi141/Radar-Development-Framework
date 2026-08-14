import unittest

from rdf_radar_eccm import EccmDecisionLayer, EccmObservations


def obs(jn_db=-300.0, sidelobe_coupling=0.0, deception_count=0, locked=False):
    return EccmObservations(jn_db, sidelobe_coupling, deception_count, locked)


class TestEccmDecisionLayer(unittest.TestCase):
    def setUp(self):
        self.layer = EccmDecisionLayer()

    def test_no_jam_all_off(self):
        self.assertFalse(self.layer.decide(obs()).any_active())

    def test_sidelobe_jam_enables_slb(self):
        a = self.layer.decide(obs(jn_db=10.0, sidelobe_coupling=0.8))
        self.assertTrue(a.enable_slb)
        self.assertFalse(a.freq_agility)

    def test_mainlobe_jam_enables_freq_agility(self):
        a = self.layer.decide(obs(jn_db=10.0, sidelobe_coupling=0.0))
        self.assertTrue(a.freq_agility)
        self.assertFalse(a.enable_slb)

    def test_deception_enables_prf_agility(self):
        self.assertTrue(self.layer.decide(obs(deception_count=3)).prf_agility)

    def test_locked_jam_enables_burn_through(self):
        a = self.layer.decide(obs(jn_db=10.0, sidelobe_coupling=0.8, locked=True))
        self.assertTrue(a.burn_through)
        self.assertTrue(a.enable_slb)

    def test_jam_hysteresis_sticky(self):
        self.layer.decide(obs(jn_db=10.0, sidelobe_coupling=0.8))
        a = self.layer.decide(obs(jn_db=7.0, sidelobe_coupling=0.8))
        self.assertTrue(a.enable_slb)          # still above off threshold -> sticky
        a = self.layer.decide(obs(jn_db=3.0, sidelobe_coupling=0.8))
        self.assertFalse(a.any_active())       # fell below off -> released

    def test_combined_sidelobe_jam_and_deception(self):
        a = self.layer.decide(obs(jn_db=10.0, sidelobe_coupling=0.8, deception_count=2))
        self.assertTrue(a.enable_slb)
        self.assertTrue(a.prf_agility)

    def test_release_clears_after_reset(self):
        self.layer.decide(obs(jn_db=10.0, sidelobe_coupling=0.8, deception_count=1))
        self.layer.reset()
        self.assertFalse(self.layer.decide(obs()).any_active())

    def test_invalid_hysteresis_rejected(self):
        with self.assertRaises(ValueError):
            EccmDecisionLayer(jn_hysteresis_db=-1.0)


if __name__ == "__main__":
    unittest.main()
