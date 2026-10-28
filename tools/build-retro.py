#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the three pinned ECAM runtimes in disposable worktrees (AI-assisted). No installation."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(*args, cwd=None):
    subprocess.run(args, cwd=cwd, check=True)


def build(kind, prepare_only=False, reuse_prepared=False):
    pin = next(p for p in json.loads((ROOT / 'tools/retro/sources.json').read_text())['sources'] if p['name'] == kind)
    source = Path(os.environ.get('ECAM_' + kind.upper() + '_SOURCE', str(ROOT / pin['path']))).resolve()
    if not prepare_only and not shutil.which('clang-18'):
        raise RuntimeError('The pinned native builds require clang-18 and prepared PS5 dependencies; use --prepare-only to inspect source overlays')
    if not (source / '.git').exists():
        source.parent.mkdir(parents=True, exist_ok=True)
        run('git', 'clone', '--no-checkout', 'https://github.com/' + pin['repository'] + '.git', str(source))
    # Separate worktree: existing dependency trees, patches and user changes are not modified.
    if subprocess.run(['git', 'cat-file', '-e', pin['commit']], cwd=source, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        run('git', 'fetch', 'origin', pin['commit'], cwd=source)
    overlay_hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT / 'tools/retro/prepare.py', ROOT / 'port/retro/runtime/ecam_bridge.h']}
    preparation = {**pin, 'overlay_sha256': overlay_hashes}
    tree = ROOT / 'build/retro-source' / kind
    tree.parent.mkdir(parents=True, exist_ok=True)
    if tree.exists():
        if not reuse_prepared or not (tree / 'ECAM-PREPARED.json').is_file():
            raise RuntimeError(f'{tree} exists; use --reuse-prepared after reviewing it, or move it aside')
        recorded = json.loads((tree / 'ECAM-PREPARED.json').read_text())
        actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=tree, text=True).strip()
        if recorded != preparation or actual != pin['commit']:
            raise RuntimeError('Prepared source does not match the pinned revision')
    else:
        run('git', 'worktree', 'add', '--detach', str(tree), pin['commit'], cwd=source)
        run('python3', str(ROOT / 'tools/retro/prepare.py'), kind, str(tree))
        (tree / 'ECAM-PREPARED.json').write_text(json.dumps(preparation, indent=2) + '\n')
    if prepare_only:
        print(f'Prepared pinned {kind} source overlay at {tree}; no binary built')
        return
    if kind in ('mupen64plus', 'fceumm'):
        options = ['VULKAN=' + os.environ.get('ECAM_N64_VULKAN', '1')] if kind == 'mupen64plus' else []
        run('make', '-C', 'ps5', 'app', 'TITLE_ID=PPSA99360', *options, cwd=tree)
        app_folder = 'app-cpu' if kind == 'mupen64plus' and options == ['VULKAN=0'] else 'app'
        package = tree / 'ps5/build' / app_folder / 'PPSA99360'
    elif kind == 'porpoise':
        run('bash', 'tools/build-porpoise.sh', cwd=tree)
        title = json.loads((tree / 'sce_sys/param.json').read_text())['titleId']
        package = tree / 'dist' / title
    else:
        run('bash', f'{kind}/tools/build.sh', cwd=tree)
        title = 'PPSA99206' if kind == 'n64' else 'PPSA99204'
        package = tree / kind / 'build-shelf/package' / title
    if not (package / 'eboot.bin').is_file():
        raise RuntimeError(f'Runtime build did not produce {package}/eboot.bin')
    target = ROOT / 'build/retro' / kind
    if target.exists():
        raise RuntimeError(f'{target} exists; move the previous staged runtime aside first')
    shutil.copytree(package, target, ignore=shutil.ignore_patterns('sce_sys', 'lapy.elf', 'lapy-manifest.json'))
    modified = subprocess.check_output(['git', 'diff', '--name-only'], cwd=tree, text=True).splitlines()
    modified += [('ps5/coreorbis/ecam_bridge.h' if kind in ('mupen64plus', 'fceumm') else 'src/ecam_bridge.h' if kind == 'porpoise' else kind + '/src/ecam_bridge.h')]
    hashes = {path: hashlib.sha256((tree / path).read_bytes()).hexdigest() for path in modified if (tree / path).is_file()}
    (target / 'ECAM-SOURCE.json').write_text(json.dumps({**pin, 'integration': 'ECAM source overlay; console validation required', 'modified_sources_sha256': hashes}, indent=2) + '\n')
    shutil.copy2(ROOT / 'LICENSE', target / 'ECAM-LICENSE.txt')
    shutil.copy2(ROOT / 'tools/retro/BRIDGE-LICENSE.txt', target / 'ECAM-BRIDGE-LICENSE.txt')
    # Some donors' app target omits notices; retain tracked source/component notices explicitly.
    for relative in subprocess.check_output(['git', 'ls-files'], cwd=tree, text=True).splitlines():
        path = Path(relative)
        if path.name.upper().startswith(('LICENSE', 'LICENCE', 'COPYING', 'NOTICE', 'THIRD_PARTY')) and (tree / path).is_file():
            destination = target / 'licenses/source' / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(tree / path, destination)
    print(f'Staged {kind}: {target}; no installation performed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reuse-prepared', action='store_true', help='Build an existing reviewed source overlay at the pinned revision')
    parser.add_argument('--prepare-only', action='store_true', help='Prepare pinned source overlays without building or installing')
    parser.add_argument('systems', nargs='+', choices=['porpoise', 'n64', 'nes', 'mupen64plus', 'fceumm'])
    args = parser.parse_args()
    for kind in args.systems:
        build(kind, args.prepare_only, args.reuse_prepared)
