#!/usr/bin/env python3
"""Dump the decrypted image of a running Need for Speed ProStreet process.

The retail `nfs.exe` is SecuROM-packed: on disk its three sections carry no
game code.  At runtime the loader decrypts the original image in place at
0x400000, so reading the process memory is the legitimate way to obtain it.

    python3 scripts/dump_running.py <pid> [out.bin]

Requires only permission to read /proc/<pid>/mem (same-user process).  No
ptrace, no debugger, no elevation.
"""
import struct
import sys

BASE = 0x400000
# Header page + the 20.9 MB .ROT region the loader decrypts into.
DEFAULT_SIZE = 0x1784000


def dump(pid, size=DEFAULT_SIZE):
    buf = bytearray(size)
    with open(f"/proc/{pid}/mem", "rb", 0) as f:
        for off in range(0, size, 0x10000):
            n = min(0x10000, size - off)
            f.seek(BASE + off)
            try:
                chunk = f.read(n)
            except OSError:
                chunk = b""
            buf[off:off + len(chunk)] = chunk
    return buf


def describe(buf):
    if buf[:2] != b"MZ":
        raise SystemExit("dump does not start with MZ — wrong pid or wrong base")
    e = struct.unpack_from("<I", buf, 0x3C)[0]
    if buf[e:e + 4] != b"PE\0\0":
        raise SystemExit("no PE header at e_lfanew — wrong pid?")
    nsec = struct.unpack_from("<H", buf, e + 6)[0]
    optsz = struct.unpack_from("<H", buf, e + 20)[0]
    opt = e + 24
    print(f"packed header: entry {struct.unpack_from('<I', buf, opt + 16)[0]:#x} "
          f"SizeOfImage {struct.unpack_from('<I', buf, opt + 56)[0]:#x} "
          f"sections {nsec}")
    for i in range(nsec):
        o = opt + optsz + i * 40
        name = buf[o:o + 8].rstrip(b"\0").decode("latin1")
        vs, va, rs, rp = struct.unpack_from("<IIII", buf, o + 8)
        nz = sum(1 for b in buf[va:va + vs] if b)
        print(f"  {name:10s} va={va:#09x} vs={vs:#09x} nonzero={nz:,}")
    # The decrypted code is the first thing in .ROT.
    rot = buf[0x1000:0x1000 + 0x1000]
    print(f"  .ROT head: {rot[:16].hex()}  ({'decrypted' if any(rot) else 'still empty'})")


def main():
    if len(sys.argv) < 2:
        raise SystemExit(f"usage: {sys.argv[0]} <pid> [out.bin]")
    pid = int(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/opencode/ps-image.bin"
    buf = dump(pid)
    open(out, "wb").write(buf)
    print(f"wrote {out}: {len(buf):,} bytes")
    describe(buf)


if __name__ == "__main__":
    main()
