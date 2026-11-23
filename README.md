# PICO-8 player for Q2 Pod

Plays PICO-8 cartridges on the Shanling Q2 with the
[FAKE-08](https://github.com/jtothebell/fake-08) VM. Carts appear in a
Cover Flow library, and **Get Games** finds free PICO-8 games on itch.io
and downloads them over the Q2's Wi-Fi. Controls, Cover Flow and the
itch.io browser follow [Pocket Boy](https://github.com/SlayTheEarth/q2-boy).
Stereo sound goes through the Q2 ALSA path (see "Audio" below).

## Install

1. Download **only** `q2-pico8-playable-experimental.zip` from the
   [latest release](https://github.com/lampsonlnguyen/q2-pico8/releases/latest).
   **Do not install `q2-femto8-browser-preview.zip` over the playable
   app.** Both v0.4.0-alpha archives used `/.pico8/` and the browser
   preview could overwrite the real emulator. As of v0.4.1-alpha the
   browser preview is a CI-only developer artifact.
   (v0.2.0-alpha and earlier crash on cart load; use v0.3.0-alpha or
   later.) Builds from unreleased commits are the
   **q2-pico8-playable-experimental** artifact of a successful
   [Q2 PICO-8 bridge run](https://github.com/lampsonlnguyen/q2-pico8/actions/workflows/fake08-port.yml).
2. Extract the ZIP to the root of the microSD card. When upgrading, replace
   everything in `.pico8` except your `carts`, `covers` and `cdata` folders.
3. Copy `.p8` or `.p8.png` carts into `/.pico8/carts/`, or use Get Games.
4. Open **Library → Games → PICO-8**.

If the app is missing, enable hidden files in your file manager and confirm
`.pico8/q2app` and `.pico8/q2-pico8` are directly under the card root's
`.pico8` folder. Reopen Library → Games after copying the files.

## If the app starts on a black screen

v0.4.0-alpha shipped both a real FAKE-08 player and a freestanding browser
preview in separate ZIP files, but both installed **the same**
`.pico8/q2-pico8` file. Installing the preview last overwrote the player.
The preview cannot launch games. This is one confirmed packaging ambiguity;
it is **not yet proven** to explain every black-screen report.

1. Install **only** the latest `q2-pico8-playable-experimental.zip`.
   Extract directly to the SD-card root and replace
   `.pico8/q2-pico8` plus `.pico8/q2app`. Do not nest
   `.pico8` within another folder. Keep `.pico8/carts`,
   `.pico8/covers`, `.pico8/cdata` and other saved files.
2. Restart PICO-8. The player now paints **Opening game library...**
   before decoding covers. If library loading crashes, this screen
   distinguishes the failure from a never-started executable.
3. If the screen remains black, copy **`.pico8/pico.log`** from the SD
   card and report which lines begin with `q2-pico8 startup:` or
   `q2-pico8: cannot open /dev/fb0`. Also provide the Q2 Pod firmware
   version and whether the loading screen appeared. The logger records
   framebuffer geometry/IOCTL, socket, and library-entry failures.
   A completely absent log may indicate a runtime-loader or install
   problem before `main()` runs.

The device build and host simulator cannot reproduce every physical Q2
framebuffer or glibc failure. The diagnostics above are intended to
identify the remaining device-specific cause rather than claiming an
unverified hardware fix.

## Library

The library opens on Cover Flow. Each cart shows its own art: the picture of
a `.p8.png` cart, or the `__label__` of a `.p8` cart drawn on a cart body. A
PNG in `/.pico8/covers/` named after the cart file plus `.png` (for example
`celeste.p8.png.png`) replaces it; Get Games saves the itch.io cover there.

| Q2 control | Library |
| --- | --- |
| Wheel, Previous / Next, swipe | browse |
| Centre, or tap the centre cover | play |
| Tap a side cover | select it |
| Play/Pause, or tap **⋯** | menu: Get Games, Sort (title or recently played), Refresh, Exit |
| Return | leave the app |

New carts copied to the card show up within a few seconds.

## Controls in a cart

The layout is Pocket Boy's:

| Q2 control | PICO-8 |
| --- | --- |
| Return / Play/Pause / Previous / Next | Up / Down / Left / Right |
| Centre | O |
| Wheel | Left or Right, held while it turns (clockwise is Right) |
| On-screen arrows, **O**, **X** | held input |
| On-screen **≡** | pause menu: Continue, Reset cart, **Exit to library**, **Exit to Q2 Pod** |
| Hold Return and Centre together (1.2 s) | leave the app |

On Q2 Pod 1.9.0 and later the firmware forwards all five buttons (its
`gameview` capability). On older firmware the player takes the buttons over
itself, as Pocket Boy does, by grabbing the Q2's GPIO key device, so the same
layout applies. While it runs, the firmware does not see those keys, so use
Return + Centre or the pause menu to leave. The grab ends when the app
exits. If the keys cannot be grabbed, the earlier fallback applies: Centre is
**O**, Play is **X**, Previous/Next are Left/Right, Back opens the pause menu
and Back twice within half a second exits.

The player saves cart data and closes the sound on its way out.

## Get Games (itch.io)

On a Q2 connected to Wi-Fi, open the library menu and choose **Get Games**.
It lists itch.io's games
[made with PICO-8](https://itch.io/games/made-with-pico-8) that the Q2 can
actually play, page by page. Change **Sort** (Popular, Newest, Top rated) or
**Price** (Free, Free and paid) at the top of the list. Each page is checked
before it appears, so it can take a while; Return cancels.

A game qualifies when it offers a `.p8` or `.p8.png` download (directly or
inside a ZIP), or when it is a browser game made with PICO-8's web export
("Web" in the list). For a web export, the helper reads the cartridge ROM
from the export's JavaScript (`_cartdat`) and saves it as a standard
`.p8.png` cart with the itch.io cover as its label. Free games download
straight into the library as `<title> [itch <id>]`. **View on itch.io**
shows a QR code of the game's page, for paid games and donations.

Get Games runs a separate helper, `q2-pico8-itch`, from the same folder. It
uses Pocket Boy's itch.io guest-download flow and the root certificates in
`itch-ca.pem`. Downloads are staged on the SD card, not in RAM.

## Source files

- `src/q2_platform.c`: MIPS32 syscalls, 375×320 rotated framebuffer (2× PICO-8 picture and the native-resolution UI canvas), the input socket and the GPIO key grab.
- `src/q2_input.c`: the button, wheel and touch mapping, shared by the device and the host simulation.
- `src/library.cpp`, `src/coverflow.cpp`, `src/covers.cpp`, `src/ui.cpp`: the Cover Flow library, its projection, cart artwork and text drawing.
- `src/getgames.cpp`, `src/itch.c`, `src/itch-helper/`: Get Games, the helper process runner and the Go helper (`go test -mod=vendor` in that folder).
- `src/cart_browser.c/.h`: SD-card directory scan and 128×128 cartridge browser, now used only by the freestanding browser preview.
- `src/cart_browser_probe.c`: Hardware preview, which **does not execute** cartridges.
- `src/femto8_bridge.c`: Earlier unlinked proof-of-concept for graphics and input integration with upstream femto8.
- `tests/browser_test.c`: Unit test for cartridge discovery and path construction.
- `tests/audio.p8`: Original CC0 test cartridge that plays a sustained sine sfx; the native smoke test runs it to verify the synth produces non-silent audio. `tests/demo.p8` is the earlier silent frame-only cart.

## FAKE-08 integration (experimental)

The port is inspired by FAKE-08's SDL1/FunKey S host but **does not use SDL**.

- `src/fake08_host.cpp`: implements the upstream `Host` interface, wheel/Center/Back input translation, 2× framebuffer drawing, cartridge enumeration, frame timing and lifecycle, and the per-frame audio fill (see "Audio").
- `src/q2_platform.c`: ALSA audio for the glibc build, resolved at runtime with `dlopen("libasound.so.2")` (same pattern as q2-atari2600); the freestanding browser build has no audio device and its stubs are no-ops.
- `src/fake08_main.cpp`: runs the library, opens the real FAKE-08 `Vm` for the chosen cart, runs the VM loop and returns to the library when the pause menu's "exit to library" loads FAKE-08's own menu cart.
- `tools/fake08-overlay.sh`: compiles a copy of upstream `vm.cpp` whose pause menu says "exit to library" and "exit to q2 pod". The upstream checkout is not modified.
- `src/q2_platform.c`: renders normal 16-color and FAKE-08's alternate 128–143 colors using the per-frame screen palette map.
- `.github/workflows/fake08-port.yml`: builds and tests the freestanding browser, links the real FAKE-08 engine, runs host gameplay/audio regression tests, cross-compiles the device ZIP, and publishes version-tagged releases. The FAKE-08 upstream source is pinned to `814991a2571ad3970e386cef48f3b148aa1c27b9`.

### Automated builds and GitHub releases

The [Q2 PICO-8 bridge workflow](https://github.com/lampsonlnguyen/q2-pico8/actions/workflows/fake08-port.yml) runs on pushes to `main`, pull requests, manual dispatches, and version tags (`v*`). It:

- Runs `make check` and verifies the browser-preview ZIP.
- Links the real FAKE-08 VM and runs the host Q2 simulation: cartridge loading, controls, rendering/frame pacing, and audio. A `q2-pico8-host-simulation-report` artifact contains the test report and summary.
- Builds the device MIPS executable with the firmware sysroot, checks its ELF/runtime ABI, runs a QEMU startup smoke test, and uploads `q2-pico8-playable-experimental` and `q2-pico8-browser-preview` artifacts.
- **Only after both test/build jobs succeed for a version tag**, creates or updates a GitHub Release with **only** `q2-pico8-playable-experimental.zip`. The colliding browser preview remains available as a CI artifact for developers, but is not published alongside the playable app. Tags containing `-alpha`, `-beta`, or `-rc` are published as prereleases.

To publish a **new tested alpha version**, update the `VERSION` file
(e.g. `0.4.1-alpha`) and merge into `main`. GitHub Actions runs both
the real FAKE-08 host simulation and Q2 cross-build, then creates the
`v<version>` tag and prerelease with **only the playable ZIP** if
every check passes. Subsequent pushes with the same VERSION do not
publish duplicate releases. Explicit `v*` tags remain supported:

```sh
git switch main
git pull --ff-only
git tag v0.4.1-alpha
git push origin v0.4.1-alpha
```

Pull requests upload downloadable test artifacts without publishing releases.
An alpha release is not considered physically validated until checked on a
real Q2. A release fails closed if the gameplay tests or device build fail.

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

`bash tests/host_q2_sim/run.sh` runs the real player natively on x86_64:
the library, Get Games, the FAKE-08 VM, the Q2 host and the input mapper,
with a host simulation of the framebuffer, a scripted input socket (raw Q2
key codes, wheel travel and touch), a WAV audio sink and a fake SD card
(see `tests/host_q2_sim/README.md`). CI runs it on every push. It checks:

- Library: carts listed, the wheel, Previous and a tap on a side cover
  move the selection, the menu opens, Return leaves.
- Library → cart → pause menu "exit to library" → back in the library.
- Get Games with a stand-in helper: list, download, the cart appears.
- Controls (`tests/host_q2_sim/check.py`): Previous/Next/Return/Play held
  move the test cart's circle Left/Right/Up/Down, Centre is O, the
  on-screen X recentres it, the wheel holds Right, and Return + Centre
  held ends the app; in the fallback layout Play is X and Back twice exits.
- A `.p8.png` cart in the layout the helper writes for web exports runs.
- Pacing (~16.2 ms ticks; 30 and 60 FPS carts) and audio (non-silent,
  262 Hz, 367/368 frames per tick).
- Invalid carts fail visibly: a missing cart and a cart with broken Lua
  show an error screen and exit 2, never FAKE-08's menu cart.

### Remaining device validation

- Check cartridge loading, touch and physical controls, and frame pacing on
  real Q2 hardware at both 30 and 60 FPS, including the GPIO grab on
  firmware without `gameview`.
- Try Get Games over Wi-Fi: catalog pages, downloads and web-export
  conversion have only been tested offline (unit tests and a stand-in helper).
- Confirm sound actually comes out of the Q2 speaker/headphones: the
  ALSA path above is implemented and verified in the native smoke test
  and the host simulation (WAV capture), but it has not yet been heard
  on a real Q2 (or in the emulator).

## Earlier Femto-8 investigation

Upstream [femto8](https://github.com/benbaker76/femto8) is MIT-licensed, but its C runtime includes Lua, libc filesystem functions and large work buffers. To run cartridges on the Q2, we need a MIPS32 userspace C library/sysroot and a Q2 backend in upstream `p8_emu.c`/`p8_input.c`. The current freestanding Q2 compiler is enough for the menu but **cannot link the full runtime**. Once integrated, use `q2_cart_path()` with femto8's existing `p8_load()` and then `p8_run()`. Also replace SDL initialization/frame presentation and confirm actual free RAM on Q2 hardware. Audio and full six-button mapping are separate tasks.

This code does not bundle third-party games. `.p8` carts remain subject to their authors' licenses, and `.p8.png` support is subject to the eventual upstream parser integration.

## Licensing

Q2 adapter: GPL-3.0-or-later. It includes code adapted from Pocket Boy
(GPL-2.0-or-later) and ships third-party notices listed in `NOTICE.md`.
No cartridge or PICO-8 proprietary assets are bundled.
