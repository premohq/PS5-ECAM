#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Switch's and Switch 2's screenshots for Orbit's background (port/app/shots.cpp): each game's first
eShop screenshot, from blawar/titledb's US.en.json, which is far too large for the console. A line for each
title with one: its title ID, the picture's name at img-eshop.cdn.nintendo.net/i/, its name as
shots.cpp's Key compares it. No picture is kept here, only what names it.

    render-switch-shots.py [OUTPUT] [--from US.en.json]     default: port/app/gametdb/switch-shots.tsv.gz
"""
import gzip
import json
import pathlib
import re
import sys
import urllib.request

URL = "https://raw.githubusercontent.com/blawar/titledb/master/US.en.json"
ROOT = pathlib.Path(__file__).resolve().parent.parent


def key(name):
    name = re.sub(r"[(\[].*?[)\]]", "", name)
    return re.sub(r"[^a-z0-9]", "", name.encode("ascii", "ignore").decode().lower())


def main(argv):
    args = list(argv)
    source = None
    if "--from" in args:
        at = args.index("--from")
        source = args[at + 1]
        del args[at : at + 2]
    output = pathlib.Path(args[0]) if args else ROOT / "port/app/gametdb/switch-shots.tsv.gz"
    text = pathlib.Path(source).read_bytes() if source else urllib.request.urlopen(URL, timeout=300).read()
    rows = {}
    for entry in json.loads(text).values():
        if not isinstance(entry, dict) or not entry.get("id") or not entry.get("screenshots"):
            continue
        found = re.search(r"/i/([0-9a-f]{32,64})\.jpg", entry["screenshots"][0])
        if found:
            rows.setdefault(entry["id"].upper(), (found[1], key(entry.get("name") or "")))
    lines = "".join(f"{id}\t{hash}\t{name}\n" for id, (hash, name) in sorted(rows.items()))
    with gzip.GzipFile(output, "wb", mtime=0) as out:
        out.write(lines.encode())
    print(f"{output}: {len(rows)} titles, {output.stat().st_size} bytes")


if __name__ == "__main__":
    main(sys.argv[1:])
