#!/usr/bin/env python3
"""extract_shaders.py - pull compiled D3D9 shader bytecode out of a game image.

ProStreet (and other EA D3D9 titles) ship *compiled* shader bytecode, not HLSL
source.  Each shader is a D3D9 token stream -- a version token, a `CTAB` constant
table comment, an optional `PRES` comment, the instruction stream, and `END`.
They live in the image's `.rsrc`, inside D3DX effect resources, stored as

    DWORD length;              // byte length of the bytecode that follows
    BYTE  bytecode[length];    // version token .. END

This scans a PE image for that pattern: a plausible length whose following DWORD
is a shader version token, and whose token stream walks to `END` exactly at
`length`.  The exact-boundary check makes false positives essentially impossible.
Blobs are written to <outdir> as <profile>_<n>.bin.

The walk mirrors the reference parser (web/runtime/web/runtime/vsemu.c): the
instruction length is bits 27-24 (dwords after the token) when present, otherwise
operands are scanned by their bit-31 parameter-token marker (shader model 1 has
no length field).  Comments use bits 30-16.  DEF/DEFI/DEFB/DCL have fixed
operand counts and are handled explicitly.

Usage:
    extract_shaders.py <pe-image> <outdir>

The extracted blobs are copyrighted game data.  Keep them OUT of the repo (write
them under /tmp, e.g. /tmp/opencode/ps-shaders).
"""
import struct, os, sys, collections

VER = {
    0xFFFE0101: "vs_1_1", 0xFFFE0200: "vs_2_0", 0xFFFE0201: "vs_2_a",
    0xFFFE0300: "vs_3_0", 0xFFFE0400: "vs_4_0",
    0xFFFF0101: "ps_1_1", 0xFFFF0102: "ps_1_2", 0xFFFF0103: "ps_1_3",
    0xFFFF0104: "ps_1_4", 0xFFFF0200: "ps_2_0", 0xFFFF0201: "ps_2_a",
    0xFFFF0300: "ps_3_0", 0xFFFF0400: "ps_4_0",
}

O_DCL, O_DEFB, O_DEFI, O_DEF = 31, 47, 48, 81


def pe_sections(d):
    e = struct.unpack_from('<I', d, 0x3c)[0]
    coff = e + 4
    nsec = struct.unpack_from('<H', d, coff + 2)[0]
    optsz = struct.unpack_from('<H', d, coff + 16)[0]
    opt = coff + 20
    out = []
    o = opt + optsz
    for i in range(nsec):
        p = o + i * 40
        name = d[p:p + 8].rstrip(b'\0').decode('latin1')
        vsz, rva, rsz, raw = struct.unpack_from('<IIII', d, p + 8)
        out.append((name, rva, vsz, raw, rsz))
    return out


def section_of(secs, foff):
    for name, rva, vsz, raw, rsz in secs:
        if raw and raw <= foff < raw + rsz:
            return name
    return "?"


def walk(d, start, limit=None, maxins=200000):
    """start = byte offset of the version token.  Returns
    (end_byte_exclusive, ninstr, has_ctab, has_pres) or None."""
    n = limit if limit is not None else len(d)
    i = start + 4
    k = 0
    ctab = pres = False
    while i + 4 <= n:
        tok = struct.unpack_from('<I', d, i)[0]
        if tok == 0x0000FFFF:                       # END
            return (i + 4, k, ctab, pres)
        op = tok & 0xFFFF
        if op == 0xFFFE:                            # comment token
            clen = (tok >> 16) & 0x7FFF
            tag = d[i + 4:i + 8]
            if tag == b'CTAB':
                ctab = True
            elif tag == b'PRES':
                pres = True
            i += 4 * (1 + clen)
        elif op == O_DCL:
            i += 4 * 3                              # token + usage + dst
        elif op == O_DEFB:
            i += 4 * 3                              # token + dst + bool
        elif op in (O_DEFI, O_DEF):
            i += 4 * 6                              # token + dst + 4 immediates
        else:
            ln = (tok >> 24) & 0xF
            if ln:
                i += 4 * (1 + ln)
            else:
                j = i + 4                           # byte offset of first operand
                while j + 4 <= n and (struct.unpack_from('<I', d, j)[0] & 0x80000000):
                    j += 4
                i = j
        k += 1
        if k > maxins:
            return None
    return None


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    path, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    d = open(path, 'rb').read()
    secs = pe_sections(d)
    print("file %s size %d" % (path, len(d)))

    found = []
    for off in range(0, len(d) - 8, 4):
        length = struct.unpack_from('<I', d, off)[0]
        if not (8 <= length <= 0x100000):
            continue
        ver = struct.unpack_from('<I', d, off + 4)[0]
        nm = VER.get(ver)
        if nm is None:
            continue
        if off + 4 + length > len(d):
            continue
        w = walk(d, off + 4, limit=off + 4 + length)
        if w is None:
            continue
        end, nins, ctab, pres = w
        if end != off + 4 + length:                 # must end exactly at length
            continue
        found.append((off + 4, nm, nins, length, ctab, pres))

    print("shaders: %d" % len(found))
    for k in sorted(collections.Counter(f[1] for f in found)):
        print("   %-8s %d" % (k, sum(1 for f in found if f[1] == k)))
    print("by section: %s" % dict(collections.Counter(section_of(secs, f[0]) for f in found)))
    print("with CTAB: %d/%d   with PRES: %d/%d" % (
        sum(f[4] for f in found), len(found), sum(f[5] for f in found), len(found)))

    idx = collections.Counter()
    for i, nm, nins, nbytes, ctab, pres in found:
        idx[nm] += 1
        with open(os.path.join(outdir, "%s_%03d.bin" % (nm, idx[nm])), 'wb') as fh:
            fh.write(d[i:i + nbytes])
    print("wrote %d blobs to %s" % (len(found), outdir))


if __name__ == "__main__":
    main()
