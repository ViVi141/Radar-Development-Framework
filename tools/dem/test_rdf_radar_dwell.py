import unittest

from rdf_radar_dwell import DwellKind, DwellTask, DwellScheduler


def task(kind, task_id, period_s, dwell_ms, deadline_s):
    return DwellTask(kind, task_id, period_s, dwell_ms, deadline_s)


class TestDwellScheduler(unittest.TestCase):
    def test_fire_control_dominates_track_regardless_of_deadline(self):
        fc = task(DwellKind.FIRE_CONTROL, 1, 0.1, 10.0, 10.0)  # later deadline
        tr = task(DwellKind.TRACK, 2, 1.0, 10.0, 1.0)          # earlier deadline
        plan = DwellScheduler(20.0).plan([tr, fc], now_s=0.0)
        self.assertEqual(plan.scheduled_ids, [1, 2])

    def test_track_dominates_search(self):
        search = task(DwellKind.SEARCH, 1, 1.0, 5.0, 0.0)
        track = task(DwellKind.TRACK, 2, 1.0, 5.0, 2.0)  # later deadline
        plan = DwellScheduler(10.0).plan([search, track], now_s=0.0)
        self.assertEqual(plan.scheduled_ids, [2, 1])

    def test_edf_within_class(self):
        late = task(DwellKind.TRACK, 1, 1.0, 5.0, 5.0)
        early = task(DwellKind.TRACK, 2, 1.0, 5.0, 1.0)
        plan = DwellScheduler(20.0).plan([late, early], now_s=0.0)
        self.assertEqual(plan.scheduled_ids, [2, 1])

    def test_budget_is_hard_cap(self):
        a = task(DwellKind.TRACK, 1, 1.0, 6.0, 0.0)
        b = task(DwellKind.TRACK, 2, 1.0, 6.0, 0.0)
        plan = DwellScheduler(10.0).plan([a, b], now_s=0.0)
        self.assertEqual(plan.scheduled_ids, [1])
        self.assertLessEqual(plan.spent_ms, 10.0 + 1e-9)

    def test_late_detection_when_skipped_past_deadline(self):
        a = task(DwellKind.TRACK, 1, 1.0, 6.0, 1.0)  # past at now=2.0
        b = task(DwellKind.TRACK, 2, 1.0, 6.0, 0.0)
        plan = DwellScheduler(10.0).plan([a, b], now_s=2.0)
        self.assertEqual(plan.scheduled_ids, [2])
        self.assertIn(1, plan.late_ids)

    def test_advance_pushes_deadline_by_period(self):
        tr = task(DwellKind.TRACK, 1, 1.0, 10.0, 0.0)
        sched = DwellScheduler(10.0)
        sched.advance(tr, 0.0)
        self.assertEqual(tr.deadline_s, 1.0)
        sched.advance(tr, 0.5)
        self.assertEqual(tr.deadline_s, 1.5)
        self.assertEqual(tr.last_scheduled_s, 0.5)

    def test_one_shot_task_never_renews(self):
        one = task(DwellKind.SEARCH, 1, 0.0, 5.0, 0.0)  # period 0 = one-shot
        sched = DwellScheduler(10.0)
        sched.advance(one, 0.0)
        self.assertEqual(one.deadline_s, float("inf"))


if __name__ == "__main__":
    unittest.main()
