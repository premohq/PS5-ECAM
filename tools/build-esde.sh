#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# EmulationStation (ES-DE 3.5.0) for the app: build/esde/es-de/, its es-de.bin and what it reads beside
# it (its resources, themes and licence), which tools/package.sh puts in the app's folder when it is
# there (port/esde/esde.h: Settings > Launcher > Start in EmulationStation). Optional: the app is whole
# without it, and the setting shows only in an app that has it.
#
# ES-DE on the PS5 is Mihawk's port (esde-port in tools/deps.json, its frontends/es-de: his patches/0001 to
# ES-DE, his PS5 glue, and the scripts that build ES-DE, its libraries and the program): built here as he
# builds it, with PS5-ECAM's glue in place of his and only what PS5-ECAM's ES-DE uses linked into it.
# ES-DE draws with OpenGL, so it is linked with the public PS5 OpenGL SDK (BlackBearReloaded's ps5-opengl
# 1.0.2, darkxex's copy), whose SDL2 bridge it opens its window through, and runs on the same clean-room
# libc.prx as the app's eboot.bin (the boilerplate's: the two record one digest for it).
#
# Each step is the upstream project's own, at the pins in tools/deps.json:
#   1. ps5-opengl: its Mesa and the OpenGL 4.6 SDK (make sdk), then SDL2 2.30.12 (ps5-payload-dev's fork,
#      which its integration/SDL2/build.py builds over that SDK);
#   2. the port, copied to build/esde/port (its checkout in .deps stays as it is), with:
#        frontends/es-de/ps5/   main_ecam.cpp, game_ecam.cpp and their headers (port/esde/ps5,
#                               port/esde/handoff.h) in place of main_ps5.cpp, game_ps5.cpp and
#                               library_ps5.cpp: PS5-ECAM writes ES-DE's systems and game lists
#                               (port/esde/esde.cpp) and plays its games; the menu pages of the port's own
#                               app (include/GuiFrameGen.h, GuiWebUI.h) left out;
#        frontends/es-de/patches/ patches/esde after Mihawk's own (ES-DE's scraper in its menu, and its
#                               notes and log lines saying what PS5-ECAM does);
#        tools/build-frontend.sh  without the files of the port's own app it links into every program
#                               (its game requests, its game library, its WebUI), which PS5-ECAM's ES-DE
#                               never calls;
#      its payload SDK (tools/setup-native-dependencies.sh, from his PS5_PayloadSDK at the revision it
#      pins), its host tool, then tools/build-frontend.sh es-de: ES-DE's libraries (FreeType, HarfBuzz,
#      ICU, pugixml, curl over mbedTLS, FFmpeg, libwebp), ES-DE itself, the program, its themes;
#   3. build/esde/es-de: the program's folder, as the app has it.
#
# Not built or run on a console yet in PS5-ECAM (docs/ROADMAP.md): Mihawk's port runs in his releases,
# built against his own OpenGL tree; this is the same port over the public SDK it came from.
#
#   tools/build-esde.sh          everything above; each step skipped when its result is already there
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
step() { printf '==> [es-de] %s\n' "$*"; }
pin() { python3 -c 'import json,sys; print(next(i["commit"] for i in json.load(open("tools/deps.json"))["items"] if i["name"] == sys.argv[1]))' "$1"; }

python3 -B tools/deps.py fetch esde-port ps5-opengl sdl-ps5
port_pin=$(pin esde-port)
opengl="$root/.deps/ps5-opengl"
sdl_source="$root/.deps/SDL-ps5"
work="$root/build/esde"
port="$work/port"
mkdir -p "$work"

# 1. The OpenGL SDK and SDL2 over it
gl_sdk="$opengl/build/sdk/ps5-opengl-gl46"
if [[ ! -f $gl_sdk/lib/libPS5OpenGLCore33.a ]]; then
    step "ps5-opengl: its sources, Mesa and the OpenGL 4.6 SDK (a long build)"
    make -C "$opengl" source-fetch
    make -C "$opengl" sdk
fi
for input in libPS5OpenGLCore33.a libSceAgc.so libSceAgcDriver.so; do
    [[ -f $gl_sdk/lib/$input ]] || { echo "error: $gl_sdk/lib/$input is missing: the OpenGL SDK did not build" >&2; exit 1; }
done

# 2. The port's tree, with PS5-ECAM's glue: made again when either (or this script) changed
stamp=$({
    echo "$port_pin"
    sha256sum tools/build-esde.sh port/esde/handoff.h port/esde/ps5/* patches/esde/*.patch
} | sha256sum | cut -c1-64)
if [[ ! -f $port/.ecam-stamp || $(<"$port/.ecam-stamp") != "$stamp" ]]; then
    step "the ES-DE port ${port_pin:0:12} with PS5-ECAM's glue, in $port"
    # what an earlier copy built is kept (its dependencies' builds take a long time): only its sources go
    mkdir -p "$port"
    (cd "$root/.deps/esde-port" && git ls-files -z) |
        (cd "$root/.deps/esde-port" && xargs -0 cp --parents -t "$port")
    glue="$port/frontends/es-de/ps5"
    rm -f "$glue/main_ps5.cpp" "$glue/game_ps5.cpp" "$glue/library_ps5.cpp" "$glue/library_ps5.h" \
        "$glue/include/GuiFrameGen.h" "$glue/include/GuiWebUI.h"
    cp port/esde/ps5/* port/esde/handoff.h "$glue/"
    # the program links only what PS5-ECAM's ES-DE uses: not the port's own app's game requests,
    # library and WebUI, which every one of its programs links
    python3 - "$port/tools/build-frontend.sh" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text()
start = text.index("# The contracts every frontend shares with eboot.bin")
end = text.index('objects+=("$work/obj/webui_link.o")\n') + len('objects+=("$work/obj/webui_link.o")\n')
path.write_text(text[:start] + "# (PS5-ECAM's build: no contracts with the port's own eboot.bin; tools/build-esde.sh)\n" + text[end:])
PY
    ! grep -q "webui_link\|ps5_game\|ps5_library" "$port/tools/build-frontend.sh" ||
        { echo "error: the port's build-frontend.sh still links its own app's files" >&2; exit 1; }
    cp patches/esde/*.patch "$port/frontends/es-de/patches/"
    for patch in patches/esde/*.patch; do
        grep -qxF "$(basename "$patch")" "$port/frontends/es-de/patches/series" ||
            basename "$patch" >> "$port/frontends/es-de/patches/series"
    done
    printf '%s\n' "$stamp" > "$port/.ecam-stamp"
fi

# Its payload SDK, from Mihawk's PS5_PayloadSDK at the revision it pins (beside it, as it expects)
sdk_revision=$(sed -n 's/^sdk_revision=//p' "$port/tools/setup-native-dependencies.sh")
sdk_fork="$work/PS5_PayloadSDK"
if ! git -C "$sdk_fork" cat-file -e "$sdk_revision^{commit}" 2>/dev/null; then
    step "Mihawk's PS5_PayloadSDK at ${sdk_revision:0:12}"
    rm -rf "$sdk_fork"
    git init -q "$sdk_fork"
    git -C "$sdk_fork" fetch -q --depth 1 https://github.com/mihawk-99/PS5_PayloadSDK.git "$sdk_revision"
    git -C "$sdk_fork" checkout -q --detach FETCH_HEAD
fi
if [[ ! -x $port/.deps/native/ps5-payload-sdk/bin/prospero-clang ]]; then
    step "the port's payload SDK"
    PS5_PAYLOAD_SDK_FORK="$sdk_fork" bash "$port/tools/setup-native-dependencies.sh"
fi

# SDL2 over the OpenGL SDK, with that SDK's compiler (ps5-opengl's integration/SDL2/README.md)
sdl_out="$opengl/build/sdl2-ecam-$(pin sdl-ps5 | cut -c1-12)"
if [[ ! -f $sdl_out/sdk/lib/libSDL2.a ]]; then
    step "SDL2 over the OpenGL SDK"
    rm -rf "$sdl_out"
    (cd "$opengl" && python3 integration/SDL2/build.py native --sdl-source "$sdl_source" --sdk-prefix "$gl_sdk" \
        --out "$sdl_out" --payload-sdk "$port/.deps/native/ps5-payload-sdk" --compiler-wrapper "$port/tooling/prospero-clang18")
fi

# Its host tool, as its tools/build.sh makes it, without building the port's own app
zlib_root="$port/.deps/native/zlib/root"
zlib_archive=$(find "$zlib_root" -type f -name libz.a -print -quit)
if [[ ! -x $port/build/host/ps5-native-tool ]]; then
    step "the host tool that links and signs the program"
    mkdir -p "$port/build/host"
    clang++-18 -std=c++20 -O2 -Wall -Wextra -I "$zlib_root/usr/include" "$port/tooling/native/native_app_builder.cpp" \
        "$port/tooling/native/self_container.cpp" "$port/tooling/native/elf_object.cpp" "$port/tooling/native/sce_module_writer.cpp" \
        "$zlib_archive" -o "$port/build/host/ps5-native-tool"
fi

# tools/build-frontend.sh writes the program into its title's folder, beside an eboot.bin it only
# checks for: PS5-ECAM's own is the one that plays the games, so a placeholder stands in
title=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$port/sce_sys/param.json")
mkdir -p "$port/dist/$title"
[[ -f $port/dist/$title/eboot.bin ]] || echo "placeholder: PS5-ECAM's eboot.bin plays ES-DE's games" > "$port/dist/$title/eboot.bin"

step "ES-DE, its libraries and the program (tools/build-frontend.sh es-de)"
(cd "$port" && PS5_OPENGL="$opengl" PS5_OPENGL_SDK="$gl_sdk" PS5_SDL2="$sdl_out/sdk" bash tools/build-frontend.sh es-de)

# 3. The program's folder, as the app has it
rm -rf "$work/es-de"
cp -a "$port/dist/$title/es-de" "$work/es-de"
cp "$root/LICENSE" "$work/es-de/LICENSE-PS5-ECAM"
step "build/esde/es-de: $(du -sh "$work/es-de" | cut -f1) (es-de.bin $(stat -c %s "$work/es-de/es-de.bin") bytes)"
