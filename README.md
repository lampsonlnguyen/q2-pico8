# PICO-8 player for Q2 Pod

Plays PICO-8 cartridges on the Shanling Q2 with the
[FAKE-08](https://github.com/jtothebell/fake-08) VM. Carts appear in a
Cover Flow library, and **Get Games** finds free PICO-8 games on itch.io
and downloads them over the Q2's Wi-Fi. Controls, Cover Flow and the
itch.io browser follow [Pocket Boy](https://github.com/SlayTheEarth/q2-boy).
Stereo sound goes through the Q2 ALSA path (see "Audio" below).

## Emulator engine: FAKE-08

**The playable Q2 PICO-8 app uses [FAKE-08](https://github.com/jtothebell/fake-08), not Femto-8.**
The Q2 build links FAKE-08's VM and Lua runtime to a native MIPS/Q2
platform layer. The Cover Flow library, controls, framebuffer and ALSA audio
are Q2-specific integrations.

| Component | Role |
| --- | --- |
| **FAKE-08** | Active cartridge emulator inside the playable ZIP |
| **Q2 platform and library** | MIPS display, input, Cover Flow and audio |
| **Femto-8** | Historical proof-of-concept, removed; **not** the playable engine |

There is one build, `q2-pico8.zip`, which uses
**FAKE-08**. The old cart-list-only browser preview
(`q2-femto8-browser-preview.zip`) was removed in v0.4.2-alpha; if a copy
of it ever replaced `.pico8/q2-pico8` on your card, reinstall the
playable ZIP.

## Install

1. Download **only** `q2-pico8.zip` from the
   [latest release](https://github.com/lampsonlnguyen/q2-pico8/releases/latest).
   Older releases use the filename `q2-pico8-playable-experimental.zip`.
   v0.4.0-alpha and v0.4.1-alpha do not start on the Q2 (see the
   CHANGELOG); use v0.4.2-alpha or later. Builds from unreleased commits are the
   **q2-pico8.zip** download in the Artifacts section of a successful
   [Q2 PICO-8 bridge run](https://github.com/lampsonlnguyen/q2-pico8/actions/workflows/fake08-port.yml).
2. Extract the ZIP to the root of the microSD card. When upgrading, replace
   everything in `.pico8` except your `carts`, `covers` and `cdata` folders.
3. Copy `.p8` or `.p8.png` carts into `/.pico8/carts/`, or use Get Games.
4. Open **Library → Games → PICO-8**.

If the app is missing, enable hidden files in your file manager and confirm
`.pico8/q2app` and `.pico8/q2-pico8` are directly under the card root's
`.pico8` folder. Reopen Library → Games after copying the files.

## If the app does not start

v0.4.0-alpha and v0.4.1-alpha crash before drawing anything on the Q2 (a
black screen, no `pico.log`): the firmware's loader does not run the
program's C++ global constructors, and v0.4.0 added globals that are used
at startup. Since v0.4.2-alpha the player runs them itself first thing in
`main()`.

If it still does not start, look at `.pico8/startup.txt` on the card:
each startup stage writes a line there, saved immediately, and also flashes
a coloured band on screen (1 red, 2 orange, 3 yellow, 4 green, 5 blue,
6 lavender, 7 Cover Flow). The last stage listed is where it stopped.
`.pico8/pico.log` has the rest of the detail once stage 4 is reached.

## Library

The library opens on Cover Flow. Each cart shows its own art: the picture of
a `.p8.png` cart, or the `__label__` of a `.p8` cart drawn on a cart body. A
PNG in `/.pico8/covers/` named after the cart file plus `.png` (for example
`celeste.p8.png.png`) replaces it; Get Games saves the itch.io cover there.

| Q2 control | Library |
| --- | --- |
| Previous / Next, swipe | browse |
| Centre, or tap the centre cover | play |
| Tap a side cover | select it |
| Play/Pause, or tap **⋯** | menu: Get Games, Sort (title or recently played), Refresh, Exit |
| Return | leave the app |

New carts copied to the card show up within a few seconds.

## Controls in a cart

The physical directions follow Pocket Boy. The capacitive wheel is ignored throughout PICO-8.

| Q2 control | PICO-8 |
| --- | --- |
| Return / Play/Pause / Previous / Next | Up / Down / Left / Right |
| Centre | X |
| Touch **O** and **X** | PICO-8 action buttons |
| Touch **≡** (top right) | Open Menu |
| Touch the pad icon (top left) | Show or hide O and X |
| Hold Return and Centre together (1.2 s) | Leave the app |

Menu pauses the cart. Use Previous and Next to move, Centre to select and Return
to go back, or tap a row. **Q2 Now Playing** opens the firmware's music screen
and Return brings you back. **Display and controls** shows the button mapping
and lets you show or hide O and X. **Audio** lets you mute the game or switch
between game sound and Q2 Music. **ROM Library** closes the cart and opens
Cover Flow. **Exit to Q2 Pod** closes the app. **Cartridge menu** opens
FAKE-08's pause menu, including Reset cart and cart-specific actions.

Touch O is the only O control. The pad icon and Menu remain available when O
and X are hidden. The library remembers the overlay setting when you return.

On firmware with `gameview`, Q2 Pod forwards all five physical buttons.
On older firmware q2-pico8 attempts to grab the GPIO key device, as Pocket Boy
does. If the grab fails, Return opens Menu and pressing it twice within half a
second exits. Play/Pause, Previous, Next and Centre still provide Down, Left,
Right and X. Q2 Now Playing and audio-source switching require firmware with
both `audio2` and `gameview`. After switching to or from older firmware,
re-enter Library → Games after the first launch so Q2 Pod rereads the audio mode.

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
- `src/q2_input.c`: the button and touch mapping, shared by the device and the host simulation.
- `src/library.cpp`, `src/coverflow.cpp`, `src/covers.cpp`, `src/ui.cpp`: the Cover Flow library, its projection, cart artwork and text drawing.
- `src/getgames.cpp`, `src/itch.c`, `src/itch-helper/`: Get Games, the helper process runner and the Go helper (`go test -mod=vendor` in that folder).
- `tests/audio.p8`: Original CC0 test cartridge that plays a sustained sine sfx; the native smoke test runs it to verify the synth produces non-silent audio. `tests/demo.p8` is the earlier silent frame-only cart.

## FAKE-08 integration (active engine; alpha release)

The port is inspired by FAKE-08's SDL1/FunKey S host but **does not use SDL**.

- `src/fake08_host.cpp`: implements the upstream `Host` interface, Q2 input translation and in-game menu, 2× framebuffer drawing, cartridge enumeration, frame timing and lifecycle, and the per-frame audio fill (see "Audio").
- `src/q2_platform.c`: ALSA audio for the glibc build, resolved at runtime with `dlopen("libasound.so.2")` (same pattern as q2-atari2600); the freestanding browser build has no audio device and its stubs are no-ops.
- `src/fake08_main.cpp`: runs the library, opens the real FAKE-08 `Vm` for the chosen cart, runs the VM loop and returns to the library when requested from the game menu.
- `tools/fake08-overlay.sh`: compiles a copy of upstream `vm.cpp` whose pause menu says "exit to library" and "exit to q2 pod". The upstream checkout is not modified.
- `src/q2_platform.c`: renders normal 16-color and FAKE-08's alternate 128–143 colors using the per-frame screen palette map.
- `.github/workflows/fake08-port.yml`: builds and tests the freestanding browser, links the real FAKE-08 engine, runs host gameplay/audio regression tests, cross-compiles the device ZIP, and publishes version-tagged releases. The FAKE-08 upstream source is pinned to `814991a2571ad3970e386cef48f3b148aa1c27b9`.

### Automated builds and GitHub releases

The [Q2 PICO-8 bridge workflow](https://github.com/lampsonlnguyen/q2-pico8/actions/workflows/fake08-port.yml) runs on pushes to `main`, pull requests, manual dispatches, and version tags (`v*`). It:

- Vets and tests the Get Games helper and syntax-checks the MIPS platform layer.
- Links the real FAKE-08 VM and runs the host Q2 simulation: cartridge loading, controls, rendering/frame pacing, and audio. A `q2-pico8-host-simulation-report` artifact contains the test report and summary.
- Builds the device MIPS executable with the firmware sysroot, checks its ELF/runtime ABI, runs a QEMU startup smoke test, and uploads `q2-pico8.zip` directly as an Actions artifact.
- **Only after both test/build jobs succeed for a new version on `main` (or an explicit version tag)**, creates a GitHub Release with **only** `q2-pico8.zip`. Tags containing `-alpha`, `-beta`, or `-rc` are published as prereleases.

To publish a **new tested version**, update the `VERSION` file
(e.g. `1.0.1`) and merge into `main`. GitHub Actions runs both
the real FAKE-08 host simulation and Q2 cross-build, then creates the
`v<version>` tag and GitHub Release with **only the playable ZIP** if
every check passes. Subsequent pushes with the same VERSION do not
publish duplicate releases. Explicit `v*` tags remain supported:

```sh
git switch main
git pull --ff-only
git tag v1.0.1
git push origin v1.0.1
```

Pull requests upload downloadable test artifacts without publishing releases.
An alpha release is not considered physically validated until checked on a
real Q2. A release fails closed if the gameplay tests or device build fail.

### Building the FAKE-08 MIPS player

The playable cross-build script compiles FAKE-08's interpreter and Lua
runtime, the Q2 host and the cartridge picker. The CI `device` job runs
this script and publishes `q2-pico8.zip` as a direct download;
`make player` runs it too.

```sh
git clone --recurse-submodules https://github.com/jtothebell/fake-08 ../fake-08
# Obtain a device-compatible MIPS C++17 toolchain and firmware-derived sysroot.
FAKE08_SRC=../fake-08 \
Q2_SYSROOT="$HOME/.cache/q2/sysroot" \
bash tools/build-fake08.sh
```

The experimental build produces `dist/q2-pico8.zip`
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

### Host Q2 simulation validation (FAKE-08 engine, simulated hardware)

`bash tests/host_q2_sim/run.sh` runs the real player natively on x86_64:
the library, Get Games, the FAKE-08 VM, the Q2 host and the input mapper,
with a host simulation of the framebuffer, a scripted input socket (raw Q2
key codes, wheel travel and touch), a WAV audio sink and a fake SD card
(see `tests/host_q2_sim/README.md`). CI runs it on every push. It checks:

- Library: button navigation and side-cover taps move the selection.
- Library → cart → Menu → ROM Library → back in the library.
- Get Games with a stand-in helper: list, download, the cart appears.
- Controls: the four directions, Centre as X, touch O and X, ignored wheel
  events, and the Return + Centre exit shortcut.
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

## Earlier Femto-8 investigation (historical only)

An early prototype targeted [femto8](https://github.com/benbaker76/femto8)
with a freestanding (libc-free) cart browser. It never became the
playable emulator and was removed in v0.4.2-alpha. The playable engine is
FAKE-08, built by `tools/build-fake08.sh`.

This project does not bundle third-party games. Cartridges remain subject
to their authors' licenses.

## Licensing

Q2 adapter: GPL-3.0-or-later. It includes code adapted from Pocket Boy
(GPL-2.0-or-later) and ships third-party notices listed in `NOTICE.md`.
No cartridge or PICO-8 proprietary assets are bundled.
