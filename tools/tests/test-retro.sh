#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
sanitize=()
if [[ ${ECAM_TEST_SANITIZE:-0} == 1 ]]; then sanitize=(-fsanitize=address,undefined -fno-omit-frame-pointer -g); fi
"${CXX:-c++}" "${sanitize[@]}" -std=c++20 -pthread -Wall -Wextra -Werror -Wno-deprecated-declarations \
 -DPS5CEMU_DATA="\"$work/data\"" -I"$root/port" -I"${ECAM_RAPIDJSON_INCLUDE:-$root/.deps/rapidjson/include}" \
 "$root/tools/tests/test-retro.cpp" "$root/port/retro/library.cpp" "$root/port/retro/launch.cpp" \
 "$root/port/frontend/settings.cpp" "$root/port/app/catalog.cpp" -o "$work/test-retro"
"$work/test-retro" "$work/files"
