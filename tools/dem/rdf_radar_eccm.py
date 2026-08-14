"""ECCM decision layer (offline model; TODO §9 S3).

Maps per-dwell EW observables to ECCM responses. Deterministic, hysteresis-
guarded, pure logic — the drift guard for the Enforce port.

Observables (what the radar already measures via GetEwStatsShort / false-plot
count):
  - jn_db               jammer-to-noise ratio (noise jamming strength)
  - sidelobe_coupling   fraction of jammer power entering via sidelobes (0..1)
  - deception_count     deception false plots seen this dwell
  - locked              a target is currently locked (burn-through policy)

Responses (orthogonal; multiple may fire):
  - enable_slb          sidelobe blanking — defeats sidelobe noise jamming
  - prf_agility         PRF stagger / set hopping — defeats range-gate pull-off
  - freq_agility        carrier frequency hopping — defeats spot/barrage jamming
  - burn_through        hold the locked track through the jam

Pure logic (no world / entity data), import-light — same convention as the rest
of tools/dem.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass
class EccmObservations:
    jn_db: float = -300.0
    sidelobe_coupling: float = 0.0   # 0..1
    deception_count: int = 0
    locked: bool = False


@dataclass
class EccmActions:
    enable_slb: bool = False
    prf_agility: bool = False
    freq_agility: bool = False
    burn_through: bool = False

    def any_active(self) -> bool:
        return (
            self.enable_slb
            or self.prf_agility
            or self.freq_agility
            or self.burn_through
        )

    def as_tuple(self) -> tuple[bool, bool, bool, bool]:
        return (
            self.enable_slb,
            self.prf_agility,
            self.freq_agility,
            self.burn_through,
        )


class EccmDecisionLayer:
    """Hysteresis-guarded ECCM decision from per-dwell EW observables.

    Noise-jam detection is sticky: it turns on when jn_db >= jn_on_db and stays
    on until jn_db < jn_on_db - jn_hysteresis_db. Within an active jam, the
    sidelobe-coupling ratio selects sidelobe blanking (sidelobe jam) vs frequency
    agility (mainlobe jam). Deception triggers PRF agility, released when
    deception clears.
    """

    def __init__(
        self,
        jn_on_db: float = 6.0,
        jn_hysteresis_db: float = 2.0,
        sidelobe_coupling_on: float = 0.3,
    ) -> None:
        if jn_hysteresis_db < 0.0:
            raise ValueError("jn_hysteresis_db must be non-negative")
        if not 0.0 <= sidelobe_coupling_on <= 1.0:
            raise ValueError("sidelobe_coupling_on must be in [0, 1]")
        self.jn_on_db = jn_on_db
        self.jn_off_db = jn_on_db - jn_hysteresis_db
        self.sidelobe_coupling_on = sidelobe_coupling_on
        self._jam_active = False
        self._prf_active = False

    def decide(self, obs: EccmObservations) -> EccmActions:
        if obs.jn_db >= self.jn_on_db:
            self._jam_active = True
        elif obs.jn_db < self.jn_off_db:
            self._jam_active = False

        if obs.deception_count > 0:
            self._prf_active = True
        else:
            self._prf_active = False

        actions = EccmActions()
        actions.prf_agility = self._prf_active
        if self._jam_active:
            if obs.sidelobe_coupling >= self.sidelobe_coupling_on:
                actions.enable_slb = True
            else:
                actions.freq_agility = True
            if obs.locked:
                actions.burn_through = True
        return actions

    def reset(self) -> None:
        self._jam_active = False
        self._prf_active = False
