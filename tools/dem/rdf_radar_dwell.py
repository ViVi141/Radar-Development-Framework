"""Phased-array dwell / resource management (offline model; TODO §9 S1).

A phased-array radar shares a finite beam-time budget per scheduling interval
across competing dwell tasks:

  * SEARCH       sweep a sector to discover new targets
  * TRACK        revisit a confirmed track to keep it alive
  * FIRE_CONTROL high-rate update of the locked target (guidance)

Scheduling rule (deterministic; the source of truth for the Enforce port):

  1. Class priority, hard dominance: FIRE_CONTROL > TRACK > SEARCH.
  2. Within a class: earliest deadline first (EDF).
  3. Hard budget cap: a task is skipped when its dwell cost would exceed the
     remaining budget.

A skipped task whose deadline has already passed is counted as late (deadline
miss). A serviced task advances its deadline by one revisit period.

Pure logic (no world / entity data) — same convention as the rest of tools/dem.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum


class DwellKind(IntEnum):
    SEARCH = 0
    TRACK = 1
    FIRE_CONTROL = 2


_CLASS_PRIORITY = {
    DwellKind.SEARCH: 0,
    DwellKind.TRACK: 1,
    DwellKind.FIRE_CONTROL: 2,
}


@dataclass
class DwellTask:
    kind: DwellKind
    task_id: int
    period_s: float          # revisit period (deadline advances by this)
    dwell_ms: float          # cost of one dwell
    deadline_s: float = 0.0  # absolute next deadline
    last_scheduled_s: float = field(default=float("-inf"))

    @property
    def priority(self) -> int:
        return _CLASS_PRIORITY[self.kind]


@dataclass
class DwellPlan:
    tasks: list[DwellTask]   # scheduled subset, in execution order
    spent_ms: float
    late_ids: list[int]      # skipped tasks whose deadline had passed

    @property
    def scheduled_ids(self) -> list[int]:
        return [t.task_id for t in self.tasks]


class DwellScheduler:
    def __init__(self, budget_ms: float):
        if budget_ms < 0.0:
            raise ValueError("budget_ms must be non-negative")
        self.budget_ms = budget_ms

    def plan(self, tasks: list[DwellTask], now_s: float) -> DwellPlan:
        ordered = sorted(
            tasks,
            key=lambda t: (-_CLASS_PRIORITY[t.kind], t.deadline_s, t.task_id),
        )
        chosen: list[DwellTask] = []
        spent = 0.0
        late_ids: list[int] = []
        for task in ordered:
            if spent + task.dwell_ms > self.budget_ms + 1e-9:
                if task.deadline_s <= now_s:
                    late_ids.append(task.task_id)
                continue
            chosen.append(task)
            spent += task.dwell_ms
        return DwellPlan(chosen, spent, late_ids)

    def advance(self, task: DwellTask, now_s: float) -> None:
        """Mark a scheduled task serviced; next revisit is service-relative.

        deadline = now + period: after a state update, uncertainty grows from
        the update time, so the next revisit is one period after service (not
        anchored to a stale fixed grid). Late service simply resets to now.
        """
        task.last_scheduled_s = now_s
        if task.period_s <= 0.0:
            task.deadline_s = float("inf")   # one-shot
            return
        task.deadline_s = now_s + task.period_s
