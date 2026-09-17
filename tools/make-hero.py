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

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent

SCALE = 2  # the whole thing is laid out in points and rendered at 2x
WIDTH = 1280
PAD = 44

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

    # The screenshot sets the height; everything else is measured from it.
    shot_w = WIDTH - PAD * 2
    shot_h = round(shot.height * shot_w / shot.width)
    shot = shot.resize((shot_w * SCALE, shot_h * SCALE), Image.LANCZOS)

    header = 96
    footer = 92
    height = header + shot_h + footer + PAD

    canvas = Image.new("RGB", (WIDTH * SCALE, height * SCALE), BASE)
    d = ImageDraw.Draw(canvas)

    def at(x, y):
        return (x * SCALE, y * SCALE)

    # ---------------------------------------------------------- the header

    d.text(at(PAD, 30), "plugshell", font=font(26), fill=INK, anchor="lm")
    d.text(
        at(PAD, 62),
        "a macOS host that lets a program work an audio plugin",
        font=font(13),
        fill=MUTE,
        anchor="lm",
    )

    # The agents it is built to be driven by, right-aligned so the eye ends on
    # them after reading the claim.
    claude = svg(ROOT / "assets" / "logos" / "claude.svg", 20, "#e8e8e8")

    label = font(13)
    codex = "Codex"
    codex_w = d.textlength(codex, font=label) / SCALE

    right = WIDTH - PAD
    gap = 22

    d.text(at(right, 46), codex, font=label, fill=INK, anchor="rm")
    right -= codex_w + gap

    d.text(at(right, 46), "Claude Code", font=label, fill=INK, anchor="rm")
    right -= d.textlength("Claude Code", font=label) / SCALE + 10

    canvas.paste(claude, (round(right * SCALE - claude.width), round(46 * SCALE - claude.height / 2)), claude)
    right -= claude.width / SCALE + gap

    d.text(at(right, 46), "driven by", font=font(12), fill=MUTE, anchor="rm")

    # ------------------------------------------------------ the screenshot

    d.line([at(PAD, 86), at(WIDTH - PAD, 86)], fill=HAIR, width=1 * SCALE)
    canvas.paste(rounded(shot, 10 * SCALE), (PAD * SCALE, header * SCALE), rounded(shot, 10 * SCALE))

    # ---------------------------------------------------------- the claims

    y = header + shot_h + 34
    d.line([at(PAD, y - 20), at(WIDTH - PAD, y - 20)], fill=HAIR, width=1 * SCALE)

    claims = [
        ("agent control", "parameters, presets and the editor, over a socket"),
        ("editor capture", "the plugin's own interface, as an image"),
        ("synthetic input", "click and drag the controls that are not parameters"),
        ("universal preset", "a patch as operations, not a binary blob"),
    ]

    column = (WIDTH - PAD * 2) / len(claims)

    for i, (name, note) in enumerate(claims):
        x = PAD + column * i

        d.line([at(x, y - 6), at(x, y + 30)], fill=ACCENT, width=2 * SCALE)
        d.text(at(x + 12, y + 2), name, font=font(13), fill=INK, anchor="lm")
        d.text(at(x + 12, y + 22), note, font=font(11), fill=MUTE, anchor="lm")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(out_path)
    print(f"wrote {out_path}  {canvas.size[0]}x{canvas.size[1]}")


if __name__ == "__main__":
    main()
