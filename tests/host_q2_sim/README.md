# Host Q2 simulation harness

Emulate the Pico-8 player **without** the Q2 emulator: no QEMU, no Q2
Pod firmware, no device. The real FAKE-08 VM and the real
`src/fake08_host.cpp` run natively on x86_64, through a host
simulation of the Q2 hardware layer that `src/q2_platform.c`
abstracts.

## What is real, what is simulated

Real (unchanged, host-compiled):
- FAKE-08 VM + Lua engine (`/tmp/fake-08`, pinned `814991a…`), including
  cart parsing, `_update`/`_update60` timing, `btn`/`btnp`, pause menu,
  and the synth.
- `src/fake08_host.cpp` — the Host interface, audio-buffer fill, input
  translation, frame timing (`waitForTargetFps`), cart enumeration.
- `src/cart_browser.c` — compiled unchanged; its MIPS `sc()` calls are
  served by the sim platform's `getdents` shim (see below).

Simulated (host-only, in `q2_sim_platform.c`):
- `/dev/fb0` → a 375×320 in-memory framebuffer, dumped to PNG per frame.
  The present path is a faithful port of `q2_platform.c`'s
  `present_mapped()`: same 2× scaling, centring, rotated bottom-up
  storage, palette mapping (`map[color] & 0x8f`), same on-screen
  control overlay.
- Input socket → a script file (`Q2_SIM_INPUT`, see `scripts/`), with
  the same datagram semantics as the device: wheel synthesises brief
  LEFT/RIGHT presses, key codes 218=O / 171=X / 222/172=LEFT /
  223/173=RIGHT, Back (170) = PAUSE once, quit on a second press
  within 500 ms, touch hits the same control rectangles.
- `q2_millis()` → real `CLOCK_MONOTONIC` time (the old smoke stub
  faked +34 ms per call, so it could not measure pacing).
- ALSA → a WAV capture sink (`audio.wav`, 22050 Hz stereo s16) that
  never blocks; peak/RMS/frames and the per-call frame-size histogram
  (367/368/735) are recorded in `stats.json`.

Why `src/q2_platform.c` itself is not compiled here: it is MIPS-only
(raw MIPS o32 syscall numbers, inline MIPS asm, MIPS `__start`). Its
interface is what the sim re-implements, with the same semantics.
Anything MIPS/NaN2008-specific (ELF ABI, loader, firmware libc) is
therefore **not** proven by this harness — that stays device-only,
covered by CI's `device` job and QEMU startup smoke test.

## Run it

```sh
# FAKE-08 sources, pinned revision 814991a2571ad3970e386cef48f3b148aa1c27b9
bash tests/host_q2_sim/run.sh [outbase]
# outbase defaults to ~/workspace/q2-pico8-emu-test/host-sim
```

`run.sh` stages a fake SD card at the real device path
`/mnt/mmc/.pico8/carts` (a host directory, not firmware), builds the
sim, then runs:

1. **browser** — wheel +1, Centre: verifies the picker lists carts,
   selection moves, and O chooses the highlighted cart.
2. **audio-30fps** (`tests/audio.p8`, `_update`, 120 frames) — pacing
   + synth audio to WAV.
3. **demo-30fps** (`tests/demo.p8`, 120 frames) — pacing, distinct
   frames every ~33 ms.
4. **update60-60fps** (`tests/update60.p8`, `_update60`, 120 frames) —
   pacing, distinct frames every ~16.7 ms.
5. **input-controls** (`tests/input.p8`, scripted keys/touch/pause/
   double-Back, up to 600 frames) — proves input reaches the cart
   (circle moves, colour changes, reset, pause menu, exit).
6. **invalid-missing / invalid-corrupt** — missing cart must exit 2
   with no frames; corrupt cart's exit is recorded as-is.

Each scenario writes `frames/frame_*.png`, `audio.wav`,
`stats.json` (frame timestamps/hashes, audio stats, exit reason) and
`run.log` under its outdir. `analyze.py` aggregates them into
`summary.json` + `report.md` in the outbase.

## Single runs

```sh
FAKE08_SRC=/tmp/fake-08 bash tests/host_q2_sim/build.sh
Q2_SIM_CART=audio.p8 Q2_SIM_MAX_FRAMES=120 \
  Q2_SIM_OUTDIR=/tmp/sim-out Q2_SIM_INPUT=tests/host_q2_sim/scripts/empty.txt \
  ./build/host-sim/q2-sim
```

Exit codes mirror `fake08_main.cpp`: 0 ok, 1 host failed to open,
2 cart failed to load.

`Q2_SIM_MAX_FRAMES` is an approximate cap: the run presents ~2 frames
past the limit, because the quit is only observed at the top of the
next GameLoop iteration and the 30 FPS coroutine yields once more
before the tick that polls input. Pacing figures use the actual
frame timestamps in `stats.json`, not the requested count.

Known player gap found by the invalid-cart run: a cart with broken
Lua does not fail visibly. `Vm::LoadCart` succeeds (the file parses),
the syntax error is only found when `vm_run()` loads the code, and
FAKE-08 then queues a cart change to its built-in BIOS menu — the
error appears only in the log, and the user sees the BIOS menu with
no on-screen error for their cart. A missing cart, by contrast,
exits 2 with 0 frames. Fixing the broken-Lua fallback would change
upstream FAKE-08 behaviour on the device too, so it is reported
here, not changed in this harness.

## Still device-only

- Sound actually coming out of the Q2 speaker/headphones (the ALSA
  `plughw:0,0`/`default` open path on the device, and the
  non-blocking drop-on-full-buffer behaviour).
- Real wheel/touch feel and the Q2 framebuffer/input-socket behaviour
  on hardware, and MIPS/NaN2008 ABI compatibility (covered by CI).
- Performance on the Q2 CPU at 30/60 FPS.
