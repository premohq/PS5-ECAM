#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds RADV, the PS5's Vulkan driver, in two steps:
#
#  1. PS5_Vulkan's own recipe (its tools/build-radv.sh release): the Mesa fork pinned in deps.json
#     (.deps/PS5_Mesa, the revision PS5_Vulkan pins) into .deps/PS5_Vulkan/.deps/native/radv-release,
#     with the SDK, host tools and cross files it sets up.
#  2. The same revision with the port's driver patches (patches/mesa) that RADV_PATCHES names,
#     configured as the recipe configures its release, into
#     .deps/PS5_Vulkan/.deps/native/radv-ps5cemu, where tools/link.sh finds it:
#
#       RADV_PATCHES=0006,0007,0008,0009,0010
#                               the default: 0006, VideoOut's refresh rate asked for again once the
#                               new launcher's swapchain is gone, without which a game's 120 Hz
#                               output stays at the launcher's 59.94 Hz; 0007, a destroyed buffer's
#                               memory kept until the GPU is done with it (3.0.0's driver had it);
#                               0008, submissions on a thread of their own when the app asks
#                               (asyncSubmit); 0009, the waits for the GPU's sleep chosen by the app
#                               (gpuWaitSleepUs), and fence status without the clock; 0010, 119.88 Hz
#                               given up only once the display has had time to settle in it (#103),
#                               which builds on 0006. 0008 and 0009 replace parts of 0001-0003 and do
#                               not apply over them; 0005 builds only on 0001-0004 (it uses their
#                               submission path)
#       RADV_PATCHES=0004,0006  those patches, by number, applied in order
#       RADV_PATCHES=all        the earlier experiments, 0001-0007 (docs/DRIVER-PERFORMANCE.md)
#       RADV_PATCHES=           none: the recipe's archive as it is
#
# The patched tree is made in a temporary index of the fork's repository and exported with git
# archive, as the recipe exports its revision: the fork's working tree and branches are not touched.
# The build id stays the pinned revision while the patches touch only the winsys and WSI, which
# keeps the games' shader caches; a patch elsewhere gets the patched tree's id, so caches start over.
#
# RADV's PS5 winsys is built on the payload SDK fork's platform layer (ps5platform/ and
# libps5platform.a: direct memory, AGC, VideoOut, the heap and libc's gaps), which the public SDK
# does not have: Mihawk-99's PS5_PayloadSDK, pinned in deps.json (.deps/PS5_PayloadSDK).
# PS5_PAYLOAD_SDK_FORK names another checkout that has the revision PS5_Vulkan's
# tools/setup-native-dependencies.sh pins.
#
# Host tools, as PS5_Vulkan's recipe needs them: meson, mako and Python's packaging (Mesa checks
# mako's version with it), ninja, rsync, glslangValidator (glslang-tools: RADV's BVH shaders), and
# for Mesa's OpenCL kernels LLVM, Clang, libclc and the SPIR-V LLVM translator (on Ubuntu:
# llvm-18-dev, libclang-18-dev, libclc-18-dev, libllvmspirvlib-18-dev, llvm-spirv-18).
#
# A RADV built elsewhere can be used instead: RADV_ARCHIVE (libvulkan_radeon.ps5.a) and RADV_SDK
# (the fork's SDK it was built with) name it to tools/link.sh.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
vulkan=$PS5CEMU_ROOT/.deps/PS5_Vulkan

export PS5_PAYLOAD_SDK_FORK=${PS5_PAYLOAD_SDK_FORK:-$PS5CEMU_ROOT/.deps/PS5_PayloadSDK}
[[ -d $PS5_PAYLOAD_SDK_FORK/platform ]] || { echo "$PS5_PAYLOAD_SDK_FORK is not the payload SDK fork: run make deps" >&2; exit 2; }
export PS5_MESA_FORK=$PS5CEMU_ROOT/.deps/PS5_Mesa
bash "$vulkan/tools/setup-native-dependencies.sh"
bash "$vulkan/tools/build-radv.sh" release
recipe=$vulkan/.deps/native/radv-release
install=$PS5CEMU_RADV
work=$vulkan/.deps/work

# the patches asked for, in order
wanted=${RADV_PATCHES-0006,0007,0008,0009,0010}
patches=()
if [[ $wanted == all ]]; then
    patches=("$PS5CEMU_ROOT"/patches/mesa/000[1-7]-*.patch)
else
    for number in ${wanted//,/ }; do
        matches=("$PS5CEMU_ROOT"/patches/mesa/"$number"-*.patch)
        [[ -f ${matches[0]} ]] || { echo "RADV_PATCHES=$wanted: there is no patches/mesa/$number-*.patch" >&2; exit 2; }
        patches+=("${matches[0]}")
    done
    if ((${#patches[@]})); then
        mapfile -t patches < <(printf '%s\n' "${patches[@]}" | sort -u)
    fi
fi

if ((${#patches[@]} == 0)); then
    rm -rf "$install"
    mkdir -p "$install/lib"
    ln -s "$recipe/lib/libvulkan_radeon.ps5.a" "$install/lib/libvulkan_radeon.ps5.a"
    echo "RADV for PS5-ECAM: the recipe's release build ($recipe), no patches" >"$install/PROVENANCE.txt"
    echo "==> [radv] no patches/mesa (RADV_PATCHES is empty): $recipe as it is"
    echo "==> [radv] $install/lib/libvulkan_radeon.ps5.a"
    exit 0
fi

mesa_revision=$(sed -n 's/^mesa_revision=//p' "$vulkan/tools/build-radv.sh")
[[ $mesa_revision =~ ^[0-9a-f]{40}$ ]] || { echo "cannot read the Mesa revision from $vulkan/tools/build-radv.sh" >&2; exit 2; }
names=$(printf '%s\n' "${patches[@]##*/}")

# the patched tree, as an object in the fork's repository
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
export GIT_INDEX_FILE=$temporary/index
git -C "$PS5_MESA_FORK" read-tree "$mesa_revision"
for patch in "${patches[@]}"; do
    git -C "$PS5_MESA_FORK" apply --cached "$patch" ||
        { echo "patches/mesa/${patch##*/} does not apply to Mesa at ${mesa_revision:0:12}" >&2; exit 1; }
done
tree=$(git -C "$PS5_MESA_FORK" write-tree)
unset GIT_INDEX_FILE
build_id=$mesa_revision
changed=$(git -C "$PS5_MESA_FORK" diff --name-only "$mesa_revision" "$tree")
if [[ -n $changed ]] && grep -qvE '^src/(amd/vulkan/winsys/|vulkan/wsi/)' <<<"$changed"; then
    build_id=$tree
fi

sdk_revision=$(cat "$vulkan/.deps/native/ps5-payload-sdk/.ps5-sdk-revision")
# the recipe's archive stands for its options too: built again (a new PS5_Vulkan), this one is as well
recipe_archive=$(sed -n 's/^archive sha256: //p' "$recipe/PROVENANCE.txt")
if [[ -f $install/lib/libvulkan_radeon.ps5.a && ! -L $install/lib/libvulkan_radeon.ps5.a ]] &&
    grep -qx "tree: $tree" "$install/PROVENANCE.txt" && grep -qx "sdk: $sdk_revision" "$install/PROVENANCE.txt" &&
    grep -qx "recipe archive sha256: $recipe_archive" "$install/PROVENANCE.txt"; then
    echo "==> [radv] $install is RADV at ${mesa_revision:0:12} with" $names
    exit 0
fi

source_tree=$work/ps5cemu-radv-src
build=$work/ps5cemu-radv-build
if [[ $(cat "$source_tree/.tree" 2>/dev/null) != "$tree" ]]; then
    # as the recipe updates its tree: files that did not change keep their times, so a change of
    # patches recompiles only what they touch
    staging=$source_tree.new
    rm -rf "$staging"
    mkdir -p "$staging" "$source_tree"
    git -C "$PS5_MESA_FORK" archive "$tree" | tar -x -C "$staging"
    rsync -rlp --checksum --delete --exclude=/.tree --exclude=/subprojects/packagecache/ \
        --exclude=/subprojects/zlib-*/ "$staging/" "$source_tree/"
    rm -rf "$staging"
    printf '%s\n' "$tree" >"$source_tree/.tree"
fi
# meson's zlib wrap, as the recipe's tree has it (its patch's host may be out of reach from a build machine)
mkdir -p "$source_tree/subprojects/packagecache"
for cached in "$work"/radv-src/subprojects/packagecache/zlib*; do
    if [[ -f $cached ]]; then
        cp -u "$cached" "$source_tree/subprojects/packagecache/"
    fi
done

# configured as the recipe configured its release, with its mesa_clc on PATH (ninja runs it)
[[ -f $work/radv-build-ps5-release/.radv-options && -x $work/radv-clc-bin/mesa_clc ]] ||
    { echo "PS5_Vulkan's recipe left no release configuration in $work: run make distclean, then make radv" >&2; exit 2; }
read -ra options <"$work/radv-build-ps5-release/.radv-options"
export PATH=$work/radv-clc-bin:$PATH
if [[ ! -f $build/build.ninja || $(cat "$build/.radv-options" 2>/dev/null) != "${options[*]}" ]]; then
    wipe=()
    [[ -f $build/build.ninja ]] && wipe=(--wipe)
    meson setup "${wipe[@]}" "$build" "$source_tree" \
        --cross-file "$work/radv-cross-constants.ini" --cross-file "$vulkan/tooling/radv/ps5-cross.ini" \
        "${options[@]}" -Dradv-build-id="$build_id" >"$build.setup.log" 2>&1 ||
        { tail -20 "$build.setup.log" >&2; exit 1; }
elif [[ $(cat "$build/.radv-build-id" 2>/dev/null) != "$build_id" ]]; then
    meson configure "$build" -Dradv-build-id="$build_id" >"$build.setup.log" 2>&1 ||
        { tail -20 "$build.setup.log" >&2; exit 1; }
fi
printf '%s\n' "${options[*]}" >"$build/.radv-options"
printf '%s\n' "$build_id" >"$build/.radv-build-id"
echo "==> [radv] building RADV at ${mesa_revision:0:12} with" $names
ninja -C "$build" src/amd/vulkan/libvulkan_radeon.a >"$build.log" 2>&1 ||
    { grep -E "error|FAILED" "$build.log" | head -20 >&2; exit 1; }

rm -rf "$install"
mkdir -p "$install/lib"
cp "$build/src/amd/vulkan/libvulkan_radeon.a" "$install/lib/libvulkan_radeon.ps5.a"
{
    echo "RADV for PS5-ECAM, built by tools/build-radv.sh"
    echo "revision: $mesa_revision"
    printf 'patch: %s\n' $names
    echo "tree: $tree"
    echo "build id: $build_id"
    echo "sdk: $sdk_revision"
    echo "recipe archive sha256: $recipe_archive"
    echo "archive sha256: $(sha256sum "$install/lib/libvulkan_radeon.ps5.a" | cut -d' ' -f1)"
} >"$install/PROVENANCE.txt"
echo "==> [radv] $install/lib/libvulkan_radeon.ps5.a"
