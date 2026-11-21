#!/usr/bin/env bash
# Build a Q2 C/C++ cross toolchain from GitHub-hosted sources only, for clang + ld.lld:
#   OUT/sysroot  glibc 2.28 headers (as on the Q2), Linux 5.10 MIPS uapi headers, and the
#                Q2's own runtime libraries (nan2008) taken from a Q2 Pod update.tar
#   OUT/libcxx   LLVM libc++/libc++abi, static, built -march=mips32r2 -mfp64 -mnan=2008
# Everything an app links statically is nan2008/FR=1; at run time it uses only the Q2's
# libc, libm, libpthread, libdl, librt, libgcc_s (unwinding) and libatomic.
#
#   bash tools/make-q2-toolchain.sh OUT [update.tar]
# Needs: git, clang/clang++ 18, lld, llvm-ar, cmake, ninja, gcc, make, bison, python3, unsquashfs
# with LZO (the Q2 rootfs is LZO). gawk is built from source when missing (glibc's configure).
set -euo pipefail
out=$(mkdir -p "$1" && cd "$1" && pwd)
fw=${2:-}
src=$out/src; sysroot=$out/sysroot; mkdir -p "$src" "$sysroot/lib"
clone() { [ -d "$src/$2" ] || GIT_LFS_SKIP_SMUDGE=1 git clone -q --depth 1 "$@" ; }
cd "$src"

# 1. Q2 runtime libraries from the firmware
if [ ! -f "$sysroot/lib/libc.so.6" ]; then
    [ -n "$fw" ] || { fw=$src/update.tar; curl -fsSL --retry 3 -o "$fw" \
        https://github.com/DiamondBond/q2-pod/releases/latest/download/update.tar; }
    tar -xOf "$fw" recovery-update/rootfs.squashfs > "$src/rootfs.squashfs"
    unsquashfs -q -f -n -d "$src/root" "$src/rootfs.squashfs" lib usr/lib >/dev/null
    for l in lib/ld-linux-mipsn8.so.1 lib/libc.so.6 lib/libm.so.6 lib/libpthread.so.0 lib/libdl.so.2 \
             lib/librt.so.1 lib/libgcc_s.so.1 lib/libatomic.so.1 usr/lib/libasound.so.2; do
        cp -L "$src/root/$l" "$sysroot/lib/"
    done
    (cd "$sysroot/lib" && for l in c:libc.so.6 m:libm.so.6 pthread:libpthread.so.0 dl:libdl.so.2 rt:librt.so.1 \
        asound:libasound.so.2 gcc_s:libgcc_s.so.1 atomic:libatomic.so.1; do ln -sf "${l#*:}" "lib${l%%:*}.so"; done)
fi

# 2. Linux 5.10 uapi headers (the Q2 runs 5.10), ARCH=mips
if [ ! -f "$sysroot/usr/include/asm/unistd_o32.h" ]; then
    [ -d linux ] || { git clone -q --depth 1 --branch v5.10 --filter=blob:none --no-checkout \
        https://github.com/torvalds/linux linux
        (cd linux && git sparse-checkout init --no-cone &&
         printf '%s\n' /Makefile /scripts/ /include/ /usr/ /tools/include/ /arch/mips/ > .git/info/sparse-checkout &&
         git checkout -q v5.10); }
    make -s -C linux ARCH=mips CROSS_COMPILE= headers 2>/dev/null || true   # rsync-free: copy usr/include ourselves
    mkdir -p "$sysroot/usr/include"
    (cd linux/usr/include && find . -name '*.h' -print0 | tar --null -cf - -T - | tar -xf - -C "$sysroot/usr/include")
fi

# 3. glibc 2.28 headers, configured for o32 nan2008 with clang standing in for GCC
if [ ! -f "$sysroot/usr/include/stdio.h" ]; then
    if ! command -v gawk >/dev/null; then
        clone https://github.com/gnu-mirror-unofficial/gawk gawk
        (cd gawk && ./configure -q --disable-nls >/dev/null && make -j"$(nproc)" gawk >/dev/null)
        export PATH=$src/gawk:$PATH
    fi
    clone --branch glibc-2.28 https://github.com/bminor/glibc glibc
    cc="clang --target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008 -fgnuc-version=8.3.0"
    mkdir -p glibc-build && cd glibc-build
    ../glibc/configure -q --host=mipsel-linux-gnu --build=x86_64-linux-gnu --prefix=/usr \
        --with-headers="$sysroot/usr/include" --enable-kernel=3.2 --disable-werror --without-selinux \
        CC="$cc" CXX=no BUILD_CC=gcc AR=llvm-ar RANLIB=llvm-ranlib LD=ld.lld NM=llvm-nm \
        OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump READELF=llvm-readelf \
        libc_cv_forced_unwind=yes libc_cv_c_cleanup=yes libc_cv_ctors_header=no \
        libc_cv_gcc_builtin_redirection=yes libc_cv_gcc_builtin_memset=yes >/dev/null
    make -s install-headers install_root="$sysroot" install-bootstrap-headers=yes >/dev/null
    printf '/* bootstrap headers: no stub list */\n' > "$sysroot/usr/include/gnu/stubs.h"
    cd "$src"
fi

# 4. libc++ / libc++abi (LLVM 18.1.3, matching clang 18), static, unwinding through the Q2's libgcc_s
if [ ! -f "$out/libcxx/lib/libc++.a" ]; then
    [ -d llvm ] || { git clone -q --depth 1 --branch llvmorg-18.1.3 --filter=blob:none --no-checkout \
        https://github.com/llvm/llvm-project llvm
        (cd llvm && git sparse-checkout init --no-cone &&
         printf '%s\n' /runtimes/ /libcxx/ /libcxxabi/ /libunwind/ /cmake/ /llvm/cmake/ /llvm/utils/ /third-party/ \
             > .git/info/sparse-checkout && git checkout -q llvmorg-18.1.3); }
    cat > toolchain.cmake <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR mips)
set(CMAKE_SYSROOT $sysroot)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET mipsel-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET mipsel-linux-gnu)
set(CMAKE_C_FLAGS_INIT "-march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008 -D_GNU_SOURCE")
set(CMAKE_CXX_FLAGS_INIT "-march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008 -D_GNU_SOURCE")
set(CMAKE_AR llvm-ar)
set(CMAKE_RANLIB llvm-ranlib)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
EOF
    cmake -S llvm/runtimes -B libcxx-build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$src/toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$out/libcxx" -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" \
        -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXXABI_ENABLE_SHARED=OFF -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON \
        -DLIBCXXABI_USE_LLVM_UNWINDER=OFF -DLIBCXX_CXX_ABI=libcxxabi -DLIBCXX_HAS_ATOMIC_LIB=OFF \
        -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXX_INCLUDE_BENCHMARKS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_TESTS=OFF >/dev/null
    ninja -C libcxx-build install-cxx install-cxxabi >/dev/null
fi
echo "Q2_SYSROOT=$sysroot"
echo "Q2_LIBCXX=$out/libcxx"
