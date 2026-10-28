#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


library = module('retro-library')
performance = module('retro-performance')


class Tools(unittest.TestCase):
    def test_import_crc_cache_and_artwork(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            rom = root / 'game.nes'
            rom.write_bytes(b'NES\x1a' + bytes(12))
            (root / 'art.png').write_bytes(b'fixture')
            xml = root / 'gamelist.xml'
            xml.write_text('<gameList><game><path>./game.nes</path><name>Named game</name><image>./art.png</image></game><game><path>../outside.nes</path><name>Outside</name></game></gameList>')
            index = library.build(root, xml, crc=True)
            game = json.loads(index.read_text())['games']['game.nes']
            self.assertEqual(game['name'], 'Named game')
            self.assertEqual(game['image'], 'art.png')
            self.assertEqual(game['crc32'], f'{zlib.crc32(rom.read_bytes()):08x}')
            self.assertNotIn('../outside.nes', json.loads(index.read_text())['games'])
            self.assertEqual(rom.read_bytes(), b'NES\x1a' + bytes(12))
            # Changed content invalidates the cached checksum; no writes to the ROM.
            rom.write_bytes(b'changed')
            game = json.loads(library.build(root, crc=True).read_text())['games']['game.nes']
            self.assertEqual(game['crc32'], f'{zlib.crc32(b"changed"):08x}')
            self.assertEqual(game['name'], 'Named game')
            self.assertEqual(game['image'], 'art.png')

    def test_entity_and_symlink_refusal(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            xml = root / 'gamelist.xml'
            xml.write_text('<!DOCTYPE x [<!ENTITY a "entity">]><gameList/>')
            with self.assertRaises(ValueError):
                library.build(root, xml)
            (root / '.ecam-library.json').symlink_to(xml)
            with self.assertRaises(ValueError):
                library.build(root)

    def test_performance_units_and_missing_samples(self):
        line = '[emu] VI 600: 60 VI/s 100% (cpu 20  rsp 10  rdp 30  vi 5  idle 35), audio queued 512 (target 1024), underruns 2'
        sample = list(performance.samples([line]))[0]
        self.assertEqual(sample['vi_per_second'], 60.0)
        self.assertEqual(sample['rdp_percent'], 30)
        self.assertEqual(sample['audio_underruns'], 2)
        self.assertEqual(list(performance.samples(['unrelated log'])), [])


if __name__ == '__main__':
    unittest.main()
