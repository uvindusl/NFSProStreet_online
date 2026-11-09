# Third-party notices

This project builds on MIT-licensed work. Nothing here contains game code,
game data or Microsoft DLLs; those are supplied locally by whoever builds it.

## nfsu2-web — th0mk

https://github.com/th0mk/nfsu2-web — MIT.

The x86→C lifter (`tools/lift.py`, `tools/genall.py`, …) and, in later stages,
the wasm runtime, D3D9→WebGPU renderer, WebAudio backend, thread model and
browser host all come from this project. It is used from a pinned checkout
(`scripts/lift.sh`) rather than copied, until this project forks the parts it
changes. Its `LICENSE` and the credits in its `README.md` (including the mods
and translations it builds on) apply to that work.

## NFSU2Recomp — Talha Müderrisoğlu

https://github.com/TalhaMudo/NFSU2Recomp — MIT.

The original x86 → C static recompiler for Underground 2, from whose MIT
version (16 October 2026) `nfsu2-web` was forked, and therefore the ancestor of
the lifter used here.

## pcrecomp — sp00nznet

https://github.com/sp00nznet/pcrecomp

The generic PC static-recompilation toolbox. Not linked into this project; its
method for recovering a function set in images without relocation tables was
used as a reference for `scripts/seed_scan.py`.

## N64: Recompiled / XenonRecomp

https://github.com/N64Recomp/N64Recomp ·
https://github.com/hedge-dev/XenonRecomp

The static-recompilation method the above projects follow.

## Tools used

* [Capstone](https://www.capstone-engine.org/) — disassembler (BSD-3).
* [pefile](https://github.com/erocarrera/pefile) — PE parser (MIT).
* [innoextract](https://constexpr.org/innoextract/) — used during recon only to
  read the installer and reach `nfsps.exe`; not a build dependency (zlib).
