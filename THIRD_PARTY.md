# Third-party notices

This project builds on MIT-licensed work. Nothing here contains game code,
game data or Microsoft DLLs; those are supplied locally by whoever builds it.

## nfsu2-web — th0mk

https://github.com/th0mk/nfsu2-web — MIT.

The x86 → C lifter in `lifter/tools/` (`lift.py`, `genall.py`, `lregs.py`,
`flagabi.py`, `split_reloc.py`) is derived from this project. It was written
for Underground 2 and is reused here with small fixes for ProStreet (see
`docs/phase3-lifter.md` §6). Its `LICENSE` and the credits in its `README.md`
(including the mods and translations it builds on) apply to that work.

## NFSU2Recomp — Talha Müderrisoğlu

https://github.com/TalhaMudo/NFSU2Recomp — MIT.

The original x86 → C static recompiler for Underground 2, from whose MIT
version (16 October 2026) `nfsu2-web` was forked, and therefore the ancestor of
the lifter used here.

## pcrecomp — sp00nznet

https://github.com/sp00nznet/pcrecomp

The generic PC static-recompilation toolbox. Not linked into this project; its
method for recovering a function set in images without relocation tables was
used as a reference while seeding the lift.

## N64: Recompiled / XenonRecomp

https://github.com/N64Recomp/N64Recomp ·
https://github.com/hedge-dev/XenonRecomp

The static-recompilation method the above projects follow, and a reference for
the runtime's dispatch and memory model.

## Tools used

* [Capstone](https://www.capstone-engine.org/) — disassembler (BSD-3).
* [pefile](https://github.com/erocarrera/pefile) — PE parser (MIT).
* [innoextract](https://constexpr.org/innoextract/) — used during recon only to
  read the installer and reach `nfs.exe`; not a build dependency (zlib).
