#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Porpoise's settings files as PS5-ECAM writes them (port/retro/porpoise_settings.cpp). Needs a C++20
# compiler. Prints PASS lines; exits 1 on a failure.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$root/port" \
 "$root/tools/tests/test-porpoise-settings.cpp" "$root/port/retro/porpoise_settings.cpp" -o "$work/test-porpoise-settings"
"$work/test-porpoise-settings" "$work/files"
# the PS5's /data refuses lstat to an app without a HEN
if [[ $(uname -s) == Linux ]]; then
    "${CC:-cc}" -shared -fPIC "$root/tools/tests/refuse-lstat.c" -o "$work/refuse-lstat.so" -ldl
    echo "lstat refused with EPERM, as /data is in an app without a HEN:"
    REFUSE_LSTAT_UNDER="$work" LD_PRELOAD="$work/refuse-lstat.so" "$work/test-porpoise-settings" "$work/refused-files"
fi
