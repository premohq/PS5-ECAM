#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw Playtendoh's art: its logo with its name, as the home screen's background (sce_sys/pic0.dds),
its tile (sce_sys/icon0.png) and the README's banner (docs/banner.png).

    render-presentation.py [--title-font FONT] [--tail-font FONT] [--banner PNG] [--wordmark PNG]
                           [--mark-svg SVG] SCE_SYS_DIR [PREVIEW_PNG]

The logo is the launcher's mark at its top left (port/frontend/shell/consoles.cpp, the owner's, 4
November 2026): a dark disc with every console's controller in a ring, in the bar's order from the
top, and the NES pad larger in the middle (where the launcher shows the open side's). The
name, Playtendoh, is beside it on the banner and the background, over the tagline centred under it,
and under it on the square tile; all of it on the launcher's ink. The name is one word in two faces
(the owner's, 3 November 2026): "Play" in the PlayStation logo's lettering (--title-font), "tendoh"
in a Nintendo-like face (--tail-font) at three quarters of the size at which its d would stand as tall
as Play's l and 85 % as wide, on one baseline, every letter a little closer than its face sets it. --mark-svg writes the mark as an SVG drawing, for the site.

The PS5 shows pic0.dds behind the app while it is selected on the home screen, and pic1.dds while it
starts; tools/package.sh installs this one image as both. The logo sits above the middle there: the
shell draws its own title, a Play button and its shading over the lower left. The console takes a
single 3840x2160 BC7_UNORM DX10 DDS without mipmaps (ps5-native-app-boilerplate's
tools/validate-assets.sh), which this encodes itself in BC7's mode 6: per 4x4 block, a line between
two colours, along the block's principal axis, and a 4-bit position on it for each pixel. The tile
is a 512x512 opaque PNG; the console rounds its corners.

This needs Pillow and numpy, and the fonts: the name's is Typodermic's Zrnic, which its licence lets
the developer use for fixed artwork like these images but not share, so it is not in this
repository: --title-font names the developer's own copy (--wordmark: the name alone, white on clear, which the site's page masks;
zrnic rg.otf; without it, Segoe UI Black
from Windows, or DejaVu Sans Bold, stand in), and --tail-font the owner's copy of tendoh's (Sayunu's
Nu Hanafudaya, whose terms allow images made with it but not sharing the font; without one, the
title's font draws the whole name); the tagline's is Segoe UI Semibold (the launcher's
Lexend SemiBold, tools/fonts, elsewhere). The output is committed, so the build needs none of them.
It prints where the background has each side's square, for shell/eden.cpp's kConsoles.
"""

import importlib.util
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 3840, 2160
FONT_DIRS = ["C:/Windows/Fonts", "/mnt/c/Windows/Fonts"]
HEAVY = [os.path.join(d, "seguibl.ttf") for d in FONT_DIRS] + ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]
# the tagline: Segoe UI Semibold from Windows, else the launcher's own Lexend at its semibold weight
LEXEND = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts", "Lexend[wght].ttf")
SEMIBOLD = [os.path.join(d, "seguisb.ttf") for d in FONT_DIRS] + [LEXEND, "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]
NAME = "Playtendoh"
HEAD, TAIL = "Play", "tendoh"  # the name's two faces' parts
TAIL_FONTS = []  # --tail-font
TAGLINE = "An all-in-one emulator for PS5"
TAIL_SCALE, TAIL_WIDTH = 0.75, 0.85  # tendoh beside Play (the owner's): its size and its width
TRACKING = 0.025  # each letter this much of the name's size closer to the last than its face sets it
JOIN = 0.09  # and tendoh's t this much further from Play's y, which would otherwise touch it
TAGLINE_COLOUR = (0xc9, 0xd3, 0xe0)
SUPERSAMPLE = 4


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(os.path.dirname(os.path.abspath(__file__)), file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ICONS = load("render_icons", "render-icons.py")
INK = ICONS.INK
WHITE = ICONS.WHITE + (255,)


def font(candidates, size):
    for path in candidates:
        if os.path.isfile(path):
            face = ImageFont.truetype(path, size)
            if path == LEXEND:
                face.set_variation_by_axes([600])  # SemiBold, as the launcher's tokens use it
            return face
    sys.exit("no font found: " + ", ".join(candidates))


def rise(face, text):
    """How far text's tallest letter rises above the baseline, and its lowest falls below it, in pixels."""
    _, top, _, bottom = face.getbbox(text, anchor="ls")
    return -top, bottom


def letters(face, text, track):
    """text in white, each letter track pixels closer to the last than the face sets them (its kerning
    kept), on a clear image. Returns (image, x of its origin, y of its baseline, its advance)."""
    ascent, descent = rise(face, text)
    pad = math.ceil(face.size * 0.3)
    image = Image.new("RGBA", (math.ceil(face.getlength(text)) + 2 * pad, math.ceil(ascent + descent) + 2 * pad), (255, 255, 255, 0))
    draw = ImageDraw.Draw(image)
    for i, letter in enumerate(text):
        draw.text((pad + face.getlength(text[:i]) - track * i, pad + ascent), letter, font=face, fill=WHITE, anchor="ls")
    return image, pad, pad + ascent, face.getlength(text) - track * (len(text) - 1)


def wordmark(size):
    """The name's two parts as one word on one baseline: HEAD in the title's face at size, TAIL in the
    tail's at TAIL_SCALE of the size at which its d would stand as tall as HEAD's l, TAIL_WIDTH as wide,
    every letter TRACKING closer than the faces set them. Returns (width, ascent, descent, draw),
    draw(image, x, baseline) putting it there, its letters' left edge at x."""
    head = font(HEAVY, size)
    if not TAIL_FONTS:
        parts = [(letters(head, NAME, size * TRACKING), 1.0)]
    else:
        matched = size * rise(head, "l")[0] / max(1, rise(font(TAIL_FONTS, size), "d")[0])
        tail = font(TAIL_FONTS, max(8, round(matched * TAIL_SCALE)))
        parts = [(letters(head, HEAD, size * TRACKING), 1.0), (letters(tail, TAIL, tail.size * TRACKING), TAIL_WIDTH)]
    base = max(part[2] for part, _ in parts)
    canvas = Image.new("RGBA", (sum(part[0].width for part, _ in parts), max(part[0].height for part, _ in parts) + round(base)),
                       (255, 255, 255, 0))
    pen = parts[0][0][1]  # where the next part's origin goes: after the last letter, TRACKING closer
    for (image, origin, baseline, advance), width in parts:
        if width != 1.0:
            image = image.resize((max(1, round(image.width * width)), image.height), Image.LANCZOS)
            origin, advance = origin * width, advance * width
        canvas.alpha_composite(image, (max(0, round(pen - origin)), round(base - baseline)))
        pen += advance - size * TRACKING + size * JOIN
    box = canvas.getchannel("A").getbbox()
    word = canvas.crop(box)
    ascent = base - box[1]
    return word.width, ascent, word.height - ascent, lambda target, x, y: target.alpha_composite(word, (round(x), round(y - ascent)))


# -- the mark: the launcher's at its top left (port/frontend/shell/consoles.cpp, DrawConsole and DrawMark) -----
# a dark disc, every console's controller in a ring on it in the bar's order from the top, and one larger in
# the middle. Each controller is a few rounded boxes in a unit square around its centre: (x, y, w, h, r, part),
# part body, cut (the disc's colour, a hole) or second (the Joy-Con's other colour, the Game Boy's screen), a
# cross as ("cross", x, y, size) and a stroke as ("line", x0, y0, x1, y1, width, part)
BAR = ("wiiu", "switch", "nds", "n3ds", "gamecube", "wii", "n64", "ique", "snes", "satellaview", "nes", "fds", "vs",
       "gba", "gbc", "gb", "sgb", "vb", "pokemini", "gw")  # shell_internal.h's kBarOrder
COLOURS = {"wiiu": 0x5aa9ff, "n3ds": 0xf4b63f, "nds": 0x7ed957, "switch": 0xf2434f,  # ui/tokens.h
           "gamecube": 0x6f6cff, "wii": 0x3fcff0, "n64": 0x2bc994, "nes": 0xff7043, "gb": 0xa8bf3a, "gbc": 0xff5ca8,
           "gba": 0xb98fff, "snes": 0xdb70f0, "vb": 0xe02130, "pokemini": 0xffe14d, "fds": 0xb8325a, "sgb": 0x34c46a,
           "gw": 0xff8c2e, "satellaview": 0xe05ad8, "vs": 0x22c2b3, "ique": 0xff6f91}  # port/retro/registry.h
JOYCON_BLUE = 0x00b8f5
MARK_GROUND, MARK_GROUND_LOW = 0x161d2b, 0x0e131d
MARK_CENTRE = "nes"  # the logo's middle (the owner's): the NES pad, where the launcher shows the open side's


def _pad(cross_x, cross_y, size):
    return [("cross", cross_x, cross_y, size)]


CONSOLES = {
    "wiiu": [(0, 0, 1.0, 0.6, 0.16, "body"), (0, 0, 0.5, 0.38, 0.03, "cut"), (-0.37, -0.12, 0.09, 0.09, 0.045, "cut"),
             (0.37, -0.12, 0.09, 0.09, 0.045, "cut")],
    "switch": [(-0.25, 0, 0.34, 0.86, 0.17, "second"), (-0.16, 0, 0.16, 0.86, 0, "second"), (0.25, 0, 0.34, 0.86, 0.17, "body"),
               (0.16, 0, 0.16, 0.86, 0, "body"), (-0.25, -0.16, 0.12, 0.12, 0.06, "cut"), (0.25, 0.12, 0.12, 0.12, 0.06, "cut")],
    "nds": [(0, -0.24, 0.72, 0.44, 0.08, "body"), (0, -0.24, 0.46, 0.3, 0.02, "cut"), (0, 0.25, 0.72, 0.44, 0.08, "body"),
            (0, 0.24, 0.38, 0.28, 0.02, "cut")],
    "n3ds": [(0, -0.24, 0.72, 0.44, 0.08, "body"), (0, -0.24, 0.56, 0.3, 0.02, "cut"), (0, 0.25, 0.72, 0.44, 0.08, "body"),
             (0, 0.24, 0.38, 0.28, 0.02, "cut"), (-0.27, 0.16, 0.09, 0.09, 0.045, "cut")],
    "gamecube": [(-0.3, 0.14, 0.28, 0.5, 0.14, "body"), (0.3, 0.14, 0.28, 0.5, 0.14, "body"), (0, -0.08, 0.9, 0.46, 0.22, "body"),
                 (-0.22, -0.1, 0.13, 0.13, 0.065, "cut"), (0.2, -0.08, 0.16, 0.16, 0.08, "cut")],
    "wii": [(0, 0, 0.3, 1.0, 0.13, "body")] + _pad(0, -0.3, 0.15) + [(0, -0.1, 0.1, 0.1, 0.05, "cut"),
            (0, 0.22, 0.06, 0.06, 0.03, "cut"), (0, 0.33, 0.06, 0.06, 0.03, "cut")],
    "n64": [(0, -0.2, 0.98, 0.34, 0.17, "body"), (-0.36, 0.12, 0.24, 0.5, 0.12, "body"), (0, 0.1, 0.22, 0.42, 0.11, "body"),
            (0.36, 0.12, 0.24, 0.5, 0.12, "body"), (0, 0.06, 0.09, 0.09, 0.045, "cut")],
    "nes": [(0, 0, 1.0, 0.44, 0.05, "body")] + _pad(-0.27, 0, 0.21) + [(-0.02, 0.05, 0.1, 0.04, 0.02, "cut"),
            (0.2, 0.05, 0.11, 0.11, 0.055, "cut"), (0.35, 0.05, 0.11, 0.11, 0.055, "cut")],
    "snes": [(0, 0, 1.0, 0.5, 0.25, "body")] + _pad(-0.27, 0, 0.19) + [(x, y, 0.08, 0.08, 0.04, "cut")
             for x, y in ((0.3, -0.09), (0.3, 0.09), (0.21, 0.0), (0.39, 0.0))],
    "gb": [(0, 0, 0.66, 1.0, 0.07, "body"), (0, -0.2, 0.52, 0.4, 0.04, "cut"), (0, -0.2, 0.34, 0.28, 0.01, "second")]
          + _pad(-0.15, 0.23, 0.16) + [(0.12, 0.27, 0.09, 0.09, 0.045, "cut"), (0.21, 0.2, 0.09, 0.09, 0.045, "cut")],
    "gbc": [(0, 0, 0.6, 1.0, 0.1, "body"), (0, -0.2, 0.46, 0.4, 0.04, "cut")] + _pad(-0.15, 0.23, 0.16)
           + [(0.12, 0.27, 0.09, 0.09, 0.045, "cut"), (0.21, 0.2, 0.09, 0.09, 0.045, "cut")],
    "gba": [(0, 0, 1.0, 0.56, 0.26, "body"), (0, -0.01, 0.42, 0.34, 0.03, "cut")] + _pad(-0.33, -0.02, 0.13)
           + [(0.31, 0.02, 0.08, 0.08, 0.04, "cut"), (0.39, -0.05, 0.08, 0.08, 0.04, "cut")],
    "vb": [(0, 0.18, 0.1, 0.32, 0, "body"), (0, 0.39, 0.62, 0.08, 0.04, "body"), (0, -0.15, 1.0, 0.4, 0.12, "body"),
           (-0.22, -0.15, 0.28, 0.18, 0.07, "cut"), (0.22, -0.15, 0.28, 0.18, 0.07, "cut")],
    "pokemini": [(0, 0, 0.74, 0.62, 0.14, "body"), (0, -0.06, 0.44, 0.3, 0.03, "cut"), (0.24, 0.17, 0.08, 0.08, 0.04, "cut")],
    "fds": [(0, 0, 0.8, 0.98, 0.05, "body"), (0, -0.26, 0.6, 0.34, 0.03, "cut"), (0, 0.24, 0.24, 0.34, 0.03, "cut")],
    "sgb": [(0, -0.34, 0.4, 0.34, 0.04, "body"), (0, -0.36, 0.26, 0.16, 0.02, "cut"), (0, 0.14, 0.92, 0.62, 0.1, "body"),
            (0, 0.17, 0.6, 0.32, 0.03, "cut")],
    "gw": [(0, 0, 1.0, 0.64, 0.08, "body"), (0, -0.02, 0.5, 0.42, 0.02, "cut"), (-0.38, 0.14, 0.13, 0.13, 0.065, "cut"),
           (0.38, 0.14, 0.13, 0.13, 0.065, "cut"), (0.33, -0.2, 0.08, 0.04, 0.02, "cut"), (0.43, -0.2, 0.08, 0.04, 0.02, "cut")],
    "satellaview": [(-0.06, 0.4, 0.1, 0.16, 0, "body"), (-0.06, 0.47, 0.5, 0.08, 0.04, "body"), (-0.06, -0.04, 0.74, 0.74, 0.37, "body"),
                    (0.06, -0.16, 0.62, 0.62, 0.31, "cut"), ("line", -0.06, -0.04, 0.3, -0.36, 0.07, "body"),
                    (0.32, -0.38, 0.13, 0.13, 0.065, "body")],
    "vs": [(0, 0.04, 0.62, 0.92, 0.06, "body"), (0, -0.42, 0.72, 0.14, 0.03, "body"), (0, -0.14, 0.46, 0.32, 0.03, "cut"),
           (0, 0.12, 0.52, 0.06, 0, "cut"), (-0.08, 0.31, 0.06, 0.12, 0.02, "cut"), (0.08, 0.31, 0.06, 0.12, 0.02, "cut")],
    "ique": [(0, 0.12, 0.42, 0.5, 0.18, "body"), (0, -0.02, 1.0, 0.48, 0.24, "body")] + _pad(-0.3, -0.04, 0.16)
            + [(0.26, -0.08, 0.09, 0.09, 0.045, "cut"), (0.36, 0.0, 0.09, 0.09, 0.045, "cut"), (0, 0.14, 0.1, 0.1, 0.05, "cut")],
}


def rgb_of(value):
    return ((value >> 16) & 255, (value >> 8) & 255, value & 255)


def mix(a, b, t):
    return tuple(round(x * (1 - t) + y * t) for x, y in zip(a, b))


def mark_shapes(diameter):
    """The mark as shapes in a diameter-wide square: ("disc",), then ("rect", x, y, w, h, r, colour) and
    ("line", x0, y0, x1, y1, width, colour), in drawing order."""
    radius = diameter / 2
    cx = cy = radius
    ground, ground_low = rgb_of(MARK_GROUND), rgb_of(MARK_GROUND_LOW)
    out = [("disc",)]

    def console(key, x, y, s, colour):
        second = rgb_of(JOYCON_BLUE) if key == "switch" else mix(colour, (255, 255, 255), 0.5) if key == "gb" else colour
        parts = {"body": colour, "cut": ground, "second": second}
        for shape in CONSOLES[key]:
            if shape[0] == "cross":
                _, px, py, size = shape
                out.append(("rect", x + (px - size / 2) * s, y + (py - size * 0.17) * s, size * s, size * 0.34 * s, 0, ground))
                out.append(("rect", x + (px - size * 0.17) * s, y + (py - size / 2) * s, size * 0.34 * s, size * s, 0, ground))
            elif shape[0] == "line":
                _, x0, y0, x1, y1, width, part = shape
                out.append(("line", x + x0 * s, y + y0 * s, x + x1 * s, y + y1 * s, width * s, parts[part]))
            else:
                px, py, w, h, r, part = shape
                out.append(("rect", x + (px - w / 2) * s, y + (py - h / 2) * s, w * s, h * s, r * s, parts[part]))

    ring = radius * 0.7
    size = min(radius * 0.36, 2 * ring * math.sin(math.pi / len(BAR)) * 0.95)
    for i, key in enumerate(BAR):
        angle = 2 * math.pi * i / len(BAR) - math.pi / 2
        colour = rgb_of(COLOURS[key]) if key == MARK_CENTRE else mix(rgb_of(COLOURS[key]), ground_low, 0.25)
        console(key, cx + math.cos(angle) * ring, cy + math.sin(angle) * ring, size, colour)
    console(MARK_CENTRE, cx, cy, radius * 0.72, rgb_of(COLOURS[MARK_CENTRE]))
    return out


def mark(diameter):
    """The mark as an image, diameter pixels wide: drawn larger and scaled down, for clean edges."""
    d = diameter * SUPERSAMPLE
    t = np.linspace(0.0, 1.0, d)[:, None, None]
    top, low = np.array(rgb_of(MARK_GROUND), dtype=np.float64), np.array(rgb_of(MARK_GROUND_LOW), dtype=np.float64)
    image = Image.fromarray(np.rint((top + (low - top) * t) * np.ones((1, d, 1))).astype(np.uint8), "RGB").convert("RGBA")
    draw = ImageDraw.Draw(image)
    for shape in mark_shapes(d)[1:]:
        if shape[0] == "rect":
            _, x, y, w, h, r, colour = shape
            draw.rounded_rectangle((x, y, x + w, y + h), radius=min(r, w / 2, h / 2), fill=colour)
        else:
            _, x0, y0, x1, y1, width, colour = shape
            draw.line((x0, y0, x1, y1), fill=colour, width=round(width))
            for px, py in ((x0, y0), (x1, y1)):
                draw.ellipse((px - width / 2, py - width / 2, px + width / 2, py + width / 2), fill=colour)
    # the disc, and its edge as the launcher's glass edge draws it (white at a tenth)
    disc = Image.new("L", (d, d), 0)
    ImageDraw.Draw(disc).ellipse((0, 0, d - 1, d - 1), fill=255)
    edge = Image.new("RGBA", (d, d), (255, 255, 255, 0))
    width = max(SUPERSAMPLE, round(d * 0.025))
    ImageDraw.Draw(edge).ellipse((width / 2, width / 2, d - 1 - width / 2, d - 1 - width / 2), outline=(255, 255, 255, 26), width=width)
    image.alpha_composite(edge)
    image.putalpha(disc)
    return image.resize((diameter, diameter), Image.LANCZOS)


def mark_svg(size=218):
    """The mark as an SVG drawing in a size-wide square, for the site (its tab's icon and its pages)."""
    def hexa(colour):
        return "#%02x%02x%02x" % colour
    r = size / 2
    out = [f'<defs><linearGradient id="mark-ground" x1="0" y1="0" x2="0" y2="1">'
           f'<stop offset="0" stop-color="{hexa(rgb_of(MARK_GROUND))}"/><stop offset="1" stop-color="{hexa(rgb_of(MARK_GROUND_LOW))}"/>'
           f'</linearGradient></defs>',
           f'<circle cx="{r}" cy="{r}" r="{r}" fill="url(#mark-ground)"/>']
    for shape in mark_shapes(size)[1:]:
        if shape[0] == "rect":
            _, x, y, w, h, rr, colour = shape
            out.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}"' +
                       (f' rx="{min(rr, w / 2, h / 2):.2f}"' if rr else "") + f' fill="{hexa(colour)}"/>')
        else:
            _, x0, y0, x1, y1, width, colour = shape
            out.append(f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{x1:.2f}" y2="{y1:.2f}" stroke="{hexa(colour)}" '
                       f'stroke-width="{width:.2f}" stroke-linecap="round"/>')
    out.append(f'<circle cx="{r}" cy="{r}" r="{r - 1.36}" fill="none" stroke="#ffffff" stroke-opacity="0.1" stroke-width="2.72"/>')
    return "".join(out)


def shadowed(layer, blur, opacity, drop):
    """A layer with a soft shadow under it, a little lower."""
    shadow = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    shadow.putalpha(layer.getchannel("A").point(lambda a: a * opacity))
    shadow = shadow.transform(layer.size, Image.AFFINE, (1, 0, 0, 0, 1, -drop)).filter(ImageFilter.GaussianBlur(blur))
    return Image.alpha_composite(shadow, layer)


def ground(width, height, lift_y):
    """The launcher's ink, lighter at the top, and a soft lift of light behind the logo."""
    t = np.linspace(0.0, 1.0, height)[:, None, None]
    top, bottom = np.array((0x0d, 0x13, 0x22), dtype=np.float64), np.array((0x05, 0x07, 0x0d), dtype=np.float64)
    image = (top + (bottom - top) * t) * np.ones((1, width, 1))
    yy, xx = np.mgrid[0:height, 0:width]
    d = np.hypot((xx - width / 2) / (width * 0.42), (yy - lift_y) / (height * 0.42))
    image += (np.clip(1.0 - d, 0.0, 1.0) ** 2)[:, :, None] * np.array((18.0, 26.0, 42.0))
    return Image.fromarray(np.clip(np.rint(image), 0, 255).astype(np.uint8), "RGB").convert("RGBA")


def lockup(size, scale, centre_y):
    """The banner's lockup at scale (1 is the README banner's 1280x320 design): the mark, then the
    name over the tagline beside it, centred across size at centre_y."""
    space = 48 * scale
    logo = mark(round(228 * scale))
    (name_width, ascent, descent, name), tag = wordmark(round(116 * scale)), font(SEMIBOLD, round(30 * scale))
    tag_width = tag.getlength(TAGLINE)
    text_width = max(name_width, tag_width)
    # the name over the tagline, the two together centred on the logo: the tagline under the name's
    # lowest letter (Play's y)
    tag_ascent, tag_descent = rise(tag, TAGLINE)
    tag_gap = 18 * scale
    block = ascent + descent + tag_gap + tag_ascent + tag_descent
    name_y = centre_y - block / 2 + ascent
    tag_y = name_y + descent + tag_gap + tag_ascent
    x = round((size[0] - (logo.width + space + text_width)) / 2)
    y = round(centre_y - logo.height / 2)
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    layer.alpha_composite(logo, (x, y))
    text = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(text)
    tx = x + logo.width + space
    # the name and the tagline each centred on the wider of the two
    name(text, tx + (text_width - name_width) / 2, name_y)
    draw.text((tx + (text_width - tag_width) / 2, tag_y), TAGLINE, font=tag, fill=TAGLINE_COLOUR + (255,), anchor="ls")
    return shadowed(Image.alpha_composite(layer, text), 6 * scale, 0.5, max(1, round(3 * scale)))


def picture():
    """pic0: the banner's lockup three times its size, above the screen's middle."""
    centre_y = 864
    image = ground(W, H, centre_y)
    return Image.alpha_composite(image, lockup((W, H), 3, centre_y)).convert("RGB")


def tile():
    """icon0: the mark over the name."""
    size = 512
    image = ground(size, size, 190)
    logo = mark(318)
    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    layer.alpha_composite(logo, ((size - logo.width) // 2, 34))
    name_size = 112
    while name_size > 8 and wordmark(name_size)[0] > 380:
        name_size -= 1
    name_width, _, descent, name = wordmark(name_size)
    name(layer, (size - name_width) / 2, min(448, size - 28 - descent))
    return Image.alpha_composite(image, shadowed(layer, 4, 0.5, 2)).convert("RGB")


def name_alone():
    """The name alone, white on clear and cut to its letters, as the site's page masks it (site/style.css)."""
    width, ascent, descent, name = wordmark(240)
    layer = Image.new("RGBA", (round(width) + 40, round(ascent + descent) + 40), (255, 255, 255, 0))
    name(layer, 20, 20 + ascent)
    return layer.crop(layer.getchannel("A").getbbox())


def banner():
    """The README's banner: the lockup on the ink, its corners rounded, at twice the design's 1280x320."""
    scale, size = 2, (2560, 640)
    image = ground(size[0], size[1], size[1] / 2)
    image = Image.alpha_composite(image, lockup(size, scale, size[1] / 2))
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius=32 * scale, fill=255)
    image.putalpha(mask)
    return image


WEIGHTS = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64])


def encode_bc7(rgb):
    """BC7 mode 6 blocks, row by row, for an opaque H x W x 3 image (W and H multiples of 4)."""
    h, w = rgb.shape[:2]
    out = []
    for y in range(0, h, 64):  # 16 rows of blocks at a time
        strip = rgb[y:y + 64].astype(np.float64)
        rows = strip.shape[0] // 4
        px = strip.reshape(rows, 4, w // 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 3)
        mean = px.mean(axis=1)
        d = px - mean[:, None, :]
        cov = np.einsum("npi,npj->nij", d, d)
        axis = np.ones((len(px), 3)) / np.sqrt(3.0)
        for _ in range(8):  # the principal axis, by power iteration
            v = np.einsum("nij,nj->ni", cov, axis)
            norm = np.linalg.norm(v, axis=1, keepdims=True)
            axis = np.where(norm > 1e-9, v / np.maximum(norm, 1e-12), axis)
        t = np.einsum("npi,ni->np", d, axis)
        ends = [np.clip(mean + t.min(axis=1)[:, None] * axis, 0, 255), np.clip(mean + t.max(axis=1)[:, None] * axis, 0, 255)]
        # 7 bits per channel and a p-bit of 1 for both ends: odd values, and alpha 255
        q = [np.clip(np.rint((e - 1.0) / 2.0), 0, 127).astype(np.int64) for e in ends]
        r0, r1 = q[0] * 2 + 1, q[1] * 2 + 1
        line = (r1 - r0).astype(np.float64)
        length = np.einsum("ni,ni->n", line, line)
        pos = np.einsum("npi,ni->np", px - r0[:, None, :], line) / np.maximum(length, 1e-9)[:, None]
        index = np.abs(pos[:, :, None] * 64.0 - WEIGHTS[None, None, :]).argmin(axis=2)
        index[length == 0] = 0
        # the first pixel's index must have its top bit clear: else swap the ends
        swap = index[:, 0] >= 8
        q[0][swap], q[1][swap] = q[1][swap].copy(), q[0][swap].copy()
        index[swap] = 15 - index[swap]
        q0, q1 = q[0].astype(np.uint64), q[1].astype(np.uint64)
        lo = np.full(len(px), 1 << 6, dtype=np.uint64)  # mode 6
        for c in range(3):  # R0 R1 G0 G1 B0 B1, then A0 A1 (127), from bit 7
            lo |= q0[:, c] << np.uint64(7 + 14 * c)
            lo |= q1[:, c] << np.uint64(14 + 14 * c)
        lo |= np.uint64(127) << np.uint64(49)
        lo |= np.uint64(127) << np.uint64(56)
        lo |= np.uint64(1) << np.uint64(63)  # P0
        hi = np.ones(len(px), dtype=np.uint64)  # P1
        idx = index.astype(np.uint64)
        hi |= idx[:, 0] << np.uint64(1)  # three bits for the first
        for i in range(1, 16):
            hi |= idx[:, i] << np.uint64(4 * i)
        out.append(np.stack([lo, hi], axis=1).astype("<u8").tobytes())
    return b"".join(out)


def decode_bc7(data, w, h):
    """The image mode 6 blocks hold (encode_bc7's check, and the preview)."""
    words = np.frombuffer(data, dtype="<u8").reshape(-1, 2)
    lo, hi = words[:, 0], words[:, 1]
    ends = []
    for e in range(2):
        channel = [((lo >> np.uint64(7 + 7 * (2 * c + e))) & np.uint64(127)).astype(np.int64) for c in range(3)]
        p = ((lo >> np.uint64(63)) & np.uint64(1)) if e == 0 else (hi & np.uint64(1))
        ends.append(np.stack(channel, axis=1) * 2 + p.astype(np.int64)[:, None])
    index = np.stack([((hi >> np.uint64(1)) & np.uint64(7))] + [((hi >> np.uint64(4 * i)) & np.uint64(15)) for i in range(1, 16)], axis=1)
    weight = WEIGHTS[index.astype(np.int64)][:, :, None]
    px = ((64 - weight) * ends[0][:, None, :] + weight * ends[1][:, None, :] + 32) >> 6
    return px.reshape(h // 4, w // 4, 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(h, w, 3).astype(np.uint8)


def write_dds(path, blocks):
    linear_size = len(blocks)
    header = struct.pack("<4sIIIIIII44x", b"DDS ", 124, 0xA1007, H, W, linear_size, 0, 1)
    # the pixel format names a DX10 header; caps: a texture
    header += struct.pack("<II4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
    header += struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    header += struct.pack("<5I", 98, 3, 0, 1, 0)  # BC7_UNORM, a 2D texture, one of it
    assert len(header) == 148
    with open(path, "wb") as out:
        out.write(header + blocks)


def main():
    args = sys.argv[1:]
    banner_path = wordmark_path = svg_path = None
    while len(args) >= 2 and args[0] in ("--title-font", "--tail-font", "--banner", "--wordmark", "--mark-svg"):
        if args[0] in ("--title-font", "--tail-font"):
            if not os.path.isfile(args[1]):
                sys.exit("no font at " + args[1])
            (HEAVY if args[0] == "--title-font" else TAIL_FONTS).insert(0, args[1])
        elif args[0] == "--wordmark":
            wordmark_path = args[1]
        elif args[0] == "--mark-svg":
            svg_path = args[1]
        else:
            banner_path = args[1]
        args = args[2:]
    if len(args) not in (1, 2):
        sys.exit(__doc__)
    out = args[0]
    image = picture()
    rgb = np.asarray(image)
    blocks = encode_bc7(rgb)
    decoded = decode_bc7(blocks, W, H)
    error = np.sqrt(np.mean((decoded.astype(np.float64) - rgb) ** 2))
    print(f"pic0.dds: BC7, {len(blocks)} bytes, RMS error {error:.2f}")
    write_dds(os.path.join(out, "pic0.dds"), blocks)
    if len(args) == 2:
        Image.fromarray(decoded, "RGB").save(args[1])
    tile().save(os.path.join(out, "icon0.png"))
    print("icon0.png: 512x512")
    if banner_path:
        banner().save(banner_path)
        print(f"{banner_path}: 2560x640")
    if wordmark_path:
        alone = name_alone()
        alone.save(wordmark_path)
        print(f"{wordmark_path}: {alone.width}x{alone.height}")
    if svg_path:
        with open(svg_path, "w") as out:
            out.write('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 218 218">' + mark_svg() + "</svg>\n")
        print(f"{svg_path}: the mark")


if __name__ == "__main__":
    main()
