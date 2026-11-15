#!/usr/bin/env python3
"""Phase 2: parse the target image and recover its function map.

    IMPORTS_JSON=out/imports-runtime.json \
        python3 scripts/analyze_binary.py game/nfs.exe.unpacked

Writes:
    out/analysis.json   machine-readable result (imports, sections, functions)

Only the standard library plus Capstone is required.
"""
import collections
import json
import os
import struct
import sys
import time

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_ESP

IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_MEM_EXECUTE = 0x20000000

# SecuROM 7 artefacts. Never scanned, never mapped, never lifted: they are not
# game code, and the pointer-looking dwords inside them are DRM's own.
DRM_SECTIONS = {"ars", "est", "artem", "celare", ".securom"}

TERMINATORS = {"ret", "retf", "iret", "iretd", "int3", "hlt", "ud2"}


class PE:
    def __init__(self, data):
        self.data = data
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
            raise SystemExit("not a PE file")
        coff = e_lfanew + 4
        (self.machine, self.nsec, self.timestamp) = struct.unpack_from("<HHI", data, coff)
        self.opt_size = struct.unpack_from("<H", data, coff + 16)[0]
        self.characteristics = struct.unpack_from("<H", data, coff + 18)[0]
        opt = coff + 20
        self.magic = struct.unpack_from("<H", data, opt)[0]
        self.entry_rva = struct.unpack_from("<I", data, opt + 16)[0]
        self.image_base = struct.unpack_from("<I", data, opt + 28)[0]
        self.size_of_image = struct.unpack_from("<I", data, opt + 56)[0]
        self.nrva = struct.unpack_from("<I", data, opt + 92)[0]
        self.dirs = []
        for i in range(self.nrva):
            self.dirs.append(struct.unpack_from("<II", data, opt + 96 + i * 8))

        self.sections = []
        sec = opt + self.opt_size
        for i in range(self.nsec):
            o = sec + i * 40
            name = data[o:o + 8].rstrip(b"\0").decode("latin1")
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
            chars = struct.unpack_from("<I", data, o + 36)[0]
            self.sections.append({
                "name": name, "va": va, "vsize": vsize,
                "rawptr": rawptr, "rawsize": rawsize, "characteristics": chars,
            })

    def rva_to_off(self, rva):
        for s in self.sections:
            if s["va"] <= rva < s["va"] + max(s["vsize"], s["rawsize"]):
                return s["rawptr"] + (rva - s["va"])
        return None

    def section_of(self, va):
        rva = va - self.image_base
        for s in self.sections:
            if s["va"] <= rva < s["va"] + max(s["vsize"], s["rawsize"]):
                return s
        return None

    def read(self, va, size):
        off = self.rva_to_off(va - self.image_base)
        if off is None:
            return None
        return self.data[off:off + size]

    def cstring(self, rva):
        off = self.rva_to_off(rva)
        end = self.data.index(b"\0", off)
        return self.data[off:end].decode("latin1")

    def imports(self):
        rva, _size = self.dirs[1]
        out = []
        if not rva:
            return out
        o = self.rva_to_off(rva)
        while True:
            ilt, _ts, _fc, name_rva, iat = struct.unpack_from("<IIIII", self.data, o)
            if not name_rva:
                break
            dll = self.cstring(name_rva)
            thunk = self.rva_to_off(ilt or iat)
            names = []
            while True:
                v = struct.unpack_from("<I", self.data, thunk)[0]
                if not v:
                    break
                if v & 0x80000000:
                    names.append(f"ord#{v & 0xffff}")
                else:
                    names.append(self.cstring((v & 0x7fffffff) + 2))
                thunk += 4
            out.append({"dll": dll, "functions": names, "iat_rva": iat})
            o += 20
        return out


def code_section(pe):
    for s in pe.sections:
        if s["characteristics"] & IMAGE_SCN_MEM_EXECUTE and s["name"] == ".text":
            return s
    raise SystemExit("no .text section")


def reloc_seeds(pe, path, text_lo, text_hi):
    """Function pointers held in relocation fixup slots.

    A stripped image has no relocation *directory*, but the table itself may
    survive (NFSU2 keeps it in its unnamed section; see nfsu2-web's
    tools/reloc2.py). Each slot is a dword that the loader would have fixed up,
    so its value is an absolute address: if it lands in .text, it is a function
    pointer, and the alignment filter used for the plain dword scan must not be
    applied to it.
    """
    seeds = {}
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        slot = int(line, 16)
        v = pe.read(slot, 4)
        if v is None:
            continue
        v = struct.unpack("<I", v)[0]
        if text_lo <= v < text_hi:
            seeds.setdefault(v, []).append(slot)
    return seeds


def seed_scan(pe, text_lo, text_hi, align=16):
    """Dwords in non-executable sections that point into .text.

    The image has no base relocations, so these are the only static evidence of
    function addresses outside the direct-call graph: vtables, callback tables,
    thread entry points, jump tables.
    """
    hits = collections.Counter()
    seeds = {}
    for s in pe.sections:
        if s["characteristics"] & IMAGE_SCN_MEM_EXECUTE:
            continue
        if s["name"] in DRM_SECTIONS:
            continue
        off, n = s["rawptr"], s["rawsize"]
        blob = pe.data[off:off + n]
        for i in range(0, len(blob) - 3, 4):
            v = struct.unpack_from("<I", blob, i)[0]
            if text_lo <= v < text_hi and (align == 1 or v % align == 0):
                seeds.setdefault(v, []).append(pe.image_base + s["va"] + i)
                hits[s["name"]] += 1
    return seeds, hits


def jump_table_targets(md, pe, insn, text_lo, text_hi, limit=4096):
    """Targets of `jmp dword ptr [reg*4 + table]` (MSVC switch), or [].

    The scale-4 memory operand with an absolute displacement and no base is the
    shape MSVC emits. Entries are read until one falls outside .text.
    """
    if insn.mnemonic != "jmp" or not insn.operands:
        return None
    op = insn.operands[0]
    if op.type != X86_OP_MEM:
        return None
    mem = op.mem
    if mem.base != 0 or mem.index == 0 or mem.scale != 4:
        return None
    table = mem.disp & 0xFFFFFFFF
    if not (text_lo <= table < text_hi):
        return None
    targets = []
    off = pe.rva_to_off(table - pe.image_base)
    if off is None:
        return None
    for i in range(limit):
        if off + i * 4 + 4 > len(pe.data):
            break
        v = struct.unpack_from("<I", pe.data, off + i * 4)[0]
        if not (text_lo <= v < text_hi):
            break
        targets.append(v)
    return (table, targets)


PROLOGUES = (
    b"\x55\x8b\xec",              # push ebp; mov ebp, esp
    b"\x6a\xff\x68",              # SEH: push -1; push handler
    b"\x8b\xff\x55\x8b\xec",      # hot-patch: mov edi, edi; push ebp; mov ebp, esp
    b"\x53\x8b\xdc",              # push ebx; mov ebx, esp
)


class Discovery:
    """Static function discovery over one code section."""

    def __init__(self, pe):
        self.pe = pe
        self.md = Cs(CS_ARCH_X86, CS_MODE_32)
        self.md.detail = True
        text = code_section(pe)
        self.text_lo = pe.image_base + text["va"]
        self.text_hi = self.text_lo + text["rawsize"]
        self.base_off = text["rawptr"]

        self.covered = bytearray(self.text_hi - self.text_lo)
        self.functions = {}
        self.origin = {}
        # Entries are real function starts; branch targets are decoded but
        # belong to the function that branches to them.
        self.queued = set()
        self.jump_tables = {}
        self.traps = collections.Counter()
        self.trap_sites = collections.defaultdict(list)
        self.refs = set()
        # Immediate operands that point into .text: MSVC stores function
        # addresses this way (`push offset f`, `mov [vtbl], offset f`). With
        # the relocation table gone (SecuROM zeroed the directory) these are
        # the only remaining record of pointers that never reach a data
        # section. They are candidates, not entries: .text also holds data, so
        # a target must decode function-shaped before it is accepted.
        self.imm_targets = set()
        self.totals = {"insns": 0, "bytes": 0}

    def run(self, entries, reason="entry"):
        md, pe = self.md, self.pe
        text_lo, text_hi, base_off = self.text_lo, self.text_hi, self.base_off
        covered, functions, queued = self.covered, self.functions, self.queued
        worklist = collections.deque()
        for a in sorted(entries):
            if a not in queued:
                queued.add(a)
                worklist.append((a, True, reason))

        while worklist:
            start, is_entry, reason = worklist.popleft()
            if is_entry and start not in functions:
                self.origin[start] = reason
                # A start inside already-decoded bytes is still a real entry:
                # record it but do not re-decode. MSVC shares epilogues and
                # calls into the middle of a function.
                functions[start] = {"insns": 0, "bytes": 0}
            addr = start
            while text_lo <= addr < text_hi:
                if covered[addr - text_lo]:
                    break
                o = base_off + (addr - text_lo)
                insn = next(md.disasm(pe.data[o:o + 16], addr), None)
                if insn is None:
                    self.traps["undecodable"] += 1
                    self.trap_sites["undecodable"].append(addr)
                    break
                s = addr - text_lo
                for i in range(s, min(s + insn.size, len(covered))):
                    covered[i] = 1
                self.totals["insns"] += 1
                self.totals["bytes"] += insn.size
                if is_entry and start in functions:
                    functions[start]["insns"] += 1
                    functions[start]["bytes"] += insn.size
                for op in insn.operands:
                    if op.type == X86_OP_IMM:
                        v = op.imm & 0xFFFFFFFF
                        if text_lo <= v < text_hi:
                            self.imm_targets.add(v)
                m = insn.mnemonic
                o0 = insn.operands[0] if insn.operands else None
                if m == "call":
                    if o0 is not None and o0.type == X86_OP_IMM:
                        t = o0.imm & 0xFFFFFFFF
                        self.refs.add(t)
                        if text_lo <= t < text_hi and t not in queued:
                            queued.add(t)
                            worklist.append((t, True, "call"))
                    addr = insn.address + insn.size
                    continue
                if m == "jmp":
                    if o0 is not None and o0.type == X86_OP_IMM:
                        # Tail call: the reference lifter follows it in place,
                        # so the target stays part of this function's body.
                        addr = o0.imm & 0xFFFFFFFF
                        continue
                    jt = jump_table_targets(md, pe, insn, text_lo, text_hi)
                    if jt:
                        table, targets = jt
                        self.jump_tables[insn.address] = {"table": table,
                                                          "targets": targets}
                        for t in targets:
                            self.refs.add(t)
                            if t not in queued:
                                queued.add(t)
                                worklist.append((t, False, "block"))
                    else:
                        self.traps["indirect_jmp"] += 1
                        self.trap_sites["indirect_jmp"].append(addr)
                    break
                if m.startswith("j"):
                    if o0 is not None and o0.type == X86_OP_IMM:
                        t = o0.imm & 0xFFFFFFFF
                        self.refs.add(t)
                        if text_lo <= t < text_hi and t not in queued:
                            queued.add(t)
                            worklist.append((t, False, "block"))
                    addr = insn.address + insn.size
                    continue
                if m in TERMINATORS:
                    break
                addr = insn.address + insn.size

    def imm_entries(self):
        """Immediate .text pointers not reached by anything else, shape-checked."""
        md, base, data = self.md, self.pe.image_base, self.pe.data
        out = set()
        for a in self.imm_targets:
            if a in self.functions or self.covered[a - self.text_lo]:
                continue
            addr, n = a, 0
            while n < 64:
                if not (self.text_lo <= addr < self.text_hi):
                    break
                insn = next(md.disasm(data[addr - base:addr - base + 16], addr), None)
                if insn is None:
                    break
                n += 1
                if insn.mnemonic in ("ret", "retf", "iret", "iretd", "jmp", "int3", "hlt", "ud2"):
                    if n >= 2:
                        out.add(a)
                    break
                addr = insn.address + insn.size
        return out

    def gaps(self, min_size=1):
        out = []
        i, n = 0, len(self.covered)
        while i < n:
            if not self.covered[i]:
                j = i
                while j < n and not self.covered[j]:
                    j += 1
                if j - i >= min_size:
                    out.append((self.text_lo + i, j - i))
                i = j
            else:
                i += 1
        out.sort(key=lambda g: -g[1])
        return out

    def recover_entries(self, min_gap=16):
        """Function starts in uncovered runs.

        Two signals, both cheap and both used only where nothing else reached:

        * an MSVC prologue (`push ebp; mov ebp, esp`, an SEH frame, ...), and
        * any byte that follows a run of `int3` padding, which is how MSVC
          separates functions, provided it decodes into a function-shaped body.

        These are the functions reached only through data we cannot read as a
        pointer, so the seed scan missed them.
        """
        data = self.pe.data
        base = self.pe.image_base
        md = self.md
        found = set()

        def function_shaped(a, limit=64):
            addr = a
            n = 0
            while n < limit:
                o = addr - base
                if not (self.text_lo <= addr < self.text_hi):
                    return False
                insn = next(md.disasm(data[o:o + 16], addr), None)
                if insn is None:
                    return False
                n += 1
                if insn.mnemonic in ("ret", "retf", "iret", "iretd", "jmp", "int3", "hlt", "ud2"):
                    return n >= 2
                addr = insn.address + insn.size
            return False

        for va, size in self.gaps(min_gap):
            off = 0
            while off < size:
                a = va + off
                o = a - base
                if data[o] != 0xCC:
                    off += 1
                    continue
                # Skip the whole padding run; the byte after it is a candidate.
                while off < size and data[va + off - base] == 0xCC:
                    off += 1
                if off >= size:
                    break
                cand = va + off
                co = cand - base
                if any(data[co:co + len(p)] == p for p in PROLOGUES) or function_shaped(cand):
                    found.add(cand)
        return found



def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} <image>")
    path = sys.argv[1]
    t0 = time.time()
    data = open(path, "rb").read()
    pe = PE(data)
    text = code_section(pe)
    text_lo = pe.image_base + text["va"]
    text_hi = text_lo + text["rawsize"]

    print(f"image      {len(data):,} bytes  base {pe.image_base:#010x}  "
          f"entry {pe.image_base + pe.entry_rva:#010x}")
    imports = pe.imports()
    imports_json = os.environ.get("IMPORTS_JSON", "")
    if imports_json:
        # SecuROM zeroes the on-disk import descriptors in memory, so a dump has
        # no readable import directory.  scripts/recover_imports.py rebuilds the
        # list from the live IAT instead.
        live = json.load(open(imports_json))
        grouped = collections.OrderedDict()
        for rec in sorted(live, key=lambda r: r["slot"]):
            grouped.setdefault(rec["dll"], []).append(rec["name"])
        imports = [{"dll": dll, "functions": fns, "iat_rva": 0}
                   for dll, fns in grouped.items()]
        print(f"imports    {sum(len(i['functions']) for i in imports)} names "
              f"from {len(imports)} DLLs (recovered from live IAT: {imports_json})")
    else:
        print(f"imports    {sum(len(i['functions']) for i in imports)} names "
              f"from {len(imports)} DLLs")

    align = int(os.environ.get("SEED_ALIGN", "16"))
    aligned, hits = seed_scan(pe, text_lo, text_hi, align=align)
    seed_slots = dict(aligned)          # entry -> the slots holding the pointer
    print(f"seeds      {len(aligned):,} unique .text pointers ({align}-byte aligned) "
          f"from {sum(hits.values()):,} slots")
    relocs_path = os.environ.get("RELOCS", "")
    reloc_slots = {}
    if relocs_path:
        reloc_slots = reloc_seeds(pe, relocs_path, text_lo, text_hi)
        extra = set(reloc_slots) - set(seed_slots)
        print(f"relocs     {len(reloc_slots):,} .text pointers from {relocs_path} "
              f"({len(extra):,} not already seeds)")
        for a, slots in reloc_slots.items():
            seed_slots.setdefault(a, []).extend(slots)

    entry = pe.image_base + pe.entry_rva
    d = Discovery(pe)
    d.run(seed_slots, "seed")
    d.run({entry}, "entry")
    seeded = set(d.functions)
    recovered = d.recover_entries()
    if recovered:
        d.run(recovered, "recovered")
    imm = d.imm_entries()
    if imm:
        d.run(imm, "imm")

    # A jump table target is a block of the function that switches on it, not a
    # function: lifting it as one makes a copy of the rest of the parent's body.
    # Drop an entry only when its every pointer slot lies inside a detected
    # table, and nothing else (a call, the entry point, a prologue, an
    # immediate) says it is a function. This is nfsu2-web's JTSWITCH rule.
    # The bytes stay covered: they belong to the parent, which a lift follows
    # through the table in place.
    table_slots = set()
    for info in d.jump_tables.values():
        table_slots.update(range(info["table"],
                                 info["table"] + 4 * len(info["targets"])))
    fixed = {a for a, r in d.origin.items()
             if r in ("entry", "recovered", "imm", "call")}
    table_targets = {a for a, slots in seed_slots.items()
                     if a in d.functions and a not in fixed
                     and slots and all(sl in table_slots for sl in slots)}
    for a in table_targets:
        d.functions.pop(a, None)
        d.origin.pop(a, None)

    origins = collections.Counter(d.origin.values())
    print(f"recovered  {len(recovered):,} function starts from int3 padding and "
          f"prologues ({origins['recovered']:,} new entries)")
    print(f"imm        {len(imm):,} entries from immediate .text pointers "
          f"({len(d.imm_targets):,} immediates seen)")
    print(f"tables     {len(table_targets):,} entries dropped as jump-table "
          f"targets only (JTSWITCH rule)")

    funcs = d.functions
    # An entry inside already-decoded bytes is an alias (a call into a shared
    # epilogue, or a switch-case label we seeded by mistake). Counting only
    # entries that own code is the honest measure of the function set.
    with_code = sum(1 for f in funcs.values() if f["insns"])
    covered_bytes = sum(d.covered)
    total_bytes = len(d.covered)
    total_insns = d.totals["insns"]
    print(f"functions  {len(funcs):,} ({with_code:,} own code, "
          f"{len(funcs) - with_code:,} aliases)")
    print(f"code       {total_insns:,} instructions  "
          f"{covered_bytes:,}/{total_bytes:,} bytes decoded "
          f"({100.0 * covered_bytes / total_bytes:.1f}%)")
    print(f"tables     {len(d.jump_tables):,} jump tables")
    print(f"traps      {dict(d.traps) or 'none'}")
    gaps = d.gaps()
    big = [g for g in gaps if g[1] >= 64]
    print(f"gaps       {len(gaps):,} uncovered ranges, "
          f"{len(big):,} of them >= 64 bytes")
    for va, size in gaps[:8]:
        print(f"             {va:#010x}  {size:>7,} bytes")

    os.makedirs("out", exist_ok=True)
    with open("out/analysis.json", "w") as f:
        json.dump({
            "file": os.path.abspath(path),
            "size": len(data),
            "image_base": pe.image_base,
            "entry": pe.image_base + pe.entry_rva,
            "size_of_image": pe.size_of_image,
            "sections": pe.sections,
            "imports": imports,
            "seed_slots": dict(hits),
            "seed_entries": len(aligned),
            "table_target_entries_dropped": len(table_targets),
            "reloc_seeds": len(reloc_slots),
            "recovered_candidates": len(recovered),
            "recovered_entries": origins["recovered"],
            "imm_entries": origins["imm"],
            "seeded_closure_count": len(seeded),
            "entry_origins": dict(origins),
            "origins": {f"{a:#010x}": r for a, r in sorted(d.origin.items())},
            "call_target_entries": origins["call"],
            "jump_table_entries": sum(len(v["targets"])
                                      for v in d.jump_tables.values()),
            "jump_tables": {f"{site:#010x}": v
                            for site, v in sorted(d.jump_tables.items())},
            "function_count": len(funcs),
            "functions_with_code": with_code,
            "alias_entries": len(funcs) - with_code,
            "instruction_count": total_insns,
            "jump_table_count": len(d.jump_tables),
            "decoded_bytes": covered_bytes,
            "text_bytes": total_bytes,
            "traps": dict(d.traps),
            "trap_sites": {k: v[:2000] for k, v in d.trap_sites.items()},
            "gaps": gaps,
            "functions": sorted(funcs),
        }, f, indent=1)

    print(f"\nwrote out/analysis.json in {time.time() - t0:.1f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
