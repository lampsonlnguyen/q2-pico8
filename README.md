# PICO-8 player for Q2 Pod — FAKE-08 port in progress

**Status (stage 3):** The production/default installable ZIP remains the **working Q2 cartridge-browser preview**, not a playable emulator. The first *real FAKE-08* C++ engine integration has been implemented separately under `src/fake08_host.cpp` and `src/fake08_main.cpp`: it uses the existing browser to select a cartridge, then constructs FAKE-08's actual VM and calls `LoadCart` / `GameLoop`. The Q2 framebuffer bridge now supports FAKE-08's 144-color extended display palette. GitHub Actions tests the C++ adapter against a pinned upstream source revision.

**Do not confuse the CI C++ syntax check with a working Q2 executable.** A compatible MIPS C++17 standard library/runtime and a Q2 firmware sysroot are still needed to compile/link this engine and verify gameplay on-device. The default `make` target intentionally remains unchanged so users don't install an untested player.

## Install and add cartridge files

1. Open [GitHub Actions](https://github.com/lampsonlnguyen/q2-pico8/actions/workflows/fake08-port.yml) and select the latest successful **Q2 PICO-8 bridge** run. Under **Artifacts**, download `q2-pico8-browser-preview`. Extract the downloaded artifact ZIP to obtain **`q2-femto8-browser-preview.zip`**. Alternatively, build locally using `make check` (requires `clang lld make zip unzip gcc file`).
2. Extract the **contents of `q2-femto8-browser-preview.zip`** directly to the **root** of your Q2 Pod microSD card. Do not extract GitHub's Source code ZIP. The install ZIP contains `.pico8/q2app`, `.pico8/q2-pico8` and an empty `.pico8/carts/` directory.
3. Add compatible `.p8` or `.p8.png` cartridge files to `.pico8/carts/` (only files in this folder are scanned).
4. Launch **PICO-8 Browser Preview** from Library → Games.
5. Rotate the wheel to select a game; Center displays the loader-not-ready notice. Press Back to exit. To rescan after copying games, relaunch the app. The list also refreshes periodically when the number of files changes.

```text
SD-card/
  .pico8/
    q2app
    q2-pico8
    carts/
      example.p8
      example2.p8.png
```

### If PICO-8 does not appear in Library → Games

Q2 Pod's scanner only lists a card game when all of the following are true:
the SD root has a folder named exactly `.pico8`, that folder contains a
readable `q2app` file with `game=1`, and the compiled MIPS binary named
`q2-pico8` exists **in that same folder**. The app currently appears as
**PICO-8 Browser Preview**, not just PICO-8.

On Windows, enable **View → Show → Hidden items** to find `.pico8`.
Check that you didn't accidentally create `microSD/q2-femto8-browser-preview/.pico8`
or `microSD/.pico8/.pico8`, or copy only a ZIP into the card.
Exit and reopen Library → Games after copying the files. If it is still
missing, safely eject and reinsert the card, then restart the Q2.

The browser preview **does not launch PICO-8 games**. The actual FAKE-08
engine is still experimental and isn't included in this artifact.

The browser scans the SD card directly using Linux `getdents64` and supports up to 64 files. Filenames over 59 bytes are skipped. An unsupported extension is ignored. The scanner and menu are written in freestanding C, and use the established Q2 framebuffer and game-socket protocol.

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

The experimental output, if cross-linking succeeds, is
`dist/.pico8/q2-pico8`. A successful build alone does not certify
on-device C++ ABI compatibility or game performance. The full upstream
C++ engine *does* link in the native stub-based CI smoke test.

FAKE-08: https://github.com/jtothebell/fake-08 — MIT licensed. Its embedded
dependencies have separate notices in its `LICENSE.MD`. This repository does
not include an emulator binary or redistribute FAKE-08 source; contributors
must obtain the upstream source and preserve the license notices when packaging.

### Remaining for a playable release

1. Build all upstream FAKE-08 `Vm`, Lua, graphics, audio and cartridge components using a MIPS C++17 toolchain compatible with the device's **NaN2008** ABI and glibc.
2. Link `fake08_main.cpp`, `fake08_host.cpp`, `q2_platform.c` and `cart_browser.c` together. Exclude `cart_browser_probe.c` (it defines the preview `q2_main`).
3. Validate memory requirements, performance, touch/physical-button mapping and frame pacing with a legally shared test cartridge on a Q2.
4. Enable stereo ALSA sound after basic graphics/gameplay work.

Keep this project separate from `q2-arcade`. Do not rename a browser preview ZIP as an emulator release.

## Earlier Femto-8 investigation

Upstream [femto8](https://github.com/benbaker76/femto8) is MIT-licensed, but its C runtime includes Lua, libc filesystem functions and large work buffers. To run cartridges on the Q2, we need a MIPS32 userspace C library/sysroot and a Q2 backend in upstream `p8_emu.c`/`p8_input.c`. The current freestanding Q2 compiler is enough for the menu but **cannot link the full runtime**. Once integrated, use `q2_cart_path()` with femto8's existing `p8_load()` and then `p8_run()`. Also replace SDL initialization/frame presentation and confirm actual free RAM on Q2 hardware. Audio and full six-button mapping are separate tasks.

This code does not bundle third-party games. `.p8` carts remain subject to their authors' licenses, and `.p8.png` support is subject to the eventual upstream parser integration.

## Licensing

Q2 adapter: GPL-3.0-or-later; retain upstream femto8's MIT license notices when combined. No cartridge or PICO-8 proprietary assets are bundled.
