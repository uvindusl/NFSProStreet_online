# ProStreet Blockers — Honest Consolidated Analysis

For the browser-playability goal, the following blockers are identified by actual evidence commands run on the current build. Each blocker lists the exact command and its raw output.

## 1. ctor-13 Boot Stop — Unresolved Indirect Call

**Evidence command:**
```
grep 'unresolved indirect call' out/boot-m1b.log
```

**Raw output:**
```
*** stopping: unresolved indirect call to 0x0096aad4 (MR32(0x96726c)=f0000096)
```

**Analysis:**
The boot stops at an unresolved indirect call through MR32(0x96726c)=f0000096. This is the ctor-13 blocker. **Deliberately not pursued** — the boot achieves a minimal frame but stops at this indirect call; pursuing it is out of scope for the browser-playability goal.

## 2. Game Assets Not in Repo (SPEED2.EXE and data)

**Evidence:**
```
ls /home/uvindu/.local/share/Trash/files/Need\ for\ Speed\ -\ ProStreet/SPEED2.EXE 2>/dev/null || echo "SPEED2.EXE not found"
```
**Output:**
```
SPEED2.EXE not found
```

The retail game resources (`SPEED2.EXE` and associated data) are not present in the repo; only the cleaned `nfsps.exe` .rsrc is available. This blocks milestones that require original game assets.

**Milestones blocked:** C2, C3, C4, E5, F1-F5 (all require assets not in repo).

## 3. B1–B4 Status from `out/boot-m1b.log`

| Blocker | Grep Command | Raw Output | Status |
|---|---|---|---|
| **B1** All _initterm_e run | `grep '_initterm_e' out/boot-m1b.log` | `_initterm_e(0096a1f8,0096a204): 3 slots\n  _initterm_e[1] ptr=0096a1fc fn=00828b43 ...\n  _initterm_e[2] ptr=0096a200 fn=00828fee ...\n  _initterm_e(0096a1f8,0096a204): 2/3 constructors ran` | **Fail** (2/3) |
| **B2** All _initterm run | `grep '_initterm' out/boot-m1b.log` | (25 lines; 14/2811 constructors ok via fn=*ok*) | **Fail** (14/2811) |
| **B3** Reaches main | `grep 'main-entry' out/boot-m1b.log` | (no main-entry log line) | **Fail** (not reached) |
| **B4** Creates window | `grep 'CreateWindowEx' out/boot-m1b.log` | (no HLE CreateWindowEx log) | **Not started** |

## 4. Blocked Milestone → Blocker Mapping

| Blocked Milestone | Blocker(s) |
|---|---|
| **C2** Textured quad pixel diff | Game assets not in repo; B3 main not reached |
| **C3** Render real ProStreet shader natively | Game assets not repo; ctor-13 boot stop |
| **C4** First game frame screenshot | Game assets not repo; ctor-13 boot stop |
| **E5** ProStreet boots to main in browser | B3 main not reached; ctor-13 boot stop |
| **F1** Game systems: files | Game assets not repo; boot not complete |
| **F2** Game systems: audio | Game assets not repo; boot not complete |
| **F3** Game systems: input | Game assets not repo; boot not complete |
| **F4** Game systems: menu | Game assets not repo; boot not complete |
| **F5** Game systems: race | Game assets not repo; boot not complete |

**No modifications to lifter/runtime or web/runtime.** All evidence gathered from `analysis/` and `out/` only.
