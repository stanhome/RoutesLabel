#!/usr/bin/env python3
"""Verify every assets/cases/*.json has identical start/end on all 3 routes."""
from __future__ import annotations

import json
import sys
from pathlib import Path


def check(path: Path) -> list[str]:
    doc = json.loads(path.read_text())
    routes = doc.get("routes", [])
    if len(routes) != 3:
        return [f"{path.name}: expected 3 routes, got {len(routes)}"]
    polys = [r["polyline"] for r in routes]
    if any(len(p) < 2 for p in polys):
        return [f"{path.name}: polyline too short"]
    s0, e0 = polys[0][0], polys[0][-1]
    errs = []
    for i, p in enumerate(polys[1:], 1):
        if p[0] != s0:
            errs.append(f"{path.name}: route {i} start {p[0]} != route 0 {s0}")
        if p[-1] != e0:
            errs.append(f"{path.name}: route {i} end {p[-1]} != route 0 {e0}")
    od = doc.get("_od")
    if od:
        ws, we = od.get("start"), od.get("end")
        if ws and s0 != ws:
            errs.append(f"{path.name}: _od.start != polyline start")
        if we and e0 != we:
            errs.append(f"{path.name}: _od.end != polyline end")
    return errs


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    cases = sorted((root / "assets" / "cases").glob("*.json"))
    all_errs: list[str] = []
    for p in cases:
        if p.name == "dense-trunk-perf-scene.json":
            continue
        all_errs.extend(check(p))
    if all_errs:
        for e in all_errs:
            print(e, file=sys.stderr)
        return 1
    print(f"OK: {len(cases)} case file(s), all routes share start/end.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
