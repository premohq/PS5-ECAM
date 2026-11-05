#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds Porpoise X, the GameCube and Wii sides' program (Dolphin for the PS5, premohq/Porpoise-X at its
# pin in tools/retro/sources.json), into build/retro/porpoise, which tools/package.sh puts in the app's
# retro/porpoise. tools/build-retro.py does the build, in a worktree of .deps/Porpoise-X with the app's
# bridge in it (tools/retro/prepare.py), with this build's RADV and Mihawk's payload SDK fork
# (PS5_VULKAN_DIR, PS5_PAYLOAD_SDK_FORK: make radv's). A staged build at the pin is kept as it is; one
# at another pin, and its worktree, make way for the new one.
#
#   tools/build-porpoise.sh

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
pin=$(python3 -c 'import json,sys; print(next(s["commit"] for s in json.load(open(sys.argv[1]))["sources"] if s["name"] == "porpoise"))' \
    "$PS5CEMU_ROOT/tools/retro/sources.json")
out=$PS5CEMU_BUILD/retro/porpoise
tree=$PS5CEMU_BUILD/retro-source/porpoise

staged=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("commit", ""))' "$out/ECAM-SOURCE.json" 2>/dev/null || true)
if [[ -f $out/eboot.bin && $staged == "$pin" ]]; then
    echo "==> [porpoise] $out is Porpoise X at ${pin:0:12}"
    exit 0
fi
rm -rf "$out"
reuse=()
if [[ -d $tree ]]; then
    if [[ $(git -C "$tree" rev-parse HEAD 2>/dev/null) == "$pin" ]]; then
        reuse=(--reuse-prepared)
    else
        git -C "$PS5CEMU_ROOT/.deps/Porpoise-X" worktree remove --force "$tree" 2>/dev/null || rm -rf "$tree"
        git -C "$PS5CEMU_ROOT/.deps/Porpoise-X" worktree prune 2>/dev/null || true
    fi
fi
export PS5_VULKAN_DIR=${PS5_VULKAN_DIR:-$PS5CEMU_ROOT/.deps/PS5_Vulkan}
export PS5_PAYLOAD_SDK_FORK=${PS5_PAYLOAD_SDK_FORK:-$PS5CEMU_ROOT/.deps/PS5_PayloadSDK}
# Porpoise pins a revision of the SDK fork of its own (its tools/setup-native-dependencies.sh), which
# may be newer than the one this app's tools/deps.json checks out: fetched into the fork when missing.
# (.deps/Porpoise-X is cloned here as tools/build-retro.py would, to read that pin first.)
porpoise_src=$PS5CEMU_ROOT/.deps/Porpoise-X
[[ -d $porpoise_src/.git ]] || git clone -q --no-checkout https://github.com/premohq/Porpoise-X.git "$porpoise_src"
git -C "$porpoise_src" cat-file -e "$pin^{commit}" 2>/dev/null || git -C "$porpoise_src" fetch -q origin "$pin"
sdk_revision=$(git -C "$porpoise_src" show "$pin:tools/setup-native-dependencies.sh" |
    sed -n 's/^sdk_revision=\([0-9a-f]\{40\}\).*/\1/p')
if [[ -n $sdk_revision ]] && ! git -C "$PS5_PAYLOAD_SDK_FORK" cat-file -e "$sdk_revision^{commit}" 2>/dev/null; then
    echo "==> [porpoise] fetching the SDK fork's ${sdk_revision:0:12}, Porpoise's pin"
    git -C "$PS5_PAYLOAD_SDK_FORK" fetch -q origin "$sdk_revision"
fi
python3 "$PS5CEMU_ROOT/tools/build-retro.py" "${reuse[@]}" porpoise
echo "==> [porpoise] $out ($(du -sh "$out" | cut -f1))"
