#!/usr/bin/env python3
"""First-pass reconnaissance of a PE32 image for static recompilation.

Prints the image layout, imports, exception-ish metadata, relocation coverage,
section entropy (packer check) and an instruction-mix sample of executable
sections.  Used to decide what the lifter has to support.
"""
import sys, math, collections
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32


def entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = collections.Counter(data)
    n = len(data)
    return -sum((c / n) * math.log2(c / n) for c in counts.values())


def main(path: str) -> None:
    pe = pefile.PE(path, fast_load=False)
    print(f"file        : {path}")
    print(f"machine     : {pe.FILE_HEADER.Machine:#x}")
    print(f"timestamp   : {pe.FILE_HEADER.TimeDateStamp}")
    print(f"image base  : {pe.OPTIONAL_HEADER.ImageBase:#x}")
    print(f"entry point : {pe.OPTIONAL_HEADER.ImageBase + pe.OPTIONAL_HEADER.AddressOfEntryPoint:#x}")
    print(f"size of img : {pe.OPTIONAL_HEADER.SizeOfImage:#x}")
    print(f"subsystem   : {pe.OPTIONAL_HEADER.Subsystem}")
    print(f"dll chars   : {pe.OPTIONAL_HEADER.DllCharacteristics:#x}")
    print(f"checksum    : {pe.OPTIONAL_HEADER.CheckSum:#x}")

    print("\nsections")
    print(f"  {'name':8} {'va':>10} {'vsize':>10} {'raw':>10} {'entropy':>8}  flags")
    for s in pe.sections:
        name = s.Name.rstrip(b"\0").decode("latin1")
        flags = []
        if s.Characteristics & 0x20000000:
            flags.append("X")
        if s.Characteristics & 0x80000000:
            flags.append("W")
        if s.Characteristics & 0x40000000:
            flags.append("R")
        print(
            f"  {name:8} {s.VirtualAddress:>10x} {s.Misc_VirtualSize:>10x} "
            f"{s.SizeOfRawData:>10x} {entropy(s.get_data()):>8.3f}  {'|'.join(flags)}"
        )

    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        print("\nimports")
        total = 0
        for d in pe.DIRECTORY_ENTRY_IMPORT:
            names = [i.name.decode("latin1") if i.name else f"ord{i.ordinal}" for i in d.imports]
            total += len(names)
            print(f"  {d.dll.decode('latin1'):20} {len(names):4}  {', '.join(names[:10])}")
        print(f"  total imports: {total}")
    else:
        print("\nimports: NONE (packed?)")

    if hasattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT"):
        print("\ndelay imports")
        for d in pe.DIRECTORY_ENTRY_DELAY_IMPORT:
            names = [i.name.decode("latin1") if i.name else f"ord{i.ordinal}" for i in d.imports]
            print(f"  {d.dll.decode('latin1'):20} {len(names):4}  {', '.join(names[:10])}")

    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT") and pe.DIRECTORY_ENTRY_EXPORT.symbols:
        print("\nexports")
        for e in pe.DIRECTORY_ENTRY_EXPORT.symbols[:20]:
            print(f"  {e.ordinal:4} {e.name}")

    reloc = pe.DIRECTORY_ENTRY_BASERELOC if hasattr(pe, "DIRECTORY_ENTRY_BASERELOC") else []
    nrel = sum(len(b.entries) for b in reloc)
    print(f"\nrelocations : {nrel} ({len(reloc)} blocks)")

    # Capstone instruction-mix sample over executable sections.
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = False
    mnems = collections.Counter()
    total_ins = 0
    for s in pe.sections:
        if not (s.Characteristics & 0x20000000):
            continue
        data = s.get_data()
        va = pe.OPTIONAL_HEADER.ImageBase + s.VirtualAddress
        for ins in md.disasm(data, va):
            mnems[ins.mnemonic] += 1
            total_ins += 1
    print(f"\ninstructions decoded: {total_ins}")
    print("top 40 mnemonics")
    for m, c in mnems.most_common(40):
        print(f"  {m:10} {c:8} {100.0 * c / max(total_ins, 1):5.2f}%")


if __name__ == "__main__":
    main(sys.argv[1])
