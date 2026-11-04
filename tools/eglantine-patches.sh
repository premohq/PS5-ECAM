#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The port's changes to EglantinePS5's own files (the program the Switch 2's games start in), kept as
# patches/eglantine/*.patch against the pinned commit.
#
#   tools/eglantine-patches.sh apply    put them on a branch named ps5 in .deps/eglantine/EglantinePS5,
#                                       and the ones added since on it (ps5cemu_patches_apply in tools/env.sh)
#   tools/eglantine-patches.sh export   write the ps5 branch's commits back to patches/eglantine
#
# EglantinePS5's build patches its Eglantine submodule itself (its tools/apply-eglantine-patches.sh), so
# what is changed in there is left out of both checks.
#
# To change EglantinePS5: edit .deps/eglantine/EglantinePS5 on the ps5 branch, commit, then export.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
eglantine=$PS5CEMU_ROOT/.deps/eglantine/EglantinePS5
patches=$PS5CEMU_ROOT/patches/eglantine
pin=$(python3 -c 'import json,sys; print(next(i["commit"] for i in json.load(open(sys.argv[1]))["items"] if i["name"] == "eglantineps5"))' \
    "$PS5CEMU_ROOT/tools/deps.json")

case ${1:-} in
apply)
    ps5cemu_patches_apply eglantine "$eglantine" "$patches" "$pin" --ignore-submodules=dirty
    ;;
export)
    [[ -z $(git -C "$eglantine" status --porcelain --untracked-files=no --ignore-submodules=dirty) ]] ||
        { echo "commit the changes in .deps/eglantine/EglantinePS5 first" >&2; exit 1; }
    rm -f "$patches"/*.patch
    mkdir -p "$patches"
    git -C "$eglantine" format-patch -q --no-signature --zero-commit --no-numbered -o "$patches" "$pin..ps5"
    ls "$patches"
    ;;
*)
    echo "usage: tools/eglantine-patches.sh apply|export" >&2
    exit 2
    ;;
esac
