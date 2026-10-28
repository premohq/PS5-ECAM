#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Backups and restores in a disposable data/mount root, never a real /mnt/usb0. Needs a C++20
# compiler and fmt headers. Prints PASS lines; exits 1 on failure. No root access needed.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
work=$(cd "$work" && pwd -P)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wno-deprecated-declarations -DFMT_HEADER_ONLY -DPS5CEMU_VERSION='"test"' \
    -DPS5ECAM_RETRO_DATA="\"$work/retro\"" -DPS5CEMU_DATA="\"$work/data\"" -DPS5CEMU_USB_ROOT="\"$work/mounts\"" -I"$root/port" \
    "$root/tools/tests/test-saves.cpp" "$root/port/app/saves.cpp" "$root/port/eden/save_transfer.cpp" -pthread -o "$work/test-saves"
"$work/test-saves"
