#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Give Switch 2 the existing Switch art in every shipped ES-DE theme."""

import pathlib
import shutil
import sys


def main():
    if len(sys.argv) != 2:
        sys.exit("tools/add-esde-switch2-theme-alias.py <themes folder>")
    themes = pathlib.Path(sys.argv[1])
    for theme in themes.iterdir():
        if not theme.is_dir():
            continue
        # Only mirror entries named exactly "switch" (with or without an extension). Per-system
        # theme assets conventionally use this stem; preserve their formats and directories.
        for folder in sorted((path for path in theme.rglob("*") if path.is_dir()), key=lambda p: len(p.parts), reverse=True):
            for source in list(folder.iterdir()):
                stem = source.name.split(".", 1)[0].lower()
                if stem != "switch":
                    continue
                suffix = source.name[len("switch"):]
                target = folder / ("switch2" + suffix)
                if target.exists():
                    continue
                if source.is_dir():
                    shutil.copytree(source, target)
                elif source.is_file():
                    shutil.copy2(source, target)


if __name__ == "__main__":
    main()
