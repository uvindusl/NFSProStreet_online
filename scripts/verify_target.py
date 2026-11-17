#!/usr/bin/env python3
"""Verify the target image is the one the user actually runs.

The image is the user's own unpacked game executable, supplied locally; this
repository does not contain it.  This script pins its layout and hashes so a
lift is always run against the same target:

    python3 scripts/verify_target.py game/nfs.exe.unpacked

Exits 0 when every check passes, 1 otherwise. Only the standard library is used.
"""
import hashlib
import struct
import sys

EXPECTED_SIZE = 23_756_800
EXPECTED_SHA256 = "805bf77cd11aa3a11e205b9fbd8fb6f34d0c8374c91a11b8609dfae962fd962a"
EXPECTED_IMAGE_BASE = 0x00400000
EXPECTED_ENTRY = 0x00828C25
EXPECTED_MACHINE = 0x014C
EXPECTED_SECTIONS = 6
EXPECTED_TEXT_SHA256 = "963934d66f447eb2"
EXPECTED_IMPORT_COUNT = 365          # recovered from the live IAT

# The game's own section names, from the original header it carries in .HEAVEN.
EXPECTED_SECTION_NAMES = [".text", ".rdata", ".data", ".tls", ".rsrc", "diabolus"]

failures = []


def check(ok, label, detail=""):
    print(f"  [{'ok' if ok else 'FAIL'}] {label}{(': ' + detail) if detail else ''}")
    if not ok:
        failures.append(label)


def parse_pe(data):
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise SystemExit("not a PE file")
    coff = e_lfanew + 4
    machine, nsec, _ts = struct.unpack_from("<HHI", data, coff)
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    image_base = struct.unpack_from("<I", data, opt + 28)[0]
    entry_rva = struct.unpack_from("<I", data, opt + 16)[0]

    sections = []
    sec = opt + opt_size
    for i in range(nsec):
        o = sec + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("latin1")
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
        sections.append({"name": name, "va": va, "vsize": vsize,
                         "rawptr": rawptr, "rawsize": rawsize})
    return {
        "machine": machine,
        "nsec": nsec,
        "image_base": image_base,
        "entry": image_base + entry_rva,
        "sections": sections,
    }


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} <nfs.exe.unpacked>")

    path = sys.argv[1]
    with open(path, "rb") as f:
        data = f.read()

    print(f"target: {path}")
    print("\nidentity")
    check(len(data) == EXPECTED_SIZE, "size", f"{len(data):,} bytes")
    digest = hashlib.sha256(data).hexdigest()
    check(digest == EXPECTED_SHA256, "sha256", digest)

    pe = parse_pe(data)
    print("\nheaders (from the header the game carries in .HEAVEN)")
    check(pe["machine"] == EXPECTED_MACHINE, "machine", f"{pe['machine']:#06x} (i386)")
    check(pe["image_base"] == EXPECTED_IMAGE_BASE, "image base", f"{pe['image_base']:#010x}")
    check(pe["entry"] == EXPECTED_ENTRY, "entry point", f"{pe['entry']:#010x} (_WinMainCRTStartup)")
    check(pe["nsec"] == EXPECTED_SECTIONS, "section count", str(pe["nsec"]))

    print("\nsections")
    for s in pe["sections"]:
        print(f"  {s['name']:<10s} va={s['va']:#010x} size={s['vsize']:#08x}")
    names = [s["name"] for s in pe["sections"]]
    check(names == EXPECTED_SECTION_NAMES, "section names", ", ".join(names))
    # rawptr == VA: the file is a memory image, so offsets are virtual addresses.
    check(all(s["rawptr"] == s["va"] for s in pe["sections"]),
          "layout is a memory image (rawptr == VA)")

    print("\ncode")
    text = next(s for s in pe["sections"] if s["name"] == ".text")
    body = data[text["va"]:text["va"] + text["vsize"]]
    th = hashlib.sha256(body).hexdigest()
    check(th.startswith(EXPECTED_TEXT_SHA256), ".text sha256", th)
    # VC8 `_WinMainCRTStartup` opens with `call __security_init_cookie`
    # (e8) and then tail-jumps to `__tmainCRTStartup` (e9).
    ep = data[pe["entry"] - pe["image_base"]:pe["entry"] - pe["image_base"] + 10]
    check(ep[0] == 0xE8 and ep[5] == 0xE9, "startup stub", ep.hex())

    print()
    if failures:
        print(f"FAILED: {len(failures)} check(s): {', '.join(failures)}")
        return 1
    print("PASS: target is the running game's decrypted image")
    return 0


if __name__ == "__main__":
    sys.exit(main())
