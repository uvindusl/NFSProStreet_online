# ProStreet runtime bring-up

This directory contains the first native S2 runtime slices. They are
hand-written and intentionally do not include generated C, executable data,
or game assets.

Current pieces:

* `cpu.h` — CPU and guest-memory contract shared with the lifted C.
* `mem.c` — sparse native guest address space and PE image loader.
* `s2_import_dispatch.*` — wasm-safe import ABI validation and dispatch
  boundary; it binds the 371-entry table's arity and DLL calling-convention
  policy, and logs unsupported imports without pretending to implement them.
* `file_layer.*` — safe case-insensitive game-data paths and open tracing.
* `native_boot.c` — diagnostic native entry-point harness.

The harness is not a playable build yet. It currently reaches CRT startup and
stops at the next unsupported or invalid runtime operation; the S2 gate still
requires the complete CRT, Win32, heap, thread, file, and null-renderer
layers.
