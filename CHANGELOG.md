# Changelog

# v0.3.0-alpha

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
