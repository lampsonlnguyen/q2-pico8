# PICO-8 player for Q2 Pod — FAKE-08 port in progress

The experimental player runs cartridges through the FAKE-08 VM. The native
smoke test runs an original cartridge and checks that it renders frames.
The physical Q2 still needs a gameplay test. Audio is currently disabled.

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

During gameplay, use the on-screen arrows and **O** / **X** buttons for
held input. Center also sends **O**, Play sends **X**, and Previous/Next
send Left/Right. The wheel sends brief Left/Right presses. Back opens the
PICO-8 pause menu. Press Back twice within half a second to exit to Q2 Pod.

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

## FAKE-08 integration (experimental)

The port is inspired by FAKE-08's SDL1/FunKey S host but **does not use SDL**.

- `src/fake08_host.cpp`: implements the upstream `Host` interface, wheel/Center/Back input translation, 2× framebuffer drawing, cartridge enumeration, frame timing and lifecycle. Audio is deliberately disabled initially.
- `src/fake08_main.cpp`: selects a cartridge with the existing Q2 browser, opens the real FAKE-08 `Vm`, loads the selected file, runs the VM loop and closes it cleanly.
- `src/q2_platform.c`: renders normal 16-color and FAKE-08's alternate 128–143 colors using the per-frame screen palette map.
- `.github/workflows/fake08-port.yml`: checks that the freestanding browser still builds and compiles the FAKE-08-specific C++ files against upstream revision `814991a2571ad3970e386cef48f3b148aa1c27b9`.

### Trying the experimental MIPS engine build

The optional cross-build script compiles FAKE-08's real interpreter and Lua
runtime, the Q2 host and the cartridge picker. It does **not** run as part of
the normal browser-preview build or publish an installable release.

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

### Remaining device validation

- Check cartridge loading, touch and physical controls, and frame pacing on
  real Q2 hardware at both 30 and 60 FPS.
- Bring up stereo ALSA sound using the Q2 audio path.

## Earlier Femto-8 investigation

Upstream [femto8](https://github.com/benbaker76/femto8) is MIT-licensed, but its C runtime includes Lua, libc filesystem functions and large work buffers. To run cartridges on the Q2, we need a MIPS32 userspace C library/sysroot and a Q2 backend in upstream `p8_emu.c`/`p8_input.c`. The current freestanding Q2 compiler is enough for the menu but **cannot link the full runtime**. Once integrated, use `q2_cart_path()` with femto8's existing `p8_load()` and then `p8_run()`. Also replace SDL initialization/frame presentation and confirm actual free RAM on Q2 hardware. Audio and full six-button mapping are separate tasks.

This code does not bundle third-party games. `.p8` carts remain subject to their authors' licenses, and `.p8.png` support is subject to the eventual upstream parser integration.

## Licensing

Q2 adapter: GPL-3.0-or-later; retain upstream femto8's MIT license notices when combined. No cartridge or PICO-8 proprietary assets are bundled.
