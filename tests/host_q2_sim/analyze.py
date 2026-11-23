#!/usr/bin/env python3
"""Analyse host Q2 sim runs under OUTBASE.
Reads each scenario's stats.json + audio.wav + frames/*.png and writes
summary.json + report.md. Pure stdlib + PIL + numpy (analysis only).
Usage: python3 tests/host_q2_sim/analyze.py <outbase>
"""
import json, math, os, statistics, sys, wave
from pathlib import Path

import numpy as np
from PIL import Image

def intervals(ms):
    d = [b - a for a, b in zip(ms, ms[1:]) if b > a]
    if not d:
        return {}
    return {"n": len(d), "mean_ms": round(statistics.mean(d), 2),
            "min_ms": min(d), "max_ms": max(d),
            "stdev_ms": round(statistics.pstdev(d), 2) if len(d) > 1 else 0.0}

def distinct_intervals(ms, hashes):
    # intervals between successive distinct frames (frame 0 counts as distinct)
    prev = [ms[i] for i in range(len(ms)) if i == 0 or hashes[i] != hashes[i - 1]]
    return intervals(prev)

def audio_stats(path):
    if not path.exists():
        return {"present": False}
    with wave.open(str(path), "rb") as w:
        n, rate, ch = w.getnframes(), w.getframerate(), w.getnchannels()
        raw = w.readframes(n)
    if n == 0:
        return {"present": True, "frames": 0, "seconds": 0.0, "peak": 0, "rms": 0.0}
    a = np.frombuffer(raw, dtype=np.int16).astype(np.float64)
    mono = a.reshape(-1, ch).mean(axis=1) if ch > 1 else a
    peak = int(np.max(np.abs(a)))
    rms = float(np.sqrt(np.mean(a ** 2)))
    # dominant frequency over the first second of mono audio
    seg = mono[:rate]
    seg = seg - seg.mean()
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    freqs = np.fft.rfftfreq(len(seg), 1 / rate)
    dom = float(freqs[int(np.argmax(spec[1:])) + 1]) if len(spec) > 2 else 0.0
    silent = float(np.mean(np.abs(mono) < 10))
    return {"present": True, "frames": int(n), "seconds": round(n / rate, 3),
            "peak": peak, "rms": round(rms, 1), "dominant_hz": round(dom, 1),
            "silent_fraction": round(silent, 3)}

def red_centroid(png):
    im = np.asarray(Image.open(png).convert("RGB")).astype(int)
    # game area: 256x256 at (59,32), 2x scale; col 8 = #ff004d
    mask = (im[:, :, 0] > 200) & (im[:, :, 1] < 80) & (im[:, :, 2] < 110)
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return None
    return {"x": round(float(xs.mean()), 1), "y": round(float(ys.mean()), 1),
            "count": int(len(xs))}

def analyse_scenario(d: Path):
    stats_path = d / "stats.json"
    if not stats_path.exists():
        return {"name": d.name, "present": False}
    s = json.loads(stats_path.read_text())
    exit_code = None
    ec = d / "exit_code"
    if ec.exists():
        try:
            exit_code = int(ec.read_text().strip())
        except ValueError:
            exit_code = None
    out = {"name": d.name, "present": True,
           "frames": s["frames_presented"], "distinct_frames": s["distinct_frames"],
           "exit_reason": s["exit_reason"], "exit_code": exit_code,
           "input_events": s["input_events_applied"],
           "tick_intervals": intervals(s["frame_ms"]),
           "distinct_intervals": distinct_intervals(s["frame_ms"], s["frame_hash"]),
           "audio_stats": s.get("audio_stats", {}),
           "audio": audio_stats(d / "audio.wav"),
           "audio_calls": {"367": s.get("audio_calls_367"), "368": s.get("audio_calls_368"),
                           "735": s.get("audio_calls_735"), "other": s.get("audio_calls_other")}}
    frames = sorted((d / "frames").glob("frame_*.png")) if (d / "frames").exists() else []
    out["png_frames"] = len(frames)
    if frames:
        # centroid trajectory, sampled
        traj = []
        for p in frames[::max(1, len(frames) // 12)] + [frames[-1]]:
            c = red_centroid(p)
            if c:
                traj.append({"frame": p.name, **c})
        out["red_centroid_trajectory"] = traj
        out["first_frame"] = str(frames[0])
        out["last_frame"] = str(frames[-1])
    return out

def main():
    outbase = Path(sys.argv[1])
    scenarios = {}
    for d in sorted(p for p in outbase.iterdir() if p.is_dir() and (p / "stats.json").exists()):
        scenarios[d.name] = analyse_scenario(d)
    summary = {"scenarios": scenarios}
    (outbase / "summary.json").write_text(json.dumps(summary, indent=2))
    lines = ["# Host Q2 sim — validation report", ""]
    for name, s in scenarios.items():
        lines.append(f"## {name}")
        if "log" in s:
            lines.append("```\n" + s["log"] + "```")
            continue
        lines.append(f"- frames presented: {s['frames']}, distinct: {s['distinct_frames']}, "
                     f"exit code: {s.get('exit_code')}, exit: {s['exit_reason']}, "
                     f"input events: {s['input_events']}, PNGs: {s['png_frames']}")
        if s["frames"] == 0:
            lines.append("- no frames: the run never entered GameLoop (cart load failed before it)")
        ti = s["tick_intervals"]
        if ti:
            lines.append(f"- GameLoop tick interval: mean {ti['mean_ms']} ms, min {ti['min_ms']}, "
                         f"max {ti['max_ms']}, stdev {ti['stdev_ms']} (n={ti['n']}) — target 16.7 ms at 60 Hz")
        di = s["distinct_intervals"]
        if s["distinct_frames"] <= 2:
            lines.append(f"- distinct frames: only {s['distinct_frames']} (static cart — "
                         f"the image does not change after the first frame)")
        elif di:
            lines.append(f"- distinct-frame interval: mean {di['mean_ms']} ms, min {di['min_ms']}, "
                         f"max {di['max_ms']}, stdev {di['stdev_ms']} (n={di['n']}) — 30 FPS carts ~33.3 ms, 60 FPS carts ~16.7 ms")
        a = s["audio"]
        if a.get("present"):
            lines.append(f"- audio: {a['frames']} frames, {a['seconds']} s, peak {a['peak']}, "
                         f"RMS {a['rms']}, dominant {a.get('dominant_hz')} Hz, silent fraction {a.get('silent_fraction')}")
            lines.append(f"  audio calls per frame: 367x{s['audio_calls']['367']}, 368x{s['audio_calls']['368']}, "
                         f"735x{s['audio_calls']['735']}, other x{s['audio_calls']['other']}")
        traj = s.get("red_centroid_trajectory") or []
        if traj:
            lines.append("- red-circle centroid (game coords, px): " +
                         ", ".join(f"{t['frame']}:({t['x']},{t['y']})" for t in traj))
        lines.append("")
    (outbase / "report.md").write_text("\n".join(lines))
    print("\n".join(lines))

if __name__ == "__main__":
    main()
