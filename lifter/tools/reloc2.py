#!/usr/bin/env python3
"""Parse the relocation table whose directory entry was zeroed, and write it.

Two input shapes:

  * a PE whose reloc directory entry was zeroed: the fixups live in a section
    named '<none>' (SecuROM's way of hiding them), parsed as the standard
    IMAGE_BASE_RELOC blocks.  This is what SPEED2.EXE gives.

  * a live memory dump (artifacts/nfs.exe.unpacked) whose relocations are
    already applied: there is no reloc table at all, and no '<none>' section.
    Every absolute code pointer in the image IS a fixup slot, so the slots are
    recovered by scanning .text/.rdata/.data for 4-byte values that point back
    into the image -- the same CHECK the block parser runs, applied to every
    aligned word.  The consumers (genall.py, argcount.py) only ever use the
    list as (a) extra function-entry seeds and (b) a conservative mask for
    jump-table target dropping, so a superset of the real slots is safe.

out/relocs.txt: one VA per line, the fixup slots.
"""
import sys, os, struct, collections
import pefile
pe = pefile.PE(sys.argv[1]); IB = pe.OPTIONAL_HEADER.ImageBase
secs=[]
for s in pe.sections:
    nm=s.Name.rstrip(b'\x00').decode('latin1').strip() or '<none>'
    secs.append((nm, IB+s.VirtualAddress, s.Misc_VirtualSize, s.get_data()))
IMG_LO, IMG_HI = IB, IB+pe.OPTIONAL_HEADER.SizeOfImage

def rd32(a):
    for nm,va,vs,d in secs:
        if va<=a<va+len(d)-3: return struct.unpack_from('<I',d,a-va)[0]
    return None
def sec_of(a):
    for nm,va,vs,d in secs:
        if va<=a<va+vs: return nm
    return '<outside>'

# ---- shape 1: a hidden '<none>' reloc table ----
try:
    blob = next(d for nm,va,vs,d in secs if nm=='<none>')
except StopIteration:
    blob = None

addrs=[]; blocks=0; bad=0; types=collections.Counter(); tot=0
if blob:
    off=0
    while off+8 <= len(blob):
        page, size = struct.unpack_from('<II', blob, off)
        if size==0: break
        if size<8 or off+size > len(blob)+8:
            bad+=1; break
        n=(min(size,len(blob)-off)-8)//2
        for i in range(n):
            (w,)=struct.unpack_from('<H', blob, off+8+i*2)
            t=w>>12; o=w&0xfff
            types[t]+=1; tot+=1
            if t==3: addrs.append(IB+page+o)
        blocks+=1; off+=size
    print(f"blocks parsed: {blocks}   entries in total: {tot}   bad: {bad}")
    for t,c in sorted(types.items()): print(f"  type {t}: {c}")
    print("\nsections holding the absolute-address fixups:")
    for k,v in collections.Counter(map(sec_of,addrs)).most_common(): print(f"  {k:10s} {v}")

# ---- shape 2: no reloc table -> scan for the already-applied pointers ----
if not addrs:
    SCAN = [nm for nm,va,vs,d in secs if nm in ('.text','.rdata','.data')]
    print(f"no reloc table (no '<none>' section); scanning {SCAN} for applied fixups")
    seen=set()
    for nm in SCAN:
        _,va,vs,d = next(x for x in secs if x[0]==nm)
        for i in range(0, len(d)-3, 4):
            a = va+i
            if a in seen: continue
            v = rd32(a)
            if v is not None and IMG_LO<=v<IMG_HI:
                seen.add(a); addrs.append(a)
    print(f"scanned {SCAN}: {len(addrs)} slots whose value points into the image")

# Check: do the 4-byte values at these locations really point into the image?
ok=sum(1 for a in addrs if (v:=rd32(a)) is not None and IMG_LO<=v<IMG_HI)
print(f"\nCHECK: the values at {ok}/{len(addrs)} locations point into the image ({IMG_LO:#x}-{IMG_HI:#x}) = {100.0*ok/max(len(addrs),1):.2f}%")
out='out/relocs.txt'; os.makedirs('out', exist_ok=True)
open(out,'w').write('\n'.join(f'{a:08x}' for a in sorted(addrs)))
print(f"-> {out} ({len(addrs)} addresses)")