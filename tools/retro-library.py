#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build an offline ECAM metadata/CRC index; never modifies ROMs, saves or standalone settings.
AI-assisted. Optional gamelist.xml and a JSON CRC32-to-title database; no network downloads.
"""
import argparse
import json
import os
from pathlib import Path
import tempfile
import xml.etree.ElementTree as ET
import zlib

EXTENSIONS = {'.gb', '.gbc', '.gba', '.nes', '.unf', '.unif', '.fds', '.z64', '.n64', '.v64', '.zip', '.7z', '.rom', '.iso', '.gcm', '.rvz', '.ciso', '.gcz', '.wbfs', '.wia', '.wad', '.dol', '.elf'}
MAX_INDEX = 8 * 1024 * 1024


def within(root, value):
    path = (root / value).resolve()
    return path if path.is_relative_to(root) else None


def read_json(path):
    if not path.exists():
        return {}
    if path.is_symlink() or path.stat().st_size > MAX_INDEX:
        raise ValueError('Metadata is a symlink or exceeds 8 MiB')
    value = json.loads(path.read_text())
    if not isinstance(value, dict):
        raise ValueError('Metadata must be an object')
    return value


def build(root, gamelist=None, database=None, crc=False, refresh=False):
    root = root.resolve(strict=True)
    if not root.is_dir():
        raise ValueError('Expected a games folder')
    output = root / '.ecam-library.json'
    old = read_json(output).get('games', {})
    if not isinstance(old, dict):
        old = {}
    names = {}
    if gamelist:
        raw = gamelist.read_bytes()
        if len(raw) > MAX_INDEX or b'<!DOCTYPE' in raw.upper() or b'<!ENTITY' in raw.upper():
            raise ValueError('Oversized XML or entity declarations are not supported')
        for game in ET.fromstring(raw).findall('game'):
            path = within(root, game.findtext('path', ''))
            name = game.findtext('name', '').strip()
            image = within(root, game.findtext('image', ''))
            if path and name and len(name.encode()) <= 512:
                names[path.relative_to(root).as_posix()] = {'name': name}
                if image and image.is_file() and image.suffix.lower() in {'.png', '.jpg', '.jpeg', '.tga'}:
                    names[path.relative_to(root).as_posix()]['image'] = image.relative_to(root).as_posix()
    db = read_json(database) if database else {}
    games = {}
    inspected = 0
    for directory, dirs, files in os.walk(root, followlinks=False):
        depth = len(Path(directory).relative_to(root).parts)
        dirs[:] = sorted(d for d in dirs if not (Path(directory) / d).is_symlink()) if depth < 4 else []
        for name in sorted(files):
            inspected += 1
            if inspected > 100000:
                raise ValueError('Library exceeds 100000 files')
            path = Path(directory) / name
            if path.is_symlink() or path.suffix.lower() not in EXTENSIONS:
                continue
            key = path.relative_to(root).as_posix()
            stat = path.stat()
            previous = old.get(key, {})
            entry = {k: v for k, v in previous.items() if k in {'name', 'image'} and isinstance(v, str)} if isinstance(previous, dict) else {}
            entry.update(names.get(key, {}))
            # Raw file CRC only: byte order/header differences intentionally have different identities.
            # Never treat an archive's CRC as the identity of an emulated ROM inside it.
            if crc and path.suffix.lower() not in {'.zip', '.7z'}:
                prior = old.get(key, {})
                if not refresh and isinstance(prior, dict) and prior.get('size') == stat.st_size and prior.get('mtime_ns') == stat.st_mtime_ns and isinstance(prior.get('crc32'), str):
                    checksum = prior['crc32']
                else:
                    value = 0
                    with path.open('rb') as source:
                        for block in iter(lambda: source.read(1024 * 1024), b''):
                            value = zlib.crc32(block, value)
                    checksum = f'{value & 0xffffffff:08x}'
                after = path.stat()
                if (after.st_size, after.st_mtime_ns) != (stat.st_size, stat.st_mtime_ns):
                    raise ValueError(f'File changed while indexing: {key}')
                entry.update(size=stat.st_size, mtime_ns=stat.st_mtime_ns, crc32=checksum)
                if not entry.get('name') and isinstance(db.get(checksum), str) and len(db[checksum].encode()) <= 512:
                    entry['name'] = db[checksum]
            if entry:
                games[key] = entry
    encoded = (json.dumps({'version': 1, 'games': games}, ensure_ascii=False, indent=2) + '\n').encode()
    if len(encoded) > MAX_INDEX:
        raise ValueError('Generated metadata exceeds 8 MiB')
    descriptor, temporary = tempfile.mkstemp(prefix='.ecam-index-', dir=root)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    parser.add_argument('--gamelist', type=Path)
    parser.add_argument('--crc-database', type=Path, help='JSON object mapping lowercase raw-file CRC32 to title')
    parser.add_argument('--crc', action='store_true', help='Read ROMs to compute/cache raw-file CRC32; may take time for disc images')
    parser.add_argument('--refresh', action='store_true', help='Recompute CRCs even when size and modification time match')
    args = parser.parse_args()
    print(build(args.folder, args.gamelist, args.crc_database, args.crc, args.refresh))
