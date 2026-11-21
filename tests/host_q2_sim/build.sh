#!/usr/bin/env bash
# Build the host Q2 simulation harness: the real FAKE-08 VM + Q2 host
# (src/fake08_host.cpp) + the real src/cart_browser.c, linked against
# the host simulation platform (tests/host_q2_sim/q2_sim_platform.c)
# instead of the MIPS q2_platform.c. Host-only; device builds untouched.
# Usage: FAKE08_SRC=/tmp/fake-08 bash tests/host_q2_sim/build.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
src="${FAKE08_SRC:-/tmp/fake-08}"
[ -f "$src/source/vm.cpp" ] || { echo "Missing FAKE-08 source at $src"; exit 2; }
[ -f "$src/libs/z8lua/lua.h" ] || { echo "Missing FAKE-08 z8lua submodule"; exit 2; }
mkdir -p build/host-sim
includes=(-Isrc "-I$src/source" "-I$src/libs/z8lua"
          "-I$src/libs/miniz" "-I$src/libs/lodepng" "-I$src/libs/simpleini")
objects=()
for file in "$src/source/"*.cpp "$src/libs/lodepng/"*.cpp \
            src/fake08_host.cpp tests/host_q2_sim/sim_main.cpp; do
    [ "${file##*/}" = main.cpp ] && continue
    out="build/host-sim/$(echo "$file" | tr '/.' '__').o"
    g++ -O1 -std=gnu++17 -fno-rtti -DQ2_GLIBC -DQ2_HOST_SIM \
        "${includes[@]}" -c "$file" -o "$out"
    objects+=("$out")
done
for file in "$src/libs/z8lua/"*.c "$src/libs/miniz/"*.c \
            "$src/libs/simpleini/"*.c; do
    [ -f "$file" ] || continue
    case "${file##*/}" in lua.c|luac.c|ltests.c) continue;; esac
    out="build/host-sim/$(echo "$file" | tr '/.' '__').o"
    g++ -O1 -std=gnu++17 -DQ2_GLIBC -DQ2_HOST_SIM "${includes[@]}" -c "$file" -o "$out"
    objects+=("$out")
done
# Real cart browser (uses the sim platform's sc() getdents shim) and
# the host simulation platform itself.
for file in src/cart_browser.c tests/host_q2_sim/q2_sim_platform.c; do
    out="build/host-sim/$(echo "$file" | tr '/.' '__').o"
    gcc -O1 -std=gnu11 -DQ2_GLIBC -DQ2_HOST_SIM "${includes[@]}" -c "$file" -o "$out"
    objects+=("$out")
done
g++ -o build/host-sim/q2-sim "${objects[@]}" -lm -pthread -lpng
# Browser picker driver (same platform + real cart browser, no VM).
gcc -O1 -std=gnu11 -DQ2_GLIBC -DQ2_HOST_SIM -Isrc \
    tests/host_q2_sim/browser_sim.c src/cart_browser.c \
    tests/host_q2_sim/q2_sim_platform.c -o build/host-sim/browser-sim -lpng -lm
echo "Built build/host-sim/q2-sim and build/host-sim/browser-sim"
