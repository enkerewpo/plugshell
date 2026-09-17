#!/usr/bin/env bash
# SPDX-License-Identifier: AAGPL-3.0-or-later
# Every tracked source file must carry an SPDX licence identifier.
set -euo pipefail

missing=0
while IFS= read -r f; do
    [ -f "$f" ] || continue
    if ! head -5 "$f" | grep -q "SPDX-License-Identifier"; then
        echo "missing SPDX header: $f"
        missing=1
    fi
done < <(git ls-files '*.cpp' '*.h' '*.mm' '*.sh' '*.yml' 'Makefile' 'CMakeLists.txt' '*/CMakeLists.txt')

if [ "$missing" -ne 0 ]; then
    echo
    echo "Add this as the first line (adjust the comment marker):"
    echo "  // SPDX-License-Identifier: AAGPL-3.0-or-later"
    exit 1
fi
echo "SPDX: all tracked sources carry a licence identifier"
