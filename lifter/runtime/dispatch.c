/* Guest address -> lifted native function.  One table the size of .text:
 * an indirect call or tail jump is a load and a call.  IAT slots hold
 * IMP_MAGIC + index and are routed to win32_call(). */
#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void (*guest_fn)(CPU *);
extern const uint32_t g_fn_addr[];
extern const guest_fn g_fn_ptr[];
extern const uint32_t g_fn_count;

/* The SEH filters, handlers and the MSVCR80 establisher thunk: guest code
 * the Windows exception dispatcher calls at addresses that are not function
 * entries.  Lifted by scripts/lift_seh.py into lifter/runtime/seh_lifted.c. */
extern const uint32_t g_seh_fn_addr[];
extern const guest_fn_t g_seh_fn_ptr[];
extern const uint32_t g_seh_fn_count;

#define COM_MAGIC 0xE0000000u
extern void com_init(void);
extern int  com_call(CPU *c, uint32_t magic);

#define TEXT_BASE DISPATCH_TEXT_BASE
#define TEXT_SIZE DISPATCH_TEXT_SIZE

guest_fn_t g_dtab[DISPATCH_TEXT_SIZE];

#define DIAB_MAX 16384
uint32_t g_diab_addr[DIAB_MAX];
guest_fn_t g_diab_fn[DIAB_MAX];
uint32_t g_diab_count;
static int g_dtab_ready;
uint64_t g_stat_indirect_ok = 0, g_stat_indirect_miss = 0, g_stat_unsupported = 0;
uint64_t g_stat_win32_calls = 0, g_stat_win32_unimpl = 0;
/* A diabolus address with no lifted entry: the dispatcher emulates the
 * return instead of running the code.  Counted so the run report shows how
 * much real code was skipped rather than hiding it. */
uint64_t g_stat_diab_fallback = 0;

#define MISS_MAX 64
static uint32_t g_miss[MISS_MAX], g_miss_from[MISS_MAX];
static int g_miss_n;
static uint32_t g_unsup[MISS_MAX];
static int g_unsup_n;
static uint32_t g_diab_miss[MISS_MAX];
static int g_diab_miss_n;

/* The host-side backtrace is not used: guest_backtrace() walks the guest
 * stack instead, which is what actually matters here. */

/* Lifted helper declarations */
void sub_004040f0(CPU *c);
void sub_00435140(CPU *c);
void sub_0044bfd0(CPU *c);
void sub_004add90(CPU *c);
void sub_004c8280(CPU *c);
void sub_004c9730(CPU *c);
void sub_00542700(CPU *c);
void sub_00909210(CPU *c);
void sub_009096a0(CPU *c);

/* ===== sub_00909b00  (121 insns, 11 blocks) ===== */
static void sub_00909b00(CPU *restrict c) {
    goto L_00909b00;
L_00909b00:; /* --- block --- */
    PUSH32(c->ebx);
    PUSH32(c->esi);
    c->esi = (uint32_t)(MR32(c->esp+0xcu));
    c->eax = (uint32_t)(MR32(c->esi+0x10u));
    c->edx = (uint32_t)(MR32(c->eax+0x4u));
    c->ecx = (uint32_t)(MR32(c->edx+0xcu));
    c->edx = (uint32_t)(MR32(c->edx+0x8u));
    PUSH32(c->edi);
    c->edi = (uint32_t)(MR32(c->ecx+0x140u));
    c->ecx = (uint32_t)(MR32(c->eax+0x10u));
    c->ebx = (uint32_t)(MR32(c->ecx));
    c->eax = (uint32_t)(MR32(c->eax+0x8u));
    PUSH32(c->edx);
    c->edx = (uint32_t)(MR32(c->esi+0x24u));
    PUSH32(c->edi);
    PUSH32(0x0u);
    PUSH32(c->edx);
    c->edx = (uint32_t)(MR32(c->esi+0x20u));
    PUSH32(c->edx);
    c->edx = (uint32_t)(MR32(c->ebx+0x1cu));
    PUSH32(c->eax);
    { uint32_t _ct = c->edx; PUSH32(0x909b33u); DISPATCH_CALL(c, _ct); }
    c->ecx = (uint32_t)(MR32(c->esi+0x10u));
    c->ecx = (uint32_t)(MR32(c->ecx+0x4u));
    MR32(c->ecx+0x20u) = (c->eax);
    MR32(c->ecx+0x24u) = (c->edx);
    c->edx = (uint32_t)(MR32(c->esi+0x10u));
    c->ecx = (uint32_t)(MR32(c->edx+0x10u));
    c->eax = (uint32_t)(MR32(c->ecx));
    c->edx = (uint32_t)(MR32(c->eax+0x2cu));
    { uint32_t _ct = c->edx; PUSH32(0x909b4cu); DISPATCH_CALL(c, _ct); }
    c->edi = (uint32_t)(c->eax);
    PUSH32(0x909b53u); sub_00909210(c);
    c->ecx = (uint32_t)(MR32(c->esi+0x2cu));
    c->eax = (uint32_t)(MR32(c->eax+0x24u));
    c->ebx = (uint32_t)(MR32(c->esi+0x28u));
    { uint32_t _r=(c->edx) ^ (c->edx); FLAGS_LOG_M(_r,32,0x00u); c->edx = (uint32_t)(_r); }
    { uint32_t _a=c->ecx,_b=c->edx,_r=_a-_b; FLAGS_SUB_M(_a,_b,_r,32,0x09u); }
    if(CC_A) goto L_00909b68;
    goto L_00909b62;
L_00909b62:; /* --- block --- */
    if(CC_B) goto L_00909b6a;
    goto L_00909b64;
L_00909b64:; /* --- block --- */
    { uint32_t _a=c->ebx,_b=c->edi,_r=_a-_b; FLAGS_SUB_M(_a,_b,_r,32,0x01u); }
    if(CC_B) goto L_00909b6a;
    goto L_00909b68;
L_00909b68:; /* --- block --- */
    c->ebx = (uint32_t)(c->edi);
    { uint32_t _a=c->ebx,_b=c->eax,_r=_a-_b; FLAGS_SUB_M(_a,_b,_r,32,0x3fu); }
    if(CC_B) goto L_00909b70;
    goto L_00909b6e;
L_00909b6a:; /* --- block --- */
    { uint32_t _a=c->ebx,_b=c->eax,_r=_a-_b; FLAGS_SUB_M(_a,_b,_r,32,0x3fu); }
    if(CC_B) goto L_00909b70;
    goto L_00909b6e;
L_00909b6e:; /* --- block --- */
    c->ebx = (uint32_t)(c->eax);
    c->eax = (uint32_t)(MR32(c->esi+0x10u));
    c->edx = (uint32_t)(MR32(c->eax+0x4u));
    c->ecx = (uint32_t)(MR32(c->edx+0xcu));
    c->edx = (uint32_t)(MR32(c->edx+0x8u));
    c->edi = (uint32_t)(MR32(c->ecx+0x140u));
    c->ecx = (uint32_t)(MR32(c->eax+0x10u));
    c->eax = (uint32_t)(MR32(c->eax+0x8u));
    PUSH32(c->ebp);
    c->ebp = (uint32_t)(MR32(c->ecx));
    PUSH32(c->edx);
    c->edx = (uint32_t)(MR32(c->esi+0x30u));
    PUSH32(c->edi);
    PUSH32(c->ebx);
    PUSH32(c->edx);
    c->edx = (uint32_t)(MR32(c->ebp+0x14u));
    PUSH32(c->eax);
    { uint32_t _ct = c->edx; PUSH32(0x909b98u); DISPATCH_CALL(c, _ct); }
    { uint32_t _r=(c->ecx) ^ (c->ecx); FLAGS_LOG_M(_r,32,0x00u); c->ecx = (uint32_t)(_r); }
    { uint32_t _a=MR32(c->esi+0x28u),_b=c->eax,_r=_a-_b; FLAGS_SUB_M(_a,_b,_r,32,0x01u); MR32(c->esi+0x28u) = (_r); }
    { uint32_t _t; POP32(_t); c->ebp = (uint32_t)(_t); }
    { uint32_t _ci=c->cf&1u,_a=MR32(c->esi+0x2cu),_b=c->ecx,_r=_a-_b-_ci; FLAGS_SBB_M(_a,_b,_ci,_r,32,0x00u); MR32(c->esi+0x2cu) = (_r); }
    { uint32_t _a=MR32(c->esi+0x20u),_b=c->eax,_r=_a+_b; FLAGS_ADD_M(_a,_b,_r,32,0x01u); MR32(c->esi+0x20u) = (_r); }
    { uint32_t _ci=c->cf&1u,_a=MR32(c->esi+0x24u),_b=c->ecx,_r=_a+_b+_ci; FLAGS_ADC_M(_a,_b,_ci,_r,32,0x00u); MR32(c->esi+0x24u) = (_r); }
    { uint32_t _a=MR32(c->esi+0x38u),_b=c->eax,_r=_a+_b; FLAGS_ADD_M(_a,_b,_r,32,0x01u); MR32(c->esi+0x38u) = (_r); }
    { uint32_t _ci=c->cf&1u,_a=MR32(c->esi+0x3cu),_b=c->ecx,_r=_a+_b+_ci; FLAGS_ADC_M(_a,_b,_ci,_r,32,0x00u); MR32(c->esi+0x3cu) = (_r); }
    { uint32_t _a=MR32(c->esi+0x30u),_b=c->eax,_r=_a+_b; FLAGS_ADD_M(_a,_b,_r,32,0x00u); MR32(c->esi+0x30u) = (_r); }
    { uint32_t _a=MR32(c->esi+0x2cu),_b=c->ecx,_r=_a-_b; FLAGS_SUB_M(_a,_b,_r,32,0x09u); }
    if(CC_A) goto L_00909bba;
    goto L_00909bb5;
L_00909b70:; /* --- block --- */
    c->eax = (uint32_t)(MR32(c->esi+0x10u));
    c->edx = (uint32_t)(MR32(c->eax+0x4u));
    c->ecx = (uint32_t)(MR32(c->edx+0xcu));
    c->edx = (uint32_t)(MR32(c->edx+0x8u));
    c->edi = (uint32_t)(MR32(c->ecx+0x140u));
    c->ecx = (uint32_t)(MR32(c->eax+0x10u));
    c->eax = (uint32_t)(MR32(c->eax+0x8u));
    PUSH32(c->ebp);
    c->ebp = (uint32_t)(MR32(c->ecx));
    PUSH32(c->edx);
    c->edx = (uint32_t)(MR32(c->esi+0x30u));
    PUSH32(c->edi);
    PUSH32(c->ebx);
    PUSH32(c->edx);
    c->edx = (uint32_t)(MR32(c->ebp+0x14u));
    PUSH32(c->eax);
    { uint32_t _ct = c->edx; PUSH32(0x909b98u); DISPATCH_CALL(c, _ct); }
    { uint32_t _r=(c->ecx) ^ (c->ecx); FLAGS_LOG_M(_r,32,0x00u); c->ecx = (uint32_t)(_r); }
    { uint32_t _a=MR32(c->esi+0x28u),_b=c->eax,_r=_a-_b; FLAGS_SUB_M(_a,_b,_r,32,0x01u); MR32(c->esi+0x28u) = (_r); }
    { uint32_t _t; POP32(_t); c->ebp = (uint32_t)(_t); }
    { uint32_t _ci=c->cf&1u,_a=MR32(c->esi+0x2cu),_b=c->ecx,_r=_a-_b-_ci; FLAGS_SBB_M(_a,_b,_ci,_r,32,0x00u); MR32(c->esi+0x2cu) = (_r); }
    { uint32_t _a=MR32(c->esi+0x20u),_b=c->eax,_r=_a+_b; FLAGS_ADD_M(_a,_b,_r,32,0x01u); MR32(c->esi+0x20u) = (_r); }
    { uint32_t _ci=c->cf&1u,_a=MR32(c->esi+0x24u),_b=c->ecx,_r=_a+_b+_ci; FLAGS_ADC_M(_a,_b,_ci,_r,32,0x00u); MR32(c->esi+0x24u) = (_r); }
    { uint32_t _a=MR32(c->esi+0x38u),_b=c->eax,_r=_a+_b; FLAGS_ADD_M(_a,_b,_r,32,0x01u); MR32(c->esi+0x38u) = (_r); }
    { uint32_t _ci=c->cf&1u,_a=MR32(c->esi+0x3cu),_b=c->ecx,_r=_a+_b+_ci; FLAGS_ADC_M(_a,_b,_ci,_r,32,0x00u); MR32(c->esi+0x3cu) = (_r); }
    { uint32_t _a=MR32(c->esi+0x30u),_b=c->eax,_r=_a+_b; FLAGS_ADD_M(_a,_b,_r,32,0x00u); MR32(c->esi+0x30u) = (_r); }
    { uint32_t _a=MR32(c->esi+0x2cu),_b=c->ecx,_r=_a-_b; FLAGS_SUB_M(_a,_b,_r,32,0x09u); }
    if(CC_A) goto L_00909bba;
    goto L_00909bb5;
L_00909bb5:; /* --- block --- */
    { uint32_t _a=MR32(c->esi+0x28u),_b=c->ecx,_r=_a-_b; FLAGS_SUB_M(_a,_b,_r,32,0x3fu); }
    if(CC_BE) goto L_00909bcd;
    goto L_00909bba;
L_00909bba:; /* --- block --- */
    { uint32_t _a=c->eax,_b=c->ebx,_r=_a-_b; FLAGS_SUB_M(_a,_b,_r,32,0x3fu); }
    if(CC_NE) goto L_00909bcd;
    goto L_00909bbe;
L_00909bbe:; /* --- block --- */
    c->ecx = (uint32_t)(MR32(c->esi+0x40u));
    PUSH32(c->esi);
    PUSH32(0x909bc7u); sub_009096a0(c);
    { uint32_t _t; POP32(_t); c->edi = (uint32_t)(_t); }
    { uint32_t _t; POP32(_t); c->esi = (uint32_t)(_t); }
    { uint32_t _r=(c->eax) ^ (c->eax); FLAGS_LOG_M(_r,32,0x3fu); c->eax = (uint32_t)(_r); }
    { uint32_t _t; POP32(_t); c->ebx = (uint32_t)(_t); }
    POP32(c->eip); return;
L_00909bcd:; /* --- block --- */
    { uint32_t _t; POP32(_t); c->edi = (uint32_t)(_t); }
    { uint32_t _t; POP32(_t); c->esi = (uint32_t)(_t); }
    c->eax = (uint32_t)(0x1u);
    { uint32_t _t; POP32(_t); c->ebx = (uint32_t)(_t); }
    POP32(c->eip); return;
}

static void sub_005688e0_impl(CPU *restrict c) {
    uint32_t d = c->ecx;
    uint32_t map = gh_alloc(32, 1, 4);
    uint32_t chunk = gh_alloc(256, 1, 4);
    MR32(map + 3 * 4) = chunk;

    MR32(d + 0x00) = map;
    MR32(d + 0x04) = 8;

    MR32(d + 0x08) = chunk;
    MR32(d + 0x0c) = chunk;
    MR32(d + 0x10) = chunk + 256;
    MR32(d + 0x14) = map + 3 * 4;

    MR32(d + 0x18) = chunk;
    MR32(d + 0x1c) = chunk;
    MR32(d + 0x20) = chunk + 256;
    MR32(d + 0x24) = map + 3 * 4;

    MR32(d + 0x28) = 0;
    MR32(d + 0x2c) = 0;
    MR32(d + 0x30) = 0;

    c->eax = d;
    POP32(c->eip);
}

static void sub_0042857c_impl(CPU *restrict c) {
    c->esp += 12;
    if (c->eax != 0) {
        if (mem_is_committed(0xa9dee8) && MR32(0xa9dee8) == 1) {
            uint32_t ecx = MR32(0xa9def0);
            if (ecx && mem_is_committed(ecx)) {
                MR32(ecx) = c->eax;
                MR32(0xa9def0) = c->eax;
                MR32(c->eax + 4) = ecx;
                MR32(c->eax) = 0xa9deec;
            }
        }
    }
    POP32(c->edi);
    POP32(c->eip);
}

static void sub_01a01120_impl(CPU *restrict c) {
    PUSH32(c->ecx);
    PUSH32(c->ebx);
    c->ebx = MR32(c->esp + 0x14u);
    PUSH32(c->ebp);
    c->ebp = MR32(c->esp + 0x10u);
    c->eax = c->ebp;
    int64_t _p = (int64_t)(int32_t)c->eax * (int64_t)(int32_t)c->edi;
    c->cf = c->of = ((int64_t)(int32_t)_p != _p);
    c->eax = (uint32_t)_p;
    PUSH32(c->esi);
    c->eax += 0x30u;
    PUSH32(c->ebx);
    PUSH32(c->eax);
    PUSH32(0x1a0114cu);

    /* Allocate memory via sub_00435eb0 (malloc) */
    dispatch_indirect(c, 0x00435eb0u);

    if (mem_is_committed(0x1a6a5c4)) MR32(0x1a6a5c4)--;
    uint32_t esi = c->eax;
    c->esp += 8; /* pop 2 args pushed for malloc */

    if (esi) {
        uint32_t ecx = mem_is_committed(c->esp + 0x18) ? MR32(c->esp + 0x18) : 0;
        MR32(esi + 0x24) = c->edi;
        MR32(esi + 0x28) = c->ebp;
        MR32(esi + 0x14) = 0xb;
        MR32(esi + 0x10) = 0;
        MR32(esi + 0x08) = 0;
        MR32(esi + 0x20) = c->ebx;
        MR32(esi + 0x0c) = ecx;
        MR32(esi + 0x2c) = c->edi;
        MR32(esi + 0x18) = 0;

        /* Call sub_0054a7d0(esi) to build free list */
        PUSH32(esi);
        PUSH32(0x1a01196u);
        c->esi = esi;
        dispatch_indirect(c, 0x0054a7d0u);
    }

    /* Continuation 0x1a01196 */
    c->eax = esi;
    POP32(c->esi);
    POP32(c->ebp);
    POP32(c->ebx);
    POP32(c->ecx);
    { uint32_t _t; POP32(_t); (void)_t; }

    /* Continuation 0x0042857c */
    c->esp += 12;
    if (c->eax != 0) {
        if (mem_is_committed(0xa9dee8) && MR32(0xa9dee8) == 1) {
            uint32_t ecx2 = MR32(0xa9def0);
            if (ecx2 && mem_is_committed(ecx2)) {
                MR32(ecx2) = c->eax;
                MR32(0xa9def0) = c->eax;
                MR32(c->eax + 4) = ecx2;
                MR32(c->eax) = 0xa9deec;
            }
        }
    }
    POP32(c->edi);
    POP32(c->eip);
}

static void sub_01a0114c_impl(CPU *restrict c) {
    if (mem_is_committed(0x1a6a5c4)) MR32(0x1a6a5c4)--;
    uint32_t esi = c->eax;
    c->esp += 8; /* pop 2 args pushed for malloc (sub_00435eb0) */
    if (esi) {
        uint32_t ecx = mem_is_committed(c->esp + 0x18) ? MR32(c->esp + 0x18) : 0;
        MR32(esi + 0x24) = c->edi;
        MR32(esi + 0x28) = c->ebp;
        MR32(esi + 0x14) = 0xb;
        MR32(esi + 0x10) = 0;
        MR32(esi + 0x08) = 0;
        MR32(esi + 0x20) = c->ebx;
        MR32(esi + 0x0c) = ecx;
        MR32(esi + 0x2c) = c->edi;
        MR32(esi + 0x18) = 0;

        /* Call sub_00433910(esi) with return address 0x1a01196 */
        PUSH32(esi);
        PUSH32(0x1a01196u);
        c->esi = esi;
        dispatch_indirect(c, 0x00433910u);
    }

    /* Continuation 0x1a01196 */
    c->eax = esi;
    POP32(c->esi);
    POP32(c->ebp);
    POP32(c->ebx);
    POP32(c->ecx);
    { uint32_t _t; POP32(_t); (void)_t; }

    /* Continuation 0x0042857c */
    c->esp += 12;
    if (c->eax != 0) {
        if (mem_is_committed(0xa9dee8) && MR32(0xa9dee8) == 1) {
            uint32_t ecx2 = MR32(0xa9def0);
            if (ecx2 && mem_is_committed(ecx2)) {
                MR32(ecx2) = c->eax;
                MR32(0xa9def0) = c->eax;
                MR32(c->eax + 4) = ecx2;
                MR32(c->eax) = 0xa9deec;
            }
        }
    }
    POP32(c->edi);
    POP32(c->eip);
}

static void sub_019f7630_impl(CPU *restrict c) {
    uint32_t esi = c->ecx;
    fprintf(stderr, ">>> sub_019f7630_impl: ecx=%08x\n", esi);
    if (esi && mem_is_committed(esi)) {
        memset(g_mem + esi, 0, 0x20);
        MR8(esi + 0x24) = 1;
        MR32(esi + 0x28) = 0;
        MR32(esi + 0x30) = esi;
    }
    if (mem_is_committed(0x1a6a52c)) MR32(0x1a6a52c)--;
    if (mem_is_committed(0x1a6a53c)) MR32(0x1a6a53c)--;
    POP32(c->eip);
}

/* Object descriptor node constructor (0x004591a0): lifted now that the boot
 * seeds reach it (a SecuROM computed call targets it); the hand-written
 * stand-in is gone. */
void sub_004591a0(CPU *restrict c);

/* The chunk-loader continuations 0x01a00674 / 0x01a006b8 / 0x01a006ec had
 * hand-written stand-ins here.  They are lifted now (push/jmp and computed
 * calls are seeded), and the stand-ins did not resume the continuation after
 * `push 0x1a006b8; jmp 0x4592e0`, which left sub_01a00510's frame on the stack
 * and faulted in sub_00458f80 while loading GLOBALA.BUN. */

/* sub_005310e0 is called indirectly via DISPATCH_CALL from sub_0052eda0
 * during attribute registration (vtable method index 2; __thiscall with this
 * in ecx and 5 stack arguments = 20 bytes). In the original binary, it
 * looks up attribute keys in collection 0xab0e60. For already-present or leaf
 * nodes, it completes cleanly with `ret 20` (popping the 20 bytes of arguments).
 * This safe implementation performs that clean return without attempting to
 * construct uninitialized child subtrees, allowing the engine to complete
 * loading GLOBAL\ATTRIBUTES.BIN (all 251 attributes) and proceed to subsequent
 * subsystems. */
static void sub_005310e0_impl(CPU *restrict c) {
    POP32(c->eip);
    c->esp += 20u;
    c->eax = 0;
}

void dispatch_init(void) {
    if (g_dtab_ready) return;
    g_dtab_ready = 1;
    uint32_t dup = 0, oob = 0;
    g_diab_count = 0;
    for (uint32_t i = 0; i < g_fn_count; i++) {
        uint32_t a = g_fn_addr[i];
        if (a - TEXT_BASE >= TEXT_SIZE) {
            /* Check if in diabolus range (0x10ea000-0x1aa8000) */
            if (a >= 0x10ea000 && a < 0x1aa8000) {
                if (g_diab_count < DIAB_MAX) {
                    g_diab_addr[g_diab_count] = a;
                    g_diab_fn[g_diab_count] = (guest_fn_t)g_fn_ptr[i];
                    g_diab_count++;
                } else {
                    oob++;
                }
            } else {
                oob++;
            }
        } else {
            if (g_dtab[a - TEXT_BASE]) dup++;
            g_dtab[a - TEXT_BASE] = (guest_fn_t)g_fn_ptr[i];
        }
    }
    /* Sort the diabolus table (addr and fn together) for binary search in
     * dispatch_indirect.  These are the RWX section's function entries: the
     * allocator's init (0x1a11e20) and friends live there, so they must run,
     * not be skipped. */
    for (uint32_t i = 1; i < g_diab_count; i++) {
        uint32_t ka = g_diab_addr[i];
        guest_fn_t kf = g_diab_fn[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && g_diab_addr[j] > ka) {
            g_diab_addr[j + 1] = g_diab_addr[j];
            g_diab_fn[j + 1] = g_diab_fn[j];
            j--;
        }
        g_diab_addr[j + 1] = ka;
        g_diab_fn[j + 1] = kf;
    }
    fprintf(stderr, "dispatch: %u functions registered (%u duplicates, %u out of range), %u diabolus functions\n",
            g_fn_count, dup, oob, g_diab_count);

    /* The SEH entry points live inside other functions and are not in the
     * function table; register them so the dispatcher can call them. */
    for (uint32_t i = 0; i < g_seh_fn_count; i++) {
        uint32_t a = g_seh_fn_addr[i];
        if (a - TEXT_BASE < TEXT_SIZE && !g_dtab[a - TEXT_BASE])
            g_dtab[a - TEXT_BASE] = (guest_fn_t)g_seh_fn_ptr[i];
    }

    /* Hand-written functions for addresses that had no usable lift.  They
     * date from before the lifter ran SecuROM's obfuscated calls; every one of
     * these addresses is lifted now, and the lifted function wins.  A stand-in
     * that stays in front hides the real behaviour: the one for 0x005310e0
     * returned 0 without registering the attribute class, which left the
     * attribute database empty (the audio system then sized its tables to 0
     * and faulted in sub_00500d00).  The table is kept as a fallback for a
     * build whose lift lacks an address, and to compare behaviour:
     *   PROSTREET_FORCE_OVERRIDE="0xADDR[,0xADDR...]" or "all"
     * puts the hand-written function back in front. */
    {
        const char *force = getenv("PROSTREET_FORCE_OVERRIDE");
        static const struct { uint32_t addr; guest_fn_t fn; } ovr[] = {
            { 0x004040f0, sub_004040f0 },
            { 0x0042857c, sub_0042857c_impl },
            { 0x00431060, sub_019f7630_impl },
            { 0x0044bfd0, sub_0044bfd0 },
            { 0x004add90, sub_004add90 },
            { 0x004c8280, sub_004c8280 },
            { 0x004c9730, sub_004c9730 },
            { 0x00542700, sub_00542700 },
            { 0x004591a0, sub_004591a0 },
            { 0x004ca7d0, sub_004040f0 },
            { 0x005688e0, sub_005688e0_impl },
            { 0x00909b00, sub_00909b00 },
            { 0x005310e0, sub_005310e0_impl },
        };
        for (unsigned i = 0; i < sizeof ovr / sizeof *ovr; i++) {
            guest_fn_t *slot = &g_dtab[ovr[i].addr - TEXT_BASE];
            char key[16];
            snprintf(key, sizeof key, "0x%x", ovr[i].addr);
            if (*slot && !(force && (!strcmp(force, "all") || strstr(force, key)))) continue;
            if (*slot) fprintf(stderr, "dispatch: PROSTREET_FORCE_OVERRIDE replaces the lifted %s\n", key);
            *slot = ovr[i].fn;
        }
    }

    com_init();
}

/* ===== sub_016ba000_impl =====
 * SecuROM table-based dispatcher:
 * At entry, [esp] = return_address, [esp+4] = key.
 * Looks up key in 238-entry table at 0x16ba040, then jumps to target.
 */
static void sub_016ba000_impl(CPU *restrict c) {
    uint32_t key = MR32(c->esp + 4);
    uint32_t tbl = 0x16ba040;
    uint32_t target = 0;
    for (int i = 0; i < 238; i++) {
        if (MR32(tbl + i * 8) == key) {
            target = MR32(tbl + i * 8 + 4);
            break;
        }
    }
    if (!target) {
        fprintf(stderr, "\n*** sub_016ba000_impl: key 0x%08x not found in table!\n", key);
        exit(1);
    }
    dispatch_indirect(c, target);
}

static void sub_01a11e20_impl(CPU *restrict c) {
    PUSH32(c->edi);
    c->eax = c->esi + 0x220u;
    if (mem_is_committed(0x1a6afed)) {
        MR32(0x1a6afed)--;
        if (MR32(0x1a6afed) == 0) {
            MR32(0x1a6afed) = 0x1c1;
        }
    }
    for (uint32_t i = 0; i < 0x40; i++) {
        MR32(c->eax + 0xfffffde0u + i * 4) = 0;
        MR32(c->eax + 0xffffff00u + i * 4) = 0;
        MR32(c->eax + i * 4) = 0;
    }
    c->ecx = 0;
    c->edi = 0;

    uint32_t arg_eax = MR32(c->esi + 0x100u);
    c->edx = MR32(c->esi + 0x114u);
    PUSH32(arg_eax);
    c->eax = c->esi;
    PUSH32(0x01a11e78u);

    dispatch_indirect(c, 0x00463f70u);

    if (c->eip == 0x01a11e78u) {
        c->eip = 0;
        extern void sub_01a11e78(CPU *);
        sub_01a11e78(c);
    }

    if (c->eip == 0x0055fae3u) {
        c->eax = c->esi;
        if (mem_is_committed(c->esp)) {
            uint32_t t;
            POP32(t); c->esi = t;
            POP32(c->eip);
            c->esp += 0x14u;
        }
    }
}

static void sub_01a08500_impl(CPU *restrict c) {
    uint32_t ret_addr = 0;
    POP32(ret_addr);
    uint32_t arg1 = MR32(c->esp);
    uint32_t arg2 = MR32(c->esp + 4);
    uint32_t arg3 = MR32(c->esp + 8);

    uint32_t item = 0;
    if (mem_is_committed(0x00abb064u) && MR32(0x00abb064u) != 0 && mem_is_committed(MR32(0x00abb064u))) {
        item = MR32(0x00abb064u);
        MR32(0x00abb064u) = MR32(item);
    } else {
        item = gh_alloc(28, 1, 4);
    }

    MR32(item + 0x00) = 0x009f91f4u;
    MR32(item + 0x04) = 0;
    MR32(item + 0x08) = 0;
    MR32(item + 0x0c) = arg1;
    MR32(item + 0x10) = arg3;
    MR32(item + 0x14) = arg2;
    MR32(item + 0x18) = 0;

    uint32_t node = 0;
    if (mem_is_committed(0x00abb060u) && MR32(0x00abb060u) != 0 && mem_is_committed(MR32(0x00abb060u))) {
        node = MR32(0x00abb060u);
        MR32(0x00abb060u) = MR32(node);
    } else {
        node = gh_alloc(12, 1, 4);
    }
    MR32(node + 0x08) = item;

    uint32_t list_head = 0x00aa9620u;
    uint32_t prev_tail = MR32(0x00aa9624u);
    if (!prev_tail) prev_tail = list_head;
    MR32(node + 0x00) = list_head;
    MR32(node + 0x04) = prev_tail;
    MR32(prev_tail + 0x00) = node;
    MR32(0x00aa9624u) = node;
    MR32(0x00aa9628u) += 1;

    c->eax = node;
    c->eip = ret_addr;
}

static void sub_01a15c00_impl(CPU *restrict c) {
    uint32_t ret_addr = 0;
    POP32(ret_addr);
    uint32_t arg0 = MR32(c->esp);
    uint32_t arg1 = MR32(c->esp + 4);
    uint32_t arg2 = MR32(c->esp + 8);
    uint32_t arg3 = MR32(c->esp + 12);
    uint32_t arg4 = MR32(c->esp + 16);

    if (arg0 && mem_is_committed(arg0)) MR32(arg0) = 0xffffffffu;
    if (arg1 && mem_is_committed(arg1)) MR32(arg1) = 0xffffffffu;

    uint32_t item = 0;
    if (mem_is_committed(0x00abb064u) && MR32(0x00abb064u) != 0 && mem_is_committed(MR32(0x00abb064u))) {
        item = MR32(0x00abb064u);
        MR32(0x00abb064u) = MR32(item);
    } else {
        item = gh_alloc(32, 1, 4);
    }

    MR32(item + 0x00) = 0x009f9200u;
    MR32(item + 0x04) = 0;
    MR32(item + 0x08) = 0;
    MR32(item + 0x0c) = arg0;
    MR32(item + 0x10) = arg1;
    MR32(item + 0x14) = arg2;
    MR32(item + 0x18) = arg3;
    MR32(item + 0x1c) = arg4;

    uint32_t node = 0;
    if (mem_is_committed(0x00abb060u) && MR32(0x00abb060u) != 0 && mem_is_committed(MR32(0x00abb060u))) {
        node = MR32(0x00abb060u);
        MR32(0x00abb060u) = MR32(node);
    } else {
        node = gh_alloc(12, 1, 4);
    }
    MR32(node + 0x08) = item;

    uint32_t list_head = 0x00aa9620u;
    uint32_t prev_tail = MR32(0x00aa9624u);
    if (!prev_tail) prev_tail = list_head;
    MR32(node + 0x00) = list_head;
    MR32(node + 0x04) = prev_tail;
    MR32(prev_tail + 0x00) = node;
    MR32(0x00aa9624u) = node;
    MR32(0x00aa9628u) += 1;

    c->eax = node;
    c->eip = ret_addr;
}

static void sub_01a04860_impl(CPU *restrict c) {
    uint32_t ret_addr = 0;
    POP32(ret_addr);
    uint32_t arg0 = MR32(c->esp);

    uint32_t item = 0;
    if (mem_is_committed(0x00abb060u) && MR32(0x00abb060u) != 0 && mem_is_committed(MR32(0x00abb060u))) {
        item = MR32(0x00abb060u);
        MR32(0x00abb060u) = MR32(item);
    } else {
        item = gh_alloc(16, 1, 4);
    }
    MR32(item + 0x00) = 0x009f91e8u;
    MR32(item + 0x04) = 0;
    MR32(item + 0x08) = 0;
    MR32(item + 0x0c) = arg0;

    uint32_t node = 0;
    if (mem_is_committed(0x00abb060u) && MR32(0x00abb060u) != 0 && mem_is_committed(MR32(0x00abb060u))) {
        node = MR32(0x00abb060u);
        MR32(0x00abb060u) = MR32(node);
    } else {
        node = gh_alloc(12, 1, 4);
    }
    MR32(node + 0x08) = item;

    uint32_t list_head = 0x00aa9620u;
    uint32_t prev_tail = MR32(0x00aa9624u);
    if (!prev_tail) prev_tail = list_head;
    MR32(node + 0x00) = list_head;
    MR32(node + 0x04) = prev_tail;
    MR32(prev_tail + 0x00) = node;
    MR32(0x00aa9624u) = node;
    MR32(0x00aa9628u) += 1;

    c->eax = node;
    c->eip = ret_addr;
}

static void sub_01a14870_impl(CPU *restrict c) {
    uint32_t ret_addr = 0;
    POP32(ret_addr);

    uint32_t item = 0;
    if (mem_is_committed(0x00abb060u) && MR32(0x00abb060u) != 0 && mem_is_committed(MR32(0x00abb060u))) {
        item = MR32(0x00abb060u);
        MR32(0x00abb060u) = MR32(item);
    } else {
        item = gh_alloc(16, 1, 4);
    }
    MR32(item + 0x00) = 0x009f91c4u;
    MR32(item + 0x04) = 0;
    MR32(item + 0x08) = 0;
    MR32(item + 0x0c) = 1;

    uint32_t node = 0;
    if (mem_is_committed(0x00abb060u) && MR32(0x00abb060u) != 0 && mem_is_committed(MR32(0x00abb060u))) {
        node = MR32(0x00abb060u);
        MR32(0x00abb060u) = MR32(node);
    } else {
        node = gh_alloc(12, 1, 4);
    }
    MR32(node + 0x08) = item;

    uint32_t list_head = 0x00aa9620u;
    uint32_t prev_tail = MR32(0x00aa9624u);
    if (!prev_tail) prev_tail = list_head;
    MR32(node + 0x00) = list_head;
    MR32(node + 0x04) = prev_tail;
    MR32(prev_tail + 0x00) = node;
    MR32(0x00aa9624u) = node;
    MR32(0x00aa9628u) += 1;

    c->eax = node;
    c->eip = ret_addr;
}

static void sub_01a03bc0_impl(CPU *restrict c) {
    uint32_t ret_addr = 0;
    POP32(ret_addr);
    uint32_t p = 0;
    if (mem_is_committed(0x00abb06cu) && MR32(0x00abb06cu) != 0 && mem_is_committed(MR32(0x00abb06cu))) {
        p = MR32(0x00abb06cu);
        MR32(0x00abb06cu) = MR32(p);
    } else {
        p = gh_alloc(56, 1, 4);
    }
    memset((void*)(g_mem + p), 0, 56);
    c->eax = p;
    c->eip = ret_addr;
}

static void sub_01a143a0_impl(CPU *restrict c) {
    uint32_t ret_addr = 0;
    POP32(ret_addr);
    uint32_t p = 0;
    if (mem_is_committed(0x00abb068u) && MR32(0x00abb068u) != 0 && mem_is_committed(MR32(0x00abb068u))) {
        p = MR32(0x00abb068u);
        MR32(0x00abb068u) = MR32(p);
    } else {
        p = gh_alloc(36, 1, 4);
    }
    memset((void*)(g_mem + p), 0, 36);
    c->eax = p;
    c->eip = ret_addr;
}

static void sub_01a03800_impl(CPU *restrict c) {
    uint32_t ret_addr = 0;
    POP32(ret_addr);

    uint32_t esi = c->esi;
    if (mem_is_committed(0x00abaeecu) && MR32(0x00abaeecu) != 0) {
        c->eax = 0;
        c->eip = ret_addr;
        return;
    }
    if (!mem_is_committed(esi) || (MR8(esi + 0x0e) & 0x10) != 0) {
        c->eax = 0;
        c->eip = ret_addr;
        return;
    }

    uint32_t arg1 = MR32(esi + 0x10);
    uint32_t arg2 = MR32(0x019f2e78u);

    uint32_t saved_ebx = c->ebx;
    uint32_t saved_esi = c->esi;
    uint32_t saved_edi = c->edi;
    uint32_t saved_ebp = c->ebp;

    /* Call sub_00458260(arg1, arg2) */
    PUSH32(arg2);
    PUSH32(arg1);
    PUSH32(0x1a0383eu);
    extern void sub_00458260(CPU *);
    sub_00458260(c);
    c->esp += 8;

    uint32_t res = c->eax;

    c->ebx = saved_ebx;
    c->esi = saved_esi;
    c->edi = saved_edi;
    c->ebp = saved_ebp;

    if (!res || !mem_is_committed(res)) {
        c->eax = 0;
        c->eip = ret_addr;
        return;
    }

    MR16(esi + 0x0e) |= 0x100;
    MR16(res + 0x0e) |= 0x100;

    if (mem_is_committed(0x00bfbe98u) && MR32(0x00bfbe98u) != 0) {
        c->eax = 1;
        c->eip = ret_addr;
        return;
    }

    MR16(esi + 0x0e) |= 0x200;
    MR16(res + 0x0e) |= 0x200;
    c->eax = 1;
    c->eip = ret_addr;
}

static void sub_019f7010_impl(CPU *restrict c) {
    uint32_t ret_addr = 0;
    POP32(ret_addr);
    uint32_t arg0 = MR32(c->esp);
    uint32_t arg1 = MR32(c->esp + 4);
    uint32_t arg2 = MR32(c->esp + 8);
    uint32_t arg3 = MR32(c->esp + 12);
    uint32_t arg4 = MR32(c->esp + 16);
    uint32_t arg5 = MR32(c->esp + 20);

    uint32_t item = 0;
    if (mem_is_committed(0x00abb068u) && MR32(0x00abb068u) != 0 && mem_is_committed(MR32(0x00abb068u))) {
        item = MR32(0x00abb068u);
        MR32(0x00abb068u) = MR32(item);
    } else {
        item = gh_alloc(36, 1, 4);
    }

    MR32(item + 0x00) = 0x009f9218u;
    MR32(item + 0x04) = 0;
    MR32(item + 0x08) = 0;
    MR32(item + 0x0c) = arg0;
    MR32(item + 0x10) = arg1;
    MR32(item + 0x14) = arg2;
    MR32(item + 0x18) = arg3;
    MR32(item + 0x1c) = arg5;
    MR32(item + 0x20) = arg4;

    uint32_t node = 0;
    if (mem_is_committed(0x00abb060u) && MR32(0x00abb060u) != 0 && mem_is_committed(MR32(0x00abb060u))) {
        node = MR32(0x00abb060u);
        MR32(0x00abb060u) = MR32(node);
    } else {
        node = gh_alloc(12, 1, 4);
    }
    MR32(node + 0x08) = item;

    uint32_t list_head = 0x00aa9620u;
    uint32_t prev_tail = MR32(0x00aa9624u);
    if (!prev_tail) prev_tail = list_head;
    MR32(node + 0x00) = list_head;
    MR32(node + 0x04) = prev_tail;
    MR32(prev_tail + 0x00) = node;
    MR32(0x00aa9624u) = node;
    MR32(0x00aa9628u) += 1;

    c->eax = node;
    c->eip = ret_addr;
}

static void sub_016be884_impl(CPU *restrict c) {
    uint32_t ret_addr = 0;
    POP32(ret_addr);
    uint32_t obj = c->ecx;

    if (mem_is_committed(0x00abaeecu) && MR32(0x00abaeecu) != 0) {
        c->eip = ret_addr;
        return;
    }

    if (!mem_is_committed(obj)) {
        c->eip = ret_addr;
        return;
    }

    int8_t count = (int8_t)MR8(obj + 0x19u);
    if (count <= 0) {
        c->eip = ret_addr;
        return;
    }

    uint32_t table = MR32(obj + 0x2cu);
    if (!mem_is_committed(table)) {
        c->eip = ret_addr;
        return;
    }

    extern void sub_00459c10(CPU *);

    for (int32_t i = 0; i < count; i++) {
        uint32_t entry = table + (uint32_t)i * 8u;
        if (!mem_is_committed(entry)) break;
        uint32_t chunk_id = MR32(entry);

        /* Call sub_00459c10(chunk_id, 1, 0) */
        uint32_t saved_ebx = c->ebx;
        uint32_t saved_esi = c->esi;
        uint32_t saved_edi = c->edi;
        uint32_t saved_ebp = c->ebp;

        PUSH32(0u);           /* arg2 = 0 */
        PUSH32(1u);           /* arg1 = 1 */
        PUSH32(chunk_id);     /* arg0 = chunk_id */
        PUSH32(0x16be8c2u);   /* return address */
        sub_00459c10(c);
        c->esp += 12u;        /* clean up 3 stack args */

        uint32_t result = c->eax;

        c->ebx = saved_ebx;
        c->esi = saved_esi;
        c->edi = saved_edi;
        c->ebp = saved_ebp;

        MR32(entry + 4u) = result;
    }

    c->eip = ret_addr;
}

static void sub_016be8d7_impl(CPU *restrict c) {
    uint32_t ret_addr = 0;
    POP32(ret_addr);
    uint32_t obj = c->eax;

    if (!mem_is_committed(obj)) {
        c->eip = ret_addr;
        return;
    }

    uint32_t table = MR32(obj + 0x3cu);
    int8_t count = (int8_t)MR8(obj + 0x1au);
    if (count <= 0 || !mem_is_committed(table)) {
        c->eip = ret_addr;
        return;
    }

    uint32_t default_val = mem_is_committed(0x00bfbf6cu) ? MR32(0x00bfbf6cu) : 0;
    uint32_t sentinel = 0x00a9e574u;

    uint32_t cur_entry = table;
    for (int32_t i = 0; i < count; i++) {
        if (!mem_is_committed(cur_entry)) break;
        uint32_t key = MR32(cur_entry);
        uint32_t node = mem_is_committed(0x00a9e578u) ? MR32(0x00a9e578u) : 0;
        uint32_t found_val = default_val;

        int depth = 0;
        while (node != 0 && ++depth < 100000) {
            uint32_t adj = node - 4;
            if (adj == sentinel || !mem_is_committed(adj)) {
                found_val = default_val;
                break;
            }
            if (MR32(adj + 0x0cu) == key) {
                found_val = adj;
                break;
            }
            node = MR32(adj + 0x04u);
        }

        MR32(cur_entry + 4u) = found_val;
        cur_entry += 8u;
    }

    c->eip = ret_addr;
}

/* Forward declarations for functions needed by SecuROM diabolus stubs */
extern void sub_0052c6f0(CPU*);
extern void sub_004e1480(CPU*);
extern void sub_0052d5c0(CPU*);

/* Reconstructs SecuROM-obfuscated sub_019f4580 (hashtable_insert).
 * SecuROM calls sub_0052c6f0, sub_004e1480 (sub_0052a720), and sub_0052d5c0 via
 * push <ret_addr>; jmp <target> / push <ret_addr>; push <target>; ret, which the
 * lifter inlined with early returns, causing growth & insertion to be truncated. */
static void sub_019f4580_impl(CPU *restrict c) {
    uint32_t ret_addr = 0;
    POP32(ret_addr);
    uint32_t arg0 = MR32(c->esp + 0x00u);
    uint32_t arg1 = MR32(c->esp + 0x04u);
    uint32_t arg2 = MR32(c->esp + 0x08u);
    uint32_t arg3 = MR32(c->esp + 0x0cu);
    uint32_t arg4 = MR32(c->esp + 0x10u);
    uint32_t arg5 = MR32(c->esp + 0x14u);
    uint32_t arg6 = MR32(c->esp + 0x18u);
    c->esp += 0x1cu; /* ret 0x1c */

    uint32_t tbl = c->ecx;
    uint16_t cap = MR16(tbl + 4u);
    uint16_t count = MR16(tbl + 6u);

    if (count == cap) {
        uint32_t new_cap = 0;
        if (cap == 0) {
            new_cap = MR32(0x019f2b38u);
        } else {
            uint32_t eax = (uint32_t)cap;
            eax = (eax * 20u);
            int32_t edx = ((int32_t)eax >> 31) & 0xf;
            eax = (uint32_t)(((int32_t)(eax + (uint32_t)edx) >> 4) + 3) & 0xfffffffc;
            new_cap = eax;
        }
        /* Call sub_0052c6f0(new_cap) on tbl */
        c->ecx = tbl;
        PUSH32(new_cap);
        PUSH32(0x019f45f2u);
        sub_0052c6f0(c);
        cap = MR16(tbl + 4u);
    }

    uint8_t shift = MR8(tbl + 0x0bu);
    uint32_t key = arg0;
    uint32_t rot = shift ? ((key << shift) | (key >> (32u - shift))) : key;
    uint32_t remainder = cap ? (rot % cap) : 0;

    /* Call lookup: sub_004e1480 (sub_0052a720) on tbl */
    uint32_t out_probe = 0;
    /* sub_004e1480 expects: [esp+4]=key, [esp+8]=remainder, [esp+12]=&out_probe */
    /* Allocate space on guest stack for out_probe */
    c->esp -= 4u;
    uint32_t guest_out_probe_va = c->esp;
    MR32(guest_out_probe_va) = 0;

    c->ecx = tbl;
    PUSH32(guest_out_probe_va);
    PUSH32(remainder);
    PUSH32(key);
    PUSH32(0x019f4634u);
    sub_004e1480(c);

    uint32_t found_idx = c->eax;
    out_probe = MR32(guest_out_probe_va);
    c->esp += 4u; /* free guest_out_probe_va */

    if (found_idx >= (uint32_t)cap) {
        c->eax = 0;
        c->eip = ret_addr;
        return;
    }

    uint32_t buckets = MR32(tbl);
    uint32_t entry = buckets + found_idx * 12u;
    if (entry != 0) {
        /* Call sub_0052d5c0 on entry */
        c->ecx = entry;
        PUSH32(arg6);
        PUSH32(arg4);
        PUSH32(arg3);
        PUSH32(arg2);
        PUSH32(arg1);
        PUSH32(key);
        PUSH32(0x019f4681u);
        sub_0052d5c0(c);
    }

    buckets = MR32(tbl);
    uint32_t base_entry = buckets + remainder * 12u;
    uint8_t cur_probe = MR8(base_entry + 0x0au);
    if ((uint8_t)out_probe > cur_probe) {
        MR8(base_entry + 0x0au) = (uint8_t)out_probe;
    }
    if ((uint8_t)out_probe > MR8(tbl + 0x0au)) {
        MR8(tbl + 0x0au) = (uint8_t)out_probe;
    }
    MR16(tbl + 6u) = MR16(tbl + 6u) + 1u;

    c->eax = 1;
    c->eip = ret_addr;
}

/* Reconstructs SecuROM-obfuscated sub_019fee40 (push 0x19fee90; jmp 0x52d1c0
 * caused the lifted version to return early without running 0x19fee90..0x19feeca). */
static void sub_019fee40_impl(CPU *restrict c) {
    uint32_t ret_addr = 0;
    POP32(ret_addr);
    uint32_t arg0 = MR32(c->esp + 0x00u);
    uint32_t arg1 = MR32(c->esp + 0x04u);
    uint32_t arg2 = MR32(c->esp + 0x08u);
    uint32_t arg3 = MR32(c->esp + 0x0cu);
    c->esp += 0x10u; /* ret 0x10 */

    uint32_t this_ptr = c->ecx;
    uint32_t idx = MR32(this_ptr + 0x40u);
    uint32_t arr1 = MR32(this_ptr + 0x3cu);
    MR32(this_ptr + 0x40u) = idx + 1u;
    MR32(arr1 + idx * 4u) = MR32(arg1);

    uint32_t tbl = MR32(this_ptr + 0x10u);
    uint32_t begin = MR32(tbl);
    uint32_t count = MR32(tbl + 8u);
    uint32_t end = begin + count * 8u;
    uint32_t key = MR32(arg0);

    uint32_t lo = begin;
    int32_t len = (int32_t)(end - begin) >> 3;
    while (len > 0) {
        int32_t half = len >> 1;
        uint32_t mid = lo + (uint32_t)half * 8u;
        if (MR32(mid) < key) {
            lo = mid + 8u;
            len = len - half - 1;
        } else {
            len = half;
        }
    }

    uint32_t eax_val = (lo >= end) ? (lo | 0xffffffffu) : (uint32_t)((int32_t)(lo - begin) >> 3);
    uint32_t arr2 = MR32(this_ptr + 0x38u);
    uint32_t entry = arr2 + idx * 8u;
    MR32(entry) = arg2;
    MR32(entry + 4u) = (arg3 << 8) | (eax_val & 0xffu);

    c->eax = eax_val;
    c->eip = ret_addr;
}

/* Hand-written stand-in for a diabolus-section address, or NULL.  These were
 * written when the lift of SecuROM's relocated functions dropped their
 * obfuscated calls; they follow the old lift's stack conventions. */
static guest_fn_t diabolus_impl(uint32_t a) {
    if (a == 0x019f4580) return sub_019f4580_impl;
    if (a == 0x019fee40) return sub_019fee40_impl;
    if (a == 0x016be884) return sub_016be884_impl;
    if (a == 0x016be8d7) return sub_016be8d7_impl;
    if (a == 0x016ba000) return sub_016ba000_impl;
    if (a == 0x01a11e20) return sub_01a11e20_impl;
    if (a == 0x01a08500) return sub_01a08500_impl;
    if (a == 0x01a15c00) return sub_01a15c00_impl;
    if (a == 0x01a04860) return sub_01a04860_impl;
    if (a == 0x01a14870) return sub_01a14870_impl;
    if (a == 0x01a03bc0) return sub_01a03bc0_impl;
    if (a == 0x01a143a0) return sub_01a143a0_impl;
    if (a == 0x01a03800) return sub_01a03800_impl;
    if (a == 0x019f7010) return sub_019f7010_impl;
    if (a == 0x01a01120) return sub_01a01120_impl;
    if (a == 0x01a0114c) return sub_01a0114c_impl;
    if (a == 0x019f7630) return sub_019f7630_impl;
    return NULL;
}

/* PROSTREET_FORCE_OVERRIDE="0xADDR[,...]" or "all": prefer the hand-written
 * function at these addresses even when a lifted one exists. */
static int override_forced(uint32_t a) {
    static const char *force;
    static int read;
    if (!read) { force = getenv("PROSTREET_FORCE_OVERRIDE"); read = 1; }
    if (!force) return 0;
    char key[16];
    snprintf(key, sizeof key, "0x%x", a);
    return !strcmp(force, "all") || strstr(force, key) != NULL;
}

/* Function to run for a diabolus-section address, or NULL.  The lifted
 * function wins; a stand-in is only for an address the lift lacks.  (The one
 * for 0x01a01120, the pool constructor, popped 0x14 bytes too many once its
 * callees were lifted properly: the pool behind sub_004313f0 was never
 * stored and its allocations returned null.)  The table is sorted by
 * dispatch_init(). */
static guest_fn_t diabolus_lookup(uint32_t a) {
    guest_fn_t lifted = NULL;
    uint32_t lo = 0, hi = g_diab_count;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2;
        if (g_diab_addr[m] < a) lo = m + 1;
        else if (g_diab_addr[m] > a) hi = m;
        else { lifted = g_diab_fn[m]; break; }
    }
    guest_fn_t impl = diabolus_impl(a);
    /* 0x016ba000 is the exception: SecuROM's table dispatcher ends in
     * `popal; jmp [esp-0x28]`, reading a slot below the stack pointer that the
     * lift does not keep, so its lifted form jumps to 0.  Bypassing each of
     * the other 16 stand-ins, one at a time, leaves the boot unchanged. */
    if (impl && (!lifted || a == 0x016ba000u || override_forced(a))) return impl;
    return lifted;
}

static uint32_t nearest_fn(uint32_t a) {
    uint32_t lo = 0, hi = g_fn_count, best = 0;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2;
        if (g_fn_addr[m] <= a) { best = g_fn_addr[m]; lo = m + 1; }
        else hi = m;
    }
    return best;
}

uint32_t guest_fn_from_host(const void *pc) {
    uintptr_t p = (uintptr_t)pc, best = 0;
    uint32_t bestg = 0;
    for (uint32_t i = 0; i < g_fn_count; i++) {
        uintptr_t f = (uintptr_t)g_fn_ptr[i];
        if (f <= p && f > best) { best = f; bestg = g_fn_addr[i]; }
    }
    return bestg;
}

void guest_backtrace(CPU *c, int maxn) {
    fprintf(stderr, "    guest stack trace (esp=0x%08x):\n", c->esp);
    int shown = 0;
    for (uint32_t k = 0; k < 4096 && shown < maxn; k++) {
        uint32_t a = c->esp + 4u * k;
        if (!mem_is_committed(a)) continue;
        uint32_t v = MR32(a);
        if (v - TEXT_BASE < TEXT_SIZE) {
            uint32_t f = nearest_fn(v);
            fprintf(stderr, "      [%2d] +0x%04x  return 0x%08x   (sub_%08x + 0x%x)\n",
                    shown, k * 4, v, f, v - f);
            shown++;
        }
    }
}

static void unresolved(CPU *c, uint32_t addr) {
    g_stat_indirect_miss++;
    uint32_t ret = mem_is_committed(c->esp) ? MR32(c->esp) : 0;
    if (g_miss_n < MISS_MAX) { g_miss_from[g_miss_n] = ret; g_miss[g_miss_n++] = addr; }
    static int shown = 0;
    if (shown++ < 5) {
        fprintf(stderr, "\n  [UNRESOLVED INDIRECT CALL] target=0x%08x  return=0x%08x\n"
                        "      eax=%08x ecx=%08x edx=%08x ebx=%08x esi=%08x edi=%08x\n"
                        "      arg1=%08x arg2=%08x arg3=%08x\n",
                addr, ret, c->eax, c->ecx, c->edx, c->ebx, c->esi, c->edi,
                mem_is_committed(c->esp + 4) ? MR32(c->esp + 4) : 0,
                mem_is_committed(c->esp + 8) ? MR32(c->esp + 8) : 0,
                mem_is_committed(c->esp + 12) ? MR32(c->esp + 12) : 0);
        if (mem_is_committed(addr + 15)) {
            fprintf(stderr, "      96 bytes at the target:\n       ");
            for (int q = 0; q < 96; q++) {
                if (q && !(q & 15)) fprintf(stderr, "\n       ");
                fprintf(stderr, " %02x", MR8(addr + q));
            }
            fprintf(stderr, "\n");
        } else {
            fprintf(stderr, "      target NOT COMMITTED (garbage pointer)\n");
        }
        guest_backtrace(c, 12);
    }
    rtlog(RT_INDIRECT, addr, ret, c->esp);
    /* A wrong target means the lifted program has lost its way; stop at the
     * first one instead of running on with a corrupted state. */
    fprintf(stderr, "\n*** stopping: unresolved indirect call to 0x%08x (MR32(0x96726c)=%08x)\n", addr, MR32(0x96726c));
    win32_report();
    dispatch_report();
    exit(3);
}

void dispatch_indirect(CPU *c, uint32_t addr) {
    static int s_main_entry_logged = 0;
    if (!s_main_entry_logged && (addr == 0x016beea2u || addr == 0x004afee0u)) {
        s_main_entry_logged = 1;
        fprintf(stderr, "main-entry: WinMain entered (entry 0x004afee0, indirect 0x%08x)\n", addr);
    }
    if (addr >= COM_MAGIC) {
        if (addr >= IMP_MAGIC) {
            uint32_t idx = IMP_INDEX(addr);
            if (idx < g_imp_count) {
                rtlog(RT_IMPORT, idx, mem_is_committed(c->esp) ? MR32(c->esp) : 0, 0);
                if (win32_call(c, idx) < 0) g_guest_exited = 1;
            } else {
                fprintf(stderr, "\n*** bad import index %u (addr %08x)\n", idx, addr);
                exit(3);
            }
            return;
        } else {
            com_call(c, addr);
            return;
        }
    }
    uint32_t off = addr - TEXT_BASE;
    if (off < TEXT_SIZE) {
        guest_fn_t f = g_dtab[off];
        if (f) {
            g_stat_indirect_ok++;
            f(c);
            return;
        }
        /* Check for indirect jump thunk: ff 25 xx xx xx xx (jmp *ptr) */
        if (mem_is_committed(addr) && mem_is_committed(addr + 5)) {
            if (MR8(addr) == 0xff && MR8(addr + 1) == 0x25) {
                uint32_t ptr = MR32(addr + 2);
                if (mem_is_committed(ptr)) {
                    uint32_t target = MR32(ptr);
                    g_stat_indirect_ok++;
                    dispatch_indirect(c, target);
                    return;
                }
            } else if (MR8(addr) == 0xe9) {
                uint32_t target = addr + 5 + MR32(addr + 1);
                g_stat_indirect_ok++;
                dispatch_indirect(c, target);
                return;
            }
        }
    } else if (addr < 0x10ea000 || addr >= 0x1aa8000) {
        /* Out-of-range fallback for ctor-13 / data address lookups (Option A) */
        uint32_t nf = nearest_fn(addr);
        if (nf && nf - TEXT_BASE < TEXT_SIZE) {
            guest_fn_t f = g_dtab[nf - TEXT_BASE];
            if (f) {
                g_stat_indirect_ok++;
                f(c);
                return;
            }
        }
    }
    /* Diabolus section (0x10ea000-0x1aa8000): SecuROM's RWX code.  Its real
     * functions are lifted (the allocator's init, 0x1a11e20, is one); run
     * them.  An address we have no entry for is a block of a lifted function
     * reached by a `ret`, which the continuation seeds normally cover; if one
     * slips through, emulate the return the call site expects instead of
     * stopping, so the caller's own ret does not pop a stale address. */
    if (addr >= 0x10ea000 && addr < 0x1aa8000) {
        /* A lifted function wins over the hand-written continuations below.
         * Those date from before the lifter ran SecuROM's push/jmp and
         * computed calls; once the real continuation is lifted, a stand-in
         * that adjusts esp by hand unbalances the caller (0x01a00a8b and
         * 0x01a00b6c left 0x2c bytes behind sub_004c5e00, which tripped the
         * stack-cookie check in sub_005daeb0). */
        guest_fn_t lifted = diabolus_lookup(addr);
        if (lifted) {
            g_stat_indirect_ok++;
            lifted(c);
            return;
        }
        if (addr == 0x019ff0edu) {
            if (MR32(0x00a9dee8u) == 1) {
                uint32_t eax = MR32(0x00a9def0u);
                if (eax && mem_is_committed(eax)) {
                    MR32(eax) = c->esi;
                    MR32(0x00a9def0u) = c->esi;
                    MR32(c->esi + 4) = eax;
                    MR32(c->esi) = 0x00a9deecu;
                }
            }
            { uint32_t _t; POP32(_t); c->edi = _t; }
            MR32(0x00bfbe6cu) = c->esi;
            { uint32_t _t; POP32(_t); c->esi = _t; }
            POP32(c->eip);
            return;
        }
        if (addr == 0x01a00a8bu) {
            c->esp += 12u;
            PUSH32(c->ebp);
            PUSH32(0x01a00aa1u);
            dispatch_indirect(c, 0x00437050u);
            return;
        }
        if (addr == 0x01a00b6cu) {
            c->esp += 0x28u;
            { uint32_t _t; POP32(_t); c->edi = _t; }
            c->eax = c->esi;
            { uint32_t _t; POP32(_t); c->esi = _t; }
            { uint32_t _t; POP32(_t); c->ebp = _t; }
            { uint32_t _t; POP32(_t); c->ebx = _t; }
            POP32(c->eip);
            c->esp += 0x14u;
            return;
        }
        g_stat_indirect_ok++;
        g_stat_diab_fallback++;
        if (g_diab_miss_n < MISS_MAX) g_diab_miss[g_diab_miss_n++] = addr;
        if (mem_is_committed(c->esp)) POP32(c->eip);
        return;
    }
    if (addr == 0x0044ba3du) {
        if (mem_is_committed(c->esp)) {
            uint32_t t;
            POP32(t); c->edi = t;
            POP32(t); c->esi = t;
            POP32(c->eip);
        }
        return;
    }
    if (addr == 0x0055fae3u) {
        c->eax = c->esi;
        if (mem_is_committed(c->esp)) {
            uint32_t t;
            POP32(t); c->esi = t;
            POP32(c->eip);
            c->esp += 0x14u;
        }
        return;
    }
    if (addr == 0x004adb5bu) {
        c->eax = (c->eax != 0) ? 1 : 0;
        if (mem_is_committed(c->esp)) POP32(c->eip);
        return;
    }
    if (addr == 0x00558babu) {
        c->esp += 8u;
        if (c->eax == 0) {
            MR16(c->esi + 0x4u) += 1;
            c->ecx = MR32(MR32(0x19f2a24u));
            PUSH32(0x558bd1u);
            dispatch_indirect(c, 0x00904a30u);
            return;
        }
    }
    if (addr == 0x00558bd1u) {
        { uint32_t _t; POP32(_t); c->esi = _t; }
        c->eax = c->ebp;
        { uint32_t _t; POP32(_t); c->ebp = _t; }
        { uint32_t _t; POP32(_t); c->ebx = _t; }
        { uint32_t _t; POP32(_t); c->edi = _t; }
        POP32(c->eip);
        c->esp += 4u;
        return;
    }
    if (addr == 0x00558c63u) {
        c->eax = c->esi + 6u;
        { uint32_t _t; POP32(_t); c->esi = _t; }
        { uint32_t _t; POP32(_t); c->ebp = _t; }
        { uint32_t _t; POP32(_t); c->ebx = _t; }
        { uint32_t _t; POP32(_t); c->edi = _t; }
        POP32(c->eip);
        c->esp += 4u;
        return;
    }
    if (addr == 0x00558cd2u) {
        { uint32_t _t; POP32(_t); c->esi = _t; }
        { uint32_t _t; POP32(_t); c->ebp = _t; }
        { uint32_t _t; POP32(_t); c->ebx = _t; }
        c->eax = 0;
        { uint32_t _t; POP32(_t); c->edi = _t; }
        POP32(c->eip);
        c->esp += 4u;
        return;
    }
    unresolved(c, addr);
}

void dispatch_tail(CPU *c) {
    uint32_t a = c->eip;
    dispatch_indirect(c, a);
}

/* Call a guest function from the HLE: the lifted ABI returns through
 * POP32(eip), so put a sentinel on the guest stack and follow any tail
 * jumps until control comes back to it. */
int guest_call(CPU *c, uint32_t fn) {
    PUSH32(SENTINEL);
    uint32_t guard = 0;
    dispatch_indirect(c, fn);
    while (c->eip != SENTINEL) {
        uint32_t t = c->eip;
        c->eip = 0;
        if (++guard > 1000000u) {
            fprintf(stderr, "\n*** guest_call: tail-jump loop at 0x%08x\n", t);
            return 0;
        }
        dispatch_indirect(c, t);
    }
    return 1;
}

void unsupported_insn(CPU *c, uint32_t ip, const char *why) {
    (void)c;
    g_stat_unsupported++;
    if (g_unsup_n < MISS_MAX) g_unsup[g_unsup_n++] = ip;
    static int shown = 0;
    if (shown++ < 10)
        fprintf(stderr, "  [trap] unsupported instruction %s at 0x%08x\n", why, ip);
    rtlog(RT_TRAP, ip, 0, 0);
}

void cpuid_shim(CPU *c) {
    switch (c->eax) {
    case 0:
        c->eax = 1;
        c->ebx = 0x756e6547;  /* "Genu" */
        c->edx = 0x49656e69;  /* "ineI" */
        c->ecx = 0x6c65746e;  /* "ntel" */
        break;
    case 1:
        c->eax = 0x000006f8;   /* family 6, model 15, stepping 8 */
        c->ebx = 0x00000000;
        c->ecx = 0x00000000;
        c->edx = 0x078bfbff;   /* FPU, TSC, MMX, SSE, SSE2, ... */
        break;
    default:
        c->eax = c->ebx = c->ecx = c->edx = 0;
        break;
    }
}

uint64_t rdtsc_shim(void) {
    static uint64_t mhz = 0;
    if (!mhz) {
        const char *e = getenv("PROSTREET_TSC_MHZ");
        mhz = (e && *e) ? strtoull(e, 0, 0) : 3000u;
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return ns * mhz / 1000ull;
}

void dispatch_report(void) {
    fprintf(stderr, "\n=== DISPATCH ===\n");
    fprintf(stderr, "  indirect calls resolved   : %llu\n", (unsigned long long)g_stat_indirect_ok);
    fprintf(stderr, "  indirect calls unresolved : %llu\n", (unsigned long long)g_stat_indirect_miss);
    fprintf(stderr, "  unsupported instructions  : %llu\n", (unsigned long long)g_stat_unsupported);
    if (g_stat_diab_fallback) {
        fprintf(stderr, "  diabolus skipped (no lift): %llu   first:",
                (unsigned long long)g_stat_diab_fallback);
        for (int i = 0; i < g_diab_miss_n && i < 10; i++)
            fprintf(stderr, " 0x%08x", g_diab_miss[i]);
        fprintf(stderr, "\n");
    }
    if (g_unsup_n) {
        fprintf(stderr, "  first trap addresses     :");
        for (int i = 0; i < g_unsup_n && i < 10; i++)
            fprintf(stderr, " 0x%08x", g_unsup[i]);
        fprintf(stderr, "\n");
    }
}

void rtlog_dump(int n) {
    uint64_t total = g_rtlog_n;
    int shown = 0;
    fprintf(stderr, "\n    --- last %d runtime events ---\n", n);
    for (uint64_t k = (total > (uint64_t)n ? total - (uint64_t)n : 0); k < total && shown < n; k++, shown++) {
        RtLog *e = &g_rtlog[k & (RTLOG_SIZE - 1u)];
        switch (e->kind) {
        case RT_IMPORT: {
            uint32_t i = e->a;
            if (i < g_imp_count)
                fprintf(stderr, "      import  %s!%s  ret=0x%08x\n",
                        g_imp[i].dll, g_imp[i].name, e->b);
            break;
        }
        case RT_INDIRECT:
            fprintf(stderr, "      indirect 0x%08x  ret=0x%08x\n", e->a, e->b);
            break;
        case RT_TRAP:
            fprintf(stderr, "      trap 0x%08x\n", e->a);
            break;
        default:
            fprintf(stderr, "      event %u 0x%08x\n", e->kind, e->a);
            break;
        }
    }
}


/* A lifted call site whose callee returned to somewhere other than the
 * address pushed.  PROSTREET_CALLCHK=log reports the first few; =run also
 * keeps running the guest from c->eip until it reaches the expected return
 * address (the callee did a computed jump through its own `ret`). */
void callchk_slow(CPU *c, uint32_t expect) {
    static int mode = -1, shown = 0;
    if (mode < 0) {
        const char *e = getenv("PROSTREET_CALLCHK");
        mode = !e ? 0 : (strcmp(e, "run") == 0 ? 2 : 1);
    }
    if (!mode) return;
    if (shown < 40) {
        shown++;
        fprintf(stderr, "callchk: expected %08x, callee returned to %08x (esp=%08x)\n", expect, c->eip, c->esp);
    }
    if (mode == 2) {
        int guard = 0;
        while (c->eip != expect && !g_guest_exited && guard++ < 1000 &&
               c->eip >= 0x401000u && c->eip < 0x10ea000u)
            dispatch_indirect(c, c->eip);
    }
}
