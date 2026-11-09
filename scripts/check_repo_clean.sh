#!/bin/bash
# Refuse to publish anything that must never leave the user's machine:
# the game, its data, Microsoft DLLs, generated C, saves, or huge blobs.
#
#   scripts/check_repo_clean.sh          # tracked files (or the work tree if
#                                        # this is not a git repository yet)
#
# Run by CI and worth running before every commit.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BANNED_EXT='\.(exe|dll|bff|bin|zip|7z|rar|iso|img|cab|msi|ogg|wav|mp3|vp6|abk|mus|dds|big|sav|sbf|pak|dat|db)$'
BANNED_PATH='^(game|work|out|ref-nfsu2-web|\.venv)/'
MAX_BYTES=$((2 * 1024 * 1024))

list() {
    if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git ls-files
    else
        find . \( -path ./.git -o -path ./game -o -path ./work -o -path ./out \
                  -o -path ./ref-nfsu2-web -o -path ./.venv \) -prune -o \
               -type f -print | sed 's|^\./||'
    fi
}

fail=0
while IFS= read -r f; do
    [ -n "$f" ] || continue
    if printf '%s' "$f" | grep -Eq "$BANNED_PATH"; then
        echo "ERROR: private directory must not be tracked: $f"
        fail=1
        continue
    fi
    if printf '%s' "$f" | grep -Eiq "$BANNED_EXT"; then
        echo "ERROR: game/binary artifact must not be tracked: $f"
        fail=1
        continue
    fi
    if [ "$(printf '%s' "$f" | tr '[:upper:]' '[:lower:]')" = ".env" ]; then
        echo "ERROR: do not commit environment files: $f"
        fail=1
        continue
    fi
    if [ -f "$f" ]; then
        sz=$(wc -c <"$f")
        if [ "$sz" -gt "$MAX_BYTES" ]; then
            echo "ERROR: file larger than 2 MiB ($((sz / 1024)) KiB): $f"
            fail=1
        fi
    fi
done < <(list)

if [ "$fail" -ne 0 ]; then
    echo
    echo "The repository must contain only source and documentation."
    echo "See AGENTS.md, rule 1: no game files, no Microsoft DLLs, no generated code."
    exit 1
fi
echo "clean: no game files, no binaries, no oversized blobs"
