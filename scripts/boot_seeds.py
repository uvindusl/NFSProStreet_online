#!/usr/bin/env python3
"""Write lifter/seeds-boot.txt, the entry points the boot lift is built from.

    python3 scripts/boot_seeds.py [pe-image] [out]

Base: every function of the full-map lift (lifter/seeds-full.txt) plus the PE
entry point.  On top of that, three things a static call graph cannot see:

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

    def rd32(va):
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
                    cont += 1
            # push ret; jmp target  (a call written as a jump: the callee's
            # ret comes back to `ret`, which the lifted caller must be able to run)
            elif img[i] == 0x68 and img[i + 5] in (0xE9, 0xEB):
                ret = struct.unpack_from('<I', img, i + 1)[0]
                if exec_addr(ret):
                    seeds.add(ret)
                    cont += 1
            i += 1

    # 2b. SecuROM computed call: push eax; mov eax,[X]; not eax; xor eax,[Y];
    #     xchg [esp],eax; add dword [esp+4],K; ret  -> continues at entry+K
    pat = bytes([0x50, 0xA1])
    for lo, hi in ((TEXT_LO, TEXT_HI), (DIA_LO, DIA_HI)):
        i = lo - base
        end = hi - base
        while True:
            i = img.find(pat, i, end)
            if i < 0:
                break
            k = None
            if (img[i + 6:i + 8] == b'\xf7\xd0' and img[i + 8:i + 10] == b'\x33\x05'
                    and img[i + 14:i + 17] == b'\x87\x04\x24'):
                if img[i + 17:i + 21] == b'\x83\x44\x24\x04' and img[i + 22] == 0xC3:
                    k = img[i + 21]
                elif img[i + 17:i + 21] == b'\x81\x44\x24\x04' and img[i + 25] == 0xC3:
                    k = struct.unpack_from('<I', img, i + 21)[0]
            if k is not None:
                r = base + i + k
                if exec_addr(r):
                    seeds.add(r)
                    cont += 1
            i += 1

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

    with open(OUT, 'w') as f:
        for a in sorted(seeds):
            f.write('%08x\n' % a)
    print('seeds-boot: %d seeds (%d .text + %d diabolus thunk targets, '
          '%d push/ret continuations, %d new data pointers: '
          '%d .rdata + %d .data + %d diabolus) -> %s'
          % (len(seeds), thunk_text, thunk_dia, cont, dataptr,
             len(rdata), len(data), len(diab), OUT))


if __name__ == '__main__':
    main()
