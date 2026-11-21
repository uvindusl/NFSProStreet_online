#!/usr/bin/env python3
"""Classify the parts of .text the static discovery pass did not reach.

    python3 scripts/classify_gaps.py game/nfsps.exe out/analysis.json

Every uncovered range is put in one of four buckets:

    prologue   starts with an MSVC function prologue (push ebp/mov ebp,esp,
               SEH frame, hot-patch stub) - real code reached only indirectly
    code       no prologue but disassembles cleanly for a while
    data       pointer table, float constants or an ASCII string
    unknown    none of the above

The point is to know how much of the missing 27 % of .text is code we still
have to find, and how much is data that lives in the code section.
"""
import json
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

PROLOGUES = [
    b"\x55\x8b\xec",                    # push ebp; mov ebp, esp
    b"\x6a\xff\x68",                    # SEH: push -1; push handler
    b"\x8b\xff\x55\x8b\xec",            # hot-patch: mov edi, edi; push ebp; mov ebp, esp
    b"\x53\x8b\xdc",                    # push ebx; mov ebx, esp
]


def classify(blob, md, va, image_lo, image_hi):
    head = blob[:8]
    for p in PROLOGUES:
        if head.startswith(p):
            return "prologue"

    if len(blob) >= 4:
        v = struct.unpack_from("<I", blob, 0)[0]
        if image_lo <= v < image_hi:
            return "data"

    printable = 0
    best = 0
    for b in blob[:128]:
        printable = printable + 1 if 32 <= b < 127 else 0
        best = max(best, printable)
    if best >= 8:
        return "data"

    # Code decodes cleanly and reaches a terminator quickly; data usually runs
    # into an invalid instruction or keeps decoding forever.
    count = 0
    for insn in md.disasm(blob[:512], va):
        count += 1
        if insn.mnemonic in ("ret", "retf", "iret", "iretd", "jmp", "int3", "hlt", "ud2"):
            return "code"
        if count > 64:
            break
    return "unknown"


def main():
    exe, analysis = sys.argv[1], sys.argv[2]
    data = open(exe, "rb").read()
    a = json.load(open(analysis))
    md = Cs(CS_ARCH_X86, CS_MODE_32)

    base = a["image_base"]
    image_lo, image_hi = base, base + a["size_of_image"]
    text = next(s for s in a["sections"] if s["name"] == ".text")
    text_raw = text["rawptr"]

    def blob_at(va, size):
        off = text_raw + (va - base - text["va"])
        return data[off:off + size]

    buckets = {"prologue": [0, 0], "code": [0, 0], "data": [0, 0], "unknown": [0, 0]}
    examples = {k: [] for k in buckets}
    for va, size in a["gaps"]:
        if size < 32:
            continue
        kind = classify(blob_at(va, size), md, va, image_lo, image_hi)
        buckets[kind][0] += 1
        buckets[kind][1] += size
        if len(examples[kind]) < 8:
            examples[kind].append((va, size))

    total_bytes = sum(v[1] for v in buckets.values())
    print(f"uncovered ranges >= 32 bytes: {sum(v[0] for v in buckets.values()):,}")
    print(f"uncovered bytes in those ranges: {total_bytes:,}\n")
    for kind, (count, size) in sorted(buckets.items(), key=lambda kv: -kv[1][1]):
        pct = 100.0 * size / total_bytes if total_bytes else 0
        print(f"{kind:9s} {count:6,} ranges  {size:9,} bytes  {pct:5.1f}%")
        for va, sz in examples[kind][:5]:
            print(f"          {va:#010x}  {sz:>7,}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
