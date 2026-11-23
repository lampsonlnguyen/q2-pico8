#!/usr/bin/env bash
# Q2 build of the real FAKE-08 player with clang + ld.lld + a static libc++,
# every object nan2008/FR=1 (no GCC legacy-NaN runtime, no ELF flag patching).
# Dynamic only against the Q2's own libc, libm, libpthread, libdl, libgcc_s, libatomic.
#   FAKE08_SRC=../fake-08 Q2_SYSROOT=... Q2_LIBCXX=... bash tools/build-fake08-clang.sh
set -euo pipefail
cd "$(dirname "$0")/.."
src="${FAKE08_SRC:-../fake-08}"
sysroot="${Q2_SYSROOT:?Q2 sysroot with glibc 2.28 headers and the Q2 libraries in lib/}"
cxx="${Q2_LIBCXX:?libc++ install prefix built for mipsel nan2008}"
out=build/fake08-clang; stage=dist/player
mkdir -p "$out" "$stage/.pico8/carts"
flags=(${Q2_EXTRA_CFLAGS:-} --target=mipsel-linux-gnu "--sysroot=$sysroot" -march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008
       -D_GNU_SOURCE -O2 -ffunction-sections -fdata-sections -G0)
cxxflags=(-std=gnu++17 -nostdinc++ -isystem "$cxx/include/c++/v1" -fno-rtti -w)
inc=(-Isrc "-I$src/source" "-I$src/libs/z8lua" "-I$src/libs/miniz" "-I$src/libs/lodepng" "-I$src/libs/simpleini")
objs=()
cc1(){ local f=$1 o="$out/$(echo "$1" | tr '/.' '__').o"; shift
       [ "$o" -nt "$f" ] || "$@" -c "$f" -o "$o"; objs+=("$o"); }
overlay="$(bash tools/fake08-overlay.sh "$src" "$out/fake08-overlay")"   # pause-menu labels
for f in "$src"/source/*.cpp; do
    [ "${f##*/}" = main.cpp ] && continue
    [ "${f##*/}" = vm.cpp ] && f="$overlay/vm.cpp"
    cc1 "$f" clang++ "${flags[@]}" "${cxxflags[@]}" "${inc[@]}"
done
for f in "$src"/libs/lodepng/*.cpp; do cc1 "$f" clang++ "${flags[@]}" "${cxxflags[@]}" "${inc[@]}"; done
for f in "$src"/libs/z8lua/*.c "$src"/libs/miniz/*.c "$src"/libs/simpleini/*.c; do
    [ -f "$f" ] || continue
    case "${f##*/}" in lua.c|luac.c|ltests.c) continue;; esac
    cc1 "$f" clang++ -x c++ "${flags[@]}" "${cxxflags[@]}" "${inc[@]}"   # upstream builds these as C++
done
for f in src/q2_platform.c src/q2_input.c src/itch.c src/crt-q2pp.c; do cc1 "$f" clang "${flags[@]}" -DQ2_GLIBC -std=gnu11 -Wall "${inc[@]}"; done
for f in src/fake08_host.cpp src/fake08_main.cpp src/app.cpp src/ui.cpp src/covers.cpp \
         src/coverflow.cpp src/library.cpp src/getgames.cpp; do cc1 "$f" clang++ "${flags[@]}" "${cxxflags[@]}" -DQ2_GLIBC "${inc[@]}"; done
ld.lld -m elf32ltsmip -e __start --dynamic-linker /lib/ld-linux-mipsn8.so.1 --image-base=0x400000 \
    -z noexecstack --hash-style=sysv --eh-frame-hdr --gc-sections --build-id=none -s -L"$sysroot/lib" \
    -o "$stage/.pico8/q2-pico8" "${objs[@]}" "$cxx/lib/libc++.a" \
    -lpthread -lm -ldl -lc -lgcc_s -latomic "$sysroot/lib/ld-linux-mipsn8.so.1"
sed 's/^name=.*/name=PICO-8/' q2app > "$stage/.pico8/q2app"
: > "$stage/.pico8/carts/.keep"
cp "$src/LICENSE.MD" "$stage/.pico8/FAKE-08-LICENSE.md"
mkdir -p "$stage/.pico8/licenses"
cp assets/icon.png assets/icon-40.png "$stage/.pico8/"
cp assets/Lucide-ISC.txt "$stage/.pico8/licenses/"
# Get Games helper and notices: see tools/build-fake08.sh, which CI packages.
(cd "$stage" && zip -q -FS ../q2-pico8.zip .pico8/q2app .pico8/q2-pico8 .pico8/carts/.keep .pico8/FAKE-08-LICENSE.md \
  .pico8/icon.png .pico8/icon-40.png .pico8/licenses/Lucide-ISC.txt)
file "$stage/.pico8/q2-pico8"; echo "Created dist/q2-pico8.zip"
