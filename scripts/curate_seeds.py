#!/usr/bin/env python3
"""Report how many raw in-.text pointer hits are plausible instruction boundaries.

The image has no base relocations, so `scripts/seed_scan.py` collects every
dword in a non-executable section that points into `.text`.  Not all of them
are function entries: a jump table, a float bit pattern or a constant can point
into the middle of an instruction too.

Classification per seed:

  clean    no instruction from any possible start in the 15 bytes before it
           spans the seed (an x86 instruction is at most 15 bytes), so the
           seed is at least an instruction boundary
  straddle some start in those 15 bytes decodes across it, which suggests the
           bytes are data or a table entry rather than code

**This is a diagnostic, not a filter.**  The straddle test has false
positives (a random start byte before a real entry often decodes across it),
and dropping a seed that really is a function also drops every function that
is only reachable through it.  Lifting a data seed instead just produces an
unreachable function.  `scripts/lift.sh` therefore lifts every seed and uses
this report only to size the noise.  `--write` emits the `clean` subset for
experiments.
"""
import sys
import collections
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

MAXLEN = 15


def load_text(exe):
    pe = pefile.PE(exe)
    ib = pe.OPTIONAL_HEADER.ImageBase
    sec = next(s for s in pe.sections if s.Name.startswith(b".text"))
    return ib + sec.VirtualAddress, sec.get_data()


def straddled(md, data, tva, addr):
    """True if some instruction starting in the 15 bytes before addr covers it."""
    off = addr - tva
    for back in range(1, MAXLEN):
        if off - back < 0:
            break
        start = addr - back
        try:
            ins = next(md.disasm(data[off - back:off - back + MAXLEN], start))
        except StopIteration:
            continue
        if ins.address < addr < ins.address + ins.size:
            return True
    return False


def main(exe, seeds_path, out_path=None, write=False):
    tva, data = load_text(exe)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    seeds = [int(x, 16) for x in open(seeds_path) if x.strip()]
    classes = collections.Counter()
    keep, drop = [], []
    for a in seeds:
        if not (tva <= a < tva + len(data)):
            classes["outside .text"] += 1
            continue
        if straddled(md, data, tva, a):
            classes["straddle (likely data)"] += 1
            drop.append(a)
        else:
            classes["clean boundary"] += 1
            keep.append(a)
    print(f"seeds        : {len(seeds)}")
    for k, v in classes.most_common():
        print(f"  {k:24} {v}")
    if write:
        with open(out_path, "w") as f:
            f.write("\n".join(f"{a:08x}" for a in keep) + "\n")
        print(f"-> {out_path} ({len(keep)} seeds)")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    write = "--write" in sys.argv
    main(args[0], args[1], args[2] if len(args) > 2 else None, write)
