#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Eden's loader statuses in words (port/eden/load_status.h)
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$root/port" "$root/tools/tests/test-eden-load-status.cpp" -o "$work/test-eden-load-status"
"$work/test-eden-load-status"
