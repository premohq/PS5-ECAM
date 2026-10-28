#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate integration anchors against the actual pinned donor sources, without modifying them."""
from pathlib import Path
import argparse
import ast
import importlib.util
import json
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('ecam_prepare', ROOT / 'tools/retro/prepare.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


def check(sources):
    pins = json.loads((ROOT / 'tools/retro/sources.json').read_text())['sources']
    for pin in pins:
        kind = pin['name']
        donor = sources / Path(pin['path']).name
        actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=donor, text=True).strip()
        assert actual == pin['commit'], f'{kind}: expected pinned donor revision'
        paths = ['src/porpoise_main.cpp', 'src/porpoise_paths.hpp'] if kind == 'porpoise' else [
            f'{kind}/src/main_shelf.cpp', 'ps5/frontend/fe_ps5.cpp', f'{kind}/tools/build_shelf.py']
        originals = {path: (donor / path).read_bytes() for path in paths}
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp)
            for path in paths:
                (target / path).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(donor / path, target / path)
            prepare.prepare(target, kind, ROOT)
            first = {path: (target / path).read_bytes() for path in paths}
            prepare.prepare(target, kind, ROOT)
            assert first == {path: (target / path).read_bytes() for path in paths}, 'overlay must be idempotent'
            main = (target / paths[0]).read_text()
            assert 'ecam_bridge::Init' in main
            if kind == 'porpoise':
                assert main.index('const char* ecam_destination') < main.index('ps5::memory::finish()')
            else:
                ast.parse((target / paths[2]).read_text())
                assert '_LAPY_BASELINE' not in (target / paths[2]).read_text()
                frontend = (target / paths[1]).read_text()
                assert 'ok = nes_game.Start(ecam_rom, renderer)' in frontend and 'if (ecam_forwarded) break;' in frontend
            assert originals == {path: (donor / path).read_bytes() for path in paths}
        print(f'PASS {kind}: exact pinned overlay, idempotence, launch/return anchors, untouched donor checkout')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources', type=Path, help='Directory containing Porpoise-X, PS5N64X and PS5NESX checkouts')
    check(parser.parse_args().sources.resolve())
