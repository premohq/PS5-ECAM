#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# EmulationStation as the other frontend: ES-DE's systems and game lists, and a game's handoff (port/esde)
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -Wno-deprecated-declarations -DPS5CEMU_DATA="\"$work/data\"" \
    -I"$root/port" -I"${ECAM_RAPIDJSON_INCLUDE:-$root/.deps/rapidjson/include}" -I"$root/.deps/pugixml-1.15/src" \
    "$root/tools/tests/test-esde.cpp" "$root/port/esde/esde.cpp" "$root/port/frontend/settings.cpp" \
    "$root/.deps/pugixml-1.15/src/pugixml.cpp" -o "$work/test-esde"
"$work/test-esde" "$work/files"
