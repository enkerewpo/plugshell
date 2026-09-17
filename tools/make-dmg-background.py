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
WINDOW = (700, 486)
APP_ICON = (185, 202)
APPLICATIONS_ICON = (505, 202)
NOTE_ICON = (612, 404)

# The application's light palette, not its dark one, and the reason is Finder.
#
# Finder draws icon labels in a colour taken from the system appearance, not
# from whatever is behind them: dark text in light mode, light text in dark
# mode. A backdrop cannot satisfy both, and the file names are drawn on top of
# it by something we do not control -- so the choice is which appearance to be
# legible in, and light mode is the one most machines are set to.
BASE = (244, 244, 243)
INK = (28, 28, 27)
MUTE = (119, 119, 111)
HAIR = (202, 202, 196)


def font(size: int):
    # Lucida Grande, because that is what the application draws with.
    #
    # Not by choice exactly: JUCE's default sans serif resolves, on macOS, to
    # Lucida Grande -- the system font from before Yosemite, which is why the
    # interface reads as an older Mac application than it is. Whether or not
    # that stays the choice, the backdrop has to be whatever the window it
    # installs is, so this follows juce_Fonts_mac.mm rather than guessing at
    # the current system font.
    for path in (
        "/System/Library/Fonts/LucidaGrande.ttc",
        "/System/Library/Fonts/Supplemental/LucidaGrande.ttc",
        "/System/Library/Fonts/SFNS.ttf",
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

    def at(x, y):
        return (x * scale, y * scale)

    title = font(27 * scale)
    caption = font(13 * scale)
    small = font(11 * scale)

    d.text(at(WINDOW[0] / 2, 48), "plugshell", font=title, fill=INK, anchor="mm")
    d.text(
        at(WINDOW[0] / 2, 77),
        "an agent-operable plugin host",
        font=caption,
        fill=MUTE,
        anchor="mm",
    )

    # The arrow stops well clear of both icons: one that touches what it points
    # at reads as a connection rather than as a direction.
    d.line(
        [at(APP_ICON[0] + 66, APP_ICON[1]), at(APPLICATIONS_ICON[0] - 66, APPLICATIONS_ICON[1])],
        fill=HAIR,
        width=2 * scale,
    )

    head = 11 * scale
    tip = at(APPLICATIONS_ICON[0] - 66, APPLICATIONS_ICON[1])
    d.polygon(
        [tip, (tip[0] - head, tip[1] - head * 0.62), (tip[0] - head, tip[1] + head * 0.62)], fill=HAIR
    )

    # Well below the icons, because Finder writes each file's name under its
    # own icon and an instruction crowded against that reads as part of it.
    d.text(at(APP_ICON[0], APP_ICON[1] + 108), "drag this", font=caption, fill=MUTE, anchor="mm")
    d.text(
        at(APPLICATIONS_ICON[0], APPLICATIONS_ICON[1] + 108),
        "onto here",
        font=caption,
        fill=MUTE,
        anchor="mm",
    )

    # Kept to the left of the note's icon, so no line runs underneath it.
    left = 52
    d.line([at(left, 346), at(WINDOW[0] - left, 346)], fill=HAIR, width=1 * scale)

    for i, line in enumerate(
        (
            "First launch is blocked. macOS cannot verify an application that is not",
            "signed with an Apple Developer ID, which is a paid membership rather",
            "than a security property.",
        )
    ):
        d.text(at(left, 374 + i * 19), line, font=small, fill=MUTE, anchor="lm")

    d.text(
        at(left, 446),
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
