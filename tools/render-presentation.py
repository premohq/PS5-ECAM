#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw Playtendoh's art: its logo with its name, as the home screen's background (sce_sys/pic0.dds),
its tile (sce_sys/icon0.png) and the README's banner (docs/banner.png).

    render-presentation.py [--title-font FONT] [--tail-font FONT] [--banner PNG] [--wordmark PNG]
                           SCE_SYS_DIR [PREVIEW_PNG]

The logo is four flat rounded squares as a window's four panes, the Wii U's top left, the 3DS's top
right, the DS's bottom left and the Switch's bottom right, each in its side's colour with its console
in white (tools/render-icons.py's FLAT and GLYPHS, which the launcher's bar shows too). The name,
Playtendoh, is beside it on the banner and the background, over the tagline, and under it on the
square tile; all of it on the launcher's ink. The name is one word in two faces (the owner's, 3
November 2026): "Play" in the PlayStation logo's lettering (--title-font), "tendoh" in a
Nintendo-like face (--tail-font) at the size at which its d stands as tall as Play's l, on Play's
baseline.

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
TAGLINE = "Nintendo systems on PlayStation 5"
TAGLINE_COLOUR = (0xc9, 0xd3, 0xe0)
SIDES = ("wiiu", "3ds", "ds", "switch")  # the panes in the launcher's order: top left, top right, bottom left, bottom right
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


def wordmark(size):
    """The name's two parts as one word: HEAD in the title's face at size, TAIL in the tail's at the
    size at which its d stands as tall as HEAD's l. Returns (width, ascent, descent, draw),
    draw(drawing, x, baseline, fill) putting it there."""
    head = font(HEAVY, size)
    if not TAIL_FONTS:
        ascent, descent = rise(head, NAME)
        return head.getlength(NAME), ascent, descent, lambda d, x, y, fill: d.text((x, y), NAME, font=head, fill=fill, anchor="ls")
    tail = font(TAIL_FONTS, size)
    tail = font(TAIL_FONTS, max(8, round(size * rise(head, "l")[0] / max(1, rise(tail, "d")[0]))))
    gap = round(size * 0.04)
    width = head.getlength(HEAD) + gap + tail.getlength(TAIL)
    ascent = max(rise(head, HEAD)[0], rise(tail, TAIL)[0])
    descent = max(rise(head, HEAD)[1], rise(tail, TAIL)[1])

    def draw(d, x, y, fill):
        d.text((x, y), HEAD, font=head, fill=fill, anchor="ls")
        d.text((x + head.getlength(HEAD) + gap, y), TAIL, font=tail, fill=fill, anchor="ls")
    return width, ascent, descent, draw


def square(size, side):
    """A side's square of the logo, size pixels: drawn larger and scaled down, for clean edges."""
    s = size * SUPERSAMPLE
    top, bottom = ICONS.FLAT[side]
    t = np.linspace(0.0, 1.0, s)[:, None, None]
    ground = (np.array(top, dtype=np.float64) + (np.array(bottom) - np.array(top)) * t) * np.ones((1, s, 1))
    colour = Image.fromarray(np.rint(ground).astype(np.uint8), "RGB").convert("RGBA")
    glyph = Image.new("L", (s, s), 0)
    draw = ImageDraw.Draw(glyph)
    for shape in ICONS.GLYPHS[side]:
        fill = 0 if shape[-1] == "cut" else 255
        if shape[0] == "box":
            _, x0, y0, x1, y1, r = shape[:6]
            draw.rounded_rectangle((x0 * s, y0 * s, x1 * s, y1 * s), radius=r * s, fill=fill)
        else:
            _, cx, cy, r = shape[:4]
            draw.ellipse(((cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s), fill=fill)
    colour.paste(Image.new("RGBA", (s, s), WHITE), (0, 0), glyph)
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, s - 1, s - 1), radius=round(s * ICONS.CORNER), fill=255)
    colour.putalpha(mask)
    return colour.resize((size, size), Image.LANCZOS)


def window(pane, gap):
    """The four squares as a window's panes, and each one's centre in it."""
    size = 2 * pane + gap
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    centres = []
    for index, side in enumerate(SIDES):
        x, y = (index % 2) * (pane + gap), (index // 2) * (pane + gap)
        image.alpha_composite(square(pane, side), (x, y))
        centres.append((x + pane / 2, y + pane / 2))
    return image, centres


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
    """The banner's lockup at scale (1 is the README banner's 1280x320 design): the logo, then the
    name over the tagline beside it, centred across size at centre_y. Returns the layer and each
    square's centre."""
    pane, gap, space = round(104 * scale), round(10 * scale), 48 * scale
    logo, centres = window(pane, gap)
    (name_width, ascent, descent, name), tag = wordmark(round(150 * scale)), font(SEMIBOLD, round(30 * scale))
    text_width = max(name_width, tag.getlength(TAGLINE) + 4 * scale)
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
    name(draw, tx, name_y, WHITE)
    draw.text((tx + 4 * scale, tag_y), TAGLINE, font=tag, fill=TAGLINE_COLOUR + (255,), anchor="ls")
    layer = shadowed(Image.alpha_composite(layer, text), 6 * scale, 0.5, max(1, round(3 * scale)))
    return layer, [(x + cx, y + cy) for cx, cy in centres]


def picture():
    """pic0: the banner's lockup three times its size, above the screen's middle."""
    centre_y = 864
    image = ground(W, H, centre_y)
    layer, centres = lockup((W, H), 3, centre_y)
    return Image.alpha_composite(image, layer).convert("RGB"), centres


def tile():
    """icon0: the logo over the name."""
    size = 512
    image = ground(size, size, 190)
    logo, _ = window(140, 12)
    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    layer.alpha_composite(logo, ((size - logo.width) // 2, 44))
    name_size = 140
    while name_size > 8 and wordmark(name_size)[0] > 420:
        name_size -= 1
    name_width, _, descent, name = wordmark(name_size)
    name(ImageDraw.Draw(layer), (size - name_width) / 2, min(448, size - 28 - descent), WHITE)
    return Image.alpha_composite(image, shadowed(layer, 4, 0.5, 2)).convert("RGB")


def name_alone():
    """The name alone, white on clear and cut to its letters, as the site's page masks it (site/style.css)."""
    width, ascent, descent, name = wordmark(240)
    layer = Image.new("RGBA", (round(width) + 40, round(ascent + descent) + 40), (255, 255, 255, 0))
    name(ImageDraw.Draw(layer), 20, 20 + ascent, WHITE)
    return layer.crop(layer.getchannel("A").getbbox())


def banner():
    """The README's banner: the lockup on the ink, its corners rounded, at twice the design's 1280x320."""
    scale, size = 2, (2560, 640)
    image = ground(size[0], size[1], size[1] / 2)
    layer, _ = lockup(size, scale, size[1] / 2 - 8 * scale)
    image = Image.alpha_composite(image, layer)
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
    banner_path = wordmark_path = None
    while len(args) >= 2 and args[0] in ("--title-font", "--tail-font", "--banner", "--wordmark"):
        if args[0] in ("--title-font", "--tail-font"):
            if not os.path.isfile(args[1]):
                sys.exit("no font at " + args[1])
            (HEAVY if args[0] == "--title-font" else TAIL_FONTS).insert(0, args[1])
        elif args[0] == "--wordmark":
            wordmark_path = args[1]
        else:
            banner_path = args[1]
        args = args[2:]
    if len(args) not in (1, 2):
        sys.exit(__doc__)
    out = args[0]
    image, _ = picture()
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
        mark = name_alone()
        mark.save(wordmark_path)
        print(f"{wordmark_path}: {mark.width}x{mark.height}")


if __name__ == "__main__":
    main()
