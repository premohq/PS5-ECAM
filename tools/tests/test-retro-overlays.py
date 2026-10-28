#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate immutable donor overlay anchors, idempotence and provenance without modifying donors."""
from pathlib import Path
import argparse
import ast
import importlib.util
import json
import shutil
import subprocess
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('ecam_prepare', ROOT / 'tools/retro/prepare.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


def paths_for(kind):
    if kind == 'porpoise':
        return ['src/porpoise_main.cpp', 'src/porpoise_paths.hpp']
    if kind in ('mupen64plus', 'fceumm'):
        paths = ['ps5/coreorbis/main-boot.cpp', 'ps5/coreorbis/orbis-shims/ProsperoJailbreak.h']
        if kind == 'mupen64plus':
            paths += ['ps5/frontend/fe_update.cpp']
        return paths
    return [f'{kind}/src/main_shelf.cpp', 'ps5/frontend/fe_ps5.cpp', f'{kind}/tools/build_shelf.py']


def check(sources=None, systems=None):
    pins = json.loads((ROOT / 'tools/retro/sources.json').read_text())['sources']
    for pin in pins:
        kind = pin['name']
        if systems and kind not in systems:
            continue
        paths = paths_for(kind)
        if sources:
            donor = sources / Path(pin['path']).name
            actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=donor, text=True).strip()
            assert actual == pin['commit'], f'{kind}: expected pinned donor revision'
            originals = {path: (donor / path).read_bytes() for path in paths}
        else:
            # CI downloads only the files tested, from full immutable commit URLs. No release binaries.
            originals = {}
            for path in paths:
                url = f'https://raw.githubusercontent.com/{pin["repository"]}/{pin["commit"]}/{path}'
                with urllib.request.urlopen(url, timeout=60) as response:
                    originals[path] = response.read(4 * 1024 * 1024 + 1)
                assert len(originals[path]) <= 4 * 1024 * 1024
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp)
            for path, content in originals.items():
                (target / path).parent.mkdir(parents=True, exist_ok=True)
                (target / path).write_bytes(content)
            prepare.prepare(target, kind, ROOT)
            first = {str(path.relative_to(target)): path.read_bytes() for path in target.rglob('*') if path.is_file()}
            prepare.prepare(target, kind, ROOT)
            assert first == {str(path.relative_to(target)): path.read_bytes() for path in target.rglob('*') if path.is_file()}, 'overlay must be idempotent'
            main = (target / paths[0]).read_text()
            assert 'ecam_bridge::Init' in main
            if kind == 'porpoise':
                assert main.index('const char* ecam_destination') < main.index('ps5::memory::finish()')
            elif kind in ('mupen64plus', 'fceumm'):
                assert 'sceSystemServiceLoadExec(ecam_bridge::ReturnProgram(), nullptr)' in main
                assert 'if (!again || (ecam_bridge::request.valid && !ecam_bridge::request.library))' in main
                assert 'ecam_bridge::request.valid ? ecam_bridge::request.rom' in main
                assert '9136' in (target / paths[1]).read_text() if kind == 'mupen64plus' else '9137' in (target / paths[1]).read_text()
                if kind == 'mupen64plus':
                    assert 'return {}; // ECAM bundle: standalone self-updater disabled.' in (target / paths[2]).read_text()
            else:
                ast.parse((target / paths[2]).read_text())
                assert '_LAPY_BASELINE' not in (target / paths[2]).read_text()
                frontend = (target / paths[1]).read_text()
                assert 'ok = nes_game.Start(ecam_rom, renderer)' in frontend and 'if (ecam_forwarded) break;' in frontend
                if kind in ('gbc', 'gba'):
                    assert main.index('ecam_bridge::Init') > main.index('elevation::request')
                    assert 'sceSystemServiceLoadExec(ecam_bridge::ReturnProgram(),nullptr)' in main
                    assert 'Lapy-MIT.txt' not in (target / paths[2]).read_text()
            if sources:
                assert originals == {path: (donor / path).read_bytes() for path in paths}
        print(f'PASS {kind}: exact pinned overlay, idempotence, launch/return anchors, untouched donor source')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources', nargs='?', type=Path, help='Directory containing the pinned donor checkouts')
    parser.add_argument("--systems", nargs="+", choices=[p["name"] for p in json.loads((ROOT / "tools/retro/sources.json").read_text())["sources"]])
    args = parser.parse_args()
    check(args.sources.resolve() if args.sources else None, args.systems)
