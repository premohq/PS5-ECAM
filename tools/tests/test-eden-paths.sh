#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
eden=${EDEN_SOURCE:-$root/.deps/ProsperoEdenX}
if [[ ! -f "$eden/headless/ecam_layout.h" ]]; then
    echo 'Fetch prosperoedenx and apply tools/eden-patches.sh first (or set EDEN_SOURCE).' >&2
    exit 1
fi
cmp "$root/port/app/system_layout.h" "$eden/headless/ecam_layout.h"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$root/port" -I"$eden/headless" \
    -DECAM_SHARED_DATA="\"$work/console/ps5cemu\"" -DPS5CEMU_DATA="\"$work/console/ps5cemu\"" \
    -DECAM_MOUNT_RECORD="\"$work/console/user/app/PPSA99360/mount.lnk\"" -DECAM_DRIVES="\"$work/console/mnt\"" \
    "$root/tools/tests/test-eden-paths.cpp" -o "$work/test-eden-paths"
for mode in fresh legacy activated invalid drive drive-scaffold drive-console drive-legacy drive-activated drive-no-data image; do
    rm -rf "$work/console"
    "$work/test-eden-paths" "$mode"
done
