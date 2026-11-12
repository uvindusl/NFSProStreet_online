#!/usr/bin/env python3
"""Find function-entry candidates in a PE32 image with no usable base-reloc table.

Scans every non-executable section for little-endian dwords that fall inside
the code section, and writes them to a seeds file (one hex address per line) for
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

    # Find the code section: prefer .text, fall back to the section containing
    # the entry point (supports games like ProStreet that use .IN).
    code_section = None
    for s in pe.sections:
        if s.Name.startswith(b".text"):
            code_section = s
            break
    if code_section is None:
        ep = pe.OPTIONAL_HEADER.AddressOfEntryPoint
        for s in pe.sections:
            if s.VirtualAddress <= ep < s.VirtualAddress + s.Misc_VirtualSize:
                code_section = s
                break
    if code_section is None:
        raise RuntimeError("cannot find code section in PE")
    lo = ib + code_section.VirtualAddress
    hi = lo + code_section.SizeOfRawData
    print(f"code section: {code_section.Name.decode().strip(chr(0))}  lo=0x{lo:x} hi=0x{hi:x}")

    hits = collections.Counter()
    seeds = set()
    for s in pe.sections:
        if s.Characteristics & 0x20000000:  # executable
            continue
        data = s.get_data()
        name = s.Name.rstrip(b"\0").decode("latin1")
        for off in range(0, len(data) - 3, 4):
            v = struct.unpack_from("<I", data, off)[0]
            if lo <= v < hi:
                # Check alignment: skip dwords that are not aligned to ALIGN bytes,
                # unless ALIGN is 1 (no alignment check).
                if ALIGN > 1 and v % ALIGN != 0:
                    continue
                seeds.add(v)
                hits[name] += 1
    with open(out, "w") as f:
        f.write("\n".join(f"{a:08x}" for a in sorted(seeds)) + "\n")
    print(f"in-code pointer slots: {sum(hits.values())}  unique entries: {len(seeds)}")
    print("slots per section:", dict(hits))
    print(f"-> {out}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[3] if len(sys.argv) > 3 else "out/seeds.txt")