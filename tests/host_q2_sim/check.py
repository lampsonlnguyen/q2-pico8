#!/usr/bin/env python3
"""Assertions on host Q2 sim runs of tests/input.p8 (circle moves with
btn 0-3, O changes its colour, X recentres it).

  check.py full <scenario dir>      Pocket Boy layout (all five buttons)
  check.py fallback <scenario dir>  Q2 Pod 1.8 fallback layout
  check.py overlay <scenario dir>   D-pad, Centre remap and optional wheel

Times match the scripts in tests/host_q2_sim/scripts/. Exits 1 on failure.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

BG = (0x1d, 0x2b, 0x53)
TEXT = (0xff, 0xf1, 0xe8)


def frame_at(d, ms):
    stats = json.loads((d / "stats.json").read_text())
    times = stats["frame_ms"]
    i = min(range(len(times)), key=lambda k: abs(times[k] - ms))
    return d / "frames" / f"frame_{i + 1:04d}.png", times[i]


def circle(d, ms):
    path, when = frame_at(d, ms)
    im = np.asarray(Image.open(path).convert("RGB")).astype(int)
    # Below the title text, inside the 2x game area, clear of the
    # translucent D-pad (x < 83) and O/X buttons (x >= 292).
    game = im[76:288, 83:292]
    other = np.ones(game.shape[:2], bool)
    for c in (BG, TEXT):
        other &= ~np.all(game == c, axis=2)
    ys, xs = np.nonzero(other)
    if len(xs) == 0:
        raise SystemExit(f"FAIL: no circle in {path.name} at {when} ms")
    colour = tuple(int(v) for v in np.median(game[ys, xs], axis=0))
    # game coordinates (2x scale)
    return ((xs.mean() + 83 - 59) / 2, (ys.mean() + 76 - 32) / 2, colour)


def pixel(d, ms, x, y):
    path, _ = frame_at(d, ms)
    return Image.open(path).convert("RGB").getpixel((x, y))


failures = []


def expect(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


def main():
    mode, d = sys.argv[1], Path(sys.argv[2])
    stats = json.loads((d / "stats.json").read_text())
    if mode == "menu-ghost":
        expect(pixel(d, 4660, 130, 270) == BG, "Centre resumes despite a stale touch-up")
    elif mode == "full":
        x0, y0, c0 = circle(d, 450)
        x1, _, _ = circle(d, 1150)
        expect(x1 < x0 - 10, f"Previous held moves Left ({x0:.0f} -> {x1:.0f})")
        x2, _, _ = circle(d, 1950)
        expect(x2 > x1 + 10, f"Next held moves Right ({x1:.0f} -> {x2:.0f})")
        _, y3, _ = circle(d, 2550)
        expect(y3 < y0 - 8, f"Return held moves Up ({y0:.0f} -> {y3:.0f})")
        _, y4, c4 = circle(d, 3150)
        expect(y4 > y3 + 8, f"Play/Pause held moves Down ({y3:.0f} -> {y4:.0f})")
        x5, y5, c5 = circle(d, 3550)
        expect(abs(x5 - 64) < 3 and abs(y5 - 64) < 3 and c5 == c4,
               f"Centre is X: recentres the circle ({x5:.0f},{y5:.0f})")
        x6, _, c6 = circle(d, 4050)
        expect(c6 != c5, f"touch O changes colour ({c5} -> {c6})")
        x7, _, _ = circle(d, 4700)
        expect(abs(x7 - x6) < 1, f"wheel leaves position unchanged ({x6:.0f} -> {x7:.0f})")
        expect(stats["exit_reason"] == "return-hold",
               f"Return held ends the app (exit: {stats['exit_reason']})")
    elif mode == "fallback":
        x0, _, _ = circle(d, 450)
        x1, _, _ = circle(d, 1150)
        expect(x1 < x0 - 10, f"Previous held moves Left ({x0:.0f} -> {x1:.0f})")
        x2, y2, _ = circle(d, 1700)
        expect(abs(x2 - 64) < 3 and abs(y2 - 64) < 3, f"Centre is X: recentres ({x2:.0f},{y2:.0f})")
        expect(stats["exit_reason"] == "double-back",
               f"Back twice ends the app (exit: {stats['exit_reason']})")
    elif mode == "overlay":
        x0, _, _ = circle(d, 450)
        x1, _, _ = circle(d, 1150)
        expect(x1 < x0 - 10, f"touch D-pad moves left ({x0:.0f} -> {x1:.0f})")
        shown = pixel(d, 1950, 17, 229)
        hidden = pixel(d, 2300, 17, 229)
        expect(sum(shown) > 100 and sum(hidden) < 20, "toggle hides the D-pad")
        expect(sum(pixel(d, 2300, 350, 17)) > 20, "Menu remains visible")
        x3, y3, c3 = circle(d, 2350)
        x4, y4, c4 = circle(d, 3350)
        expect(abs(x4 - x3) < 1 and abs(y4 - y3) < 1 and c4 == c3,
               "hidden D-pad and O ignore touches")
        x5, _, c5 = circle(d, 5300)
        expect(abs(x5 - x4) < 1 and c5 != c4, "mapped Centre presses O")
        expect(sum(pixel(d, 5300, 350, 207)) > 100, "complementary touch X is visible")
        expect(sum(pixel(d, 5300, 320, 245)) < 20, "mapped O is removed from overlay")
        x6, y6, _ = circle(d, 5750)
        expect(abs(x6 - 64) < 3 and abs(y6 - 64) < 3, "touch X recentres the circle")
        x7, _, _ = circle(d, 6650)
        expect(x7 > x6 + 6, "enabled clickwheel moves the circle right")
    else:
        raise SystemExit(__doc__)
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
