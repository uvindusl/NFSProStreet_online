#!/usr/bin/env python3
"""Inventory the traps in the generated C and measure what can reach them.

    python3 scripts/lift_traps.py out/lift-reachable out/analysis.json out/lift-traps.json

Reads the generated shards, builds the direct-call graph between lifted
functions, walks it from the entry point, and writes a JSON report:

  * every function that contains an unsupported instruction, with its reasons,
  * whether a direct-call path from `_WinMainCRTStartup` reaches it,
  * how many lifted functions are reachable that way at all.

Indirect calls and jumps are not followed: a function that is only reachable
through a vtable or an import thunk is reported as unreachable, which is a
lower bound, not a claim that it is dead.
"""
import collections
import json
import os
import re
import sys

DEF_RE = re.compile(r"^(?:static inline[^\n]*)?void sub_([0-9a-f]{8})\(", re.M)
CALL_RE = re.compile(r"sub_([0-9a-f]{8})\(c\)")
TRAP_RE = re.compile(r'unsupported_insn\(c,\s*0x([0-9a-fA-F]+)u,\s*"([^"]*)"\)')


def scan(gen_dir):
    edges = collections.defaultdict(set)
    traps = collections.defaultdict(list)
    defined = set()
    for sh in sorted(os.listdir(gen_dir)):
        if not re.fullmatch(r"shard_\d+\.c", sh):
            continue
        src = open(os.path.join(gen_dir, sh), encoding="latin1").read()
        hits = list(DEF_RE.finditer(src))
        for i, m in enumerate(hits):
            fn = int(m.group(1), 16)
            defined.add(fn)
            end = hits[i + 1].start() if i + 1 < len(hits) else len(src)
            body = src[m.start():end]
            for c in CALL_RE.finditer(body):
                edges[fn].add(int(c.group(1), 16))
            for t in TRAP_RE.finditer(body):
                traps[fn].append({"ip": int(t.group(1), 16), "reason": t.group(2)})
    return defined, edges, traps


def reachable_from(edges, entry):
    seen = {entry}
    stack = [entry]
    while stack:
        fn = stack.pop()
        for callee in edges.get(fn, ()):
            if callee not in seen:
                seen.add(callee)
                stack.append(callee)
    return seen


def main(gen_dir, analysis_path, out_path):
    a = json.load(open(analysis_path))
    entry = a["entry"]
    defined, edges, traps = scan(gen_dir)
    reachable = reachable_from(edges, entry)

    report = {
        "entry": entry,
        "lifted_functions": len(defined),
        "direct_call_reachable": len(reachable),
        "trapped_functions": [],
        "trap_sites": sum(len(v) for v in traps.values()),
        "reachable_trap_sites": sum(len(traps[f]) for f in traps if f in reachable),
    }
    for fn in sorted(traps):
        reasons = collections.Counter(t["reason"] for t in traps[fn])
        report["trapped_functions"].append({
            "address": fn,
            "reachable": fn in reachable,
            "sites": len(traps[fn]),
            "reasons": dict(reasons),
            "instructions": [t["ip"] for t in traps[fn]],
        })
    json.dump(report, open(out_path, "w"), indent=1)

    print(f"lifted functions           {report['lifted_functions']:,}")
    print(f"direct-call reachable      {report['direct_call_reachable']:,} "
          f"from {entry:#x}")
    print(f"trap sites                 {report['trap_sites']} in "
          f"{len(traps)} functions")
    print(f"  reachable from entry     {report['reachable_trap_sites']} in "
          f"{sum(1 for f in traps if f in reachable)} functions")
    print(f"wrote {out_path}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(f"usage: {sys.argv[0]} <gen-dir> <analysis.json> <out.json>")
    main(*sys.argv[1:])
