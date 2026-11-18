#!/usr/bin/env bash
# CI only: compile and link the actual FAKE-08 VM/Lua engine plus our Q2
# adapter using host-side syscall stubs. Does NOT prove device ABI support.
set -euo pipefail
cd "$(dirname "$0")/.."
src="${FAKE08_SRC:-/tmp/fake-08}"
[ -f "$src/source/vm.cpp" ] || { echo "Missing FAKE-08 source"; exit 2; }
[ -f "$src/libs/z8lua/lua.h" ] || { echo "Missing FAKE-08 z8lua submodule"; exit 2; }
mkdir -p build/fake08-link
includes=(-Isrc "-I$src/source" "-I$src/libs/z8lua"
          "-I$src/libs/miniz" "-I$src/libs/lodepng" "-I$src/libs/simpleini")
objects=()
for file in "$src/source/"*.cpp "$src/libs/lodepng/"*.cpp \
            src/fake08_host.cpp src/fake08_main.cpp; do
    [ "${file##*/}" = main.cpp ] && continue
    out="build/fake08-link/$(echo "$file" | tr '/.' '__').o"
    g++ -O0 -std=gnu++17 -fno-rtti -fno-exceptions -DQ2_GLIBC \
        "${includes[@]}" -c "$file" -o "$out"
    objects+=("$out")
done
# z8lua is a C++-compiled Lua fork despite its .c filenames.
for file in "$src/libs/z8lua/"*.c "$src/libs/miniz/"*.c \
            "$src/libs/simpleini/"*.c; do
    [ -f "$file" ] || continue
    case "${file##*/}" in lua.c|luac.c|ltests.c) continue;; esac
    out="build/fake08-link/$(echo "$file" | tr '/.' '__').o"
    g++ -O0 -std=gnu++17 -fno-exceptions -DQ2_GLIBC "${includes[@]}" -c "$file" -o "$out"
    objects+=("$out")
done
for file in src/cart_browser.c tests/q2_fake08_stub.c; do
    out="build/fake08-link/$(echo "$file" | tr '/.' '__').o"
    gcc -O0 -std=gnu11 -DQ2_GLIBC "${includes[@]}" -c "$file" -o "$out"
    objects+=("$out")
done
g++ -o build/fake08-link/fake08-q2-linux-smoke "${objects[@]}" -lm -pthread
./build/fake08-link/fake08-q2-linux-smoke
echo "Full FAKE-08 engine linked against Q2 host ABI successfully (host stub)."
