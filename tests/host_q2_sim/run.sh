#!/usr/bin/env bash
# Run the full host Q2 sim validation and collect evidence.
# No QEMU, no Q2 firmware, no device: the real player (library, Get Games,
# FAKE-08 VM, Q2 host, input mapper) runs natively on x86_64 through
# tests/host_q2_sim/q2_sim_platform.c.
# Usage: bash tests/host_q2_sim/run.sh [outbase]
#   outbase defaults to ~/workspace/q2-pico8-emu-test/host-sim
set -u
cd "$(dirname "$0")/../.."
OUTBASE="${1:-$HOME/workspace/q2-pico8-emu-test/host-sim}"
mkdir -p "$OUTBASE"
OUTBASE="$(cd "$OUTBASE" && pwd)"
SIMDIR="tests/host_q2_sim"
SD="$OUTBASE/sd"   # fake SD card: Q2_PICO8_DATA replaces /mnt/mmc/.pico8

reset_sd() {
    rm -rf "$SD"
    mkdir -p "$SD/carts"
    cp tests/audio.p8 tests/demo.p8 tests/input.p8 tests/update60.p8 tests/invalid.p8 "$SD/carts/"
    python3 tests/make_png_cart.py tests/input.p8 "$SD/carts/Blue Circle [itch 12345].p8.png" 29adff
    python3 tests/make_png_cart.py tests/demo.p8 "$SD/carts/orange.p8.png" ffa300
}

FAKE08_SRC="${FAKE08_SRC:-/tmp/fake-08}" bash "$SIMDIR/build.sh" || exit 1

fail=0
run() { # name script [VAR=value ...]; the run's output goes to $OUTBASE/name
    local name="$1" script="$2"; shift 2
    local out="$OUTBASE/$name"
    rm -rf "$out"
    mkdir -p "$out"
    echo "=== $name: script=$script $* ==="
    env Q2_PICO8_DATA="$SD" Q2_SIM_OUTDIR="$out" Q2_SIM_INPUT="$SIMDIR/scripts/$script" \
        Q2_PICO8_ITCH="$PWD/$SIMDIR/fake-itch.sh" "$@" \
        timeout 60 ./build/host-sim/q2-sim > "$out/run.log" 2>&1
    local rc=$?
    echo "$rc" > "$out/exit_code"
    # FAKE-08's Logger_Initialize reopens stderr as pico.log on the SD card.
    cp "$SD/pico.log" "$out/pico.log" 2>/dev/null || true
    echo "$name exit=$rc"
    return "$rc"
}
need() { # scenario pattern message
    grep -q -- "$2" "$OUTBASE/$1/pico.log" || { echo "FAIL: $3"; fail=1; }
}
refuse() {
    grep -q -- "$2" "$OUTBASE/$1/pico.log" && { echo "FAIL: $3"; fail=1; }
}

reset_sd

# --- 1. library: Cover Flow browsing, menu, Return leaves ---
run library library.txt || { echo "FAIL: library"; fail=1; }
need library "q2-pico8 startup: first framebuffer frame submitted" "startup frame never presented"
need library "library: 7 carts" "library did not list the 7 carts"
# A successful host-sim exit alone would miss a blank first frame.
# Check the actual first PNG so the black-screen regression is visible in CI.
python3 - "$OUTBASE/library/frames/frame_0001.png" <<'PY' || fail=1
from PIL import Image
import sys
frame = Image.open(sys.argv[1]).convert("RGB")
visible = sum(1 for r,g,b in frame.getdata() if r+g+b > 75)
if visible < 300:
    print(f"FAIL: startup framebuffer essentially black ({visible} visible pixels)")
    sys.exit(1)
print(f"ok   startup frame visibly painted ({visible} pixels)")
PY
need library "library: selected 1 " "Next did not move the selection"
need library "library: selected 2 demo.p8" "a tap on a side cover did not select it"
need library "library: exit" "Return did not leave the library"

# --- 2. library -> cart -> Menu via ROM Library -> leave ---
run play-return play-return.txt || { echo "FAIL: play-return"; fail=1; }
need play-return "library: launch input.p8" "Centre did not launch the selected cart"
need play-return "Start of GameLoop" "the cart did not run"
[ "$(grep -c 'library: [0-9]* carts' "$OUTBASE/play-return/pico.log")" -ge 2 ] \
    || { echo "FAIL: did not return to the library"; fail=1; }
need play-return "library: exit" "Return did not leave after the cart"

# --- 3. Get Games with a stand-in helper (no network) ---
run getgames getgames.txt || { echo "FAIL: getgames"; fail=1; }
[ -f "$SD/carts/Celeste Classic [itch 101].p8" ] || { echo "FAIL: Get Games did not add the cart"; fail=1; }
need getgames "library: added Celeste Classic \[itch 101\].p8" "the library did not pick up the new cart"

reset_sd
# --- 4-6. pacing + audio, 120 frames each, one cart without the library ---
run audio-30fps empty.txt Q2_PICO8_CART=audio.p8 Q2_SIM_MAX_FRAMES=120 || { echo "FAIL: audio-30fps"; fail=1; }
run demo-30fps empty.txt Q2_PICO8_CART=demo.p8 Q2_SIM_MAX_FRAMES=120 || { echo "FAIL: demo-30fps"; fail=1; }
run update60-60fps empty.txt Q2_PICO8_CART=update60.p8 Q2_SIM_MAX_FRAMES=120 || { echo "FAIL: update60-60fps"; fail=1; }
run png-cart empty.txt "Q2_PICO8_CART=Blue Circle [itch 12345].p8.png" Q2_SIM_MAX_FRAMES=30 \
    || { echo "FAIL: PNG cart (web-export layout) did not run"; fail=1; }
need png-cart "Start of GameLoop" "the PNG cart did not reach GameLoop"

# --- 7. controls: Pocket Boy layout, Return + Centre ends the app ---
run input-controls controls-full.txt Q2_PICO8_CART=input.p8 || { echo "FAIL: input-controls"; fail=1; }
python3 "$SIMDIR/check.py" full "$OUTBASE/input-controls" || fail=1

# --- 7b. touch overlay: O/X and hide/show ---
run input-overlay overlay.txt Q2_PICO8_CART=input.p8 || { echo "FAIL: input-overlay"; fail=1; }
python3 "$SIMDIR/check.py" overlay "$OUTBASE/input-overlay" || fail=1

# --- 8. controls: Q2 Pod 1.8 fallback, Back twice ends the app ---
run input-fallback controls-fallback.txt Q2_PICO8_CART=input.p8 Q2_SIM_FULL=0 \
    || { echo "FAIL: input-fallback"; fail=1; }
python3 "$SIMDIR/check.py" fallback "$OUTBASE/input-fallback" || fail=1

# --- 9. invalid carts must fail visibly ---
run invalid-missing empty.txt Q2_PICO8_CART=missing.p8 Q2_SIM_MAX_FRAMES=10; rc=$?
[ $rc -eq 2 ] || { echo "FAIL: missing cart exit=$rc, expected 2"; fail=1; }
# Broken Lua is caught before the VM starts: an error screen, never the
# BIOS menu as if it were the cart.
run invalid-corrupt empty.txt Q2_PICO8_CART=invalid.p8 Q2_SIM_MAX_FRAMES=10; rc=$?
[ $rc -eq 2 ] || { echo "FAIL: broken-Lua cart exit=$rc, expected 2"; fail=1; }
need invalid-corrupt "cart Lua error:" "broken-Lua cart did not report its Lua error"
need invalid-corrupt "syntax error" "broken-Lua cart error was not the syntax error"
python3 -c "import json,sys; s=json.load(open('$OUTBASE/invalid-corrupt/stats.json')); sys.exit(0 if s['frames_presented']>=1 else 1)" \
    || { echo "FAIL: broken-Lua cart showed no error screen"; fail=1; }
refuse invalid-corrupt "Loading cart __FAKE08-DEFAULT" "broken-Lua cart fell back to the BIOS menu"
refuse invalid-corrupt "Start of GameLoop" "broken-Lua cart entered GameLoop"

# --- analyse ---
python3 "$SIMDIR/analyze.py" "$OUTBASE" > /dev/null
cp "$OUTBASE/report.md" "$OUTBASE/summary.json" build/host-sim/ 2>/dev/null || true
echo "Evidence in $OUTBASE"
[ "$fail" -eq 0 ] && echo "HOST-SIM: ALL EXPECTED CHECKS PASSED" || echo "HOST-SIM: FAILURES (see above)"
exit "$fail"
