#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Every EmulationStation theme the app ships has every system PS5-ECAM gives ES-DE.

    tools/check-esde-themes.py <themes folder>     (tools/build-esde.sh: build/esde/es-de/themes)

ES-DE picks a system's art by its theme name (port/esde/esde.cpp's kSystems: wiiu, n3ds, nds, switch,
gc, ... pokemini), from each of a theme's per-system folders (its logos, backgrounds, icons, metadata).
A per-system folder is one that holds several systems' files; each must hold a file or folder for
every system here, or that system shows without its art in that theme. Exits 1, naming what is
missing, when one does not.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
# a folder of per-system files: it has several of these
PROBE = {"nes", "snes", "n64", "gba", "gc", "megadrive", "psx"}


def systems():
    source = (ROOT / "port/esde/esde.cpp").read_text()
    block = source[source.index("constexpr Known kSystems[] = {"):]
    block = block[:block.index("};")]
    names = re.findall(r'\{"([a-z0-9]+)", "', block)
    if len(names) < 14:
        sys.exit(f"port/esde/esde.cpp: kSystems has {len(names)} systems, not read right")
    return names


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    themes = pathlib.Path(sys.argv[1])
    wanted = systems()
    problems = 0
    found = 0
    for theme in sorted(p for p in themes.iterdir() if p.is_dir()):
        folders = 0
        for folder in sorted(p for p in theme.rglob("*") if p.is_dir() and ".git" not in p.parts):
            stems = {entry.name.split(".")[0].lower() for entry in folder.iterdir()}
            if len(stems & PROBE) < 3:
                continue
            folders += 1
            missing = [name for name in wanted if name not in stems]
            if missing:
                problems += 1
                print(f"{theme.name}: {folder.relative_to(theme)} has no {', '.join(missing)}")
        if folders == 0:
            problems += 1
            print(f"{theme.name}: no per-system folder found")
        found += 1
    if found == 0:
        sys.exit(f"no themes in {themes}")
    print(f"{found} themes, each with art for all {len(wanted)} systems" if not problems else f"{problems} problems")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
