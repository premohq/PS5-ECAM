#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Normalize Mupen64Plus PS5 boot-log performance samples to CSV or JSON (AI-assisted).
VI/s is video interrupts per second, not unique rendered frames. No performance claim is inferred.
"""
import argparse
import csv
import json
from pathlib import Path
import re
import sys

SAMPLE = re.compile(r'\[emu\] VI (\d+): ([\d.]+) VI/s ([\d.]+)% \(cpu (\d+)\s+rsp (\d+)\s+rdp (\d+)\s+vi (\d+)\s+idle (\d+)\), audio queued (\d+) \(target (\d+)\), underruns (\d+)')
FIELDS = ['vi_count', 'vi_per_second', 'speed_percent', 'cpu_percent', 'rsp_percent', 'rdp_percent', 'vi_percent', 'idle_percent', 'audio_queued', 'audio_target', 'audio_underruns']


def samples(lines):
    for line in lines:
        match = SAMPLE.search(line)
        if match:
            yield {key: float(value) if key in {'vi_per_second', 'speed_percent'} else int(value) for key, value in zip(FIELDS, match.groups())}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--format', choices=['csv', 'json'], default='csv')
    args = parser.parse_args()
    with args.log.open(errors='replace') as stream:
        rows = list(samples(stream))
    if not rows:
        parser.error('No recognized Mupen64Plus performance samples; enable its FPS display and collect a boot.log')
    if args.format == 'json':
        json.dump({'metric': 'VI/s, not rendered FPS', 'samples': rows}, sys.stdout, indent=2)
        print()
    else:
        writer = csv.DictWriter(sys.stdout, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)
