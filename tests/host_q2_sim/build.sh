#!/usr/bin/env bash
# Build the host Q2 simulation: the REAL player (src/fake08_main.cpp with
# the Cover Flow library, Get Games and src/fake08_host.cpp), the real
# input mapper (src/q2_input.c) and the real FAKE-08 VM, linked against the
# host simulation platform (tests/host_q2_sim/q2_sim_platform.c) instead
# of the MIPS q2_platform.c. Host-only; device builds untouched.
# Usage: FAKE08_SRC=/tmp/fake-08 bash tests/host_q2_sim/build.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
src="${FAKE08_SRC:-/tmp/fake-08}"
[ -f "$src/source/vm.cpp" ] || { echo "Missing FAKE-08 source at $src"; exit 2; }
[ -f "$src/libs/z8lua/lua.h" ] || { echo "Missing FAKE-08 z8lua submodule"; exit 2; }
out=build/host-sim
mkdir -p "$out"
includes=(-Isrc "-I$src/source" "-I$src/libs/z8lua"
          "-I$src/libs/miniz" "-I$src/libs/lodepng" "-I$src/libs/simpleini")
objects=()
compile() { # compiler file [flags...]
    local cc="$1" file="$2"; shift 2
    local obj="$out/$(echo "$file" | tr '/.' '__').o"
    if [ ! "$obj" -nt "$file" ] || [ "${Q2_SIM_REBUILD:-0}" = 1 ]; then
        "$cc" -O1 -DQ2_GLIBC -DQ2_HOST_SIM "${includes[@]}" "$@" -c "$file" -o "$obj"
    fi
    objects+=("$obj")
}
overlay="$(bash tools/fake08-overlay.sh "$src" "$out/fake08-overlay")"
for file in "$src/source/"*.cpp "$src/libs/lodepng/"*.cpp; do
    [ "${file##*/}" = main.cpp ] && continue
    [ "${file##*/}" = vm.cpp ] && file="$overlay/vm.cpp"
    compile g++ "$file" -std=gnu++17 -fno-rtti
done
for file in "$src/libs/z8lua/"*.c "$src/libs/miniz/"*.c "$src/libs/simpleini/"*.c; do
    [ -f "$file" ] || continue
    case "${file##*/}" in lua.c|luac.c|ltests.c) continue;; esac
    compile g++ "$file" -std=gnu++17
done
# Our sources are always rebuilt: they are small and change often.
Q2_SIM_REBUILD=1
for file in src/fake08_host.cpp src/fake08_main.cpp src/app.cpp src/ui.cpp src/covers.cpp \
            src/coverflow.cpp src/library.cpp src/getgames.cpp; do
    compile g++ "$file" -std=gnu++17 -fno-rtti -Wall -Wextra -Werror
done
for file in src/q2_input.c src/itch.c; do
    compile gcc "$file" -std=gnu11 -Wall -Wextra -Werror
done
compile gcc tests/host_q2_sim/q2_sim_platform.c -std=gnu11
g++ -o "$out/q2-sim" "${objects[@]}" -lm -pthread -lpng
echo "Built $out/q2-sim"
