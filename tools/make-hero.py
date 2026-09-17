#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>
"""Composes the project's lead image.

The screenshot is evidence, not the subject. What the image has to carry is
the four things this does that nothing else does, at a size readable in a
README's column -- which is narrow, and which is where every earlier version
of this failed: type sized against a full-width preview reads as decoration
once it has been scaled into a page.

So the claims are the middle of the picture, and the window sits underneath at
whatever size is left over.

Usage:
    tools/make-hero.py <window-capture.png> [out.png]

The capture wants to come from `screencapture -l <window id>`, which returns
the window with its real rounded corners and transparency around them. A
rectangle grabbed with -R has the desktop showing in those corners, and no
shadow drawn behind it will ever line up.
"""

import subprocess
import sys
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
LOGOS = ROOT / "assets" / "logos"

SCALE = 2
WIDTH = 1280
PAD = 48

# The application's light palette. Light rather than dark because this is read
# inside a page, and a dark plate dropped into one announces itself as a
# picture before it has said anything about the software.
BASE = (244, 244, 243)
CARD = (255, 255, 255)
INK = (24, 24, 23)
MUTE = (110, 110, 104)
HAIR = (219, 219, 213)

CLAUDE = "#D97757"  # Anthropic's own, from simple-icons' index
OPENAI = "#000000"


def font(size: int, path: str = "/System/Library/Fonts/LucidaGrande.ttc"):
    # The face the application draws with: JUCE's default sans resolves to
    # Lucida Grande on macOS.
    try:
        return ImageFont.truetype(path, size * SCALE)
    except OSError:
        return ImageFont.load_default()


def svg(name: str, height: int, colour: str, stroke: bool = False) -> Image.Image:
    """An SVG recoloured in the markup and rasterised at its drawn size.

    Recoloured before rasterising rather than tinted after, because tinting a
    raster leaves every antialiased edge the colour it started as.
    """
    markup = (LOGOS / f"{name}.svg").read_text()

    if stroke:
        markup = markup.replace('stroke="currentColor"', f'stroke="{colour}"')
    elif 'fill="currentColor"' in markup:
        markup = markup.replace('fill="currentColor"', f'fill="{colour}"')
    else:
        markup = markup.replace("<svg", f'<svg fill="{colour}"', 1)

    out = subprocess.run(
        ["rsvg-convert", "-h", str(height * SCALE), "-f", "png"],
        input=markup.encode(),
        capture_output=True,
        check=True,
    )
    return Image.open(BytesIO(out.stdout)).convert("RGBA")


def shadowed(window: Image.Image, spread: int, drop: int) -> Image.Image:
    """A soft shadow cast from the window's own silhouette.

    From its alpha channel, not from a rounded rectangle guessed at. A window's
    corner radius belongs to the system, and the moment a guess is a pixel out
    the shadow shows along the curve -- which is exactly what it was doing.
    """
    pad = spread * 3
    field = Image.new("RGBA", (window.width + pad * 2, window.height + pad * 2 + drop), (0, 0, 0, 0))

    cast = Image.new("RGBA", field.size, (0, 0, 0, 0))
    cast.paste((0, 0, 0, 150), (pad, pad + drop), window.getchannel("A"))
    field.alpha_composite(cast.filter(ImageFilter.GaussianBlur(spread)))

    field.alpha_composite(window, (pad, pad))
    return field


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)

    window = Image.open(sys.argv[1]).convert("RGBA")
    out_path = Path(sys.argv[2] if len(sys.argv) > 2 else ROOT / "assets" / "hero.png")

    header_h = 124
    card_h = 22 + 22 + 18 + 20 * 2 + 22  # inset, icon row, gap, two lines, inset
    agents_h = 76
    shot_w = 660

    shot_h = round(window.height * shot_w / window.width)
    height = header_h + card_h + agents_h + shot_h + 40

    canvas = Image.new("RGB", (WIDTH * SCALE, height * SCALE), BASE)
    d = ImageDraw.Draw(canvas)

    def at(x, y):
        return (round(x * SCALE), round(y * SCALE))

    # ------------------------------------------------------------ the name

    mark = Image.open(ROOT / "assets" / "icon-1024.png").convert("RGBA")
    mark = mark.resize((68 * SCALE, 68 * SCALE), Image.LANCZOS)
    canvas.paste(mark, at(PAD, 26), mark)

    left = PAD + 68 + 20

    d.text(at(left, 48), "plugshell", font=font(42), fill=INK, anchor="lm")
    d.text(
        at(left, 84),
        "a host that lets an agent work an audio plugin",
        font=font(20),
        fill=MUTE,
        anchor="lm",
    )

    # ---------------------------------------------------------- the claims
    #
    # The middle of the picture, and the reason it exists. Marked with an icon
    # rather than a coloured rule down one side: the rule decorates without
    # saying anything, and says mostly that a template was used.

    claims = [
        ("terminal", "agent control", "parameters, presets and\nthe editor, over a socket"),
        ("camera", "editor capture", "the plugin's own\ninterface, as an image"),
        ("mouse-pointer-click", "synthetic input", "click and drag what the\nplugin never published"),
        ("list-ordered", "universal preset", "a patch as operations,\nnot a binary blob"),
    ]

    # One padding everywhere, one baseline grid. The icon sat alone above the
    # title with a gap that belonged to neither of them, and the note crowded
    # the bottom edge -- three different margins in a box 290 wide.
    inset = 22
    gap = 16
    card_w = (WIDTH - PAD * 2 - gap * (len(claims) - 1)) / len(claims)
    top = header_h

    icon_size = 22
    title = font(20)
    body = font(14)
    leading = 20

    for i, (icon, name, note) in enumerate(claims):
        x = PAD + (card_w + gap) * i

        d.rounded_rectangle(
            [at(x, top), at(x + card_w, top + card_h)], 12 * SCALE, fill=CARD, outline=HAIR, width=SCALE
        )

        # Mark and name on one line, because they name the same thing. Stacked,
        # the icon reads as a separate element that happens to be nearby.
        row = top + inset + icon_size / 2

        glyph = svg(icon, icon_size, "#1a1a19", stroke=True)
        canvas.paste(glyph, at(x + inset, row - icon_size / 2), glyph)

        d.text(at(x + inset + icon_size + 11, row), name, font=title, fill=INK, anchor="lm")

        # The note starts a full line below the row, and every line sits on the
        # same leading, so two cards with different wording still line up.
        note_top = row + icon_size / 2 + 18

        for n, line in enumerate(note.split("\n")):
            d.text(at(x + inset, note_top + n * leading), line, font=body, fill=MUTE, anchor="la")

    # ---------------------------------------------------------- the agents
    #
    # In their own colours. A brand mark recoloured to suit a layout stops
    # being the mark anyone recognises, which is the whole reason to show one.

    y = header_h + card_h + agents_h / 2 + 8
    logo_h = 32
    label = font(20)

    pieces = [("claude", CLAUDE, "Claude Code"), ("openai", OPENAI, "Codex")]

    total = d.textlength("driven by", font=label) / SCALE + 22
    for _, _, name in pieces:
        total += logo_h + 12 + d.textlength(name, font=label) / SCALE + 46
    total -= 46

    x = (WIDTH - total) / 2
    d.text(at(x, y), "driven by", font=label, fill=MUTE, anchor="lm")
    x += d.textlength("driven by", font=label) / SCALE + 22

    for icon, colour, name in pieces:
        logo = svg(icon, logo_h, colour)
        canvas.paste(logo, at(x, y - logo_h / 2), logo)
        x += logo_h + 12

        d.text(at(x, y), name, font=label, fill=INK, anchor="lm")
        x += d.textlength(name, font=label) / SCALE + 46

    # ------------------------------------------------- the window, as proof

    shot = window.resize((shot_w * SCALE, shot_h * SCALE), Image.LANCZOS)
    framed = shadowed(shot, 18 * SCALE, 12 * SCALE)

    canvas.paste(
        framed,
        (
            (WIDTH * SCALE - framed.width) // 2,
            round((header_h + card_h + agents_h) * SCALE) - 18 * SCALE * 3 + 12 * SCALE,
        ),
        framed,
    )

    out_path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(out_path)
    print(f"wrote {out_path}  {canvas.size[0]}x{canvas.size[1]}")


if __name__ == "__main__":
    main()
