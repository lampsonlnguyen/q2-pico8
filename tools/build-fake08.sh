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
sysroot="${Q2_SYSROOT:-${SYSROOT:-$HOME/.cache/q2/sysroot}}"
cxx="${Q2_CXX:-mipsel-linux-gnu-g++}"
cc="${Q2_CC:-mipsel-linux-gnu-gcc}"
[ -f "$src/source/vm.cpp" ] || { echo "Missing FAKE-08 sources in $src" >&2; exit 2; }
[ -f "$src/libs/z8lua/lua.h" ] || { echo "Run git submodule update --init in $src" >&2; exit 2; }
for library in libc.so.6 ld-linux-mipsn8.so.1; do
    if [ ! -f "$sysroot/lib/$library" ]; then
        echo "Missing $sysroot/lib/$library. Set Q2_SYSROOT or SYSROOT to your Q2 firmware sysroot." >&2
        exit 2
    fi
done
command -v "$cxx" >/dev/null
command -v "$cc" >/dev/null

out=build/fake08
stage=dist/player
binary="$stage/.pico8/q2-pico8"
mkdir -p "$out" "$stage/.pico8/carts"
includes=(-Isrc "-I$src/source" "-I$src/libs/z8lua"
          "-I$src/libs/miniz" "-I$src/libs/lodepng"
          "-I$src/libs/simpleini")
# The firmware's glibc is 2.28. GCC11 libstdc++ from Ubuntu Noble needs
# glibc 2.34+ (__clock_gettime64, __libc_single_threaded, ...).
# Use Debian Buster's GCC8 C++ headers/static runtime, compatible with 2.28.
old_cxx=()
old_lib=()
if [ -n "${Q2_GCC8_ROOT:-}" ]; then
    test -f "$Q2_GCC8_ROOT/usr/lib/gcc/mipsel-linux-gnu/8/libstdc++.a"
    old_cxx=(-nostdinc++ -isystem "$Q2_GCC8_ROOT/usr/include/c++/8"
             -isystem "$Q2_GCC8_ROOT/usr/include/mipsel-linux-gnu/c++/8")
    old_lib=(-L"$Q2_GCC8_ROOT/usr/lib/gcc/mipsel-linux-gnu/8")
fi
# GCC cross packages bring headers from modern glibc; those refer to
# __time64 and __isoc23_* missing from the Q2's glibc 2.28. Override the
# entire C header search rather than just the linker's library search.
gcc_include="$("$cc" -print-file-name=include)"
flags=(-O2 -march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008
       -fno-rtti -ffunction-sections -fdata-sections
       "--sysroot=$sysroot" -nostdinc)
sysincludes=(-isystem "$gcc_include"
             -isystem "$sysroot/usr/include"
             -isystem "$sysroot/usr/include/mipsel-linux-gnu")
objects=()
compile_cpp(){
    local input="$1" name
    name="$(echo "$input" | tr '/.' '__')"
    "$cxx" "${flags[@]}" "${old_cxx[@]}" "${sysincludes[@]}" "${includes[@]}" -std=gnu++17 -c "$input" -o "$out/$name.o"
    objects+=("$out/$name.o")
}
compile_c(){
    local input="$1" name
    name="$(echo "$input" | tr '/.' '__')"
    "$cc" "${flags[@]}" "${sysincludes[@]}" "${includes[@]}" -std=gnu11 -c "$input" -o "$out/$name.o"
    objects+=("$out/$name.o")
}
# The native Q2 runtime uses glibc's main(), not the standalone browser's
# custom __start. The upstream main.cpp and SDL hosts must not be linked.
# vm.cpp comes from the Q2 overlay (pause-menu labels; see the script).
overlay="$(bash tools/fake08-overlay.sh "$src" "$out/fake08-overlay")"
for file in "$src/source/"*.cpp; do
    [ "${file##*/}" = main.cpp ] && continue
    [ "${file##*/}" = vm.cpp ] && file="$overlay/vm.cpp"
    compile_cpp "$file"
done
for file in "$src/libs/lodepng/"*.cpp; do compile_cpp "$file"; done
for file in "$src/libs/z8lua/"*.c "$src/libs/miniz/"*.c \
            "$src/libs/simpleini/"*.c; do
    [ -f "$file" ] || continue
    case "${file##*/}" in lua.c|luac.c|ltests.c) continue;; esac
    # Upstream compiles these .c sources in C++ mode.
    compile_cpp "$file"
done
# Q2_GLIBC suppresses the freestanding browser's custom __start.
for file in src/q2_platform.c src/q2_input.c src/itch.c; do
    name="$(echo "$file" | tr '/.' '__')"
    "$cc" "${flags[@]}" "${sysincludes[@]}" "${includes[@]}" -DQ2_GLIBC -std=gnu11 -c "$file" -o "$out/$name.o"
    objects+=("$out/$name.o")
done
for file in src/fake08_host.cpp src/fake08_main.cpp src/app.cpp src/ui.cpp src/covers.cpp \
            src/coverflow.cpp src/library.cpp src/getgames.cpp; do
    name="$(echo "$file" | tr '/.' '__')"
    "$cxx" "${flags[@]}" "${old_cxx[@]}" "${sysincludes[@]}" "${includes[@]}" -DQ2_GLIBC -std=gnu++17 \
        -c "$file" -o "$out/$name.o"
    objects+=("$out/$name.o")
done
# -ldl: q2_platform.c resolves ALSA at runtime with dlopen; the firmware's
# glibc 2.28 keeps dlopen in libdl.so.2 (unlike glibc 2.34+, where it
# moved into libc).
"$cxx" "${flags[@]}" "${old_lib[@]}" -static-libstdc++ -Wl,--as-needed -Wl,--no-warn-mismatch -Wl,--gc-sections -L"$sysroot/lib" -o "$binary" "${objects[@]}" -lm -lpthread -ldl \
    -Wl,--no-as-needed -l:ld-linux-mipsn8.so.1
# The static libstdc++ references __tls_get_addr, which is defined only in
# ld.so, so the loader must be on the link line — but it must NOT stay a
# DT_NEEDED entry. As a NEEDED, ld.so is loaded as a regular shared object
# on top of its PT_INTERP role, so its dl-minimal malloc/realloc interpose
# ahead of libc's and the first realloc of a non-last block aborts
# (dl-minimal.c: realloc: Assertion `ptr == alloc_last_block' failed) in
# the FAKE-08 Vm constructor, exit 127 ("It couldn't start"). Strip the
# entry back out: the __tls_get_addr reference stays undefined in the
# binary and resolves at runtime from the interpreter, exactly like
# libc.so.6's own undefined reference to it. PT_INTERP is untouched.
command -v patchelf >/dev/null || { echo "patchelf is required (apt install patchelf)" >&2; exit 2; }
patchelf --remove-needed ld-linux-mipsn8.so.1 "$binary"
# GCC's legacy-NaN startup archive can overwrite ELF NaN2008 e_flags.
# This is an experimental ABI bridge, not proof of mixed-lib correctness.
python3 tools/mark-mips-nan2008.py "$binary"
file "$binary"
sed 's/^name=.*/name=PICO-8/' q2app > "$stage/.pico8/q2app"
: > "$stage/.pico8/carts/.keep"
mkdir -p "$stage/.pico8/licenses"
cp "$src/LICENSE.MD" "$stage/.pico8/FAKE-08-LICENSE.md"
cp src/FONT-LICENSE "$stage/.pico8/licenses/Liberation-Sans-OFL.txt"
cp NOTICE.md "$stage/.pico8/licenses/NOTICE.md"
members=(.pico8/q2app .pico8/q2-pico8 .pico8/carts/.keep .pico8/FAKE-08-LICENSE.md
         .pico8/licenses/Liberation-Sans-OFL.txt .pico8/licenses/NOTICE.md)

# itch.io helper for Get Games (Go, cross-built for the Q2's MIPS soft-float
# user space) and the root certificates it verifies itch.io with.
if bash tools/build-itch.sh "$stage/.pico8/q2-pico8-itch"; then
    certs="${Q2_CA_BUNDLE:-/etc/ssl/certs/ca-certificates.crt}"
    cp "$certs" "$stage/.pico8/itch-ca.pem"
    helper=src/itch-helper
    cp "$helper/NextUI-Itchio-Pak-LICENSE" "$stage/.pico8/licenses/NextUI-Itchio-Pak.txt"
    cp "$helper/vendor/github.com/skip2/go-qrcode/LICENSE" "$stage/.pico8/licenses/go-qrcode.txt"
    cp "$helper/vendor/golang.org/x/net/LICENSE" "$stage/.pico8/licenses/golang.org-x-net.txt"
    cp "$helper/vendor/golang.org/x/image/LICENSE" "$stage/.pico8/licenses/golang.org-x-image.txt"
    cp "$("${Q2_GO:-go}" env GOROOT)/LICENSE" "$stage/.pico8/licenses/Go.txt"
    members+=(.pico8/q2-pico8-itch .pico8/itch-ca.pem .pico8/licenses/NextUI-Itchio-Pak.txt
              .pico8/licenses/go-qrcode.txt .pico8/licenses/golang.org-x-net.txt
              .pico8/licenses/golang.org-x-image.txt .pico8/licenses/Go.txt)
elif [ "${Q2_REQUIRE_ITCH:-0}" = 1 ]; then
    echo "The itch.io helper did not build" >&2
    exit 1
else
    echo "warning: no Go toolchain: the package has no Get Games helper" >&2
fi
(
    cd "$stage"
    rm -f ../q2-pico8-playable-experimental.zip
    zip -q -FS ../q2-pico8-playable-experimental.zip "${members[@]}"
)
echo "Created dist/q2-pico8-playable-experimental.zip"
