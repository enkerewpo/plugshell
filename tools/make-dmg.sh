#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>
#
# Builds a distributable disk image, signing and notarising it when the
# credentials for that exist and saying plainly what the result is worth when
# they do not.

set -euo pipefail

APP_NAME="plugshell"
BUNDLE_ID="ai.plugshell.app"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
CONFIG="${CONFIG:-RelWithDebInfo}"
APP="$BUILD/src/app/${APP_NAME}_app_artefacts/$CONFIG/$APP_NAME.app"
OUT_DIR="${OUT_DIR:-$ROOT/dist}"
STAGE="$OUT_DIR/.stage"

# Set to a "Developer ID Application: ..." identity to produce an image that
# opens without a Gatekeeper warning. Anything else -- a development
# certificate, an ad-hoc signature -- will not, however valid it is for
# running the application locally.
SIGN_ID="${PLUGSHELL_DEVELOPER_ID:-}"

# A notarytool keychain profile, created once with:
#   xcrun notarytool store-credentials plugshell-notary \
#       --apple-id <id> --team-id <team> --password <app-specific-password>
NOTARY_PROFILE="${PLUGSHELL_NOTARY_PROFILE:-}"

say() { printf '\033[1m%s\033[0m\n' "$*"; }
warn() { printf '\033[33m%s\033[0m\n' "$*"; }

[ -d "$APP" ] || {
    echo "No application at $APP -- build it first (make build)." >&2
    exit 1
}

VERSION="$(defaults read "$APP/Contents/Info.plist" CFBundleShortVersionString 2>/dev/null || echo 0.1.0)"
DMG="$OUT_DIR/$APP_NAME-$VERSION.dmg"

rm -rf "$STAGE" "$DMG"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"

# ---------------------------------------------------------------- signing

if [ -n "$SIGN_ID" ]; then
    say "Signing with $SIGN_ID"

    # The hardened runtime is required before anything can be notarised, and
    # it blocks loading code signed by anyone else -- which for a plugin host
    # is every plugin it exists to load. Library validation has to be
    # disabled or the shipped application will refuse to open a single VST3.
    codesign --force --deep --options runtime --timestamp \
        --entitlements "$ROOT/tools/plugshell.entitlements" \
        --sign "$SIGN_ID" "$STAGE/$APP_NAME.app"

    codesign --verify --strict --verbose=2 "$STAGE/$APP_NAME.app"
else
    # Re-signed ad-hoc rather than shipped with whatever the build used.
    # The development certificate that makes local permissions stick carries
    # the developer's real name and team, and publishing a binary is not the
    # place for it -- especially as Gatekeeper rejects a development signature
    # just as firmly as no signature at all, so it buys the user nothing.
    codesign --force --deep --sign - "$STAGE/$APP_NAME.app"

    warn "No Developer ID (PLUGSHELL_DEVELOPER_ID unset): signed ad-hoc."
    warn "The image installs and runs, but the first launch is blocked and the"
    warn "user has to allow it once in System Settings > Privacy & Security."
    warn "INSTALL.txt in the image explains it. See docs/DISTRIBUTION.md."
fi

# ---------------------------------------------------------------- the image

ln -s /Applications "$STAGE/Applications"

# The first launch is blocked and the reason is not the user's fault, so the
# explanation travels with the image rather than living in a README they would
# have to already have found.
# Always written: the window points at it, and the detour it describes is
# the same whether or not the image happens to be signed.
cat >"$STAGE/INSTALL.txt" <<'NOTE'
plugshell

1. Drag plugshell.app onto the Applications folder here.

2. Open it. macOS will refuse, saying it cannot verify the developer.
   This is expected: the application is not signed with an Apple
   Developer ID, which is a paid membership rather than a security
   property.

3. Open System Settings, go to Privacy & Security, and scroll down.
   A message about plugshell will be there with an "Open Anyway"
   button. Click it and confirm.

   That is a one-time step. It will open normally from then on.

4. Two features need permission, and both fail silently without it:

     Screen recording  - reading a plugin's editor as an image
     Accessibility     - clicking and dragging inside that editor

   plugshell's own Settings panel has a row for each, showing whether
   it is granted. Everything else works without them.

Source, and the reasoning behind all of the above:
https://github.com/enkerewpo/plugshell
NOTE

# --------------------------------------------------------- the backdrop

say "Drawing the backdrop"
python3 "$ROOT/tools/make-dmg-background.py" "$ROOT/assets" >/dev/null

# Both resolutions in one file, which is how a background is told that it has
# a Retina half.
tiffutil -cathidpicheck "$ROOT/assets/dmg-background.png" "$ROOT/assets/dmg-background@2x.png" \
    -out "$ROOT/assets/dmg-background.tiff" >/dev/null 2>&1

mkdir -p "$STAGE/.background"
cp "$ROOT/assets/dmg-background.tiff" "$STAGE/.background/background.tiff"

# ---------------------------------------------------------------- the image
#
# Built writable first. Icon positions and the window's appearance live in the
# volume's own .DS_Store, and there is no way to write one except by arranging
# the window and letting Finder save it. Compressed and made read-only after.

say "Building $DMG"

RW="$OUT_DIR/.rw.dmg"
rm -f "$RW"

hdiutil create -volname "$APP_NAME" -srcfolder "$STAGE" -fs HFS+ -format UDRW -ov "$RW" >/dev/null

MOUNT="$(hdiutil attach -readwrite -noverify -noautoopen "$RW" | grep -o '/Volumes/.*')"
sleep 2

# The positions here and the marks on the backdrop have to agree; both come
# from the constants at the top of make-dmg-background.py.
osascript <<APPLESCRIPT
tell application "Finder"
    tell disk "$APP_NAME"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {180, 120, 860, 628}

        set opts to the icon view options of container window
        set arrangement of opts to not arranged
        set icon size of opts to 96
        set text size of opts to 12
        set background picture of opts to file ".background:background.tiff"

        set position of item "$APP_NAME.app" of container window to {180, 225}
        set position of item "Applications" of container window to {500, 225}
        set position of item "INSTALL.txt" of container window to {598, 372}

        -- Out of the window entirely. It has to be on the volume, because it
        -- holds the picture behind all of this, but a Finder set to reveal
        -- hidden files would otherwise show it as a stray folder in the middle
        -- of the instructions.
        try
            set position of item ".background" of container window to {1200, 1200}
        end try

        update without registering applications
        delay 2
        close
    end tell
end tell
APPLESCRIPT

# The volume's own bookkeeping, which is of no interest to anyone opening the
# image and shows up as stray folders for anyone whose Finder reveals hidden
# files.
rm -rf "$MOUNT/.fseventsd" "$MOUNT/.Trashes" "$MOUNT/.TemporaryItems" 2>/dev/null || true

sync
hdiutil detach "$MOUNT" >/dev/null 2>&1 || hdiutil detach "$MOUNT" -force >/dev/null 2>&1
sleep 1

rm -f "$DMG"
hdiutil convert "$RW" -format UDZO -imagekey zlib-level=9 -o "$DMG" >/dev/null
rm -f "$RW"

rm -rf "$STAGE"

# ------------------------------------------------------- notarise and staple

if [ -n "$SIGN_ID" ]; then
    codesign --force --timestamp --sign "$SIGN_ID" "$DMG"
fi

if [ -n "$NOTARY_PROFILE" ] && [ -n "$SIGN_ID" ]; then
    say "Notarising (this takes a few minutes)"
    xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait

    # Stapling writes the notarisation ticket into the image, so a machine
    # that is offline the first time it opens it still gets a verdict.
    say "Stapling"
    xcrun stapler staple "$DMG"
    xcrun stapler validate "$DMG"
elif [ -n "$SIGN_ID" ]; then
    warn "Signed but not notarised (PLUGSHELL_NOTARY_PROFILE is unset)."
    warn "Gatekeeper will still block it on a machine that has not seen it."
fi

say "Done: $DMG"
ls -lh "$DMG" | awk '{print "  " $5, $9}'
