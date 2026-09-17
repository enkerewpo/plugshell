#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>
"""Composes the project's lead image.

A screenshot on its own answers "what does it look like" and leaves the more
important question untouched, which is what the thing is for. So the shot is
framed by the two claims that distinguish it -- that a program can work the
plugin, and that a patch can be a sequence of operations rather than a binary
blob -- and by the marks of the agents it is built to be driven by.

Usage:
    tools/make-hero.py <screenshot.png> [out.png]
"""

import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent

SCALE = 2  # the whole thing is laid out in points and rendered at 2x
WIDTH = 1280
PAD = 40

BASE = (18, 18, 18)
PANEL = (26, 26, 26)
INK = (232, 232, 232)
MUTE = (138, 138, 138)
HAIR = (54, 54, 54)
ACCENT = (110, 170, 230)


def font(size: int, path: str = "/System/Library/Fonts/LucidaGrande.ttc"):
    # Lucida Grande, the same face the application draws with -- JUCE's default
    # sans serif resolves to it on macOS. The image and the window it shows
    # should read as one piece of work.
    try:
        return ImageFont.truetype(path, size * SCALE)
    except OSError:
        return ImageFont.load_default()


def svg(path: Path, height: int, colour: str) -> Image.Image:
    """An SVG recoloured and rasterised at the size it will be drawn.

    Recoloured by rewriting the markup rather than by tinting the result: these
    are single-colour marks, and tinting a rasterised one leaves the edges the
    colour they started as.
    """
    markup = path.read_text()

    if "fill=" not in markup.split(">", 1)[0]:
        markup = markup.replace("<svg", f'<svg fill="{colour}"', 1)
    else:
        markup = markup.replace('fill="currentColor"', f'fill="{colour}"')

    out = subprocess.run(
        ["rsvg-convert", "-h", str(height * SCALE), "-f", "png"],
        input=markup.encode(),
        capture_output=True,
        check=True,
    )

    from io import BytesIO

    return Image.open(BytesIO(out.stdout)).convert("RGBA")


def shadowed(image: Image.Image, radius: int, spread: int, drop: int) -> Image.Image:
    """The image on a transparent field, with a soft shadow beneath it.

    A screenshot pasted flat onto a background reads as part of the background
    -- a region of a picture rather than a window sitting in front of one. The
    shadow is what says it is a window, and it is the cheapest way to say it.
    """
    pad = spread * 3
    field = Image.new("RGBA", (image.width + pad * 2, image.height + pad * 2 + drop), (0, 0, 0, 0))

    cast = Image.new("RGBA", field.size, (0, 0, 0, 0))
    ImageDraw.Draw(cast).rounded_rectangle(
        [pad, pad + drop, pad + image.width, pad + drop + image.height], radius, fill=(0, 0, 0, 190)
    )
    field.alpha_composite(cast.filter(ImageFilter.GaussianBlur(spread)))

    rounded_image = rounded(image, radius)
    field.alpha_composite(rounded_image, (pad, pad))
    return field


def rounded(image: Image.Image, radius: int) -> Image.Image:
    mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, image.size[0] - 1, image.size[1] - 1], radius, fill=255)
    out = image.convert("RGBA")
    out.putalpha(mask)
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)

    shot = Image.open(sys.argv[1]).convert("RGB")
    out_path = Path(sys.argv[2] if len(sys.argv) > 2 else ROOT / "assets" / "hero.png")

    shot_w = WIDTH - PAD * 2
    shot_h = round(shot.height * shot_w / shot.width)
    shot = shot.resize((shot_w * SCALE, shot_h * SCALE), Image.LANCZOS)

    header = 116
    footer = 132
    height = header + shot_h + footer

    # Transparent, so the image sits on whatever the page behind it is rather
    # than punching a dark rectangle into a light one. Everything drawn on it
    # therefore has to carry its own contrast: the text sits on panels, not on
    # an assumption about what is underneath.
    canvas = Image.new("RGBA", (WIDTH * SCALE, height * SCALE), (0, 0, 0, 0))
    d = ImageDraw.Draw(canvas)

    def at(x, y):
        return (round(x * SCALE), round(y * SCALE))

    def panel(x, y, w, h, radius=10):
        plate = Image.new("RGBA", (round(w * SCALE), round(h * SCALE)), (0, 0, 0, 0))
        ImageDraw.Draw(plate).rounded_rectangle(
            [0, 0, plate.width - 1, plate.height - 1], radius * SCALE, fill=(24, 24, 24, 246)
        )
        canvas.alpha_composite(plate, at(x, y))

    # ---------------------------------------------------------- the header

    panel(0, 0, WIDTH, 92, 14)

    mark_size = 56
    mark = Image.open(ROOT / "assets" / "icon-1024.png").convert("RGBA")
    mark = mark.resize((mark_size * SCALE, mark_size * SCALE), Image.LANCZOS)
    canvas.alpha_composite(mark, at(PAD - 4, 18))

    left = PAD + mark_size + 14

    d.text(at(left, 36), "plugshell", font=font(34), fill=INK, anchor="lm")
    d.text(
        at(left, 68),
        "a host that lets a program work an audio plugin",
        font=font(16),
        fill=MUTE,
        anchor="lm",
    )

    # The agents it is built for, at a size that reads at a glance -- which is
    # the only size worth drawing a logo at.
    logo_h = 30
    claude = svg(ROOT / "assets" / "logos" / "claude.svg", logo_h, "#e8e8e8")
    openai = svg(ROOT / "assets" / "logos" / "openai.svg", logo_h, "#e8e8e8")

    right = WIDTH - PAD
    label = font(16)

    for logo, name in ((openai, "Codex"), (claude, "Claude Code")):
        width = d.textlength(name, font=label) / SCALE
        d.text(at(right, 46), name, font=label, fill=INK, anchor="rm")
        right -= width + 12

        canvas.alpha_composite(logo, (round(right * SCALE - logo.width), round(46 * SCALE - logo.height / 2)))
        right -= logo.width / SCALE + 30

    d.text(at(right, 46), "driven by", font=font(15), fill=MUTE, anchor="rm")

    # ------------------------------------------------------ the screenshot

    framed = shadowed(shot, 10 * SCALE, 16 * SCALE, 10 * SCALE)
    canvas.alpha_composite(
        framed, (PAD * SCALE - (framed.width - shot.width) // 2, header * SCALE - 16 * SCALE * 3)
    )

    # ---------------------------------------------------------- the claims
    #
    # Each on its own plate, because four labels floating on a transparent
    # field are four labels nobody can read against an unknown background.

    claims = [
        ("agent control", "parameters, presets and\nthe editor, over a socket"),
        ("editor capture", "the plugin's own\ninterface, as an image"),
        ("synthetic input", "click and drag what\nis not a parameter"),
        ("universal preset", "a patch as operations,\nnot a binary blob"),
    ]

    gap = 14
    plate_w = (WIDTH - PAD * 2 - gap * (len(claims) - 1)) / len(claims)
    plate_h = 96
    top = header + shot_h + 16

    for i, (name, note) in enumerate(claims):
        x = PAD + (plate_w + gap) * i
        panel(x, top, plate_w, plate_h)

        d.rounded_rectangle(
            [at(x + 16, top + 20), at(x + 20, top + plate_h - 20)], 2 * SCALE, fill=ACCENT
        )

        d.text(at(x + 30, top + 30), name, font=font(17), fill=INK, anchor="lm")
        d.multiline_text(
            at(x + 30, top + 52), note, font=font(12), fill=MUTE, spacing=6 * SCALE, anchor="la"
        )

    out_path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(out_path)
    print(f"wrote {out_path}  {canvas.size[0]}x{canvas.size[1]}  (transparent)")


if __name__ == "__main__":
    main()
