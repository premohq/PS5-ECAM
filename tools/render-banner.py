#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw the README's banner, with nothing but the standard library (and fontTools for --title-font).

    render-banner.py [--title-font FONT | --title-from SVG] OUTPUT_SVG

writes docs/banner.svg (1280x320): PS5-ECAM's three sides, in the order of the launcher's switch,
in three equal bands with each console in the middle of its own and the name across the three under
them. On the left Cemu's: the Wii U GamePad (tools/render-icons.py's shapes) on the Wii U Homebrew
Launcher's background as the launcher draws it (tools/render-background.py: its gradient and the
discs of the top 320 rows of its 1280x720 screen), dimmed to the app's dark blue. In the middle
Azahar's: the 3DS on the 3DS Homebrew Launcher's waves, glowing gold on a dark amber ground
(port/frontend/wave.cpp's colours). On the right melonDS's: the DS Lite on rounded pixels rising in
melonDS's green (the launcher's DS motif, Shell::DrawPixels). Where the sides meet, a soft seam;
each console glows in its side's colour. The trailer opens and closes on it.

--title-font draws the name from the developer's copy of its typeface; --title-from takes the
outlined name from a banner drawn before (docs/banner.svg), so the banner can be drawn again
without the font.
"""

import importlib.util
import math
import os
import random
import re
import sys

W, H = 1280, 320
HALF = W // 2
SEAMS = (round(W / 3), round(2 * W / 3))  # three equal sides: the Wii U's, the 3DS's, the DS's
CENTRES = (round(W / 6), HALF, round(5 * W / 6))  # each console in the middle of its side
T = 172  # a device's unit square, in banner pixels: the same for the three
ROW = 82  # the consoles' middles, on one line
BASELINE, TAGLINE = 250, 294  # the name's and the tagline's, under the consoles
TITLE_SCALE = 1.4  # the name, drawn 88 pixels high, as large as the banner allows
OVERLAY, WAVE_OVERLAY = 0.7, 0.8  # dark, as the app's start screen has its two sides
BODY = '#f4f8fc'
BLUE = {'top': '#163b62', 'bottom': '#071225', 'accent': '#5aa9ff', 'detail': '#6a7e93'}
GOLD = {'top': '#6b4a0e', 'bottom': '#170f04', 'accent': '#f4b63f', 'detail': '#8f826c'}
# melonDS's green (nds, #7ed957)
GREEN = {'top': '#1f5a18', 'bottom': '#071a05', 'accent': '#7ed957', 'detail': '#748c6c'}
MELON_TOP, MELON_BOTTOM, MELON_OVERLAY = (126, 217, 87), (52, 128, 36), 0.8
WAVE_TOP, WAVE_BOTTOM = (255, 204, 64), (236, 158, 22)
# top, wavelength, amplitude, colour, alpha: port/frontend/wave.cpp's layers, at the banner's size
WAVES = [(196, 430, 12, '#f4b63f', 0.16), (228, 320, 9, '#f8c65a', 0.18), (262, 240, 7, '#ffd98a', 0.20)]
FONT = "'Segoe UI', 'Helvetica Neue', Helvetica, Arial, sans-serif"


def background():
    spec = importlib.util.spec_from_file_location(
        'render_background', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'render-background.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Device:
    """SVG shapes in a device's unit square, T pixels wide, its corner at (ox, oy)."""

    def __init__(self, ox, oy, palette, gradient, size=T):
        self.ox, self.oy, self.palette, self.gradient, self.t = ox, oy, palette, gradient, size

    def x(self, u):
        return round(self.ox + u * self.t, 2)

    def y(self, v):
        return round(self.oy + v * self.t, 2)

    def box(self, cx, cy, hw, hh, r, fill, extra=''):
        return (f'<rect x="{self.x(cx - hw)}" y="{self.y(cy - hh)}" width="{round(2 * hw * self.t, 2)}" '
                f'height="{round(2 * hh * self.t, 2)}" rx="{round(r * self.t, 2)}" fill="{fill}"{extra}/>')

    def dot(self, cx, cy, r, fill):
        return f'<circle cx="{self.x(cx)}" cy="{self.y(cy)}" r="{round(r * self.t, 2)}" fill="{fill}"/>'

    def play(self, cx, cy, size):
        # render-icons.py's play(): from cx - 0.45 size to its tip at cx + 0.55 size
        half = size * 0.62 / 1.15
        return (f'<polygon points="{self.x(cx - 0.45 * size)},{self.y(cy - half)} {self.x(cx - 0.45 * size)},'
                f'{self.y(cy + half)} {self.x(cx + 0.55 * size)},{self.y(cy)}" fill="{self.palette["accent"]}"/>')


def gamepad(d):
    accent, detail = d.palette['accent'], d.palette['detail']
    shapes = [
        d.box(0.5, 0.53, 0.40, 0.205, 0.085, BODY, ' filter="url(#shadow)"'),
        d.box(0.5, 0.52, 0.205, 0.145, 0.02, accent),
        d.box(0.5, 0.52, 0.19, 0.13, 0.014, f'url(#{d.gradient})'),
        d.play(0.515, 0.52, 0.09),
    ]
    for cx in (0.175, 0.825):  # sticks
        shapes += [d.dot(cx, 0.43, 0.04, detail), d.dot(cx, 0.43, 0.024, BODY)]
    shapes += [d.box(0.175, 0.585, 0.045, 0.014, 0.005, detail), d.box(0.175, 0.585, 0.014, 0.045, 0.005, detail)]
    for cx, cy, c in ((0.825, 0.545, accent), (0.86, 0.585, detail), (0.79, 0.585, detail), (0.825, 0.625, detail)):
        shapes.append(d.dot(cx, cy, 0.017, c))
    return shapes


def n3ds(d):
    accent, detail = d.palette['accent'], d.palette['detail']
    shapes = [
        f'<g filter="url(#shadow)">{d.box(0.5, 0.4925, 0.285, 0.03, 0.02, detail)}'
        f'{d.box(0.5, 0.30, 0.31, 0.175, 0.05, BODY)}{d.box(0.5, 0.685, 0.31, 0.175, 0.05, BODY)}</g>',
        d.box(0.5, 0.30, 0.215, 0.135, 0.016, accent),
        d.box(0.5, 0.30, 0.2, 0.12, 0.01, f'url(#{d.gradient})'),
        d.play(0.51, 0.30, 0.085),
        d.box(0.5, 0.665, 0.13, 0.1, 0.012, detail),
        d.box(0.5, 0.665, 0.12, 0.09, 0.008, f'url(#{d.gradient})'),
        d.dot(0.27, 0.6, 0.042, detail), d.dot(0.27, 0.6, 0.028, BODY),
        d.box(0.27, 0.735, 0.038, 0.012, 0.004, detail), d.box(0.27, 0.735, 0.012, 0.038, 0.004, detail),
    ]
    for cx, cy, c in ((0.73, 0.6, accent), (0.765, 0.635, detail), (0.695, 0.635, detail), (0.73, 0.67, detail)):
        shapes.append(d.dot(cx, cy, 0.0155, c))
    for cx in (0.43, 0.5, 0.57):  # Select, Home, Start
        shapes.append(d.box(cx, 0.8, 0.022, 0.007, 0.007, detail))
    return shapes


def nds(d):
    """render-icons.py's DS Lite: two screens of the same size (4:3), the top one framed by the speakers."""
    accent, detail = d.palette['accent'], d.palette['detail']
    shapes = [
        f'<g filter="url(#shadow)">{d.box(0.5, 0.495, 0.27, 0.03, 0.02, detail)}'
        f'{d.box(0.5, 0.30, 0.30, 0.17, 0.045, BODY)}{d.box(0.5, 0.69, 0.30, 0.17, 0.045, BODY)}</g>',
        d.box(0.5, 0.30, 0.155, 0.12, 0.014, accent),
        d.box(0.5, 0.30, 0.14, 0.105, 0.008, f'url(#{d.gradient})'),
        d.play(0.51, 0.30, 0.08),
        d.box(0.5, 0.685, 0.15, 0.115, 0.012, detail),
        d.box(0.5, 0.685, 0.14, 0.105, 0.008, f'url(#{d.gradient})'),
        d.box(0.27, 0.66, 0.04, 0.013, 0.004, detail), d.box(0.27, 0.66, 0.013, 0.04, 0.004, detail),
    ]
    for cx in (0.285, 0.715):  # the speakers
        shapes += [d.dot(cx, cy, 0.008, detail) for cy in (0.27, 0.30, 0.33)]
    for cx, cy, c in ((0.765, 0.66, accent), (0.73, 0.695, detail), (0.73, 0.625, detail), (0.695, 0.66, detail)):
        shapes.append(d.dot(cx, cy, 0.0155, c))
    shapes += [d.dot(0.735, cy, 0.008, detail) for cy in (0.75, 0.78)]  # Start and Select
    return shapes


def pixels():
    """The DS side's motif as the launcher draws it (Shell::DrawPixels): squares with rounded corners in
    melonDS's green, rising, fewer and larger than the bubbles; a fixed scatter, so the file stays the same."""
    rng = random.Random(1144)
    squares = []
    for _ in range(26):
        size = rng.uniform(9, 30)
        x, y = rng.uniform(SEAMS[1] - 10, W + 10), rng.uniform(-10, H + 10)
        alpha = rng.uniform(0.14, 0.42)
        squares.append(f'<rect x="{x - size / 2:.1f}" y="{y - size / 2:.1f}" width="{size:.1f}" height="{size:.1f}" '
                       f'rx="{size * 0.22:.1f}" fill="{GREEN["accent"]}" fill-opacity="{alpha:.2f}"/>')
    return squares


def title_from(path):
    """The outlined name of a banner drawn before: the path in its title's group."""
    with open(path, encoding='utf-8') as file:
        found = re.search(r'filter="url\(#text-shadow\)"[^>]*>\s*<path d="([^"]+)"', file.read())
    if not found:
        sys.exit(f'{path} has no outlined name')
    return found.group(1)


def wave(x0, x1, top, length, amplitude):
    points = [f'{x},{top + amplitude * (1 - math.cos(2 * math.pi * (x - x0) / length)):.1f}' for x in range(x0, x1 + 1, 8)]
    return f'M{x0},{H} L' + ' L'.join(points) + f' L{x1},{H} Z'


def outlined(font_path, text, centre_x, baseline, size, tracking):
    """The app's name as paths, not text: the name's typeface (Typodermic's Zrnic) may be used for
    fixed artwork such as an outlined SVG, but not shared, so the banner carries its letters' shapes
    and never the font. Centred on centre_x, each letter its advance plus tracking apart."""
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.ttLib import TTFont
    font = TTFont(font_path)
    glyphs = font.getGlyphSet()
    cmap = font.getBestCmap()
    scale = size / font['head'].unitsPerEm
    names = [cmap[ord(c)] for c in text]
    width = sum(glyphs[n].width * scale for n in names) + tracking * (len(names) - 1)
    x = centre_x - width / 2
    paths = []
    for name in names:
        pen = SVGPathPen(glyphs)
        glyphs[name].draw(TransformPen(pen, (scale, 0, 0, -scale, x, baseline)))
        if pen.getCommands():
            paths.append(pen.getCommands())
        x += glyphs[name].width * scale + tracking
    return ' '.join(paths)


def main():
    args = sys.argv[1:]
    # the name is drawn 88 pixels high at baseline 176, then scaled and moved under the consoles, so
    # --title-from finds it as it was made
    place = f'transform="translate({HALF} {BASELINE}) scale({TITLE_SCALE}) translate({-HALF} -176)"'
    title = f'<text x="{HALF}" y="176" font-size="88" font-weight="800" letter-spacing="-1" fill="{BODY}" {place}>Playtendoh</text>'
    if len(args) >= 2 and args[0] == '--title-font':
        title = f'<path d="{outlined(args[1], "Playtendoh", HALF, 176, 88, 1)}" {place} fill="{BODY}"/>'
        args = args[2:]
    elif len(args) >= 2 and args[0] == '--title-from':
        title = f'<path d="{title_from(args[1])}" {place} fill="{BODY}"/>'
        args = args[2:]
    if len(args) != 1:
        sys.exit(__doc__)
    sys.argv = [sys.argv[0]] + args
    left, middle = SEAMS
    hbl = background()
    top = '#%02x%02x%02x' % hbl.TOP
    # the gradient runs over the launcher's whole 720 rows; the banner shows the top 320
    bottom = '#%02x%02x%02x' % tuple(round(a + (b - a) * H / 720) for a, b in zip(hbl.TOP, hbl.BOTTOM))
    discs = []
    for x, y, radius, alpha in hbl.particles():
        cx, cy, r = x * 1280 - 320, y * 720, radius * 1280
        if r >= 0.5 and cy - r < H and cx - r < left and cx + r > 0:
            discs.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" fill="#fff" fill-opacity="{alpha:.2f}"/>')
    waves = [f'<path d="{wave(left, middle, t, l, a)}" fill="{c}" fill-opacity="{alpha}"/>' for t, l, a, c, alpha in WAVES]
    # each console's unit square placed so its body's middle is on the row: the GamePad's at v 0.53,
    # the 3DS's at 0.4925 (0.125 to 0.86), the DS's at 0.495 (0.13 to 0.86)
    devices = [gamepad(Device(CENTRES[0] - T / 2, ROW - 0.53 * T, BLUE, 'screen-blue')),
               n3ds(Device(CENTRES[1] - T / 2, ROW - 0.4925 * T, GOLD, 'screen-gold')),
               nds(Device(CENTRES[2] - T / 2, ROW - 0.495 * T, GREEN, 'screen-green'))]
    glows = [f'<ellipse cx="{cx}" cy="{ROW}" rx="150" ry="80" fill="{palette["accent"]}" fill-opacity="{opacity}" filter="url(#glow)"/>'
             for cx, palette, opacity in zip(CENTRES, (BLUE, GOLD, GREEN), (0.34, 0.32, 0.28))]
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" role="img" aria-label="PS5-ECAM: Cemu, Azahar and melonDS, the Wii U, Nintendo 3DS and DS emulators, on PlayStation 5 homebrew">
  <defs>
    <clipPath id="tile"><rect width="{W}" height="{H}" rx="32"/></clipPath>
    <clipPath id="left"><rect width="{left}" height="{H}"/></clipPath>
    <clipPath id="gold"><rect x="{left}" width="{middle - left}" height="{H}"/></clipPath>
    <clipPath id="green"><rect x="{middle}" width="{W - middle}" height="{H}"/></clipPath>
    <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{top}"/>
      <stop offset="1" stop-color="{bottom}"/>
    </linearGradient>
    <linearGradient id="sand" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#{'%02x%02x%02x' % WAVE_TOP}"/>
      <stop offset="1" stop-color="#{'%02x%02x%02x' % WAVE_BOTTOM}"/>
    </linearGradient>
    <linearGradient id="melon" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#{'%02x%02x%02x' % MELON_TOP}"/>
      <stop offset="1" stop-color="#{'%02x%02x%02x' % MELON_BOTTOM}"/>
    </linearGradient>
    <linearGradient id="screen-blue" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{BLUE['top']}"/>
      <stop offset="1" stop-color="{BLUE['bottom']}"/>
    </linearGradient>
    <linearGradient id="screen-gold" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{GOLD['top']}"/>
      <stop offset="1" stop-color="{GOLD['bottom']}"/>
    </linearGradient>
    <linearGradient id="screen-green" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{GREEN['top']}"/>
      <stop offset="1" stop-color="{GREEN['bottom']}"/>
    </linearGradient>
    <filter id="shadow" x="-20%" y="-30%" width="140%" height="170%">
      <feDropShadow dx="0" dy="{round(0.02 * T, 1)}" stdDeviation="{round(0.02 * T, 1)}" flood-color="#000" flood-opacity="0.45"/>
    </filter>
    <filter id="text-shadow" x="-5%" y="-30%" width="110%" height="160%">
      <feDropShadow dx="0" dy="2" stdDeviation="3" flood-color="#000" flood-opacity="0.45"/>
    </filter>
    <filter id="soft" x="-20%" y="-60%" width="140%" height="220%"><feGaussianBlur stdDeviation="26"/></filter>
    <filter id="glow" x="-50%" y="-80%" width="200%" height="260%"><feGaussianBlur stdDeviation="34"/></filter>
    <linearGradient id="seam" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#fff" stop-opacity="0"/>
      <stop offset="0.5" stop-color="#fff" stop-opacity="0.45"/>
      <stop offset="1" stop-color="#fff" stop-opacity="0"/>
    </linearGradient>
  </defs>
  <g clip-path="url(#tile)">
    <g clip-path="url(#left)">
      <rect width="{left}" height="{H}" fill="url(#sky)"/>
      {(chr(10) + '      ').join(discs)}
      <rect width="{left}" height="{H}" fill="#000" fill-opacity="{OVERLAY}"/>
    </g>
    <g clip-path="url(#gold)">
      <rect x="{left}" width="{middle - left}" height="{H}" fill="url(#sand)"/>
      <rect x="{left}" width="{middle - left}" height="{H}" fill="#000" fill-opacity="{WAVE_OVERLAY}"/>
      {(chr(10) + '      ').join(waves)}
    </g>
    <g clip-path="url(#green)">
      <rect x="{middle}" width="{W - middle}" height="{H}" fill="url(#melon)"/>
      <rect x="{middle}" width="{W - middle}" height="{H}" fill="#000" fill-opacity="{MELON_OVERLAY}"/>
      {(chr(10) + '      ').join(pixels())}
    </g>
    <rect x="{left - 1}" width="2" height="{H}" fill="url(#seam)"/>
    <rect x="{middle - 1}" width="2" height="{H}" fill="url(#seam)"/>
    {(chr(10) + '    ').join(glows)}
  </g>
  {(chr(10) + '  ').join(shape for device in devices for shape in device)}
  <rect x="{HALF - 420}" y="{BASELINE - 100}" width="840" height="150" rx="44" fill="#040810" fill-opacity="0.34" filter="url(#soft)"/>
  <g font-family="{FONT}" filter="url(#text-shadow)" text-anchor="middle">
    {title}
    <text x="{HALF}" y="{TAGLINE}" font-size="26" font-weight="600" fill="#e6eef6">Wii U, 3DS and DS on PlayStation 5</text>
  </g>
</svg>
'''
    with open(sys.argv[1], 'w', newline='\n', encoding='utf-8') as out:
        out.write(svg)


if __name__ == '__main__':
    main()
