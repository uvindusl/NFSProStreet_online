#!/usr/bin/env python3
"""Record which mods are installed in a copy of the game.

The port's behaviour depends on which runtime patches a reference run had
active, and mods are third-party binaries that must never enter this
repository.  This script writes a small JSON manifest (hashes only) and prints
a table, so a trace can name the exact set.

    scripts/mod_inventory.py [game dir] [--json out.json]

A game dir is the folder that contains `nfsps.exe`; by default
`game/install/app`.  It scans that folder and its `scripts/` subdirectory for
ASI plugins, their INI files and an ASI loader, and also records the
executable itself.
"""
import hashlib
import json
import os
import sys


def sha256(path, limit=None):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def scan(app_dir):
    entries = []
    names = []
    if os.path.isfile(os.path.join(app_dir, "nfsps.exe")):
        names.append("nfsps.exe")
    if os.path.isfile(os.path.join(app_dir, "dinput8.dll")):
        names.append("dinput8.dll")
    sdir = os.path.join(app_dir, "scripts")
    if os.path.isdir(sdir):
        names += [os.path.join("scripts", n) for n in sorted(os.listdir(sdir))]
    for rel in names:
        p = os.path.join(app_dir, rel)
        if not os.path.isfile(p):
            continue
        entries.append({
            "file": rel,
            "size": os.path.getsize(p),
            "sha256": sha256(p),
        })
    return entries


def main(app_dir, json_out):
    entries = scan(app_dir)
    if not entries:
        print(f"no game files under {app_dir}", file=sys.stderr)
        return 1
    w = max(len(e["file"]) for e in entries)
    for e in entries:
        print(f"{e['file']:<{w}}  {e['size']:>12,}  {e['sha256'][:16]}")
    print()
    print(f"executable: {entries[0]['sha256']}")
    if json_out:
        with open(json_out, "w") as f:
            json.dump({"app_dir": os.path.abspath(app_dir), "files": entries}, f, indent=2)
            f.write("\n")
        print(f"-> {json_out}")
    return 0


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = None
    if "--json" in sys.argv:
        i = sys.argv.index("--json")
        if i + 1 < len(sys.argv):
            out = sys.argv[i + 1]
            args = [a for a in args if a != out]
    sys.exit(main(args[0] if args else "game/install/app", out))
