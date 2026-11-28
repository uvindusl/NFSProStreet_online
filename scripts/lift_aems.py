#!/usr/bin/env python3
"""Lift the EA AEMS audio module banks a run pinned and dumped.

    scripts/lift_aems.py [dumpdir] [outdir]     (default out/aems out/lift-aems)

Input, written by a run with PROSTREET_AEMS_DUMP=1 (lifter/runtime/aems.c):
  dumpdir/banks.txt          "<hash> <base> <size>" per bank (hex)
  dumpdir/bank_<hash>_<base>.bin   the bank after the game resolved it there
  dumpdir/seeds.txt          "<hash> <offset>": a call reached that offset
                             of a pinned bank with no lifted function

A resolved bank depends only on its base (aems.c pins each bank to the same
address on every run), so its code is lifted like the image's: one C
function per entry, calls between the bank's functions are direct, calls out
of the bank go through dispatch_indirect.

Output:
  outdir/bank_<hash>.c       the bank's functions
  outdir/aems_table.c        aems_known[] (hash, base, size: the pins) and the
                             sorted lookup table aems.c serves to dispatch.c

Both are game-derived and stay in out/.  Run it, rebuild the runtime
(scripts/build_runtime.sh compiles outdir when it exists) and run again:
calls that reach a function not lifted yet are added to seeds.txt, so a
few rounds of run + lift cover the code the game reaches.
"""
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'lifter', 'tools'))
os.environ.setdefault('JTSWITCH', '1')
from lift import Lifter, RawLifter  # noqa: E402

# RawLifter borrows a fixed list of Lifter's methods (enough for the
# instruction tests); a whole function needs the rest too.
for _n, _v in vars(Lifter).items():
    if not _n.startswith('__') and not hasattr(RawLifter, _n):
        setattr(RawLifter, _n, _v)


def main():
    dump = sys.argv[1] if len(sys.argv) > 1 else 'out/aems'
    out = sys.argv[2] if len(sys.argv) > 2 else 'out/lift-aems'
    # One address per bank: the first one recorded, unless it overlaps a bank
    # already placed (that bank is pinned elsewhere on the next run, dumped
    # again and placed then).  The runtime pins known banks at these addresses.
    banks, taken = {}, []
    with open(os.path.join(dump, 'banks.txt')) as f:
        for line in f:
            p = line.split()
            if len(p) != 3:
                continue
            h, base, size = int(p[0], 16), int(p[1], 16), int(p[2], 16)
            if h in banks or not os.path.exists(os.path.join(dump, f'bank_{h:08x}_{base:08x}.bin')):
                continue
            if any(base < b + s and b < base + size for b, s in taken):
                continue
            banks[(h, base)] = size
            taken.append((base, size))
    seeds = collections.defaultdict(set)     # hash -> offsets
    sp = os.path.join(dump, 'seeds.txt')
    if os.path.exists(sp):
        with open(sp) as f:
            for line in f:
                p = line.split()
                if len(p) == 2:
                    seeds[int(p[0], 16)].add(int(p[1], 16))
    os.makedirs(out, exist_ok=True)
    for name in os.listdir(out):
        if name.endswith('.c'):
            os.remove(os.path.join(out, name))

    table = []          # (addr, hash)
    known = []
    for (h, base), size in sorted(banks.items(), key=lambda kv: kv[0][1]):
        path = os.path.join(dump, f'bank_{h:08x}_{base:08x}.bin')
        with open(path, 'rb') as f:
            data = f.read()
        if len(data) != size:
            print(f'lift_aems: {path}: {len(data)} bytes, banks.txt says {size:#x}; skipped', file=sys.stderr)
            continue
        known.append((h, base, size))
        L = RawLifter(data, base=base, comments=False)
        L.hookret = set()
        todo = sorted(base + o for o in seeds.get(h, ()) if o < size)
        done, parts, failed = set(), [], 0
        while todo:
            a = todo.pop()
            if a in done:
                continue
            done.add(a)
            try:
                code, tg = L.lift(a)
            except Exception as e:      # an entry the lifter cannot decode
                failed += 1
                print(f'lift_aems: bank {h:08x}: {a:#x} not lifted ({type(e).__name__}: {e})', file=sys.stderr)
                code = (f'void sub_{a:08x}(CPU *restrict c) {{ unsupported_insn(c, 0x{a:x}u, "aems-lift-error"); '
                        f'POP32(c->eip); }}')
                tg = set()
            parts.append(code)
            todo.extend(t for t in tg if L.in_text(t) and t not in done)
        if not done:
            print(f'lift_aems: bank {h:08x} at {base:#x}: no seeds yet (run with PROSTREET_AEMS_DUMP=1 to collect them)',
                  file=sys.stderr)
        with open(os.path.join(out, f'bank_{h:08x}.c'), 'w') as f:
            f.write('#include "cpu.h"\n#include <math.h>\n')
            f.write('void dispatch_indirect(CPU*,uint32_t);\nvoid dispatch_tail(CPU*);\nvoid unsupported_insn(CPU*,uint32_t,const char*);\n')
            f.write('void cpuid_shim(CPU*);\nuint64_t rdtsc_shim(void);\n\n')
            for a in sorted(done):
                f.write(f'void sub_{a:08x}(CPU *restrict c);\n')
            f.write('\n' + '\n\n'.join(parts) + '\n')
        table += [(a, h) for a in done]
        print(f'lift_aems: bank {h:08x} at {base:#x} ({size:#x} bytes): {len(done)} functions, {failed} failed')

    table.sort()
    with open(os.path.join(out, 'aems_table.c'), 'w') as f:
        f.write('#include "cpu.h"\n\n')
        f.write('typedef struct { uint32_t hash, base, size; } AemsBank;\ntypedef void (*aems_fn_t)(CPU *);\n')
        for a, _ in table:
            f.write(f'void sub_{a:08x}(CPU *restrict c);\n')
        f.write(f'const uint32_t aems_known_count = {len(known)};\n')
        f.write('const AemsBank aems_known[] = {\n' +
                ''.join(f'    {{ 0x{h:08x}u, 0x{b:08x}u, 0x{s:x}u }},\n' for h, b, s in known) +
                ('' if known else '    { 0, 0, 0 },\n') + '};\n')
        f.write(f'const uint32_t aems_fn_count = {len(table)};\n')
        f.write('const uint32_t aems_fn_addr[] = {\n' + ''.join(f'    0x{a:08x}u,\n' for a, _ in table) +
                ('' if table else '    0,\n') + '};\n')
        f.write('const aems_fn_t aems_fn[] = {\n' + ''.join(f'    sub_{a:08x},\n' for a, _ in table) +
                ('' if table else '    0,\n') + '};\n')


if __name__ == '__main__':
    main()
