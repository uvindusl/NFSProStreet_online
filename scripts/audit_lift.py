#!/usr/bin/env python3
"""Audit what the lift still cannot do.

Scans the generated shards for `unsupported_insn(c, 0xADDR, "reason")` stubs
and groups them by reason and by the function that contains them.  This is the
gate for lifter completeness: a build is only as good as the guest functions
it can actually execute.
"""
import glob
import re
import sys
import collections

TRAP = re.compile(r'unsupported_insn\(c,\s*0x([0-9a-f]+)u,\s*"([^"]*)"\)')
FUNC = re.compile(r'^void sub_([0-9a-f]{8})\(CPU \*restrict c\)', re.M)


def main(pattern: str) -> None:
    per_reason = collections.Counter()
    per_func = collections.Counter()
    where = {}
    files = sorted(glob.glob(pattern))
    for path in files:
        src = open(path, encoding="utf-8", errors="replace").read()
        starts = [(m.start(), m.group(1)) for m in FUNC.finditer(src)]
        for m in TRAP.finditer(src):
            reason = m.group(2)
            per_reason[reason] += 1
            owner = "<none>"
            for off, addr in starts:
                if off > m.start():
                    break
                owner = addr
            per_func[owner] += 1
            where.setdefault(reason, owner if owner != "<none>" else m.group(1))
    print(f"shards scanned     : {len(files)}")
    print(f"trap sites         : {sum(per_reason.values())}")
    print(f"functions affected : {len(per_func)}")
    print("\nsites per reason")
    for reason, n in per_reason.most_common():
        print(f"  {reason:16} {n:5}  e.g. {where[reason]}")
    print("\nworst functions")
    for addr, n in per_func.most_common(20):
        print(f"  sub_{addr}  {n}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "work/gen/shard_*.c")
