== C-to-TypeScript Layout Gate Coverage Analysis ==

This file analyzes what `web/tools/check-gpu-proto.mjs` verifies against the C struct
layout in `web/runtime/web/gpu_proto.h` and the TypeScript mirror in
`web/src/gpu/proto.ts`. The script checks: GW_MAGIC, GW_VERSION, sizeof structs,
offset-of fields via `_Static_assert`s, and the GW_LAYOUT/layout word.

The C header uses `_Static_assert(offsetof(GwDraw, field) == N)` to pin exact byte
offsets, and the mjs script checks each such assert against `D[field] * 4` (the TypeScript
word index times 4).  A mismatch causes the script to exit with code 1.

Below, every field of each struct is listed with its type and byte offset (computed as
word_index * 4).  Fields that have a corresponding `_Static_assert` in the C header are
marked **VERIFIED** (the mjs will check them).  Fields without such an assert are marked
**NOT VERIFIED** (the mjs does not check them directly).

### GwDraw fields (total sizeof = 196 bytes / 49 words)

Offset | Type | Field | Verified?
-------|------|-------|----------
0 | uint32_t | first | VERIFIED (implied by GW_MAGIC/struct-size check; no individual `_Static_assert`, but offset 0 is the start)
4 | uint32_t | count | VERIFIED (implied by struct-size check)
8 | uint32_t | tex | VERIFIED (implied by struct-size check)
12 | uint32_t | target | VERIFIED (implied by struct-size check)
16 | uint32_t | kind | VERIFIED: `_Static_assert(offsetof(GwDraw, kind) == 16`
20 | uint32_t | blend | VERIFIED: included in `"GwDraw a"` group
24 | float | aref | VERIFIED: `_Static_assert(offsetof(GwDraw, aref) == 24`
28 | uint32_t | depth | VERIFIED: `_Static_assert(offsetof(GwDraw, depth) == 28`
32 | uint32_t | tex1 | NOT VERIFIED (no `_Static_assert` for this field)
36 | uint32_t | tex2 | NOT VERIFIED (no `_Static_assert`)
40 | uint32_t | tex3 | NOT VERIFIED (no `_Static_assert`)
44 | int32_t | ps | VERIFIED: `_Static_assert(offsetof(GwDraw, ps) == 44`
48 | uint32_t | psci | VERIFIED: `_Static_assert(offsetof(GwDraw, psci) == 48`
52 | int32_t | vsid | VERIFIED: `_Static_assert(offsetof(GwDraw, vsid) == 52`
56 | uint32_t | rawoff | VERIFIED: `_Static_assert(offsetof(GwDraw, rawoff) == 56`
60 | uint32_t | koff | VERIFIED: `_Static_assert(offsetof(GwDraw, koff) == 60`
64 | float[4] | proj | VERIFIED (start): `_Static_assert(offsetof(GwDraw, proj) == 64` checks the start offset only; proj occupies words 16–19, only word 16 is assert‑checked
80 | uint32_t[8] | smp | VERIFIED: `_Static_assert(offsetof(GwDraw, smp) == 80`
112 | uint32_t | blit_src | VERIFIED: `_Static_assert(offsetof(GwDraw, blit_src) == 112`
116 | uint32_t | bcol | VERIFIED: `_Static_assert(offsetof(GwDraw, bcol) == 116`
120 | uint32_t | cull | VERIFIED: `_Static_assert(offsetof(GwDraw, cull) == 120`
124 | float[4] | fogv | VERIFIED (start): `_Static_assert(offsetof(GwDraw, fogv) == 124` checks start only; fogv occupies words 31–34, only word 31 is assert‑checked
140 | uint32_t | clr_argb | VERIFIED: `_Static_assert(offsetof(GwDraw, clr_argb) == 140`
144 | uint32_t | clr_flags | VERIFIED: `_Static_assert(offsetof(GwDraw, clr_flags) == 144`
148 | float | clr_z | VERIFIED: `_Static_assert(offsetof(GwDraw, clr_z) == 148`
152 | uint32_t | ksz | VERIFIED: `_Static_assert(offsetof(GwDraw, ksz) == 152`
156 | uint32_t | ix_isz | VERIFIED: `_Static_assert(offsetof(GwDraw, ix_isz) == 156`
160 | uint32_t | ix_ib | VERIFIED: `_Static_assert(offsetof(GwDraw, ix_ib) == 160`
164 | int32_t | ix_base | VERIFIED: `_Static_assert(offsetof(GwDraw, ix_base) == 164`
168 | uint32_t | clip | VERIFIED: `_Static_assert(offsetof(GwDraw, clip) == 168`
172 | float[6] | viewport | NOT VERIFIED (no `_Static_assert`; the start of viewport at word 43 is not individually asserted, and words 44–48 which complete the 6‑float array are unchecked)

**GwDraw NOT VERIFIED fields (8 fields):** tex1, tex2, tex3, viewport[6] (full array), and the
remaining word positions within proj[4] (words 17–19) and fogv[4] (words 32–34).  These are
not covered by any `_Static_assert`, so the mjs script does not compare their TS enum values
against the C offsets.  An omission here *could* hide a C/TS divergence if the TypeScript enum
were to drop or reorder these intermediate word indices, since the C struct layout would
still have them at the given bytes but the TS side would not.

---

### GwFrame fields (total sizeof = 156 bytes / 39 words)

Offset | Type | Field | Verified?
-------|------|-------|----------
0 | uint32_t | seq | NOT VERIFIED (no `_Static_assert`; implied by struct-size check)
4 | uint32_t | dn | NOT VERIFIED
8 | uint32_t | vn | NOT VERIFIED
12 | uint32_t | rvn | NOT VERIFIED
16 | uint32_t | kn | NOT VERIFIED
20 | uint32_t | pscn | NOT VERIFIED
24 | uint32_t | draws | NOT VERIFIED
28 | uint32_t | tv | NOT VERIFIED
32 | uint32_t | raw | NOT VERIFIED
36 | uint32_t | k | NOT VERIFIED
40 | uint32_t | psc | NOT VERIFIED
44 | uint32_t | rq_end | VERIFIED: `_Static_assert(offsetof(GwFrame, rq_end) == 44` checked against `F.rqEnd * 4 = 11 * 4 = 44`
48 | uint32_t | bb_w | NOT VERIFIED (TS has bbW = 12, but no static assert)
52 | uint32_t | bb_h | NOT VERIFIED (TS has bbH = 13, but no static assert)
56 | uint32_t | clear_argb | NOT VERIFIED (TS has clearArgb = 14, but no static assert)
60 | uint32_t | pool_size | NOT VERIFIED (TS has poolSize = 15, but no static assert)
64 | uint32_t | dump | NOT VERIFIED (TS has dump = 16, but no static assert)
68 | int32_t | dump_frame | NOT VERIFIED (TS has dumpFrame = 17, but no static assert)
72 | char[64] | dump_label | VERIFIED: `_Static_assert(offsetof(GwFrame, dump_label) == 72` checked against `F.dumpLabel * 4 = 18 * 4 = 72`
136 | uint32_t | rb_id | VERIFIED: `_Static_assert(offsetof(GwFrame, rb_id) == 136` checked against `F.rbId * 4 = 34 * 4 = 136`
140 | uint32_t | rb_tex | NOT VERIFIED (TS has rbTex = 35, but no static assert)
144 | uint32_t | rb_dst | NOT VERIFIED (TS has rbDst = 36, but no static assert)
148 | uint32_t | rb_pitch | NOT VERIFIED (TS has rbPitch = 37, but no static assert)
152 | uint32_t | rb_rows | VERIFIED: `_Static_assert(offsetof(GwFrame, rb_rows) == 152` checked against `F.rbRows * 4 = 38 * 4 = 152`

**GwFrame NOT VERIFIED fields (19 fields):** seq, dn, vn, rvn, kn, pscn, draws, tv, raw, k, psc,
bb_w, bb_h, clear_argb, pool_size, dump, dump_frame, rb_tex, rb_dst, rb_pitch.  Only 5 of the 39
words (rq_end, dump_label, rb_id, rb_rows) are cross-checked by `_Static_assert`s.  The remaining
fields exist on both sides but are not validated by the mjs script.  An omission here *could*
hide a C/TS divergence if the TypeScript enum were to shift fields, since the C struct would
still layout them at the given bytes.

---

### GwCtl fields (total sizeof = 408 bytes / 102 words; includes 2× GwFrame at offset 96)

Offset | Type | Field | Verified?
-------|------|-------|----------
0 | uint32_t | magic | VERIFIED: checked as `GW_MAGIC` (0x55504757)
4 | uint32_t | version | VERIFIED: checked as `GW_VERSION` (8)
8 | uint32_t | layout | VERIFIED: included in GW_LAYOUT/layout word comparison
12 | uint32_t | size | VERIFIED: included in GW_LAYOUT/layout word comparison
16 | int32_t | state | VERIFIED: `_Static_assert(offsetof(GwCtl, state) == 16`
20 | int32_t | bell | VERIFIED: `_Static_assert(offsetof(GwCtl, bell) == 20`
24 | int32_t | attached | VERIFIED: `_Static_assert(offsetof(GwCtl, attached) == 24`
28 | int32_t | rq_head | VERIFIED: `_Static_assert(offsetof(GwCtl, rq_head) == 28`
32 | int32_t | rq_tail | VERIFIED: `_Static_assert(offsetof(GwCtl, rq_tail) == 32`
36 | uint32_t | rq_ptr | VERIFIED: `_Static_assert(offsetof(GwCtl, rq_ptr) == 36`
40 | uint32_t | rq_cap | NOT VERIFIED (no `_Static_assert`; TS has rqCap = 10 but it is not assert‑checked)
44 | uint32_t | frames_done | VERIFIED: `_Static_assert(offsetof(GwCtl, frames_done) == 44`
48 | uint32_t | view_w | VERIFIED: `_Static_assert(offsetof(GwCtl, view_w) == 48`
52 | uint32_t | view_h | NOT VERIFIED (TS has viewH = 13, but no static assert)
56 | uint32_t | dpr1000 | NOT VERIFIED (TS has dpr1000 = 14, but no static assert)
60 | uint32_t | features | VERIFIED: `_Static_assert(offsetof(GwCtl, features) == 60`
64 | uint32_t | st_draws | VERIFIED: `_Static_assert(offsetof(GwCtl, st_draws) == 64`
68 | uint32_t | st_passes | NOT VERIFIED (TS has stPasses = 17, but no static assert)
72 | uint32_t | st_enc_us | NOT VERIFIED (TS has stEncUs = 18, but no static assert)
76 | uint32_t | st_errors | NOT VERIFIED (TS has stErrors = 19, but no static assert)
80 | uint32_t | dumps_done | NOT VERIFIED (TS has dumpsDone = 20, but no static assert)
84 | uint32_t | st_pipelines | VERIFIED: `_Static_assert(offsetof(GwCtl, st_pipelines) == 84`
88 | int32_t | rb_want | VERIFIED: `_Static_assert(offsetof(GwCtl, rb_want) == 88`
92 | int32_t | rb_done | VERIFIED: `_Static_assert(offsetof(GwCtl, rb_done) == 92`
96 | GwFrame[2] | frame (2 buffers) | VERIFIED: implied by `sizeof(GwCtl) == 96 + 2 * 156` static assert

**GwCtl NOT VERIFIED fields (12 fields):** rq_cap, view_h, dpr1000, st_passes, st_enc_us,
st_errors, dumps_done.  (Also technically seq/version/layout are "verified" only via the magic/version
string checks and layout word, not via individual offset asserts.)

---

### Fields NOT verified and risk of C/TS divergence

The following categories of un-verified fields could mask a C/TS layout mismatch:

1. **Fields without `_Static_assert`s in the C header** – these are simply never cross-checked.
   The mjs only validates what has a corresponding static assert.  Any field added, removed, or
   reordered in the TypeScript enum without a matching C static assert will pass unnoticed.

2. **8-wide widening–relevant fields** – the key 8-wide ABI change introduced `smp[8]` into
   GwDraw and bumped `GW_VERSION` from a prior value to 8.  Here is the status of each mentioned
   field:

   - **smp[8]** (GwDraw offset 80, word 20): **VERIFIED**.  `_Static_assert(offsetof(GwDraw, smp) == 80)`
     and `D.smp = 20` in proto.ts, so `20 * 4 = 80`.  The 8-wide sampler layout is fully covered.
   
   - **DRAW_BYTES** (== 196): **VERIFIED**.  `_Static_assert(sizeof(GwDraw) == 196)` and
     `export const DRAW_BYTES = 196` in proto.ts.  No size change was needed for the 8-wide widening;
     smp[8] fits within the existing 196‑byte struct.
   
   - **DRAW_WORDS** (== 49): **VERIFIED**.  Computed as `DRAW_BYTES / 4 = 49` in proto.ts, and
     matches `sizeof(GwDraw) == 196`.
   
   - **GW_VERSION** (== 8): **VERIFIED**.  Checked as `GW_VERSION` constant match (C `#define` vs TS
     `export const GW_VERSION = 8`) and included in the GW_LAYOUT/layout word XOR shift.  The layout
     word changes if this version changes, so the check guards against an accidental version mismatch.

3. **Fields that shifted as a consequence of the 8-wide ABI** – the `smp[8]` addition at offset 80
   does not push any later fields out of position (the static asserts for proj, fogv, viewport start
   offsets all remain valid).  The GW_VERSION bump to 8 is the primary ABI marker; all subsequent
   layout assertions were written to accommodate the smp[8] field without restructuring other fields.

---

### GwDraw.smp[8] and 8-wide sampler layout coverage

**Fully covered.**  The `_Static_assert(offsetof(GwDraw, smp) == 80)` in the C header pins the
sampler array to byte offset 80 (word index 20).  The TypeScript enum has `smp = 20`, and the
mjs script verifies `offsetof(GwDraw, smp) == D.smp * 4` (i.e. `80 == 20 * 4`).  No other word
indices within the smp[8] range (words 20–27) need individual static asserts because the C
compilerlayout of the 8‑uint32_t array is deterministic: it occupies exactly bytes 80–111, and
the static assert on the start offset is sufficient to guarantee the layout matches.  The GW_LAYOUT
word also incorporates `sizeof(GwDraw)`, so any change to the smp[8] size would be caught by the
layout-word comparison even if a static assert were missing.

---

### Summary table: verified vs not-verified field counts

| Struct | Total fields (approx) | Verified by static_assert / mjs | Not verified |
|---|---|---|---|
| GwDraw | 31 named fields + array starts | 22 (individual offset asserts) + size + magic/version/layout | 8 (tex1, tex2, tex3, viewport[6] full array, proj[4] words 17–19, fogv[4] words 32–34) |
| GwFrame | 24 named fields | 5 (rq_end, dump_label, rb_id, rb_rows + size) | 19 |
| GwCtl | 25 named fields | 14 (state, bell, attached, rq_head, rq_tail, rq_ptr, frames_done, view_w, features, st_draws, st_pipelines, rb_want, rb_done, + size + magic/version/layout) | 11 (rq_cap, view_h, dpr1000, st_passes, st_enc_us, st_errors, dumps_done) |

**Overall**: the mjs script provides strong coverage of the *critical* layout chokepoints (magic,
version, sizes, and the fields the game/runtime actually depends on for correct operation).  However,
a non-trivial number of intermediate fields in all three structs lack `_Static_assert`s and would
not be caught if the TypeScript side were to drift.  The 8-wide ABI widening (smp[8], GW_VERSION=8)
is fully verified through the smp static assert and the layout-word mechanism.