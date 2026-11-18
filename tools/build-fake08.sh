#!/usr/bin/env bash
# Experimental whole-engine Q2 build. Run from the repository root:
#   FAKE08_SRC=../fake-08 Q2_SYSROOT=$HOME/.cache/q2/sysroot \
#     bash tools/build-fake08.sh
#
# Requires a MIPS C++17 cross-toolchain compatible with the Q2's NaN2008 ABI.
# It deliberately does not alter the proven freestanding preview Makefile.
set -euo pipefail
cd "$(dirname "$0")/.."
src="${FAKE08_SRC:-../fake-08}"
sysroot="${Q2_SYSROOT:-$HOME/.cache/q2/sysroot}"
cxx="${Q2_CXX:-mipsel-linux-gnu-g++}"
cc="${Q2_CC:-mipsel-linux-gnu-gcc}"
[ -f "$src/source/vm.cpp" ] || { echo "Missing FAKE-08 sources in $src" >&2; exit 2; }
[ -f "$src/libs/z8lua/lua.h" ] || { echo "Run git submodule update --init in $src" >&2; exit 2; }
[ -f "$sysroot/lib/libc.so.6" ] || { echo "Missing Q2 glibc sysroot at $sysroot" >&2; exit 2; }
command -v "$cxx" >/dev/null
command -v "$cc" >/dev/null

out=build/fake08
mkdir -p "$out" dist/.pico8
includes=(-Isrc "-I$src/source" "-I$src/libs/z8lua"
          "-I$src/libs/miniz" "-I$src/libs/lodepng"
          "-I$src/libs/simpleini")
flags=(-O2 -march=mips32r2 -mabi=32 -mfp64 -mnan=2008
       -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections
       "--sysroot=$sysroot")
objects=()
compile_cpp(){
    local input="$1" name
    name="$(echo "$input" | tr '/.' '__')"
    "$cxx" "${flags[@]}" "${includes[@]}" -std=gnu++17 -c "$input" -o "$out/$name.o"
    objects+=("$out/$name.o")
}
compile_c(){
    local input="$1" name
    name="$(echo "$input" | tr '/.' '__')"
    "$cc" "${flags[@]}" "${includes[@]}" -std=gnu11 -c "$input" -o "$out/$name.o"
    objects+=("$out/$name.o")
}
# The native Q2 runtime uses glibc's main(), not the standalone browser's
# custom __start. The upstream main.cpp and SDL hosts must not be linked.
for file in "$src/source/"*.cpp; do
    [ "${file##*/}" = main.cpp ] && continue
    compile_cpp "$file"
done
for file in "$src/libs/lodepng/"*.cpp; do compile_cpp "$file"; done
for file in "$src/libs/z8lua/"*.c; do
    case "${file##*/}" in lua.c|luac.c|ltests.c) continue;; esac
    compile_c "$file"
done
for file in "$src/libs/miniz/"*.c "$src/libs/simpleini/"*.c; do
    [ -f "$file" ] && compile_c "$file"
done
compile_c src/q2_platform.c
compile_c src/cart_browser.c
# Recompile Q2 bridge units with Q2_GLIBC so they use the standard
# C/C++ startup rather than the freestanding preview's __start symbol.
# The two q2_*.c files were already compiled above; remove and redo them.
objects=("${objects[@]:0:${#objects[@]}-2}")
for file in src/q2_platform.c src/cart_browser.c; do
    name="$(echo "$file" | tr '/.' '__')"
    "$cc" "${flags[@]}" "${includes[@]}" -DQ2_GLIBC -std=gnu11 -c "$file" -o "$out/$name.o"
    objects+=("$out/$name.o")
done
"$cxx" "${flags[@]}" "${includes[@]}" -DQ2_GLIBC -std=gnu++17 \
    -c src/fake08_host.cpp -o "$out/fake08_host.o"
objects+=("$out/fake08_host.o")
"$cxx" "${flags[@]}" "${includes[@]}" -DQ2_GLIBC -std=gnu++17 \
    -c src/fake08_main.cpp -o "$out/fake08_main.o"
objects+=("$out/fake08_main.o")
"$cxx" "${flags[@]}" -Wl,--gc-sections -o dist/.pico8/q2-pico8 "${objects[@]}" -lm -lpthread
file dist/.pico8/q2-pico8
echo "Experimental FAKE-08 binary linked; Q2 hardware compatibility is NOT verified."
