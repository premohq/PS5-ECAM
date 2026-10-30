#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A Switch 2 title's game folder, its ExeFS and RomFS (port/eden/exefs.h)
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$root/port" "$root/tools/tests/test-switch2-games.cpp" -o "$work/test-switch2-games"
"$work/test-switch2-games" "$work/games"
