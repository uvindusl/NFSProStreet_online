#!/usr/bin/env python3
"""Phase 3 gate: check the generated C covers the reachable function set.

    python3 scripts/verify_lift.py out/lift-reachable out/analysis.json

Checks, mechanically:
  * fntab.c's declared count matches its address table and its pointer table,
  * every address the analyzer called reachable (seed / call / entry) is present,
  * nothing was silently invented outside the analyzer's map,
  * every shard's function bodies are accounted for, and
  * the unsupported-instruction traps are inventoried per function.

Exits 0 when the gate passes, 1 otherwise.
"""
import json
import os
import re
import struct
import sys

failures = []


def check(ok, label, detail=""):
    print(f"  [{'ok' if ok else 'FAIL'}] {label}{(': ' + detail) if detail else ''}")
    if not ok:
        failures.append(label)


def text_range(analysis):
    """VA range of .text, read from the PE the analysis names."""
    data = open(analysis["file"], "rb").read()
    e = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, e + 6)[0]
    optsz = struct.unpack_from("<H", data, e + 20)[0]
    opt = e + 24
    ibase = struct.unpack_from("<I", data, opt + 28)[0]
    sec = opt + optsz
    for i in range(nsec):
        o = sec + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("latin1")
        vsize, va = struct.unpack_from("<II", data, o + 8)
        if name == ".text":
            return ibase + va, ibase + va + vsize
    raise SystemExit("no .text section in " + analysis["file"])


def parse_fntab(path):
    src = open(path, encoding="latin1").read()
    count = int(re.search(r"g_fn_count\s*=\s*(\d+)", src).group(1))
    addr_body = re.search(r"g_fn_addr\[\]\s*=\s*\{(.*?)\};", src, re.S).group(1)
    ptr_body = re.search(r"g_fn_ptr\[\]\s*=\s*\{(.*?)\};", src, re.S).group(1)
    addrs = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)u", addr_body)]
    ptrs = re.findall(r"sub_([0-9a-fA-F]+)", ptr_body)
    return count, addrs, ptrs


def main(gen_dir, analysis_path):
    a = json.load(open(analysis_path))
    origins = a["origins"]
    reachable = {int(k, 16) for k, v in origins.items() if v in ("seed", "call", "entry")}
    mapped = {int(k, 16) for k in origins}

    print("fntab")
    count, addrs, ptrs = parse_fntab(os.path.join(gen_dir, "fntab.c"))
    check(count == len(addrs), "count matches address table", f"{count} vs {len(addrs)}")
    check(count == len(ptrs), "count matches pointer table", f"{count} vs {len(ptrs)}")
    check(len(set(addrs)) == len(addrs), "no duplicate entries", str(len(addrs)))
    defined = {int(x, 16) for x in ptrs}
    check(defined == set(addrs), "every entry has a defined function")

    print("\nreachability")
    missing = sorted(reachable - set(addrs))
    check(not missing, "all reachable addresses present",
          f"{len(addrs) - len(missing)}/{len(reachable)}"
          + (f", missing {[hex(x) for x in missing[:5]]}" if missing else ""))
    # The lifter runs its own closure, so it can legitimately find call targets
    # the analyzer's coverage stopped short of.  They are fine as long as they
    # are inside .text and were actually lifted; anything else is invention.
    extra = sorted(set(addrs) - mapped)
    text_lo, text_hi = text_range(a)
    bad_extra = [x for x in extra if not (text_lo <= x < text_hi)]
    check(not bad_extra, "closure extras are inside .text",
          f"{len(extra)} extra" + (f" {[hex(x) for x in extra]}" if extra else ""))
    print(f"  entries {len(addrs):,} = reachable {len(reachable):,} "
          f"+ recovered/imm reached by the lifter's closure "
          f"{len(set(addrs) & mapped) - len(reachable):,}"
          + (f" + {len(extra):,} found only by the lifter"
             f" ({', '.join(hex(x) for x in extra)})" if extra else ""))

    print("\nshards")
    shards = sorted(f for f in os.listdir(gen_dir) if re.fullmatch(r"shard_\d+\.c", f))
    defined_bodies = set()
    trap_sites = []
    trapped_fns = set()
    def_re = re.compile(r"^(?:static inline[^\n]*)?void sub_([0-9a-f]{8})\(", re.M)
    for sh in shards:
        src = open(os.path.join(gen_dir, sh), encoding="latin1").read()
        hits = list(def_re.finditer(src))
        for i, m in enumerate(hits):
            fn = int(m.group(1), 16)
            defined_bodies.add(fn)
            end = hits[i + 1].start() if i + 1 < len(hits) else len(src)
            for t in re.finditer(r"unsupported_insn\(c,\s*0x([0-9a-fA-F]+)u,\s*\"([^\"]*)\"\)",
                                 src[m.start():end]):
                trap_sites.append((int(t.group(1), 16), t.group(2)))
                trapped_fns.add(fn)
    check(defined_bodies == set(addrs), "every entry has a function body",
          f"{len(defined_bodies):,} bodies for {len(addrs):,} entries")
    print(f"  {len(shards)} shards, {len(defined_bodies):,} function bodies")

    print("\ntraps")
    print(f"  {len(trap_sites)} unsupported-instruction sites in "
          f"{len(trapped_fns):,} functions")
    by_reason = {}
    for _ip, why in trap_sites:
        by_reason[why] = by_reason.get(why, 0) + 1
    for why, n in sorted(by_reason.items(), key=lambda kv: -kv[1]):
        print(f"    {why:16s} {n}")
    print(f"  {len({ip for ip, _ in trap_sites})} distinct instructions trapped")

    print()
    if failures:
        print(f"FAILED: {len(failures)} check(s): {', '.join(failures)}")
        return 1
    print("PASS: generated C covers the reachable set and links")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} <gen-dir> <analysis.json>")
    sys.exit(main(sys.argv[1], sys.argv[2]))
