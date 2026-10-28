#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply the ECAM handoff overlay to a disposable pinned donor worktree (AI-assisted)."""
import argparse
from pathlib import Path
import shutil


def replace(path, before, after):
    text = path.read_text()
    if after in text:
        return
    if text.count(before) != 1:
        raise ValueError(f'{path}: expected one integration anchor, found {text.count(before)}')
    path.write_text(text.replace(before, after))


def prepare(source, kind, project):
    if kind in ('mupen64plus', 'fceumm'):
        folder = source / 'ps5/coreorbis'
        shutil.copyfile(project / 'port/retro/runtime/ecam_bridge.h', folder / 'ecam_bridge.h')
        main = folder / 'main-boot.cpp'
        replace(main, '#include "OrbisPaths.h"', '#include "OrbisPaths.h"\n#include "ecam_bridge.h"')
        replace(main, 'int main(int argc, char** argv)\n{', f'int main(int argc, char** argv)\n{{\n    ecam_bridge::Init(argc, argv, "{kind}");')
        replace(main, 'sceSystemServiceLoadExec("exit", nullptr)', 'sceSystemServiceLoadExec(ecam_bridge::ReturnProgram(), nullptr)')
        # Existing native teardown runs after one forwarded game, including a failed boot.
        replace(main, 'if (!again)\n', 'if (!again || (ecam_bridge::request.valid && !ecam_bridge::request.library))\n')
        if kind == 'mupen64plus':
            replace(main, 'std::string rom = (argc > 1 && argv[1] && OrbisIsFile(argv[1])) ? argv[1] : "";',
                    'std::string rom = ecam_bridge::request.valid ? ecam_bridge::request.rom : ((argc > 1 && argv[1] && OrbisIsFile(argv[1])) ? argv[1] : "");')
            # Bundled runtimes are updated with ECAM only, never through a standalone installer.
            update = source / 'ps5/frontend/fe_update.cpp'
            replace(update, 'UpdateOffer CheckForUpdate()\n{', 'UpdateOffer CheckForUpdate()\n{\n    return {}; // ECAM bundle: standalone self-updater disabled.\n')
        else:
            replace(main, 'const bool from_command_line = argc > 1 && argv[1] && OrbisIsFile(argv[1]);',
                    'const bool from_command_line = (ecam_bridge::request.valid && !ecam_bridge::request.library) || (argc > 1 && argv[1] && OrbisIsFile(argv[1]));')
            replace(main, 'std::string rom = from_command_line ? argv[1] : "";',
                    'std::string rom = ecam_bridge::request.valid ? ecam_bridge::request.rom : (from_command_line ? argv[1] : "");')
        # Dedicated ECAM helper ports avoid colliding with running standalone helpers.
        helper = folder / 'orbis-shims/ProsperoJailbreak.h'
        port = 9064 if kind == 'mupen64plus' else 9085
        replace(helper, f'constexpr int kHelperPort = {port};', f'constexpr int kHelperPort = {9136 if kind == "mupen64plus" else 9137}; // ECAM bundle')
        return
    if kind == 'porpoise':
        folder = source / 'src'
        shutil.copyfile(project / 'port/retro/runtime/ecam_bridge.h', folder / 'ecam_bridge.h')
        main = folder / 'porpoise_main.cpp'
        replace(main, '#include "porpoise_core.hpp"', '#include "porpoise_core.hpp"\n#include "ecam_bridge.h"')
        replace(main, 'int main(int argc, char **argv)\n{', 'int main(int argc, char **argv)\n{\n    ecam_bridge::Init(argc, argv, "porpoise");')
        replace(main, '[[noreturn]] void leave(int status)\n{', '[[noreturn]] void leave(int status)\n{\n    const char* ecam_destination = ecam_bridge::ReturnProgram();')
        replace(main, '[[noreturn]] void restart_porpoise()\n{', '[[noreturn]] void restart_porpoise()\n{\n    if (ecam_bridge::request.valid) leave(0);')
        # The bridge augments Porpoise's existing forwarding protocol, including the argc=1 case.
        replace(main, 'const porpoise::forward::Args forwarded = porpoise::forward::parse(argc, argv);',
                'porpoise::forward::Args forwarded = porpoise::forward::parse(argc, argv);\n    if (ecam_bridge::request.valid && !ecam_bridge::request.library) { forwarded.rom = ecam_bridge::request.rom; forwarded.exit_after_game = true; }')
        replace(main, 'const int result = sceSystemServiceLoadExec("exit", nullptr);',
                'const int result = sceSystemServiceLoadExec(ecam_destination, nullptr);')
        # Return after a failed forwarded boot too, rather than trapping ECAM users in another library.
        replace(main, 'if (exit == porpoise::core::Exit::Home)', 'if (ecam_bridge::request.valid && exit == porpoise::core::Exit::Failed) leave(2);\n        if (exit == porpoise::core::Exit::Home)')
        replace(main, '    g_app.init(&g_gfx, &g_library, &g_settings, g_settings_path, g_options_path, g_saves_path);',
                '    if (ecam_bridge::request.valid && !ecam_bridge::request.library && !forwarded_game) leave(2);\n    g_app.init(&g_gfx, &g_library, &g_settings, g_settings_path, g_options_path, g_saves_path);')
        replace(folder / 'porpoise_paths.hpp', '#define PORPOISE_APP "/app0"', '#define PORPOISE_APP "/app0/retro/porpoise"')
    else:
        folder = source / kind / 'src'
        shutil.copyfile(project / 'port/retro/runtime/ecam_bridge.h', folder / 'ecam_bridge.h')
        main = folder / 'main_shelf.cpp'
        replace(main, '#include "fe_ps5.h"', '#include "fe_ps5.h"\n#include "ecam_bridge.h"')
        replace(main, 'int main() {', f'int main(int argc, char** argv) {{\n    ecam_bridge::Init(argc, argv, "{kind}");')
        before = 'sceSystemServiceLoadExec("exit",nullptr)' if kind == 'nes' else 'sceSystemServiceLoadExec("exit",nullptr)'
        replace(main, before, 'sceSystemServiceLoadExec(ecam_bridge::request.valid ? ecam_bridge::request.returnProgram.c_str() : "exit",nullptr)')
        frontend = source / 'ps5/frontend/fe_ps5.cpp'
        anchor = f'{kind}::ShelfRuntime nes_game(paths.top_dir);'
        replace(frontend, anchor, anchor + '\n    const char* ecam_rom = std::getenv("PS5ECAM_ROM");\n    const bool ecam_forwarded = ecam_rom && *ecam_rom;\n    if (ecam_forwarded) { if (audio) audio->Stop(); ok = nes_game.Start(ecam_rom, renderer); }')
        replace(frontend, 'orbis_web_now_playing("");', 'orbis_web_now_playing("");\n                if (ecam_forwarded) break;')
        # The standalone's old signed helper targets another title. ECAM supplies its
        # own elevation path; do not require or ship another title's opaque helper.
        build = source / kind / 'tools/build_shelf.py'
        text = build.read_text()
        marker = "baseline=Path(os.environ['PS5NES_LAPY_BASELINE'])" if kind == 'nes' else "baseline=os.environ.get('PS5N64_LAPY_BASELINE')"
        if marker in text:
            start = text.index(marker)
            stop = text.index("licenses=app/'licenses'", start)
            text = text[:start] + '# ECAM runtime: no title-specific standalone Lapy helper.\n' + text[stop:]
            build.write_text(text)
        # Donor resources remain embedded; no external standalone installation is required.
        replace(frontend, 'original_ui.asset_dir=paths.top_dir+"/porpoise-ui";', f'original_ui.asset_dir="/app0/retro/{kind}/porpoise-ui";')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=['porpoise', 'n64', 'nes', 'mupen64plus', 'fceumm'])
    parser.add_argument('source', type=Path)
    args = parser.parse_args()
    prepare(args.source.resolve(), args.kind, Path(__file__).resolve().parents[2])
