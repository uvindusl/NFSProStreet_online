#!/bin/bash
# Refresh lifter/tools/web/ from the canonical web/runtime/web/ copies.
# For every file that exists in both locations, copies the canonical file
# over the mirror (cp -p) and prints REFRESHED <name>. Mirror-only files
# are left alone. Then runs check_mirrors.sh and exits with its status.
#
#   ./lifter/tools/refresh_mirrors.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"

MIRROR_DIR="$REPO/lifter/tools/web"
CANON_DIR="$REPO/web/runtime/web"

shopt -s nullglob
for f in "$CANON_DIR"/*; do
    [ -e "$f" ] || continue
    name="$(basename "$f")"
    mirror="$MIRROR_DIR/$name"
    # Only refresh files that also exist in the mirror directory.
    [ -e "$mirror" ] || continue
    cp -p "$f" "$mirror"
    echo "REFRESHED $name"
done

bash "$REPO/lifter/tools/check_mirrors.sh"
exit $?
