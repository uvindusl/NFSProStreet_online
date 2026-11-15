#!/usr/bin/env python3
"""Recover the authoritative import table for ProStreet.

The packed retail nfs.exe that the running game decrypts has its import
directory zeroed by SecuROM: RVA 0x64793c holds 16 empty descriptors, and the
IAT directory (RVA 0x567000, 0x608 bytes) holds decoy pointers.  Matching
those decoy values against the live process's loaded DLLs produced
out/imports-runtime.json -- 351 of its 365 names are wrong.

game/nfsps.exe (the unprotected build in the same install) carries the real
import directory at the same RVAs, and its .text is byte-identical to the
image the game decrypts in memory (SHA-256 963934d6...e65e31).  This script
reads that table, checks it against the target image, and writes
out/imports-table.json:

    [{"slot": <rva>, "va": <image base + rva>, "dll": "...", "name": "..."}]

Usage: scripts/imports_from_table.py [source.exe] [target.exe]
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "game/nfsps.exe"
TGT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "artifacts/nfs.exe.unpacked"
OUT = ROOT / "out/imports-table.json"

DIR_RVA = 0x64793C  # import directory (same in both files)
TEXT_RVA = 0x1000
TEXT_SHA = "963934d66f447eb21e81ca66f0df8d8e8d2a598acc9f0726c3dc1e68b5e65e31"


def parse(data):
    e = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e:e + 4] != b"PE\0\0":
        raise SystemExit("not a PE")
    nsec, = struct.unpack_from("<H", data, e + 6)
    optsz, = struct.unpack_from("<H", data, e + 20)
    opt = e + 24
    base, = struct.unpack_from("<I", data, opt + 28)
    sh = opt + optsz
    secs = []
    for i in range(nsec):
        s = sh + i * 40
        vs, va, rs, ro = struct.unpack_from("<IIII", data, s + 8)
        secs.append((va, max(vs, rs), ro))
    return base, secs


def main():
    src = SRC.read_bytes()
    tgt = TGT.read_bytes()
    base, secs = parse(src)

    def off(rva):
        for va, sz, ro in secs:
            if va <= rva < va + sz:
                return ro + (rva - va)
        raise SystemExit(f"RVA {rva:#x} is not in any section")

    def cstr(rva):
        o = off(rva)
        return src[o:src.index(b"\0", o)].decode("latin1")

    # The source and the target must be the same code, or the table does not
    # describe the image we lift.
    tsha = hashlib.sha256(src[TEXT_RVA:TEXT_RVA + 0x56507B]).hexdigest()
    if tsha != TEXT_SHA:
        raise SystemExit(f"{SRC}: .text SHA-256 {tsha} is not the pinned {TEXT_SHA}")

    rows = []
    o = off(DIR_RVA)
    for i in range(16):
        oft, _tds, _fc, nm, ft = struct.unpack_from("<IIIII", src, o + i * 20)
        if not nm:
            break
        dll = cstr(nm)
        j = 0
        while True:
            t, = struct.unpack_from("<I", src, off(oft) + j * 4)
            if t == 0:
                break
            name = f"#{t & 0xffff}" if t & 0x80000000 else cstr((t & 0x7FFFFFFF) + 2)
            va = base + ft + j * 4
            rows.append({"slot": ft + j * 4, "va": va, "dll": dll, "name": name})
            j += 1

    # Every slot must land in the target's IAT range and be non-zero there
    # (SecuROM's decoy values are still pointers, which is why a value-based
    # match looked plausible).
    iat_rva, iat_size = struct.unpack_from("<II", src, struct.unpack_from("<I", src, 0x3C)[0] + 24 + 96 + 12 * 8)
    lo, hi = iat_rva, iat_rva + iat_size
    bad = [r for r in rows if not (lo <= r["slot"] < hi)]
    if bad:
        raise SystemExit(f"{len(bad)} slots outside the IAT directory {lo:#x}..{hi:#x}")
    zero = [r for r in rows if struct.unpack_from("<I", tgt, r["slot"])[0] == 0]
    if zero:
        raise SystemExit(f"{len(zero)} slots are zero in {TGT.name}")

    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(json.dumps(rows, indent=0) + "\n")
    dlls = sorted({r["dll"] for r in rows})
    print(f"{SRC.name}: {len(rows)} imports across {len(dlls)} DLLs")
    print(f"  IAT 0x{lo:x}..0x{hi:x} ({iat_size // 4} slots), "
          f"{len(rows)} named by {len(dlls)} descriptors")
    for d in dlls:
        print(f"  {sum(1 for r in rows if r['dll'] == d):4d}  {d}")
    print(f"-> {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
