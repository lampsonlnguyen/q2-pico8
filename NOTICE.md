# Third-party code in the Q2 PICO-8 player

The Q2 adapter is GPL-3.0-or-later (see `LICENSE`). It combines:

| Component | Used for | License |
| --- | --- | --- |
| [FAKE-08](https://github.com/jtothebell/fake-08) (pinned `814991a`) | PICO-8 VM, Lua, synth, PNG decoding; built from upstream source, with the pause menu's two exit labels changed by `tools/fake08-overlay.sh` | MIT, plus the notices in its `LICENSE.MD` (shipped as `FAKE-08-LICENSE.md`) |
| [Pocket Boy / q2-boy](https://github.com/SlayTheEarth/q2-boy) | Button layout and GPIO grab, Cover Flow projection (`src/coverflow.cpp`), text drawing (`src/ui.cpp`), the itch.io helper and its process runner (`src/itch-helper`, `src/itch.c`) | GPL-2.0-or-later |
| Liberation Sans Regular, as Pocket Boy's generated masks (`src/ui_font.h`) | UI text | SIL Open Font License 1.1 (`src/FONT-LICENSE`) |
| NextUI-Itchio-Pak (Christophe Vanlancker) | itch.io guest download flow, via Pocket Boy | MIT (`src/itch-helper/NextUI-Itchio-Pak-LICENSE`) |
| Go standard library, `golang.org/x/net`, `golang.org/x/image`, `github.com/skip2/go-qrcode` | The `q2-pico8-itch` helper (vendored in `src/itch-helper/vendor`) | BSD-3-Clause, BSD-3-Clause, BSD-3-Clause, MIT |
| [Lucide gamepad-2](https://lucide.dev/icons/gamepad-2) | App icon, rasterized with a colored background | ISC (`assets/Lucide-ISC.txt`) |
| Root certificates (`itch-ca.pem` in the package) | HTTPS to itch.io | Mozilla's CA bundle as shipped by the build host's `ca-certificates` |

No cartridges or PICO-8 proprietary assets are bundled. Carts you download
with Get Games remain subject to their authors' licenses.
