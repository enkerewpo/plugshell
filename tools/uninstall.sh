#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>
#
# Removes plugshell and everything it wrote. Shows what it will delete and
# asks first, because an uninstaller that guesses wrong is worse than none.

set -euo pipefail

BUNDLE_ID="ai.plugshell.app"
ASSUME_YES=0
[ "${1:-}" = "--yes" ] && ASSUME_YES=1

TARGETS=(
    "/Applications/plugshell.app"
    "$HOME/Applications/plugshell.app"
    "$HOME/Library/Application Support/plugshell"
    "$HOME/Library/Preferences/$BUNDLE_ID.plist"
    "$HOME/Library/Saved Application State/$BUNDLE_ID.savedState"
    "$HOME/Library/Caches/$BUNDLE_ID"
)

present=()
for t in "${TARGETS[@]}"; do
    [ -e "$t" ] && present+=("$t")
done

if [ ${#present[@]} -eq 0 ]; then
    echo "Nothing to remove."
else
    echo "Will remove:"
    for t in "${present[@]}"; do
        printf '  %s  (%s)\n' "$t" "$(du -sh "$t" 2>/dev/null | cut -f1)"
    done

    if [ "$ASSUME_YES" -eq 0 ]; then
        read -r -p "Proceed? [y/N] " reply
        [[ "$reply" =~ ^[Yy]$ ]] || { echo "Cancelled."; exit 0; }
    fi

    for t in "${present[@]}"; do
        rm -rf "$t"
        echo "  removed $t"
    done
fi

# The permission grants live in the system's own database and survive
# deleting the application, so they are reset explicitly rather than left
# behind pointing at something that no longer exists.
echo
echo "Resetting macOS permissions:"
for service in ScreenCapture Accessibility; do
    if tccutil reset "$service" "$BUNDLE_ID" >/dev/null 2>&1; then
        echo "  reset $service"
    else
        echo "  could not reset $service; remove it in System Settings > Privacy & Security"
    fi
done

echo
echo "Done. Your plugins and their presets were not touched -- plugshell never"
echo "writes to them."
