#!/usr/bin/env python3
"""Resolve every EA AEMS module bank of the game offline, for lifting.

    scripts/aems_catalog.py [gamedir] [image] [dumpdir]
        (default game artifacts/nfs.exe.unpacked out/aems)

The sound library's module banks are the game's .abk files (magic "ABKC"),
loaded verbatim.  sub_008b9e60 copies a bank to the memory its allocator
returns and sub_008b9bf0 resolves it there:

  - layout: bytes [0, [+0x18]) at the base, then [+0x24] bytes of data from
    the file's offset [+0x20] at base + [+0x18]; [+0x40] = base + [+0x20]
  - function fixups (list at file offset [+0x30]: count, then offsets): the
    word at the offset holds an index into the primitive table at 0x00a8e348
    and becomes a rel32 to that primitive
  - static-data fixups (list at [+0x34]): the word gets the base added
  - interface references (list at [+0x38], 12-byte entries {offset, name,
    kind}): looked up by name at run time; they must land in data, not code
    (checked below), so the lifted code does not depend on them

A run pins each bank at a fixed address (lifter/runtime/aems.c), so this
script can produce the same resolved bytes for every bank, including the
ones a given run never loads (the damage and car banks change per run).

Output, the same format a PROSTREET_AEMS_DUMP=1 run writes, so
scripts/lift_aems.py lifts it:
  dumpdir/bank_<hash>_<base>.bin, dumpdir/banks.txt
  dumpdir/seeds.txt   + every module's entry point, as "<hash> <offset>"

Banks already in banks.txt keep their address; the others are placed after
them (AEMS_RELAYOUT=1 places every bank afresh).  A bank the runs saw added twice at once keeps both addresses.  When a
run dump of a bank exists (bank_<hash>_<base>.bin), the code ranges are
compared with this script's result first and a mismatch stops it.
Everything written is game data and stays in out/.
"""
import collections
import os
import struct
import sys

AEMS_BASE, AEMS_END, ALIGN = 0x6c000000, 0x6ff00000, 0x10000
PRIMS, NPRIMS = 0x00a8e348, 256


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def bank_hash(b, n):
    h = 2166136261
    for i in range(n):
        if 0x3c <= i < 0x44:
            continue
        h = ((h ^ b[i]) * 16777619) & 0xffffffff
    return h


class Image:
    def __init__(self, path):
        d = open(path, 'rb').read()
        pe = u32(d, 0x3c)
        ns = struct.unpack_from('<H', d, pe + 6)[0]
        so = pe + 24 + struct.unpack_from('<H', d, pe + 20)[0]
        self.d, self.secs = d, []
        for i in range(ns):
            _, vs, va, rs, raw = struct.unpack_from('<8sIIII', d, so + 40 * i)
            self.secs.append((0x400000 + va, max(vs, rs), raw, rs))

    def u32(self, va):
        for base, size, raw, rs in self.secs:
            if base <= va < base + size and va - base + 4 <= rs:
                return u32(self.d, raw + va - base)
        raise KeyError(hex(va))


def module_entries(b, size):
    """Each module's entry point as a bank offset: descriptors at [+0x1c],
    [+0xa] of them, entry at +0x28 (sub_008b9bf0 relocates it with +0x2c and
    makes [+0x28, +0x2c) executable), next descriptor at
    +0x3c + 4 * ([+0x24].b + [+0x27].b)."""
    out = []
    d, n = u32(b, 0x1c), struct.unpack_from('<H', b, 0xa)[0]
    for _ in range(n):
        if d + 0x40 > size:
            break
        e = u32(b, d + 0x28)
        if 0x50 <= e < size:
            out.append(e)
        d += 0x3c + 4 * (b[d + 0x24] + b[d + 0x27])
    return out


def code_bytes(code, base, entries, size):
    """Offsets of the bytes of every instruction reachable from `entries`
    through direct jumps and calls inside the bank."""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    seen, todo, cov = set(), list(entries), set()
    while todo:
        a = todo.pop()
        if a in seen or not 0 <= a < size:
            continue
        seen.add(a)
        for i in md.disasm(code[a:size], base + a):
            o = i.address - base
            cov.update(range(o, o + i.size))
            m = i.mnemonic
            if (m == 'call' or m.startswith('j')) and i.op_str.startswith('0x'):
                t = int(i.op_str, 16) - base
                if 0 <= t < size:
                    todo.append(t)
            if m in ('ret', 'jmp', 'int3', 'hlt') or m.startswith('ret'):
                break
    return cov


def resolve(src, base, prims):
    size, doff, dlen = u32(src, 0x18), u32(src, 0x20), u32(src, 0x24)
    out = bytearray(size + dlen)
    out[:size] = src[:size]
    if doff:
        out[size:size + dlen] = src[doff:doff + dlen]
        struct.pack_into('<I', out, 0x40, base + doff)
    fl = u32(src, 0x30)
    for i in range(u32(src, fl)):
        off = u32(src, fl + 4 + 4 * i)
        idx = u32(out, off)
        struct.pack_into('<I', out, off, (prims[idx] - (base + off) - 4) & 0xffffffff)
    sl = u32(src, 0x34)
    statics = []
    for i in range(u32(src, sl)):
        off = u32(src, sl + 4 + 4 * i)
        v = (u32(out, off) + base) & 0xffffffff
        struct.pack_into('<I', out, off, v)
        statics.append((off, v))
    il = u32(src, 0x38)
    iface = [u32(src, il + 4 + 12 * i) for i in range(u32(src, il))]
    return bytes(out), statics, iface


def main():
    game = sys.argv[1] if len(sys.argv) > 1 else 'game'
    image = sys.argv[2] if len(sys.argv) > 2 else 'artifacts/nfs.exe.unpacked'
    dump = sys.argv[3] if len(sys.argv) > 3 else 'out/aems'
    img = Image(image)
    prims = [img.u32(PRIMS + 4 * i) for i in range(NPRIMS)]
    os.makedirs(dump, exist_ok=True)

    placed = collections.defaultdict(list)       # hash -> [base] from runs
    taken = []
    bt = os.path.join(dump, 'banks.txt')
    if os.path.exists(bt):
        for line in open(bt):
            p = line.split()
            if len(p) == 3:
                h, b, s = int(p[0], 16), int(p[1], 16), int(p[2], 16)
                if b not in placed[h] and not any(b < x + y and x < b + s for x, y in taken):
                    placed[h].append(b)
                    taken.append((b, s))
    # AEMS_RELAYOUT=1: place every bank afresh from AEMS_BASE, keeping as many
    # addresses per bank as the runs needed (the run dumps no longer match the
    # new addresses, so they cannot be compared).
    slots = {h: len(b) for h, b in placed.items()}
    if os.environ.get('AEMS_RELAYOUT') == '1':
        placed, taken = collections.defaultdict(list), []
    nxt = max([AEMS_BASE] + [(x + y + ALIGN - 1) & ~(ALIGN - 1) for x, y in taken])

    files = []
    for root, _, fs in os.walk(os.path.join(game, 'SOUND')):
        files += [os.path.join(root, f) for f in fs if f.lower().endswith('.abk')]
    lines, seeds, bad, checked, iface_in_code, outputs = [], set(), 0, 0, 0, []
    for path in sorted(files):
        src = open(path, 'rb').read()
        if src[:4] != b'ABKC':
            continue
        size, total = u32(src, 0x18), u32(src, 0x18) + u32(src, 0x24)
        h = bank_hash(src, size)
        bases = placed.get(h) or []
        if not bases:
            for _ in range(max(1, slots.get(h, 1))):
                if nxt + total > AEMS_END:
                    print(f'aems_catalog: no room for {path} ({total:#x} bytes)', file=sys.stderr)
                    break
                bases.append(nxt)
                nxt = (nxt + total + ALIGN - 1) & ~(ALIGN - 1)
        entries = module_entries(src, size)
        for base in bases:
            out, statics, iface = resolve(src, base, prims)
            run = os.path.join(dump, f'bank_{h:08x}_{base:08x}.bin')
            if os.path.exists(run):
                got = open(run, 'rb').read()
                checked += 1
                # Interface references are filled in at run time (heap
                # pointers): skip their words.  Anything else in the code
                # ranges must match what the game resolved.
                cov = code_bytes(out, base, entries, size)
                diff = sorted(i for i in cov if i < len(got) and got[i] != out[i])
                if diff:
                    print(f'aems_catalog: {path} at {base:#x}: {len(diff)} code bytes differ from the run dump, '
                          f'first at +{diff[0]:#x} (run {got[diff[0]:diff[0]+8].hex()} here {out[diff[0]:diff[0]+8].hex()})',
                          file=sys.stderr)
                    bad += 1
            cov = code_bytes(out, base, entries, size)
            for off in iface:
                if any(off + j in cov for j in range(4)):
                    iface_in_code += 1
                    print(f'aems_catalog: {path}: interface reference at +{off:#x} is inside an instruction', file=sys.stderr)
            outputs.append((run, out))
            lines.append(f'{h:08x} {base:08x} {total:x}')
            seeds.update((h, e) for e in entries)
    if bad or iface_in_code:
        sys.exit(f'aems_catalog: {bad} code mismatches, {iface_in_code} interface references in code; nothing written')
    for run, out in outputs:
        with open(run, 'wb') as f:
            f.write(out)
    with open(bt, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    sp = os.path.join(dump, 'seeds.txt')
    old = set()
    if os.path.exists(sp):
        for line in open(sp):
            p = line.split()
            if len(p) == 2:
                old.add((int(p[0], 16), int(p[1], 16)))
    with open(sp, 'w') as f:
        f.write(''.join(f'{h:08x} {o:x}\n' for h, o in sorted(old | seeds)))
    print(f'aems_catalog: {len(lines)} bank addresses ({len(files)} files), {len(seeds)} module entry points, '
          f'{checked} checked against run dumps, region used up to {nxt:#x}')


if __name__ == '__main__':
    main()
