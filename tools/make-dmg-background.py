#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>
"""Draws the backdrop for the disk image window.

An installer window is the first thing anyone sees of an application, and the
default one -- three icons in a row against white, in whatever order the
filesystem returned them -- says nothing and instructs nobody. What it has to
do is name the thing and show where the icon goes, which is two pieces of
information and an arrow between them.

Generated rather than checked in as a binary, so the wording and the geometry
stay next to the script that positions the icons against them. The positions
in make-dmg.sh and the marks drawn here have to agree, and the way to keep two
numbers in agreement is to write them down once: they are the constants below.
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# Shared with make-dmg.sh. Points, not pixels; the image is rendered at twice
# this for the Retina half of the pair.
WINDOW = (680, 480)
APP_ICON = (180, 225)
APPLICATIONS_ICON = (500, 225)
NOTE_ICON = (600, 390)

INK = (228, 228, 228)
MUTE = (122, 122, 122)
HAIR = (58, 58, 58)
BASE = (22, 22, 22)


def font(size: int, bold: bool = False):
    for path in (
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
    ):
        if Path(path).exists():
            try:
                return ImageFont.truetype(path, size)
            except OSError:
                continue
    return ImageFont.load_default()


def draw(scale: int) -> Image.Image:
    w, h = WINDOW[0] * scale, WINDOW[1] * scale
    image = Image.new("RGB", (w, h), BASE)
    d = ImageDraw.Draw(image)

    def at(point):
        return (point[0] * scale, point[1] * scale)

    # The arrow, from beside the application to beside the folder. It stops
    # well clear of both icons: an arrow that touches what it points at reads
    # as a connection rather than as a direction.
    start = (APP_ICON[0] + 62, APP_ICON[1])
    end = (APPLICATIONS_ICON[0] - 62, APPLICATIONS_ICON[1])

    d.line([at(start), at(end)], fill=HAIR, width=2 * scale)

    head = 11 * scale
    tip = at(end)
    d.polygon(
        [tip, (tip[0] - head, tip[1] - head * 0.62), (tip[0] - head, tip[1] + head * 0.62)],
        fill=HAIR,
    )

    title = font(26 * scale)
    caption = font(13 * scale)
    small = font(11 * scale)

    d.text((w / 2, 56 * scale), "plugshell", font=title, fill=INK, anchor="mm")
    d.text(
        (w / 2, 86 * scale),
        "an agent-operable host for audio plugins",
        font=caption,
        fill=MUTE,
        anchor="mm",
    )

    # Under each icon, so the instruction sits where the hand is going.
    d.text(
        (at(APP_ICON)[0], at((0, APP_ICON[1] + 96))[1]), "drag this", font=caption, fill=MUTE, anchor="mm"
    )
    d.text(
        (at(APPLICATIONS_ICON)[0], at((0, APPLICATIONS_ICON[1] + 96))[1]),
        "onto here",
        font=caption,
        fill=MUTE,
        anchor="mm",
    )

    # Kept to the left, because the note's icon sits on the right and text
    # running underneath an icon is text nobody reads.
    left = 56 * scale
    d.line(
        [(left, (WINDOW[1] - 128) * scale), ((WINDOW[0] - 56) * scale, (WINDOW[1] - 128) * scale)],
        fill=HAIR,
        width=1 * scale,
    )

    d.text(
        (left, (WINDOW[1] - 104) * scale),
        "First launch is blocked. macOS cannot verify an application that is",
        font=small,
        fill=MUTE,
        anchor="lm",
    )
    d.text(
        (left, (WINDOW[1] - 84) * scale),
        "not signed with an Apple Developer ID, which is a paid membership",
        font=small,
        fill=MUTE,
        anchor="lm",
    )
    d.text(
        (left, (WINDOW[1] - 64) * scale),
        "rather than a security property.",
        font=small,
        fill=MUTE,
        anchor="lm",
    )
    d.text(
        (left, (WINDOW[1] - 36) * scale),
        "To allow it once:  System Settings > Privacy & Security > Open Anyway",
        font=caption,
        fill=INK,
        anchor="lm",
    )

    return image


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "assets")
    out.mkdir(parents=True, exist_ok=True)

    draw(1).save(out / "dmg-background.png")
    draw(2).save(out / "dmg-background@2x.png")

    print(f"wrote {out / 'dmg-background.png'} and its 2x")


if __name__ == "__main__":
    main()
