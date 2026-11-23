#!/usr/bin/env bash
# Prepare Q2 copies of the FAKE-08 files the player changes, without touching
# the upstream checkout. Prints the directory holding them; compile
# <dir>/vm.cpp in place of <src>/source/vm.cpp (it includes its neighbours
# with quotes, so the copied p8GlobalLuaFunctions.h wins and the rest still
# resolves through -I<src>/source).
#
# Change: the pause menu's "exit to menu" / "exit to settings", which load
# FAKE-08's own BIOS carts, read "exit to library" / "exit to q2 pod": the
# Q2 host returns to the Cover Flow library or leaves the app instead.
#   bash tools/fake08-overlay.sh <fake-08 src> <out dir>
set -euo pipefail
src="$1"
out="$2"
mkdir -p "$out"
cp "$src/source/vm.cpp" "$out/vm.cpp"
sed -e 's/{"exit to menu", __loaddefaultcart}/{"exit to library", __loaddefaultcart}/' \
    -e 's/{"exit to settings", __loadsettingscart}/{"exit to q2 pod", __loadsettingscart}/' \
    "$src/source/p8GlobalLuaFunctions.h" > "$out/p8GlobalLuaFunctions.h"
grep -q '"exit to library", __loaddefaultcart' "$out/p8GlobalLuaFunctions.h"
grep -q '"exit to q2 pod", __loadsettingscart' "$out/p8GlobalLuaFunctions.h"
echo "$out"
