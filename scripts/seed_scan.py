#!/usr/bin/env python3
"""Find function-entry candidates in a PE32 image with no usable base-reloc table.

Scans every non-executable section for little-endian dwords that fall inside
.text, and writes them to a seeds file (one hex address per line) for
pcrecomp-style / nfsu2-web-style function discovery.

The image here is a SecuROM-stripped build: the base relocation directory is
zeroed, so vtable/data references are the only static evidence of function
entry points outside the direct-call graph.
"""
import sys, struct, collections
import pefile

ALIGN = int(sys.argv[2]) if len(sys.argv) > 2 else 16


def main(exe: str, out: str) -> None:
    pe = pefile.PE(exe)
    ib = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    lo = ib + text.VirtualAddress
    hi = lo + text.SizeOfRawData
    hits = collections.Counter()
    seeds = set()
    for s in pe.sections:
        if s.Characteristics & 0x20000000:  # executable
            continue
        data = s.get_data()
        name = s.Name.rstrip(b"\0").decode("latin1")
        for off in range(0, len(data) - 3, 4):
            v = struct.unpack_from("<I", data, off)[0]
            if lo <= v < hi and v % ALIGN == 0:
                seeds.add(v)
                hits[name] += 1
    with open(out, "w") as f:
        f.write("\n".join(f"{a:08x}" for a in sorted(seeds)) + "\n")
    print(f"in-text pointer slots: {sum(hits.values())}  unique entries: {len(seeds)}")
    print("slots per section:", dict(hits))
    print(f"-> {out}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[3] if len(sys.argv) > 3 else "out/seeds.txt")
