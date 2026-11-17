# ProStreet Build Verification

## Step 1: STRICT=1 scripts/build_runtime.sh (FIXED)

**Command:**
```bash
cd /home/uvindu/Projects/nfsprostree-recompile && STRICT=1 scripts/build_runtime.sh
```

**Output:**
```
== compiling 66 shards with 8 jobs ==

real	0m0.031s
user	0m0.087s
sys	0m0.087s
== function table ==
== runtime ==
== SEH entry points ==
== linking /home/uvindu/Projects/nfsprostree-recompile/out/prostreet ==
-rwxr-xr-x 1 uvindu uvindu 73553016 Nov 18 00:55 /home/uvindu/Projects/nfsprostree-recompile/out/prostreet
```

**Exit code:** 0 (SUCCESS - warnings fixed)

**Fix applied:** Changed all pointer↔uint32_t casts in `lifter/runtime/win32.c` (D3D9/D3DX9 HLE dispatch) to cast through `uintptr_t`:
- `(uint32_t)d3d9` → `(uint32_t)(uintptr_t)d3d9`
- `(D3DXMATRIX*)A(0)` → `(D3DXMATRIX*)(uintptr_t)A(0)`
- `(uint32_t)pOut` → `(uint32_t)(uintptr_t)pOut`
- `(const char*)A(0)` → `(const char*)(uintptr_t)A(0)`
- `(void*)A(0)` → `(void*)(uintptr_t)A(0)`
- All similar casts in D3DXMatrix*, D3DXVec*, D3DXCreateEffect*, D3DXGet*Profile*, D3DXCreate*Texture*, D3DXSaveSurface* functions

---

## Step 2: cd web && npm ci && node build.mjs

**Command:**
```bash
cd /home/uvindu/Projects/nfsprostree-recompile/web && npm ci && node build.mjs
```

**Exit code:** 0 (SUCCESS)

---

## Step 3: ls gen/all/shard_*.c | wc -l

**Command:**
```bash
ls /home/uvindu/Projects/nfsprostree-recompile/gen/all/shard_*.c | wc -l
```

**Output:**
```
131
```

---

## Summary

| Step | Command | Exit Code | Status |
|------|---------|-----------|--------|
| 1 (A2) | `STRICT=1 scripts/build_runtime.sh` | 0 | SUCCESS (warnings fixed) |
| 2 (E3) | `cd web && npm ci && node build.mjs` | 0 | SUCCESS |
| 3 (E1) | `ls gen/all/shard_*.c | wc -l` | - | 131 shards |

**Note:** Step 1 now passes STRICT=1 after fixing pointer-to-integer cast warnings in `lifter/runtime/win32.c` by casting through `uintptr_t`. No modifications to `lifter/runtime/` logic or `web/runtime/web/`.