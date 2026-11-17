# femto8 → Q2 Pod native port (stage 2: cartridge browser preview)

**Status:** The SD-card cartridge **browser works**, but the femto8 Lua emulator is **not integrated**. Selecting a game currently displays **RUNTIME NOT READY**, rather than launching it. This is not yet a playable Pico-8 emulator. The compiled executable targets MIPS32, not an emulator or SDL runtime.

## Install and add cartridge files

1. Build on Ubuntu/WSL with `make check` (requires `clang lld make zip unzip gcc file`), or use the `q2-femto8-browser-preview.zip` build artifact.
2. Extract the ZIP to the **root** of your Q2 Pod microSD card. The ZIP includes an empty `.pico8/carts/` directory.
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

The browser scans the SD card directly using Linux `getdents64` and supports up to 64 files. Filenames over 59 bytes are skipped. An unsupported extension is ignored. The scanner and menu are written in freestanding C, and use the established Q2 framebuffer and game-socket protocol.

## Source files

- `src/q2_platform.c`: MIPS32 syscalls, 375×320 rotated framebuffer, 2× Pico-8 image display and Q2 inputs.
- `src/cart_browser.c/.h`: SD-card directory scan and 128×128 cartridge browser. `q2_cart_path()` returns the selected cartridge path ready for a future call to femto8's `p8_load()`.
- `src/cart_browser_probe.c`: Hardware preview, which **does not execute** cartridges.
- `src/femto8_bridge.c`: Earlier unlinked proof-of-concept for graphics and input integration with upstream femto8.
- `tests/browser_test.c`: Unit test for cartridge discovery and path construction.

## What remains to run games

Upstream [femto8](https://github.com/benbaker76/femto8) is MIT-licensed, but its C runtime includes Lua, libc filesystem functions and large work buffers. To run cartridges on the Q2, we need a MIPS32 userspace C library/sysroot and a Q2 backend in upstream `p8_emu.c`/`p8_input.c`. The current freestanding Q2 compiler is enough for the menu but **cannot link the full runtime**. Once integrated, use `q2_cart_path()` with femto8's existing `p8_load()` and then `p8_run()`. Also replace SDL initialization/frame presentation and confirm actual free RAM on Q2 hardware. Audio and full six-button mapping are separate tasks.

This code does not bundle third-party games. `.p8` carts remain subject to their authors' licenses, and `.p8.png` support is subject to the eventual upstream parser integration.

## Licensing

Q2 adapter: GPL-3.0-or-later; retain upstream femto8's MIT license notices when combined. No cartridge or PICO-8 proprietary assets are bundled.
