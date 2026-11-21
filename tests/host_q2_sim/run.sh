#!/usr/bin/env bash
# Run the full host Q2 sim validation and collect evidence.
# No QEMU, no Q2 firmware, no device: the real FAKE-08 VM + Q2 host
# run natively on x86_64 through tests/host_q2_sim/q2_sim_platform.c.
# Usage: bash tests/host_q2_sim/run.sh [outbase]
#   outbase defaults to ~/workspace/q2-pico8-emu-test/host-sim
set -u
cd "$(dirname "$0")/../.."
OUTBASE="${1:-$HOME/workspace/q2-pico8-emu-test/host-sim}"
mkdir -p "$OUTBASE"
SIMDIR="tests/host_q2_sim"

# --- fake SD card at the real device path (host dir, not firmware) ---
mkdir -p /mnt/mmc/.pico8/carts /mnt/mmc/.pico8/cdata
cp tests/audio.p8 tests/demo.p8 tests/input.p8 tests/update60.p8 tests/invalid.p8 \
   /mnt/mmc/.pico8/carts/
ls /mnt/mmc/.pico8/carts/

# --- build ---
FAKE08_SRC="${FAKE08_SRC:-/tmp/fake-08}" bash "$SIMDIR/build.sh"

fail=0
run_player() { # name cart max_frames script
    local name="$1" cart="$2" frames="$3" script="$4"
    local out="$OUTBASE/$name"
    mkdir -p "$out"
    echo "=== $name: cart=$cart frames=$frames script=$script ==="
    Q2_SIM_CART="$cart" Q2_SIM_MAX_FRAMES="$frames" \
    Q2_SIM_OUTDIR="$out" Q2_SIM_INPUT="$SIMDIR/scripts/$script" \
    ./build/host-sim/q2-sim 2>&1 | tee "$out/run.log"
    local rc=${PIPESTATUS[0]}
    echo "$name exit=$rc" | tee -a "$out/run.log"
    echo "$rc" > "$out/exit_code"
    return "$rc"
}

# --- 1. browser picker: wheel +1, then Centre ---
mkdir -p "$OUTBASE/browser"
Q2_SIM_OUTDIR="$OUTBASE/browser" Q2_SIM_INPUT="$SIMDIR/scripts/browser.txt" \
    ./build/host-sim/browser-sim > "$OUTBASE/browser/browser.log" 2>&1
brc=$?
cat "$OUTBASE/browser/browser.log"
if [ $brc -ne 0 ]; then echo "FAIL: browser picker"; fail=1; fi
grep -q "chosen=1 path=/mnt/mmc/.pico8/carts/" "$OUTBASE/browser/browser.log" \
    || { echo "FAIL: browser did not choose a cart"; fail=1; }

# --- 2-4. pacing + audio, 120 frames each ---
run_player audio-30fps audio.p8 120 empty.txt; [ $? -eq 0 ] || { echo "FAIL: audio-30fps"; fail=1; }
run_player demo-30fps demo.p8 120 empty.txt; [ $? -eq 0 ] || { echo "FAIL: demo-30fps"; fail=1; }
run_player update60-60fps update60.p8 120 empty.txt; [ $? -eq 0 ] || { echo "FAIL: update60-60fps"; fail=1; }

# --- 5. controls: scripted keys/touch/pause/double-Back, 600-frame cap ---
run_player input-controls input.p8 600 controls.txt; [ $? -eq 0 ] || { echo "FAIL: input-controls"; fail=1; }
grep -q '"exit_reason": "double-back"' "$OUTBASE/input-controls/stats.json" \
    || { echo "FAIL: input-controls did not exit via double-Back"; fail=1; }

# --- 6. invalid carts must fail visibly ---
run_player invalid-missing missing.p8 10 empty.txt; rc=$?
[ $rc -eq 2 ] || { echo "FAIL: missing cart exit=$rc, expected 2"; fail=1; }
run_player invalid-corrupt invalid.p8 10 empty.txt; rc=$?
echo "invalid-corrupt exit=$rc (recorded; see run.log for the VM's error handling)"

# --- analyse ---
python3 "$SIMDIR/analyze.py" "$OUTBASE"
cp "$OUTBASE/report.md" "$OUTBASE/summary.json" build/host-sim/ 2>/dev/null || true
echo "Evidence in $OUTBASE"
[ "$fail" -eq 0 ] && echo "HOST-SIM: ALL EXPECTED CHECKS PASSED" || echo "HOST-SIM: FAILURES (see above)"
exit "$fail"
