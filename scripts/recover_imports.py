#!/usr/bin/env python3
"""Recover the import table of the running game from its live process.

SecuROM zeroes the IMAGE_IMPORT_DESCRIPTOR array in memory, so a memory dump
cannot simply be re-imported.  The IAT, however, is intact: every slot holds the
address of the function it was bound to.  We resolve those addresses against the
modules actually loaded in the process.

    python3 scripts/recover_imports.py <pid> <unpacked.exe> <out.json>
"""
import json
import struct
import sys


def read_mem(pid, addr, size):
    with open(f"/proc/{pid}/mem", "rb", 0) as f:
        f.seek(addr)
        return f.read(size)


def modules(pid):
    """Base address + image size of every mapped PE, from /proc/pid/maps."""
    seen = {}
    for line in open(f"/proc/{pid}/maps"):
        parts = line.split()
        if len(parts) < 6:
            continue
        lo, hi = (int(x, 16) for x in parts[0].split("-"))
        off = int(parts[2], 16)
        path = parts[5]
        if not path.lower().endswith((".dll", ".exe", ".drv")):
            continue
        if off != 0:
            continue
        seen.setdefault(path, lo)
    return seen


def parse_pe(data, base):
    if data[:2] != b"MZ":
        return None
    e = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e:e + 4] != b"PE\0\0":
        return None
    coff = e + 4
    nsec = struct.unpack_from("<H", data, coff + 2)[0]
    optsz = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    ibase = struct.unpack_from("<I", data, opt + 28)[0]
    if magic == 0x10B:
        dirs_off, nrva = opt + 92, struct.unpack_from("<I", data, opt + 92)[0]
    else:
        dirs_off, nrva = opt + 108, struct.unpack_from("<I", data, opt + 108)[0]
    dirs = [struct.unpack_from("<II", data, dirs_off + 4 + i * 8) for i in range(nrva)]
    sec = opt + optsz
    sections = []
    for i in range(nsec):
        o = sec + i * 40
        vs, va, rs, rp = struct.unpack_from("<IIII", data, o + 8)
        sections.append((va, max(vs, rs), rp, rs))

    def rva2off(rva):
        for va, size, rp, rs in sections:
            if va <= rva < va + size:
                if rva - va >= rs:
                    return None
                return rp + (rva - va)
        return None

    return {"base": base, "imagebase": ibase, "dirs": dirs, "rva2off": rva2off,
            "size": struct.unpack_from("<I", data, opt + 56)[0]}


def exports(pid, path, base):
    try:
        hdr = read_mem(pid, base, 0x1000)
    except OSError:
        return {}
    pe = parse_pe(hdr, base)
    if not pe or len(pe["dirs"]) < 1:
        return {}
    rva, size = pe["dirs"][0]
    if not rva:
        return {}
    off = pe["rva2off"](rva)
    if off is None:
        return {}
    try:
        d = read_mem(pid, base + off, min(size, 0x10000))
    except OSError:
        return {}
    nfun, nname, afun, aname, aord = struct.unpack_from("<IIIII", d, 20)
    out = {}
    for i in range(min(nfun, 20000)):
        try:
            frva = struct.unpack_from("<I", d, afun - rva + i * 4)[0]
            nrva = struct.unpack_from("<I", d, aname - rva + i * 4)[0]
        except struct.error:
            break
        if not frva or not nrva:
            continue
        noff = pe["rva2off"](nrva)
        if noff is None:
            continue
        try:
            nm = read_mem(pid, base + noff, 256).split(b"\0")[0].decode("latin1")
        except OSError:
            continue
        if frva < pe["size"]:
            out[base + frva] = nm
    return out


def main(pid, exe, out_json):
    data = open(exe, "rb").read()
    pe = parse_pe(data, 0)
    base = struct.unpack_from("<I", data, struct.unpack_from("<I", data, 0x3C)[0] + 24 + 28)[0]
    iat_rva, iat_size = pe["dirs"][12]
    iat_off = pe["rva2off"](iat_rva)
    print(f"IAT rva=0x{iat_rva:x} size=0x{iat_size:x} ({iat_size // 4} slots)")

    mods = modules(pid)
    print(f"{len(mods)} modules mapped in pid {pid}")
    exp = {}
    for path, mbase in sorted(mods.items(), key=lambda kv: kv[1]):
        exp[path] = exports(pid, path, mbase)

    def owner(addr):
        for path, mbase in mods.items():
            if mbase <= addr < mbase + 0x10000000:
                # narrow: check the module's real size when known
                return path
        return None

    # Build an address->(dll, name) index; addresses are unique per module.
    index = {}
    for path, table in exp.items():
        for addr, nm in table.items():
            index.setdefault(addr, (path, nm))

    result = []
    unresolved = []
    for i in range(iat_size // 4):
        slot = iat_rva + i * 4
        off = pe["rva2off"](slot)
        if off is None:
            break
        addr = struct.unpack_from("<I", data, off)[0]
        if not addr:
            continue
        if addr in index:
            path, nm = index[addr]
            result.append({"slot": slot, "addr": addr, "dll": path.rsplit("/", 1)[-1], "name": nm})
        else:
            unresolved.append((slot, addr))
    print(f"resolved {len(result)} / {len(result) + len(unresolved)} non-null slots")
    for slot, addr in unresolved[:15]:
        print(f"  unresolved slot 0x{slot:x} -> 0x{addr:x}  owner={owner(addr)}")
    json.dump(result, open(out_json, "w"), indent=1)
    print("wrote", out_json)


if __name__ == "__main__":
    main(int(sys.argv[1]), sys.argv[2], sys.argv[3])
