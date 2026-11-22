# PICO-8 player for Q2 Pod — FAKE-08 port in progress

The experimental player runs cartridges through the FAKE-08 VM. The native
smoke test runs an original cartridge and checks that it renders frames
and that the synth produces non-silent audio. Stereo sound is wired up
through the Q2 ALSA path (see "Audio" below); the physical Q2 still needs
a gameplay test to confirm sound actually comes out of the device.

## Install the experimental player

1. Open [Q2 PICO-8 bridge runs](https://github.com/lampsonlnguyen/q2-pico8/actions/workflows/fake08-port.yml)
   and download the **q2-pico8-playable-experimental** artifact from a successful
   run. Unwrap the artifact to get the install ZIP of the same name.
2. Extract that install ZIP to the root of the microSD card. If you already
   installed the browser preview, replace `.pico8/q2app` and
   `.pico8/q2-pico8` and keep your existing carts.
3. Copy cartridge files ending in `.p8` or `.p8.png` into
   `/.pico8/carts/` on the card. The browser scans up to 64 files and skips
   names over 59 bytes.
4. Open **Library → Games → PICO-8**. Turn the wheel to select a cart and
   press Center to start. Back exits the picker.

In the cart picker, turn the wheel one notch per cart and press Center to
start one; Back leaves.

During gameplay on Q2 Pod 1.9.0 and later (firmware with the `gameview`
capability, which hands the game all five buttons):

| Q2 control | PICO-8 |
| --- | --- |
| Return / Play/Pause / Previous / Next | Up / Down / Left / Right |
| Center | O |
| Wheel | Left or Right, held while it turns (clockwise is Right) |
| On-screen arrows, **O**, **X** | held input |
| On-screen **≡** | pause menu (Continue, Reset cart, Exit to menu) |
| Hold Return and Center together | exit to Q2 Pod |

On Q2 Pod 1.8.9 and earlier, which don't forward Return to a game, Center is
**O**, Play is **X**, Previous/Next are Left/Right, Back opens the pause menu,
and Back twice within half a second exits (holding Back also exits: the
firmware ends a game after a 700 ms Return hold). Either way the player
saves cart data and closes the sound on its way out.

If the app is missing, enable hidden files in your file manager and confirm
`.pico8/q2app` and `.pico8/q2-pico8` are directly under the card root's
`.pico8` folder. Reopen Library → Games after copying the files.

`make check` builds and verifies the separate **PICO-8 Browser Preview**
ZIP. Use it to test cartridge discovery on its own. Install the experimental
player ZIP to run cartridges.

## Source files

- `src/q2_platform.c`: MIPS32 syscalls, 375×320 rotated framebuffer, 2× Pico-8 image display and Q2 inputs.
- `src/cart_browser.c/.h`: SD-card directory scan and 128×128 cartridge browser. `q2_cart_path()` returns the selected cartridge path ready for a future call to femto8's `p8_load()`.
- `src/cart_browser_probe.c`: Hardware preview, which **does not execute** cartridges.
- `src/femto8_bridge.c`: Earlier unlinked proof-of-concept for graphics and input integration with upstream femto8.
- `tests/browser_test.c`: Unit test for cartridge discovery and path construction.
- `tests/audio.p8`: Original CC0 test cartridge that plays a sustained sine sfx; the native smoke test runs it to verify the synth produces non-silent audio. `tests/demo.p8` is the earlier silent frame-only cart.

## FAKE-08 integration (experimental)

The port is inspired by FAKE-08's SDL1/FunKey S host but **does not use SDL**.

- `src/fake08_host.cpp`: implements the upstream `Host` interface, wheel/Center/Back input translation, 2× framebuffer drawing, cartridge enumeration, frame timing and lifecycle, and the per-frame audio fill (see "Audio").
- `src/q2_platform.c`: ALSA audio for the glibc build, resolved at runtime with `dlopen("libasound.so.2")` (same pattern as q2-atari2600); the freestanding browser build has no audio device and its stubs are no-ops.
- `src/fake08_main.cpp`: selects a cartridge with the existing Q2 browser, opens the real FAKE-08 `Vm`, loads the selected file, runs the VM loop and closes it cleanly.
- `src/q2_platform.c`: renders normal 16-color and FAKE-08's alternate 128–143 colors using the per-frame screen palette map.
- `.github/workflows/fake08-port.yml`: checks that the freestanding browser still builds and compiles the FAKE-08-specific C++ files against upstream revision `814991a2571ad3970e386cef48f3b148aa1c27b9`.

### Trying the experimental MIPS engine build

The optional cross-build script compiles FAKE-08's real interpreter and Lua
runtime, the Q2 host and the cartridge picker. The CI `device` job runs
this script and publishes the `q2-pico8-playable-experimental` artifact;
it does **not** run as part of the normal browser-preview build.

```sh
git clone --recurse-submodules https://github.com/jtothebell/fake-08 ../fake-08
# Obtain a device-compatible MIPS C++17 toolchain and firmware-derived sysroot.
FAKE08_SRC=../fake-08 \
Q2_SYSROOT="$HOME/.cache/q2/sysroot" \
bash tools/build-fake08.sh
```

The experimental build produces `dist/q2-pico8-playable-experimental.zip`
and stages its executable at `dist/player/.pico8/q2-pico8`. You can reuse the
sysroot from q2-nes by exporting the same `SYSROOT` value. `Q2_SYSROOT`
takes precedence when both are set. A successful build alone does not certify
on-device C++ ABI compatibility or game performance. The full upstream
C++ engine *does* link in the native stub-based CI smoke test.

FAKE-08: https://github.com/jtothebell/fake-08 — MIT licensed. Its embedded
dependencies have separate notices in its `LICENSE.MD`. This repository does
not include an emulator binary or redistribute FAKE-08 source; contributors
must obtain the upstream source and preserve the license notices when packaging.

### Audio

FAKE-08's synth runs at 22050 Hz stereo. The host fills one video
frame's worth of samples per `GameLoop` iteration — 735 frames at
30 FPS, 367/368 alternating at 60 FPS (fractional part carried
forward, so the audio clock never drifts from the frame clock) — and
hands them to ALSA as interleaved s16. `q2_audio_open()` opens the
output Q2 Pod passes as `argv[1]` (`plughw:0,0` for the jack,
`plug:bluealsa`, or `usb`, looked up in `/proc/asound/cards`), retrying
for 2 s on the jack and 5 s otherwise while the music player lets go of
the PCM. On the jack it clears the DAC's mute and PCM mode through
`/dev/shanling_dac` after the open and mutes it again at close. A soft
volume in `argv[2]` (`NN`; `hNN` is the DAC's own) and Mono Audio / Swap
L/R (`/tmp/q2mono`, `/tmp/q2swap`) are applied to the samples. If nothing
opens, the game runs silent rather than failing. Audio writes never block the game loop: a full
device buffer drops that frame's chunk instead of stalling pacing.
The native smoke test drives the real synth with `tests/audio.p8`
(a sustained sine sfx) and fails if it produces no audio frames or
only silence — last verified run wrote 4042 frames with peak sample
8191 over 12 rendered frames.

### Host Q2 simulation validation (no emulator, no firmware)

`bash tests/host_q2_sim/run.sh` runs the real FAKE-08 VM + Q2 host
natively on x86_64 through a host simulation of the Q2 hardware layer
(framebuffer, input script, WAV audio sink, fake SD card at
`/mnt/mmc/.pico8/carts`). Verified 2026-11-21 (see
`tests/host_q2_sim/README.md` for what is real vs simulated):

- Cart browser: 5 carts listed, wheel +1 moves the selection to
  `demo.p8`, Centre chooses it.
- Pacing: GameLoop tick mean ~16.1 ms (target 16.7 ms); distinct
  frames every ~32.0 ms for 30 FPS carts (`demo.p8`) and ~16.1 ms
  for 60 FPS carts (`update60.p8`); audio fills alternate 367/368
  frames per tick with no drift.
- Audio: `audio.p8` / `update60.p8` produce 2.0 s of non-silent
  audio (peak 8191, RMS ~4723, dominant 262 Hz) captured to WAV.
- Controls: scripted LEFT/RIGHT/UP/DOWN move the circle by the
  expected amounts, X resets it to centre, O changes its colour,
  a single Back opens the pause menu (CONTINUE / RESET CART /
  EXIT TO MENU / EXIT TO SETTINGS), touch taps hit the on-screen
  controls, and a double-Back exits.
- Invalid carts: a missing cart exits 2 with 0 frames. A cart with
  broken Lua now also fails visibly instead of falling back to the
  BIOS menu: the host pre-validates the cart's Lua (see
  `src/cart_validate.h`) before starting the VM, shows a LOAD ERROR
  screen with the cart name and the syntax error, and exits 2 on
  Centre/Back. Verified in the host sim: `invalid.p8` exits 2 with
  the error screen presented and no BIOS load, no GameLoop entry.

### Remaining device validation

- Check cartridge loading, touch and physical controls, and frame pacing on
  real Q2 hardware at both 30 and 60 FPS.
- Confirm sound actually comes out of the Q2 speaker/headphones: the
  ALSA path above is implemented and verified in the native smoke test
  and the host simulation (WAV capture), but it has not yet been heard
  on a real Q2 (or in the emulator).

## Earlier Femto-8 investigation

Upstream [femto8](https://github.com/benbaker76/femto8) is MIT-licensed, but its C runtime includes Lua, libc filesystem functions and large work buffers. To run cartridges on the Q2, we need a MIPS32 userspace C library/sysroot and a Q2 backend in upstream `p8_emu.c`/`p8_input.c`. The current freestanding Q2 compiler is enough for the menu but **cannot link the full runtime**. Once integrated, use `q2_cart_path()` with femto8's existing `p8_load()` and then `p8_run()`. Also replace SDL initialization/frame presentation and confirm actual free RAM on Q2 hardware. Audio and full six-button mapping are separate tasks.

This code does not bundle third-party games. `.p8` carts remain subject to their authors' licenses, and `.p8.png` support is subject to the eventual upstream parser integration.

## Licensing

Q2 adapter: GPL-3.0-or-later; retain upstream femto8's MIT license notices when combined. No cartridge or PICO-8 proprietary assets are bundled.
