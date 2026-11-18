# Fate of `lifter/tools/web/vsemu.c` — Mirror Status Report

## 1. File Existence, Sizes, and mtimes

| Path | Exists | Size | mtime |
|------|--------|------|-------|
| `web/runtime/web/vsemu.c` | **NO** | — | — |
| `web/runtime/web/runtime/vsemu.c` | YES | 76,461 bytes | 2026-11-18 03:02 |
| `lifter/tools/web/vsemu.c` | YES | 76,357 bytes | 2026-11-18 02:19 |

**Note:** The canonical source is `web/runtime/web/runtime/vsemu.c`, not `web/runtime/web/vsemu.c` (which does not exist).

---

## 2. Git Tracking Status

```bash
git ls-files lifter/tools/web/vsemu.c
```
**Output:** *(empty — file is NOT tracked by git)*

---

## 3. Byte Comparison vs Canonical

```bash
cmp lifter/tools/web/vsemu.c web/runtime/web/runtime/vsemu.c
```
**Output:**
```
lifter/tools/web/vsemu.c web/runtime/web/runtime/vsemu.c differ: byte 21533, line 480
```

**Diff details:**
```diff
--- lifter/tools/web/vsemu.c
+++ web/runtime/web/runtime/vsemu.c
@@ -477,8 +477,8 @@
                     d[2]=s[0][0]*s[1][1]-s[0][1]*s[1][0];
                     DSTW(x,d); break;
         case O_NRM: { float l=sqrtf(s[0][0]*s[0][0]+s[0][1]*s[0][1]+s[0][2]*s[0][2]);
-                      float r=(l==0.f)?0.f:1.f/l;
-                      d[0]=s[0][0]*r; d[1]=s[0][1]*r; d[2]=s[0][2]*r; d[3]=r;
+                      float r=(l==0.f)?3.402823466e+38f:1.f/l;
+                      d[0]=s[0][0]*r; d[1]=s[0][1]*r; d[2]=s[0][2]*r; d[3]=s[0][3]*r;
                       DSTW(x,d); } break;
@@ -1367,7 +1367,7 @@
                         "float4((%s).x==0.0?-3.4e38:log2(abs((%s).x)))", s0, s0); break;
         case O_POW:  snprintf(ex,sizeof ex,"float4(pow(abs((%s).x),(%s).x))", s0, s1); break;
         case O_CRS:  snprintf(ex,sizeof ex,"float4(cross((%s).xyz,(%s).xyz),1.0)", s0, s1); break;
-        case O_NRM:  snprintf(ex,sizeof ex,"float4(length((%s).xyz)==0.0?float3(0.0):normalize((%s).xyz),1.0)", s0, s0); break;
+        case O_NRM:  snprintf(ex,sizeof ex,"float4(length((%s).xyz)==0.0?float3(0.0):normalize((%s).xyz), (%s).w * (length((%s).xyz)==0.0 ? 3.402823466e+38 : 1.0/length((%s).xyz)))", s0, s0, s0, s0, s0); break;
```

**Two critical divergences in `lifter/tools/web/vsemu.c` (both bugs):**
1. **Line 480 (vs_run O_NRM):** Returns `r=0` instead of `FLT_MAX` for zero-length, and writes `d[3]=r` instead of `d[3]=src.w * r` → drops the `src0.w` factor.
2. **Line 1370 (MSL emitter O_NRM):** Emits bare `1.0` for w-component instead of `src.w * (1/|src|)`.

---

## 4. Repo-Wide References

```bash
grep -r "lifter/tools/web/vsemu" --exclude-dir=.git --exclude-dir=node_modules
```
**Output:**
```
/home/uvindu/Projects/nfsprostree-recompile/analysis/repo-hygiene.md:- lifter/tools/web/vsemu_psref.inc
/home/uvindu/Projects/nfsprostree-recompile/debug/shader-emitter-semantics.md:...identical mirror `lifter/tools/web/vsemu.c` line 468.
/home/uvindu/Projects/nfsprostree-recompile/debug/untracked-notes-inventory.md:- `lifter/tools/web/vsemu.c` — Lifter runtime file (not in repo; tracked elsewhere)
/home/uvindu/Projects/nfsprostree-recompile/debug/vertex-path-differential.md:The mirror `lifter/tools/web/vsemu_wgsl.inc` is byte-identical. Regression:
/home/uvindu/Projects/nfsprostree-recompile/debug/abi-8wide-design.md:5. `lifter/tools/web/vsemu_wgsl.inc` refreshed byte-identical to
/home/uvindu/Projects/nfsprostree-recompile/debug/abi-8wide-remaining-4wide.md:| `lifter/tools/web/vsemu.c:245` | ...
```

**No build scripts, test scripts, Makefiles, or package.json reference `lifter/tools/web/vsemu.c`.** Only documentation files mention it (incorrectly calling it an "identical mirror").

```bash
grep -r "tools/web/vsemu" --exclude-dir=.git --exclude-dir=node_modules
```
Same output — only docs mention it.

---

## 5. `check_mirrors.sh` Coverage

**Script logic:** Only compares files that exist in **both** `lifter/tools/web/` AND `web/runtime/web/` (same filename).

```bash
for f in "$MIRROR_DIR"/*; do
    canon="$CANON_DIR/$name"
    [ -e "$canon" ] || continue  # skips if no same-name file in web/runtime/web/
```

**Result:** `lifter/tools/web/vsemu.c` is **NOT checked** because `web/runtime/web/vsemu.c` does not exist (the canonical is at `web/runtime/web/runtime/vsemu.c`). The mirror check silently skips it.

---

## 6. `.gitignore` Status

```bash
cat .gitignore
```
**Relevant lines:** None matching `lifter/tools/web/vsemu.c`.

The file is **untracked but not ignored** — it appears in `git status` as an untracked file.

---

## Recommendation

**DELETE** the file.

**Reasoning:**
1. It is **not a mirror** — it diverges from the canonical `web/runtime/web/runtime/vsemu.c` in two critical NRM implementations (both dropping the `src0.w` factor).
2. It is **not tracked by git**, not ignored by `.gitignore`, and not referenced by any build/test script.
3. The `check_mirrors.sh` script **does not protect it** (canonical path mismatch).
4. Documentation incorrectly claims it's an "identical mirror" — it is not.
4. Keeping it creates confusion and a silent correctness hazard (the NRM bugs would be baked in if anyone used it).

**Action:** `rm lifter/tools/web/vsemu.c`