#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the program the Switch 2's games start in (build/eglantine): EglantinePS5, heydemoura's PS5
# port of Djihads80's Eglantine (Eden with Switch 2 support; GPL-3.0-or-later), from
# .deps/eglantine/EglantinePS5 with patches/eglantine, by its own build (its Makefile's package: the
# Eglantine submodule, its pinned inputs, RADV, FFmpeg, Eden). The app carries it as eglantine/
# (tools/package.sh): the Switch side starts eglantine/eboot.bin with LoadExec for a Switch 2 game
# (port/eden/launch.cpp), and it starts PS5-ECAM's eboot.bin again when the game ends
# (patches/eglantine). Until PS5-ECAM's loading screen and in-game menu are in it, as they are in
# Eden's, it shows its own, and its launcher when a game stops with an error.
#
#   tools/build-eglantine.sh    apply patches/eglantine if needed, build, copy the program to build/eglantine
#
# It lives in a folder of its own, .deps/eglantine: its build fetches its inputs into its ../ (the
# boilerplate, Mihawk's Mesa, PS5_Vulkan and payload SDK) as ProsperoEdenX's does, and ProsperoEdenX's
# build patches the driver it builds there (patches/eden 0017), so each keeps its own. As Eden's, its
# build wants CMake 3.31 or later (EDEN_CMAKE_DIR, default ~/hosttools/cmake-3.31.8/bin), and
# RADV_BUILD_JOBS (default 4) and EGLANTINE_JOBS (default 5) keep its compiles within a 7 GB WSL.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
eglantine=$PS5CEMU_ROOT/.deps/eglantine/EglantinePS5
out=$PS5CEMU_BUILD/eglantine

bash "$PS5CEMU_ROOT/tools/eglantine-patches.sh" apply

# pacbrew beside it, as this repository has it at the same pin (tools/deps.json)
mkdir -p "$eglantine/.deps" "$out"
[[ -e $eglantine/.deps/pacbrew-0.40.2 ]] || ln -s ../../../pacbrew-0.40.2 "$eglantine/.deps/pacbrew-0.40.2"

export EDEN_HOST_PATH=${EDEN_CMAKE_DIR:-$HOME/hosttools/cmake-3.31.8/bin}
[[ -x $EDEN_HOST_PATH/cmake ]] || { echo "no cmake in $EDEN_HOST_PATH (EDEN_CMAKE_DIR): EglantinePS5 wants 3.31 or later" >&2; exit 2; }
export RADV_BUILD_JOBS=${RADV_BUILD_JOBS:-4}
make -C "$eglantine" package JOBS="${EGLANTINE_JOBS:-5}"

# The program and the files it reads from its own folder (storage_paths.h: AppDir() is the app's
# eglantine/). Left out: its sce_sys and sce_module (the app's are PS5-ECAM's), its self-updater (the
# app updates eglantine/ with everything else, and the check is off in patches/eglantine) and the test
# program it carries. Its launcher's ui/ stays: it shows its own loading screen, and its launcher when
# a game stops with an error. Its Lapy helper (lapy.elf and its manifest, built for PPSA99360 by
# patches/eglantine) stays: where no HEN answers its requests, it reaches /data through it.
package=$eglantine/build/release/PPSA98515
[[ -f $package/eboot.bin ]] || { echo "$package/eboot.bin is missing" >&2; exit 2; }
[[ -f $package/lapy.elf ]] || { echo "$package/lapy.elf is missing: EglantinePS5 could not reach /data" >&2; exit 2; }
rm -rf "$out/app"
mkdir -p "$out/app"
(cd "$package" && find . -mindepth 1 -maxdepth 1 \
    ! -name sce_sys ! -name sce_module ! -name self-updater.elf ! -name core-homebrew.nro \
    -exec cp -a {} "$out/app/" \;)
cp "$eglantine/LICENSE" "$out/app/LICENSE.EglantinePS5.txt"
cp "$eglantine/THIRD_PARTY_NOTICES.md" "$out/app/THIRD_PARTY_NOTICES.md"
# the unstripped program, for its crash reports (EglantinePS5's tools/symbolize-crash.py)
cp "$eglantine/build/headless-native/llvm-pie.elf" "$out/eglantine.elf" 2>/dev/null || true
git -C "$eglantine" rev-parse HEAD >"$out/commit"
echo "==> [eglantine] $out/app ($(du -sh "$out/app" | cut -f1), eboot.bin $(stat -c %s "$out/app/eboot.bin") bytes)"
