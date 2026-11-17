#!/bin/bash
# Check that lifter/tools/web/ is a byte-identical mirror of web/runtime/web/
# for every file they share.  Exits non-zero if any shared file differs.
#
#   ./lifter/tools/check_mirrors.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"

MIRROR_DIR="$REPO/lifter/tools/web"
CANON_DIR="$REPO/web/runtime/web"

status=0
shopt -s nullglob
for f in "$MIRROR_DIR"/*; do
    [ -e "$f" ] || continue
    name="$(basename "$f")"
    canon="$CANON_DIR/$name"
    # Only compare files that exist in both locations.
    [ -e "$canon" ] || continue
    if cmp -s "$f" "$canon"; then
        echo "PASS $name"
    else
        echo "DIFF $name"
        status=1
    fi
done
exit $status
