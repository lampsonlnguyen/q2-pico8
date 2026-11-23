#!/usr/bin/env python3
"""Assertions on host Q2 sim runs of tests/input.p8 (circle moves with
btn 0-3, O changes its colour, X recentres it).

  check.py full <scenario dir>      Pocket Boy layout (all five buttons)
  check.py fallback <scenario dir>  Q2 Pod 1.8 fallback layout
  check.py overlay <scenario dir>   touch D-pad, diagonals, hide/show

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
    if mode == "full":
        x0, y0, c0 = circle(d, 450)
        x1, _, _ = circle(d, 1150)
        expect(x1 < x0 - 10, f"Previous held moves Left ({x0:.0f} -> {x1:.0f})")
        x2, _, _ = circle(d, 1950)
        expect(x2 > x1 + 10, f"Next held moves Right ({x1:.0f} -> {x2:.0f})")
        _, y3, _ = circle(d, 2550)
        expect(y3 < y0 - 8, f"Return held moves Up ({y0:.0f} -> {y3:.0f})")
        _, y4, c4 = circle(d, 3150)
        expect(y4 > y3 + 8, f"Play/Pause held moves Down ({y3:.0f} -> {y4:.0f})")
        _, _, c5 = circle(d, 3550)
        expect(c5 != c4, f"Centre is O: colour {c4} -> {c5}")
        x6, y6, _ = circle(d, 4050)
        expect(abs(x6 - 64) < 3 and abs(y6 - 64) < 3, f"on-screen X recentres ({x6:.0f},{y6:.0f})")
        x7, _, _ = circle(d, 4700)
        expect(x7 > x6 + 6, f"clockwise wheel holds Right ({x6:.0f} -> {x7:.0f})")
        expect(stats["exit_reason"] == "return-centre",
               f"Return + Centre held ends the app (exit: {stats['exit_reason']})")
    elif mode == "fallback":
        x0, _, _ = circle(d, 450)
        x1, _, _ = circle(d, 1150)
        expect(x1 < x0 - 10, f"Previous held moves Left ({x0:.0f} -> {x1:.0f})")
        x2, y2, _ = circle(d, 1700)
        expect(abs(x2 - 64) < 3 and abs(y2 - 64) < 3, f"Play is X: recentres ({x2:.0f},{y2:.0f})")
        expect(stats["exit_reason"] == "double-back",
               f"Back twice ends the app (exit: {stats['exit_reason']})")
    elif mode == "overlay":
        x0, y0, _ = circle(d, 450)
        x1, y1, _ = circle(d, 1150)
        expect(x1 < x0 - 10 and abs(y1 - y0) < 2, f"D-pad left arm moves Left ({x0:.0f} -> {x1:.0f})")
        x2, y2, _ = circle(d, 1950)
        expect(x2 > x1 + 10 and y2 < y1 - 10,
               f"D-pad up-right corner moves diagonally (({x1:.0f},{y1:.0f}) -> ({x2:.0f},{y2:.0f}))")
        shown = pixel(d, 1950, 17, 229)
        hidden = pixel(d, 2300, 17, 229)
        expect(sum(shown) > 60 and sum(hidden) < 10, f"toggle hides the D-pad ({shown} -> {hidden})")
        expect(sum(pixel(d, 2300, 350, 17)) > 20, "pause stays visible while hidden")
        x3, y3, _ = circle(d, 2350)
        x4, y4, _ = circle(d, 3350)
        expect(abs(x4 - x3) < 1 and abs(y4 - y3) < 1,
               f"hidden X and D-pad ignore touches (({x3:.0f},{y3:.0f}) -> ({x4:.0f},{y4:.0f}))")
        x5, y5, _ = circle(d, 4100)
        expect(sum(pixel(d, 4100, 17, 229)) > 60, "toggle shows the D-pad again")
        expect(abs(x5 - 64) < 3 and abs(y5 - 64) < 3, f"shown X recentres ({x5:.0f},{y5:.0f})")
    else:
        raise SystemExit(__doc__)
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
