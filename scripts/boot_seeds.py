#!/usr/bin/env python3
"""Write lifter/seeds-boot.txt, the entry points the boot lift is built from.

    python3 scripts/boot_seeds.py [pe-image] [out]

Base: every function of the full-map lift (lifter/seeds-full.txt) plus the PE
entry point.  On top of that, four things a static call graph cannot see
(the fourth, function pointers held in code, is described at its step below):

1. SecuROM thunks.  `.text` reaches part of the image only through
   `jmp/call dword ptr [slot]` where `slot` lives in the RWX 'diabolus' section
   and the loader fills it in at run time.  The boot stopped first on 0x52ea80
   (a .text target) and then on 0x1a11e20 (the allocator's init, in diabolus);
   reading the slot out of the image gives the target to seed.

2. SecuROM's obfuscated call.  `push ret; push target; ret` is a call: the
   `ret` jumps to `target` with `ret` left as the return address, so control
   comes back to an address in the middle of the enclosing function.  The lift
   keeps the literal stack semantics, so the dispatcher needs that continuation
   as a function of its own.  1,348 of them in this image; without them the
   first obfuscated call lands on an address that is not in the function table.

3. Function pointers held in data.  A pointer in a data table is invisible to
   the call graph: nothing `call`s it, so nothing discovers it.  The CRT's
   static-constructor arrays are the first ones the boot needs
   (`_initterm_e(0x96a1f8,0x96a204)` = {0x828b43, 0, 0x828fee}); the C++
   vtables in the same section are the next.  Every 4-byte-aligned word that
   points into executable memory is seeded, in three places:

     .rdata     the image the runtime loads (the dump)
     .data      the *retail* image, because the runtime overlays .data from
                it (the dump's .data holds live heap pointers)
     diabolus   the RWX section, where SecuROM keeps the pointers it calls
                through (`mov eax,[slot]; call eax`)

   Words that are not really pointers just add an unreachable function, and
   one that lands inside an already-lifted body does not truncate it: phase 2
   lifts each entry independently from its own address.

The image must be the runtime dump (`artifacts/nfs.exe.unpacked`): the retail
exe still has the encrypted slots and points them elsewhere.
"""
import os
import re
import struct
import sys

IMAGE = sys.argv[1] if len(sys.argv) > 1 else 'artifacts/nfs.exe.unpacked'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'lifter/seeds-boot.txt'
BASE = 'lifter/seeds-full.txt'
# The runtime overlays .data from the retail image, so the function pointers
# the guest actually reads out of .data are the retail ones.
CLEAN = os.environ.get('PROSTREET_CLEAN_IMAGE', 'game/nfsps.exe')

TEXT_LO, TEXT_HI = 0x00401000, 0x00401000 + 0x56507b
RDATA_LO, RDATA_HI = 0x00967000, 0x00967000 + 0x0e276a
DATA_LO, DATA_HI = 0x00a4a000, 0x00a4a000 + 0x5c54dc
DIA_LO, DIA_HI = 0x010ea000, 0x01aa8000
ENTRY = 0x00828c25


def main():
    img = open(IMAGE, 'rb').read()
    base = 0x400000  # ImageBase of the dump; sections are at file offset == RVA

    # SecuROM mixes bytes of the PE header page into its computed calls.  The
    # dump's header was rebuilt, so read that page from the original executable
    # (the runtime maps it there too, see overlay_original_headers in boot.c).
    hdr = None
    if os.path.exists(CLEAN):
        with open(CLEAN, 'rb') as f:
            hdr = f.read(0x1000)
        if hdr[:2] != b'MZ':
            hdr = None
    if hdr is None:
        print('boot_seeds: no original executable at %s, header-keyed gadgets use the dump header' % CLEAN)

    def rd32(va):
        if hdr is not None and base <= va <= base + len(hdr) - 4:
            return struct.unpack_from('<I', hdr, va - base)[0]
        return struct.unpack_from('<I', img, va - base)[0]

    def exec_addr(a):
        return TEXT_LO <= a < TEXT_HI or DIA_LO <= a < DIA_HI

    def ptrs_in(buf, lo, hi):
        """the distinct executable addresses a data region points at"""
        out = set()
        for va in range(lo, hi - 3, 4):
            v = struct.unpack_from('<I', buf, va - base)[0]
            if exec_addr(v):
                out.add(v)
        return out

    seeds = {ENTRY}
    with open(BASE) as f:
        seeds |= {int(x, 16) for x in f if x.strip()}

    # 1. jmp/call dword ptr [imm32] where imm32 is a diabolus slot
    thunk_text = thunk_dia = 0
    i = TEXT_LO - base
    end = TEXT_HI - base
    while i < end - 6:
        if img[i] == 0xFF and img[i + 1] in (0x25, 0x15):
            slot = struct.unpack_from('<I', img, i + 2)[0]
            if DIA_LO <= slot < DIA_HI:
                target = rd32(slot)
                if TEXT_LO <= target < TEXT_HI:
                    seeds.add(target)
                    thunk_text += 1
                elif DIA_LO <= target < DIA_HI:
                    seeds.add(target)
                    thunk_dia += 1
        i += 1

    # 2. push ret; push target; ret  ->  call target, continue at ret
    cont = 0
    for lo, hi in ((TEXT_LO, TEXT_HI), (DIA_LO, DIA_HI)):
        i = lo - base
        end = hi - base
        while i < end - 11:
            if img[i] == 0x68 and img[i + 5] == 0x68 and img[i + 10] == 0xC3:
                ret = struct.unpack_from('<I', img, i + 1)[0]
                target = struct.unpack_from('<I', img, i + 6)[0]
                if exec_addr(ret) and exec_addr(target):
                    seeds.add(ret)
                    seeds.add(target)   # now dispatched, no longer inlined
                    cont += 1
            # push ret; jmp target  (a call written as a jump: the callee's
            # ret comes back to `ret`, which the lifted caller must be able to run)
            elif img[i] == 0x68 and img[i + 5] in (0xE9, 0xEB):
                ret = struct.unpack_from('<I', img, i + 1)[0]
                if img[i + 5] == 0xE9:
                    tgt = (base + i + 10 + struct.unpack_from('<i', img, i + 6)[0]) & 0xffffffff
                else:
                    tgt = (base + i + 7 + struct.unpack_from('<b', img, i + 6)[0]) & 0xffffffff
                if exec_addr(ret):
                    seeds.add(ret)
                    if exec_addr(tgt):
                        seeds.add(tgt)   # dispatched as a call, no longer inlined
                    cont += 1
            # push ret; jmp dword [slot]  (the same call through a pointer, e.g. an import)
            elif img[i] == 0x68 and img[i + 5] == 0xFF and img[i + 6] == 0x25:
                ret = struct.unpack_from('<I', img, i + 1)[0]
                if exec_addr(ret):
                    seeds.add(ret)
                    cont += 1
            i += 1

    # 2b. SecuROM computed call:  push R; <compute R from image data>;
    #     xchg [esp],R; add dword [esp+4],K; ret
    #     = call T (the computed R) returning to entry+K.  Seed both.  Without
    #     the return seed the dispatcher skips the rest of the caller (it treats
    #     an unlifted diabolus address as junk), which leaks the caller's frame.
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    X = capstone.x86_const
    comp = 0
    FULL = {'al': 'eax', 'cl': 'ecx', 'dl': 'edx', 'bl': 'ebx'}
    REGNAMES = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']

    def gadget_value(pos, end, reg):
        """Evaluate the instructions between `push R` and `xchg [esp],R` and
        return the value R ends up with, or None if they are not a gadget.
        The image uses these shapes (117 gadgets in all):
            mov R,[X]; (not R); xor|add|sub R,[Y]
            clc|stc; mov R,[X]; adc|sbb R,[Y]
            mov R,[X]; push r2; mov r2,[Y]; ror|rol R,cl; pop r2
        each optionally preceded by a use counter, `dec dword [counter]; je trap`
        or `lea R,[a]; sub R,b; dec dword [R]; je trap`, whose trap branch is
        not followed here."""
        regs, saved, cf = {}, [], 0

        def name(r):
            n = md.reg_name(r)
            return FULL.get(n, n)

        def src(o):
            if o.type == X.X86_OP_IMM:
                v = o.imm & 0xffffffff
            elif o.type == X.X86_OP_MEM and o.mem.base == 0 and o.mem.index == 0:
                a = o.mem.disp & 0xffffffff
                if not base <= a <= base + len(img) - 4:
                    return None
                v = rd32(a)
            elif o.type == X.X86_OP_REG:
                v = regs.get(name(o.reg))
            else:
                return None
            if v is not None and o.size == 1:
                v &= 0xff
            return v

        while pos < end:
            ins = next(md.disasm(img[pos:pos + 15], base + pos), None)
            if ins is None:
                return None
            ops, mn = ins.operands, ins.mnemonic
            dst = name(ops[0].reg) if ops and ops[0].type == X.X86_OP_REG else None
            if mn in ('clc', 'stc'):
                cf = int(mn == 'stc')
            elif mn in ('pushfd', 'popfd', 'nop'):
                pass
            elif len(ops) >= 2 and dst and dst != REGNAMES[reg] and dst not in regs \
                    and mn in ('and', 'or', 'shrd', 'shld', 'shl', 'shr', 'sar', 'test', 'cmp'):
                pass                    # filler on a register the gadget does not use
            elif mn == 'dec' and ops[0].type == X.X86_OP_MEM:
                pass
            elif mn == 'lea' and dst and ops[1].mem.base == 0 and ops[1].mem.index == 0:
                # the use counter addressed through the gadget's own register:
                # lea R,[imm]; sub R,imm; dec dword [R]; je trap   (0x19fdfc8)
                regs[dst] = ops[1].mem.disp & 0xffffffff
            elif mn == 'je':
                pass
            elif mn == 'push' and dst:
                saved.append(regs.get(dst))
            elif mn == 'pop' and dst and saved:
                regs[dst] = saved.pop()
            elif mn == 'mov' and dst:
                regs[dst] = src(ops[1])
                if regs[dst] is None:
                    return None
            elif mn in ('not', 'neg') and dst and regs.get(dst) is not None:
                regs[dst] = (~regs[dst] if mn == 'not' else -regs[dst]) & 0xffffffff
            elif mn in ('xor', 'add', 'sub', 'adc', 'sbb', 'ror', 'rol') and dst and regs.get(dst) is not None:
                y, v = src(ops[1]), regs[dst]
                if y is None:
                    return None
                if mn in ('ror', 'rol'):
                    n = y & 31
                    if mn == 'rol':
                        n = (32 - n) & 31
                    v = (v >> n | v << (32 - n)) & 0xffffffff
                else:
                    v = {'xor': v ^ y, 'add': v + y, 'sub': v - y,
                         'adc': v + y + cf, 'sbb': v - y - cf}[mn] & 0xffffffff
                regs[dst] = v
            else:
                return None
            pos += ins.size
        return regs.get(REGNAMES[reg]) if pos == end and not saved else None
    # the swap is `xchg [esp],R`, or the same thing as three xors
    swaps = (re.compile(rb'\x87([\x04\x0c\x14\x1c\x2c\x34\x3c])\x24'),
             re.compile(rb'\x33([\x04\x0c\x14\x1c\x2c\x34\x3c])\x24\x31\1\x24\x33\1\x24'))
    for lo, hi in ((TEXT_LO, TEXT_HI), (DIA_LO, DIA_HI)):
        for pat in swaps:
            for m in pat.finditer(img, lo - base, hi - base):
                p0, p1 = m.start(), m.end()
                reg = (m.group(1)[0] >> 3) & 7
                if img[p1:p1 + 4] == b'\x83\x44\x24\x04' and img[p1 + 5] == 0xC3:
                    k = img[p1 + 4]
                elif img[p1:p1 + 4] == b'\x81\x44\x24\x04' and img[p1 + 8] == 0xC3:
                    k = struct.unpack_from('<I', img, p1 + 4)[0]
                else:
                    continue
                for back in range(2, 48):
                    st = p0 - back
                    if img[st] != 0x50 + reg:
                        continue
                    val = gadget_value(st + 1, p0, reg)
                    if val is not None and exec_addr(val):
                        seeds.add(val)
                        r = base + st + k
                        if exec_addr(r):
                            seeds.add(r)
                        comp += 1
                        break
    cont += comp

    # 2c. SecuROM computed jump:  push R; <compute R>; xchg [esp],R; ret
    #     (also `xchg [esp+4],R; popfd; ret`, and the swap as three xors).
    #     = jmp T.  The lifter runs T as a tail call, so T must be a function.
    cjmp = 0
    ends = (re.compile(rb'\x87([\x04\x0c\x14\x1c\x2c\x34\x3c])\x24\xc3'),
            re.compile(rb'\x87([\x44\x4c\x54\x5c\x6c\x74\x7c])\x24\x04\x9d\xc3'),
            re.compile(rb'\x33([\x04\x0c\x14\x1c\x2c\x34\x3c])\x24\x31\1\x24\x33\1\x24\xc3'))
    for lo, hi in ((TEXT_LO, TEXT_HI), (DIA_LO, DIA_HI)):
        for pat in ends:
            for m in pat.finditer(img, lo - base, hi - base):
                p0 = m.start()
                reg = (m.group(1)[0] >> 3) & 7
                for back in range(2, 48):
                    st = p0 - back
                    if img[st] != 0x50 + reg:
                        continue
                    val = gadget_value(st + 1, p0, reg)
                    if val is not None and exec_addr(val):
                        seeds.add(val)
                        cjmp += 1
                        break
    cont += cjmp

    # 3. function pointers held in data tables
    rdata = ptrs_in(img, RDATA_LO, RDATA_HI)
    diab = ptrs_in(img, DIA_LO, DIA_HI)
    data = set()
    if os.path.exists(CLEAN):
        clean = open(CLEAN, 'rb').read()
        data = ptrs_in(clean, DATA_LO, DATA_HI)
    else:
        print('boot_seeds: no clean image at %s, .data not scanned' % CLEAN)
    before = len(seeds)
    seeds |= rdata | data | diab
    dataptr = len(seeds) - before

    # 4. function pointers held in code: `push offset fn`, `mov reg, offset fn`,
    #    `mov [mem], offset fn` (callbacks, thread entries, sort comparators).
    #    Nothing calls them directly, so the call graph misses them; the first
    #    one the boot needed was 0x8b4e80 (a sound-system callback).  Any 4 bytes
    #    of code that hold the address of a 16-byte-aligned .text location
    #    following padding or a return are taken as one.  A false hit only adds
    #    an unreachable function.
    before = len(seeds)
    for lo, hi in ((TEXT_LO, TEXT_HI), (DIA_LO, DIA_HI)):
        for off in range(lo - base, hi - base - 3):
            v = struct.unpack_from('<I', img, off)[0]
            if v & 0xF or not (TEXT_LO < v < TEXT_HI):
                continue
            o = v - base
            if img[o - 1] in (0xCC, 0xC3, 0x90) or img[o - 3] == 0xC2:
                seeds.add(v)
    codeptr = len(seeds) - before

    with open(OUT, 'w') as f:
        for a in sorted(seeds):
            f.write('%08x\n' % a)
    print('seeds-boot: %d seeds (%d .text + %d diabolus thunk targets, '
          '%d push/ret continuations, %d new data pointers: '
          '%d .rdata + %d .data + %d diabolus, %d new code immediates) -> %s'
          % (len(seeds), thunk_text, thunk_dia, cont, dataptr,
             len(rdata), len(data), len(diab), codeptr, OUT))


if __name__ == '__main__':
    main()
