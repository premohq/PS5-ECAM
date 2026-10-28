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
    tree = ROOT / 'build/retro-source' / kind
    tree.parent.mkdir(parents=True, exist_ok=True)
    if tree.exists():
        if not reuse_prepared or not (tree / 'ECAM-PREPARED.json').is_file():
            raise RuntimeError(f'{tree} exists; use --reuse-prepared after reviewing it, or move it aside')
        recorded = json.loads((tree / 'ECAM-PREPARED.json').read_text())
        actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=tree, text=True).strip()
        if recorded != pin or actual != pin['commit']:
            raise RuntimeError('Prepared source does not match the pinned revision')
    else:
        run('git', 'worktree', 'add', '--detach', str(tree), pin['commit'], cwd=source)
        run('python3', str(ROOT / 'tools/retro/prepare.py'), kind, str(tree))
        (tree / 'ECAM-PREPARED.json').write_text(json.dumps(pin, indent=2) + '\n')
    if prepare_only:
        print(f'Prepared pinned {kind} source overlay at {tree}; no binary built')
        return
    if kind == 'porpoise':
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
    modified += ['src/ecam_bridge.h' if kind == 'porpoise' else kind + '/src/ecam_bridge.h']
    hashes = {path: hashlib.sha256((tree / path).read_bytes()).hexdigest() for path in modified if (tree / path).is_file()}
    (target / 'ECAM-SOURCE.json').write_text(json.dumps({**pin, 'integration': 'ECAM source overlay; console validation required', 'modified_sources_sha256': hashes}, indent=2) + '\n')
    shutil.copy2(ROOT / 'LICENSE', target / 'ECAM-LICENSE.txt')
    print(f'Staged {kind}: {target}; no installation performed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reuse-prepared', action='store_true', help='Build an existing reviewed source overlay at the pinned revision')
    parser.add_argument('--prepare-only', action='store_true', help='Prepare pinned source overlays without building or installing')
    parser.add_argument('systems', nargs='+', choices=['porpoise', 'n64', 'nes'])
    args = parser.parse_args()
    for kind in args.systems:
        build(kind, args.prepare_only, args.reuse_prepared)
