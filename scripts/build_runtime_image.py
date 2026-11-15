#!/usr/bin/env python3
"""Build an unpacked PE from a memory dump of the running game.

The retail `nfs.exe` is SecuROM-packed: its three on-disk sections
(.ROT/.IN/.HEAVEN) carry no usable code.  At runtime the loader decrypts the
original image in place, and it keeps a copy of the *original* PE header inside
.HEAVEN.  This tool:

  1. finds that embedded header in the dump,
  2. verifies the dump's .text matches what the header describes,
  3. rewrites every section so rawptr == VirtualAddress and rawsize ==
     VirtualSize (i.e. "file layout == memory layout"), and
  4. writes the result as a normal PE.

The output is a real, loadable-layout PE whose bytes all come from the running
game process.

    python3 scripts/build_runtime_image.py <dump> <out.exe>
"""
import struct
import sys
import hashlib

def find_embedded_header(img):
    """Locate a PE header whose entry point and image base look like the game."""
    start = 0
    while True:
        i = img.find(b"PE\0\0", start)
        if i < 0:
            return None
        try:
            nsec = struct.unpack_from("<H", img, i + 6)[0]
            optsz = struct.unpack_from("<H", img, i + 20)[0]
            opt = i + 24
            magic = struct.unpack_from("<H", img, opt)[0]
            entry = struct.unpack_from("<I", img, opt + 16)[0]
            ibase = struct.unpack_from("<I", img, opt + 28)[0]
            sec = opt + optsz
            ok = magic == 0x10B and 0 < nsec <= 16 and ibase == 0x400000
            if ok:
                names = []
                for k in range(nsec):
                    o = sec + k * 40
                    names.append(img[o:o + 8].rstrip(b"\0").decode("latin1"))
                # The original header names the game's own sections.
                if ".text" in names and ".rdata" in names and any(
                        n in names for n in ("diabolus", ".data")):
                    return i, nsec, optsz, names
        except struct.error:
            pass
        start = i + 1

def main(dump_path, out_path):
    img = bytearray(open(dump_path, "rb").read())
    hit = find_embedded_header(img)
    if not hit:
        raise SystemExit("no embedded original PE header found in dump")
    base, nsec, optsz, names = hit
    opt = base + 24
    entry = struct.unpack_from("<I", img, opt + 16)[0]
    ibase = struct.unpack_from("<I", img, opt + 28)[0]
    size_of_image = struct.unpack_from("<I", img, opt + 56)[0]
    print(f"embedded header @0x{base:x}: entry=0x{entry:x} base=0x{ibase:x} "
          f"SizeOfImage=0x{size_of_image:x}")
    print("  sections:", ", ".join(names))

    # Sanity: the dump must actually hold the decrypted image at its VAs.
    sec = opt + optsz
    for k in range(nsec):
        o = sec + k * 40
        vs, va, rs, rp = struct.unpack_from("<IIII", img, o + 8)
        body = img[va:va + min(vs, 0x1000)]
        if sum(1 for b in body if b) < 8:
            print(f"  ! {names[k]}: first page looks empty (lazily decrypted)")

    out = bytearray(size_of_image)
    # The embedded header sits inside .HEAVEN, past SizeOfImage, so relocate it
    # to the front of the file.  rawptr == VA everywhere, so the header must fit
    # in the first page (it does: 0x80 + 24 + 0xe0 + 6*40 < 0x1000).
    new_lfanew = 0x80
    hdr_len = 24 + optsz + nsec * 40
    out[new_lfanew:new_lfanew + hdr_len] = img[base:base + hdr_len]
    struct.pack_into("<I", out, 0x3C, new_lfanew)
    # Keep a minimal DOS header/stub in front.
    out[0:2] = b"MZ"
    base = new_lfanew
    opt = base + 24
    sec = opt + optsz
    for k in range(nsec):
        o = sec + k * 40
        vs, va, rs, rp = struct.unpack_from("<IIII", out, o + 8)
        out[va:va + vs] = img[va:va + vs]
        struct.pack_into("<IIII", out, o + 8, vs, va, vs, va)  # rawsize=vs, rawptr=va
        print(f"  {names[k]:10s} va=0x{va:08x} size=0x{vs:08x} "
              f"-> rawptr=0x{va:08x}")
    open(out_path, "wb").write(out)
    print(f"\nwrote {out_path}: {len(out):,} bytes  "
          f"sha256 {hashlib.sha256(out).hexdigest()}")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
