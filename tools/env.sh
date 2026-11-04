# SPDX-License-Identifier: GPL-3.0-or-later
# Sourced by the build scripts: where the pinned inputs and the build outputs live.

ps5cemu_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export PS5CEMU_ROOT=$ps5cemu_root
export PS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK:-$ps5cemu_root/.deps/ps5-native-app-boilerplate/.deps/native/ps5-payload-sdk}
export PS5CEMU_BOILERPLATE=$ps5cemu_root/.deps/ps5-native-app-boilerplate
export PS5CEMU_PACBREW=$ps5cemu_root/.deps/pacbrew-0.40.2/opt/ps5-payload-sdk/target/user/homebrew
export PS5CEMU_SYSROOT=$ps5cemu_root/build/sysroot
export PS5CEMU_BUILD=$ps5cemu_root/build
export PS5CEMU_TOOLCHAIN=$ps5cemu_root/tools/ps5.cmake
export JOBS=${JOBS:-$(nproc)}
# RADV with patches/mesa (tools/build-radv.sh), and the archive tools/link.sh links: that one, unless
# RADV_ARCHIVE names a build made elsewhere
export PS5CEMU_RADV=$ps5cemu_root/.deps/PS5_Vulkan/.deps/native/radv-ps5cemu
export PS5CEMU_RADV_ARCHIVE=${RADV_ARCHIVE:-$PS5CEMU_RADV/lib/libvulkan_radeon.ps5.a}

ps5cemu_radv_stamp() {
    # Which RADV archive a link takes: its real path, size and time. tools/link.sh keeps it beside the
    # ELF, and tools/build-cemu.sh links again when it changes, which ninja would not see.
    [[ -f $PS5CEMU_RADV_ARCHIVE ]] || return 0
    printf '%s %s\n' "$(realpath -- "$PS5CEMU_RADV_ARCHIVE")" "$(stat -L -c '%s %Y' -- "$PS5CEMU_RADV_ARCHIVE")"
}

ps5cemu_cmake() {
    # ps5cemu_cmake SOURCE BUILD [cmake arguments...]: configure, build and install into the sysroot.
    local source=$1 build=$2
    shift 2
    cmake -S "$source" -B "$build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$PS5CEMU_TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PS5CEMU_SYSROOT" "$@" >"$build.configure.log" 2>&1 ||
        { tail -40 "$build.configure.log"; return 1; }
    cmake --build "$build" -j "$JOBS" >"$build.build.log" 2>&1 || { grep -m20 -B2 -A8 "error" "$build.build.log"; return 1; }
    cmake --install "$build" >"$build.install.log" 2>&1 || { tail -20 "$build.install.log"; return 1; }
}

ps5cemu_patches_apply() {
    # ps5cemu_patches_apply NAME CHECKOUT PATCHES PIN (tools/NAME-patches.sh apply): PATCHES/*.patch as
    # the commits of a branch named ps5 in CHECKOUT, made from PIN the first time. A branch already there
    # is held against the patches one by one, by patch ID (the change, not its message or file name):
    # patches added since it was made, as a pull brings them, are applied on it; commits of its own, not
    # exported yet, are built as they are, which is said; when the two differ otherwise, the build stops
    # (it used to build the branch as it was, leaving the new patches out without a word). A release
    # (PS5CEMU_RELEASE=1, which make release sets) takes the patches only as they are: commits not
    # exported and changes not committed stop it too. A fifth argument, --ignore-submodules=dirty, leaves
    # out what a checkout's own build changes in its submodules (EglantinePS5 patches its Eglantine).
    local name=$1 checkout=$2 patches=$3 pin=$4 status=(${5:-})
    local where=${checkout#"$PS5CEMU_ROOT"/} folder=${patches#"$PS5CEMU_ROOT"/} script=tools/$1-patches.sh
    local identity=(-c user.name=ps5cemu -c user.email=ps5cemu@localhost)
    local files=() file
    for file in "$patches"/*.patch; do
        if [[ -f $file ]]; then
            files+=("$file")
        fi
    done
    if ! git -C "$checkout" rev-parse -q --verify refs/heads/ps5 >/dev/null; then
        git -C "$checkout" checkout -q -b ps5 "$pin"
        if ((${#files[@]})); then
            git "${identity[@]}" -C "$checkout" am -q --keep-cr "${files[@]}"
        fi
        echo "==> [$name] applied ${#files[@]} patches on branch ps5"
        return 0
    fi

    # the branch made again from the pin, on which the next build applies every patch
    local again="git -C $where checkout -q -B ps5 ${pin:0:12}"
    if [[ $(git -C "$checkout" symbolic-ref -q HEAD) != refs/heads/ps5 ]]; then
        echo "$where is not on its branch ps5, the one the build takes: git -C $where checkout ps5" >&2
        return 1
    fi
    if ! git -C "$checkout" merge-base --is-ancestor "$pin" ps5 2>/dev/null; then
        echo "the branch ps5 of $where is not on the commit tools/deps.json pins: $again makes it again" >&2
        return 1
    fi
    # each patch's ID and each commit's, in order: --verbatim, as a space in a string is a change too
    local want=() have=() commits=() commit
    for file in "${files[@]}"; do
        want+=("$(git -C "$checkout" patch-id --verbatim <"$file" | cut -d' ' -f1)")
    done
    mapfile -t commits < <(git -C "$checkout" rev-list --reverse "$pin..ps5")
    for commit in "${commits[@]}"; do
        have+=("$(git -C "$checkout" format-patch --stdout -1 "$commit" | git -C "$checkout" patch-id --verbatim | cut -d' ' -f1)")
    done
    local same=0
    while ((same < ${#want[@]} && same < ${#have[@]})) && [[ ${want[same]} == "${have[same]}" ]]; do
        same=$((same + 1))
    done
    local changes=
    if [[ -n $(git -C "$checkout" status --porcelain --untracked-files=no "${status[@]}") ]]; then
        changes="$where has changes not committed"
    fi

    if ((same == ${#want[@]} && same == ${#have[@]})); then
        # the patches as they are
        if [[ -n $changes && ${PS5CEMU_RELEASE:-0} == 1 ]]; then
            echo "$changes, which a release does not take: commit and export them ($script export), or undo them" >&2
            return 1
        fi
        if [[ -n $changes ]]; then
            echo "==> [$name] $changes: built with them"
        fi
        return 0
    fi
    if ((same == ${#have[@]})); then
        # patches added since the branch was made: applied on it
        local added=("${files[@]:same}")
        if [[ -n $changes ]]; then
            echo "$folder has patches the branch ps5 lacks, and $changes: git -C $where stash, build, then git -C $where stash pop" >&2
            printf '    %s\n' "${added[@]##*/}" >&2
            return 1
        fi
        git "${identity[@]}" -C "$checkout" am -q --keep-cr "${added[@]}"
        if ((same == 0)); then
            echo "==> [$name] applied ${#added[@]} patches on branch ps5" # a branch made again from the pin
        else
            echo "==> [$name] applied ${#added[@]} new patches on branch ps5:"
            printf '    %s\n' "${added[@]##*/}"
        fi
        return 0
    fi
    if ((same == ${#want[@]})); then
        # commits of the branch's own, not exported yet
        local own=$((${#have[@]} - same))
        if [[ ${PS5CEMU_RELEASE:-0} == 1 ]]; then
            echo "the branch ps5 of $where has $own commits $folder does not, which a release does not take: export them ($script export) and commit $folder, or drop them" >&2
            git -C "$checkout" log --reverse -n "$own" --format='    %s' ps5 >&2
            return 1
        fi
        echo "==> [$name] the branch ps5 has $own commits $folder does not (not exported yet): built with them${changes:+; $changes}"
        return 0
    fi
    # they differ from a patch on, and which is the newer is not known here
    echo "$folder and the branch ps5 of $where differ from ${files[same]##*/} on (the branch has \"$(git -C "$checkout" log -1 --format=%s "${commits[same]}")\" there)." >&2
    echo "If $folder is the newer (a pull): $again, then build again. If the branch is the newer: $script export writes it to $folder." >&2
    return 1
}
