# Changelog

## v1.0.1

- The playable build downloads as a single `q2-pico8.zip` from GitHub Actions and Releases.
- Physical directions follow Pocket Boy. Centre maps to X, the capacitive wheel is ignored, and touch O and X remain available.
- The in-game menu adds Q2 Now Playing, Display and controls, Audio, ROM Library, and Exit to Q2 Pod.

## v0.4.2-alpha — starts on the Q2 again

- **Fixes the black screen with no `pico.log` in v0.4.0/v0.4.1:** the Q2
  firmware's glibc 2.28 loader does not run the executable's
  `.init_array`, so C++ globals added with the library were never
  constructed and the app crashed in `app::set_exe` before drawing (#6).
  `main()` now runs the init array itself, once.
- **Touch controls laid out like a handheld:** a D-pad cross (corners
  press diagonals) on the left, O and X on the right, pause at the top
  right, and a pad icon at the top left that hides or shows them; also in
  the library menu as **Touch controls**, remembered on the card (#7).
- **Startup diagnostics (#8):** the display opens before FAKE-08's logger
  or any library work (v0.3.0's order), the logger's stdout echo goes to
  `/dev/null`, and each startup stage flashes a coloured band and writes an
  fsync'd line to `.pico8/startup.txt` (latest launch only): 1 red main
  entered, 2 orange stdout and paths, 3 yellow display open (dark purple:
  failed), 4 green `pico.log` open, 5 blue first frame, 6 lavender library,
  7 first Cover Flow frame.
- **One build:** the old cart-list-only browser preview, which installed
  over the player's executable, is gone, with the Femto-8 prototype files.

## v0.4.1-alpha — black-screen recovery and startup diagnostics

- **Install correction:** v0.4.0-alpha accidentally published the browser-preview
  ZIP alongside the playable ZIP even though both overwrite the same
  `.pico8/q2-pico8` executable. The release now ships only the playable ZIP;
  the browser preview remains available as a CI-only artifact.
- **Visible startup:** the player presents a PICO-8 loading frame before
  entering Cover Flow, rather than leaving the display blank while the
  library and covers are initialized.
- **Diagnostic logging:** `.pico8/pico.log` now records framebuffer and
  input setup stages and precise startup failures.
- **Failure signaling:** missing or unsupported `/dev/fb0` now returns a
  nonzero exit code on the Q2 instead of silently succeeding. Headless
  QEMU CI explicitly opts in to the no-framebuffer test path.
- **Regression test:** the host simulator checks that the first frame is
  visibly nonblack before testing game loading and input.

**Physical-device status:** this is a testable fix candidate, not a verified
hardware resolution. The available CI cannot inspect a physical Shanling
Q2's framebuffer. If startup still fails, preserve and share
`.pico8/pico.log` so the remaining failure can be identified.

### Install

Download **only** `q2-pico8-playable-experimental.zip` from
[v0.4.1-alpha](https://github.com/lampsonlnguyen/q2-pico8/releases/tag/v0.4.1-alpha),
extract it to the root of the SD card, and replace the old
`.pico8/q2-pico8` and `.pico8/q2app` files. Keep your carts, covers and
game saves.

## v0.3.0-alpha

### Fixes the cart-load crash in v0.2.0-alpha

v0.2.0-alpha crashed on every cart ("It couldn't start", exit 127). The build had linked the dynamic loader `ld-linux-mipsn8.so.1` as a `DT_NEEDED` library, so glibc 2.28's ld.so minimal `malloc`/`realloc` took precedence over libc's and aborted inside the FAKE-08 VM constructor. The loader is now removed from `DT_NEEDED` after linking (`patchelf --remove-needed`). `PT_INTERP` and the NaN2008 flags are unchanged. CI now fails if the loader ever shows up as `DT_NEEDED` again (#2).

Also:
- CI builds this ZIP and attaches it to each published release (#3).
- The README install steps now point to Releases.

### Install
Extract `q2-pico8-playable-experimental.zip` to the root of the microSD card, copy `.p8` / `.p8.png` carts into `/.pico8/carts/`, then open **Library → Games → PICO-8**. When upgrading, replace `.pico8/q2app` and `.pico8/q2-pico8` and keep your carts.

### Tested
- Q2 Pod 1.9.0 emulator (QEMU): carts load and animate at full speed, wheel and touch controls work. The emulator has no sound card, so audio was not checked there.
- CI: `make check`, native FAKE-08 link test, MIPS cross-build and `qemu-mipsel` startup smoke test.

**Not yet tested on real Q2 hardware**, including audio output.
