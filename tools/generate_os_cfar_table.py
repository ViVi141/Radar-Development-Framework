#!/usr/bin/env python3
"""
Generate offline OS-CFAR multiplier lookup CSV for RDF_CFar.
This uses the analytic expression for E[exp(-alpha * X_r)] and finds alpha s.t. E == target_pfa.
Outputs: scripts/Game/RDF/Radar/Data/os_cfar_multipliers.csv
"""
import math
from pathlib import Path

OUT_PATH = Path("scripts/Game/RDF/Radar/Data/os_cfar_multipliers.csv")
OUT_PATH.parent.mkdir(parents=True, exist_ok=True)

# Parameter grid (matches user's selection)
windows = [8, 16, 32]
ranks = [2, 4, 8]
pfas = [1e-3, 1e-6, 1e-9]

# Numeric helpers using log-gamma for stability

def log_E(alpha: float, n: int, r: int) -> float:
    # E(alpha) = n! * Gamma(r + alpha) / ( (r-1)! * Gamma(n + alpha + 1) )
    # logE = lgamma(n+1) + lgamma(r + alpha) - lgamma(r) - lgamma(n + alpha + 1)
    return math.lgamma(n + 1) + math.lgamma(r + alpha) - math.lgamma(r) - math.lgamma(n + alpha + 1)


def E(alpha: float, n: int, r: int) -> float:
    return math.exp(log_E(alpha, n, r))


def find_alpha(n: int, r: int, target_pfa: float, tol=1e-12, max_iter=200) -> float:
    # E(alpha) is monotonic decreasing in alpha from 1->0. Use bisection on [0, high].
    lo = 0.0
    hi = 1.0
    # increase hi until E(hi) < target_pfa
    while E(hi, n, r) > target_pfa:
        hi *= 2.0
        if hi > 1e12:
            raise RuntimeError("failed to bracket root for n=%d r=%d pfa=%g" % (n, r, target_pfa))
    # bisection
    for _ in range(max_iter):
        mid = 0.5 * (lo + hi)
        val = E(mid, n, r)
        if abs(val - target_pfa) <= tol:
            return mid
        if val > target_pfa:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi)


# Build table
rows = []
for n in windows:
    for r in ranks:
        if r < 1 or r > n:
            continue
        for p in pfas:
            alpha = find_alpha(n, r, p, tol=1e-14)
            rows.append((n, r, p, alpha))

# Write CSV
with OUT_PATH.open("w", encoding="utf-8") as f:
    f.write("# OS-CFAR multiplier lookup table\n")
    f.write("# Columns: window,rank,pfa,multiplier\n")
    f.write("window,rank,pfa,multiplier\n")
    for n, r, p, a in rows:
        f.write(f"{n},{r},{p:.0e},{a:.12e}\n")

print(f"Wrote {OUT_PATH} ({len(rows)} rows)")
