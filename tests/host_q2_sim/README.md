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
- `src/fake08_main.cpp` and the library: Cover Flow (`library.cpp`,
  `coverflow.cpp`, `covers.cpp`, `ui.cpp`), Get Games (`getgames.cpp`,
  `itch.c`, running a stand-in helper) and the cart flow.
- `src/q2_input.c` — the button and touch mapping the device uses.

Simulated (host-only, in `q2_sim_platform.c`):
- `/dev/fb0` → a 375×320 in-memory framebuffer, dumped to PNG per frame.
  The present path is a faithful port of `q2_platform.c`'s
  `present_mapped()`: same 2× scaling, centring, rotated bottom-up
  storage, palette mapping (`map[color] & 0x8f`), same on-screen
  control overlay.
- Input socket and GPIO keys → a script file (`Q2_SIM_INPUT`, see
  `scripts/`) of raw events (Q2 Pod key codes 170/171/222/223/218,
  wheel travel, touch down/move/up) fed to `src/q2_input.c` as the
  device feeds them. `Q2_SIM_FULL=1` (default) is all five buttons
  (gameview or grabbed keys); `Q2_SIM_FULL=0` is Q2 Pod 1.8's fallback.
- The native UI canvas (`q2_present_canvas`) is stored with the device's
  rotation and dumped to PNG like game frames.
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

`run.sh` stages a fake SD card in `<outbase>/sd` (passed to the player as
`Q2_PICO8_DATA`, which replaces `/mnt/mmc/.pico8`), with the test carts and
two `.p8.png` carts from `tests/make_png_cart.py`, builds the sim, then runs
the real `src/fake08_main.cpp`:

1. **library** (`scripts/library.txt`) — Cover Flow: ignored wheel, Next, Previous, a tap
   on a side cover, the menu, Return leaves.
2. **play-return** — library → `input.p8` → on-screen menu → "exit to
   library" → back in the library → Return.
3. **getgames** — the menu's Get Games with `fake-itch.sh` standing in for
   `q2-pico8-itch` (same commands and output, no network): list, download,
   the new cart appears in the library.
4. **audio-30fps / demo-30fps / update60-60fps** (`Q2_PICO8_CART`, 120
   frames) — pacing and synth audio to WAV.
5. **png-cart** — a `.p8.png` in the layout the helper writes for web
   exports loads and runs.
6. **input-controls / input-fallback** — `tests/input.p8` with the
   Pocket Boy layout and with the Q2 Pod 1.8 fallback (`Q2_SIM_FULL=0`);
   `check.py` measures the circle in the frames.
7. **invalid-missing / invalid-corrupt** — exit 2 with an error screen,
   never FAKE-08's menu cart.

Each scenario writes `frames/frame_*.png`, `audio.wav`,
`stats.json` (frame timestamps/hashes, audio stats, exit reason) and
`run.log` under its outdir. `analyze.py` aggregates them into
`summary.json` + `report.md` in the outbase.

## Single runs

```sh
FAKE08_SRC=/tmp/fake-08 bash tests/host_q2_sim/build.sh
Q2_PICO8_DATA=/tmp/sd Q2_PICO8_CART=audio.p8 Q2_SIM_MAX_FRAMES=120 \
  Q2_SIM_OUTDIR=/tmp/sim-out Q2_SIM_INPUT=tests/host_q2_sim/scripts/empty.txt \
  ./build/host-sim/q2-sim
# Without Q2_PICO8_CART the library opens; script it with raw Q2 events:
#   <ms> key RETURN|PLAY|PREV|NEXT|CENTRE down|up|press
#   <ms> wheel <travel>   <ms> touch <x> <y> down|move|up   <ms> quit
```

Exit codes mirror `fake08_main.cpp`: 0 ok, 2 cart failed to load.

`Q2_SIM_MAX_FRAMES` is an approximate cap: the run presents ~2 frames
past the limit, because the quit is only observed at the top of the
next GameLoop iteration and the 30 FPS coroutine yields once more
before the tick that polls input. Pacing figures use the actual
frame timestamps in `stats.json`, not the requested count.

Broken-Lua carts used to fall back to the BIOS menu silently:
`Vm::LoadCart` succeeds (the file parses), the syntax error is only
found when the VM first ticks the cart inside `GameLoop()`, and
FAKE-08 then queues a switch to its built-in BIOS menu — and the
load error is clobbered when that BIOS cart is loaded, so the BIOS
shows no error either. That is now fixed in the Q2 host, without
changing upstream FAKE-08: `src/cart_validate.h` /
`q2_cart_lua_ok()` (in `src/fake08_host.cpp`) parses the cart with
upstream's own `Cart` class and compiles its Lua with z8lua before
the VM starts. A bad cart shows an error screen (cart name +
syntax error) and exits 2; it never enters `GameLoop()` and never
loads the BIOS. A missing cart still exits 2 with 0 frames.
Runtime errors *during* play (e.g. in `_update`) make upstream load its
menu cart; the player then returns to the library and shows the error.

## Still device-only

- Sound actually coming out of the Q2 speaker/headphones (the ALSA
  `plughw:0,0`/`default` open path on the device, and the
  non-blocking drop-on-full-buffer behaviour).
- Real wheel/touch feel and the Q2 framebuffer/input-socket behaviour
  on hardware, and MIPS/NaN2008 ABI compatibility (covered by CI).
- Performance on the Q2 CPU at 30/60 FPS.
