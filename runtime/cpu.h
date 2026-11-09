/* ProStreet x86-32 CPU state and guest-memory access.
 *
 * This is the contract between tools/lift.py and the generated C in
 * work/gen/.  Lifted code is never hand-edited, so the macro names, the CPU
 * field names and the dispatch entry points here are fixed: change them and
 * every shard stops compiling.
 *
 * Adapted from nfsu2-web's runtime/cpu.h (MIT, th0mk; see THIRD_PARTY.md).
 * The differences from that file are listed in runtime/README.md.
 */
#ifndef NFSPS_CPU_H
#define NFSPS_CPU_H
#include <math.h>
#include <stdint.h>
#include <string.h>

/* Guest memory base.  Native: 4 GiB is reserved PROT_NONE and the regions in
 * use are committed page by page, so an access to unmapped guest memory is a
 * diagnosable signal rather than corruption.
 * wasm: the guest address is the linear-memory address. */
#ifdef __wasm__
#define g_mem ((uint8_t *)0)
#define GUEST_END 0x80000000u
#define GSZ ((uint64_t)GUEST_END)
#else
extern uint8_t *g_mem;
#define GSZ 0x100000000ull
#endif

/* Guest memory is untyped: the game stores a float with fstp and reads the
 * same bytes back with mov (every by-value vector copy does), so an f32 and
 * an i32 view of one address are never assumed disjoint.  Plain float/
 * uint32_t lvalues would let the compiler's type-based alias analysis do
 * exactly that.  The attribute makes every MR* access share the character
 * type's alias set. */
#if defined(__GNUC__) || defined(__clang__)
typedef uint16_t __attribute__((may_alias)) gm_u16;
typedef uint32_t __attribute__((may_alias)) gm_u32;
typedef uint64_t __attribute__((may_alias)) gm_u64;
typedef float    __attribute__((may_alias)) gm_f32;
typedef double   __attribute__((may_alias)) gm_f64;
#else
typedef uint16_t gm_u16;
typedef uint32_t gm_u32;
typedef uint64_t gm_u64;
typedef float    gm_f32;
typedef double   gm_f64;
#endif

#define MR8(a)   (*(uint8_t *)(g_mem + (uint32_t)(a)))
#define MR16(a)  (*(gm_u16  *)(g_mem + (uint32_t)(a)))
#define MR32(a)  (*(gm_u32  *)(g_mem + (uint32_t)(a)))
#define MR64(a)  (*(gm_u64  *)(g_mem + (uint32_t)(a)))
#define MRF32(a) (*(gm_f32  *)(g_mem + (uint32_t)(a)))
#define MRF64(a) (*(gm_f64  *)(g_mem + (uint32_t)(a)))

enum { R_EAX=0,R_ECX,R_EDX,R_EBX,R_ESP,R_EBP,R_ESI,R_EDI };

/* An indirect or out-of-range jmp at the end of a lifted function: control
 * goes to the target and never comes back here. */
struct CPU;
void dispatch_indirect(struct CPU *c, uint32_t addr);
void dispatch_tail(struct CPU *c);

/* The dispatch table (dispatch.c): guest .text address -> lifted function.
 * An indirect call or jump in lifted code that finds its target there calls
 * it directly: one load and one call instead of a call into the dispatcher. */
#define DISPATCH_TEXT_BASE 0x00401000u
#define DISPATCH_TEXT_SIZE 0x00566000u
typedef void (*guest_fn_t)(struct CPU *);
extern guest_fn_t g_dtab[DISPATCH_TEXT_SIZE];
static inline guest_fn_t dispatch_fast(uint32_t a){
    uint32_t o = a - DISPATCH_TEXT_BASE;
    return o < DISPATCH_TEXT_SIZE ? g_dtab[o] : (guest_fn_t)0;
}
#define DISPATCH_CALL(c_, t_) do { uint32_t _dc = (uint32_t)(t_); guest_fn_t _df = dispatch_fast(_dc); \
    if (_df) _df(c_); else dispatch_indirect((c_), _dc); } while (0)
#ifdef __wasm__
#define DISPATCH_TAIL(c_, t_) { uint32_t _dc = (uint32_t)(t_); guest_fn_t _df = dispatch_fast(_dc); \
    if (_df) { __attribute__((musttail)) return _df(c_); } \
    (c_)->eip = _dc; __attribute__((musttail)) return dispatch_tail(c_); }
#else
#define DISPATCH_TAIL(c_, t_) { dispatch_indirect((c_), (uint32_t)(t_)); return; }
#endif

typedef struct CPU {
    union {
        uint32_t r[8];
        struct { uint32_t eax,ecx,edx,ebx,esp,ebp,esi,edi; };
    };
    uint32_t eip;
    uint32_t fs_base, gs_base;   /* TEB / TLS base (flat model) */
    /* flags - each kept as 0/1 (clang DCE drops the unused ones) */
    uint32_t cf,pf,af,zf,sf,of,df;
    /* x87 stack */
    double   st[8];
    int32_t  top;
    uint16_t fcw, fsw;
    /* MMX (not shared with x87, kept separately) */
    uint64_t mm[8];
    /* SSE */
    union { uint8_t b[16]; float f[4]; double d[2]; uint32_t u32[4]; uint64_t u64[2]; } xmm[8];
    /* EFLAGS system bits (preserved by pushfd/popfd): ID (21), AC (18).
     * Without them the game's "is CPUID there" test (it tries to flip the
     * ID bit) always fails, the CPU MHz is never measured and the first run
     * picks performance level 0. */
    uint32_t sysfl;
} CPU;

/* ---- registers in locals (tools/lregs.py) ----------------------------
 * A shard lifted with LREGS=1 defines NFSPS_LREGS: its functions keep the
 * general registers and the six arithmetic flags in locals and store them
 * to the CPU struct only around calls, returns, tail jumps and the helpers
 * that take the CPU.  RG_() names them.  ProStreet's lift does not use it
 * yet, but keeping the macros means the lifter option stays buildable. */
#ifdef NFSPS_LREGS
#define RG_(x) _r_##x
#else
#define RG_(x) c->x
#endif

/* ---- flag helpers ----------------------------------------------------- */
static inline uint32_t parity8(uint32_t v){ v&=0xff; v^=v>>4; v^=v>>2; v^=v>>1; return (~v)&1; }

/* The generator solves flag liveness over the control-flow graph and passes
 * each instruction a "flags read later" mask.  The mask is a compile-time
 * constant, so unused work is removed entirely. */
#define F_CF  0x01u
#define F_PF  0x02u
#define F_AF  0x04u
#define F_ZF  0x08u
#define F_SF  0x10u
#define F_OF  0x20u
#define F_ALL 0x3Fu

#define SETZSP_M(res,bits,M) do{ \
    if((M)&F_ZF) RG_(zf) = ((uint##bits##_t)(res)==0); \
    if((M)&F_SF) RG_(sf) = (((uint##bits##_t)(res))>>((bits)-1))&1; \
    if((M)&F_PF) RG_(pf) = parity8((uint32_t)(res)); }while(0)
#define SETZSP(res,bits) SETZSP_M(res,bits,F_ALL)

/* ADD */
#define FLAGS_ADD_M(a,b,res,bits,M) do{ SETZSP_M(res,bits,M); \
    if((M)&F_CF) RG_(cf) = ((uint##bits##_t)(res) < (uint##bits##_t)(a)); \
    if((M)&F_AF) RG_(af) = ((((a)^(b)^(res))>>4)&1); \
    if((M)&F_OF) RG_(of) = ((~((a)^(b)) & ((a)^(res)))>>((bits)-1))&1; }while(0)
#define FLAGS_ADD(a,b,res,bits) FLAGS_ADD_M(a,b,res,bits,F_ALL)
/* SUB / CMP */
#define FLAGS_SUB_M(a,b,res,bits,M) do{ SETZSP_M(res,bits,M); \
    if((M)&F_CF) RG_(cf) = ((uint##bits##_t)(a) < (uint##bits##_t)(b)); \
    if((M)&F_AF) RG_(af) = ((((a)^(b)^(res))>>4)&1); \
    if((M)&F_OF) RG_(of) = ((((a)^(b)) & ((a)^(res)))>>((bits)-1))&1; }while(0)
#define FLAGS_SUB(a,b,res,bits) FLAGS_SUB_M(a,b,res,bits,F_ALL)
/* ADC/SBB: with carry-in 1 the result can EQUAL the input; the (res<a)
 * test then misses the carry (sbb eax,eax CF=1 -> CF=1). */
#define FLAGS_ADC_M(a,b,cin,res,bits,M) do{ SETZSP_M(res,bits,M); \
    if((M)&F_CF) RG_(cf) = (cin) ? ((uint##bits##_t)(res) <= (uint##bits##_t)(a)) \
                               : ((uint##bits##_t)(res) <  (uint##bits##_t)(a)); \
    if((M)&F_AF) RG_(af) = ((((a)^(b)^(res))>>4)&1); \
    if((M)&F_OF) RG_(of) = ((~((a)^(b)) & ((a)^(res)))>>((bits)-1))&1; }while(0)
#define FLAGS_ADC(a,b,cin,res,bits) FLAGS_ADC_M(a,b,cin,res,bits,F_ALL)
#define FLAGS_SBB_M(a,b,cin,res,bits,M) do{ SETZSP_M(res,bits,M); \
    if((M)&F_CF) RG_(cf) = (cin) ? ((uint##bits##_t)(a) <= (uint##bits##_t)(b)) \
                               : ((uint##bits##_t)(a) <  (uint##bits##_t)(b)); \
    if((M)&F_AF) RG_(af) = ((((a)^(b)^(res))>>4)&1); \
    if((M)&F_OF) RG_(of) = ((((a)^(b)) & ((a)^(res)))>>((bits)-1))&1; }while(0)
#define FLAGS_SBB(a,b,cin,res,bits) FLAGS_SBB_M(a,b,cin,res,bits,F_ALL)
/* logical: CF=OF=0 */
#define FLAGS_LOG_M(res,bits,M) do{ SETZSP_M(res,bits,M); \
    if((M)&F_CF) RG_(cf)=0; if((M)&F_OF) RG_(of)=0; if((M)&F_AF) RG_(af)=0; }while(0)
#define FLAGS_LOG(res,bits) FLAGS_LOG_M(res,bits,F_ALL)

/* ---- stack ------------------------------------------------------------ */
/* The operand may depend on esp (push [esp+8], push esp).  x86 semantics:
 * PUSH reads the value first, then decrements esp. */
#define PUSH32(v) do{ uint32_t _pv = (uint32_t)(v); RG_(esp) -= 4; MR32(RG_(esp)) = _pv; }while(0)
/* POP: esp is incremented first; the destination uses the NEW esp. */
#define POP32(d)  do{ uint32_t _qv = MR32(RG_(esp)); RG_(esp) += 4; (d) = _qv; }while(0)

/* ---- conditions ------------------------------------------------------- */
#define CC_O   (RG_(of))
#define CC_NO  (!RG_(of))
#define CC_B   (RG_(cf))
#define CC_AE  (!RG_(cf))
#define CC_E   (RG_(zf))
#define CC_NE  (!RG_(zf))
#define CC_BE  (RG_(cf)||RG_(zf))
#define CC_A   (!RG_(cf)&&!RG_(zf))
#define CC_S   (RG_(sf))
#define CC_NS  (!RG_(sf))
#define CC_P   (RG_(pf))
#define CC_NP  (!RG_(pf))
#define CC_L   (RG_(sf)!=RG_(of))
#define CC_GE  (RG_(sf)==RG_(of))
#define CC_LE  (RG_(zf)||(RG_(sf)!=RG_(of)))
#define CC_G   (!(RG_(zf))&&(RG_(sf)==RG_(of)))


/* ---- x87 FPU stack ----------------------------------------------------
 * Host doubles stand in for 80-bit extended values; the difference is the
 * extended exponent range and precision, which this game's arithmetic does
 * not depend on. */
#define ST(n)    (c->st[(c->top + (n)) & 7])
/* The source value must be computed BEFORE top changes.  ST(i) is defined
 * relative to top, so "fld st(0)" has to read the old slot, not the newly
 * opened one. */
#define FPUSH(v) do{ double _fpv = (double)(v); \
    c->top = (c->top - 1) & 7; c->st[c->top] = _fpv; }while(0)
#define FPOP()   do{ c->top = (c->top + 1) & 7; }while(0)

/* x87 compare -> status word C0/C2/C3 bits (8/10/14) */
#define FSW_C0 (1u<<8)
#define FSW_C1 (1u<<9)
#define FSW_C2 (1u<<10)
#define FSW_C3 (1u<<14)
/* fnstsw: the TOP field (bits 11-13) is read as well. */
static inline uint16_t fsw_get(const struct CPU *c){
    return (uint16_t)((c->fsw & (uint16_t)~0x3800u) | (uint16_t)((c->top & 7u) << 11));
}
static inline uint16_t fsw_get_top(const struct CPU *c, uint32_t top){
    return (uint16_t)((c->fsw & (uint16_t)~0x3800u) | (uint16_t)((top & 7u) << 11));
}
static inline void fcompare(struct CPU *c, double a, double b){
    c->fsw &= (uint16_t)~(FSW_C0|FSW_C2|FSW_C3);
    if (a != a || b != b)      c->fsw |= FSW_C0|FSW_C2|FSW_C3;  /* unordered (NaN) */
    else if (a > b)            ;                                 /* all 0 */
    else if (a < b)            c->fsw |= FSW_C0;
    else                       c->fsw |= FSW_C3;
}
/* fcomi / fucomi: write EFLAGS directly */
#define FCOMI(a,b) do{ double _x=(a),_y=(b); \
    if(_x!=_x||_y!=_y){ RG_(zf)=1;RG_(pf)=1;RG_(cf)=1; } \
    else { RG_(pf)=0; RG_(zf)=(_x==_y); RG_(cf)=(_x<_y); } RG_(of)=RG_(sf)=RG_(af)=0; }while(0)

/* ---- EFLAGS pack/unpack (pushfd/popfd/sahf) --------------------------- */
static inline uint32_t eflags_get(const struct CPU *c){
    return 0x00000202u | (c->cf&1u) | ((c->pf&1u)<<2) | ((c->af&1u)<<4)
         | ((c->zf&1u)<<6) | ((c->sf&1u)<<7) | ((c->df&1u)<<10) | ((c->of&1u)<<11)
         | (c->sysfl & 0x00240000u);
}
static inline void eflags_set(struct CPU *c, uint32_t f){
    c->cf=f&1u; c->pf=(f>>2)&1u; c->af=(f>>4)&1u;
    c->zf=(f>>6)&1u; c->sf=(f>>7)&1u; c->df=(f>>10)&1u; c->of=(f>>11)&1u;
    c->sysfl = f & 0x00240000u;
}

/* ---- string instructions ---------------------------------------------- */
#define DFSTEP(n) (c->df ? -(int32_t)(n) : (int32_t)(n))

/* ---- x87 rounding ------------------------------------------------------
 * The result follows the control word's RC field (bits 10-11).
 * 00=nearest (even), 01=down, 10=up, 11=toward zero (chop).
 * The CRT's float->int paths set RC to 11; ignoring it makes every
 * conversion that lands on a fraction >= 0.5 come out one too high. */
static inline double x87_round(uint16_t fcw, double v){
    switch ((fcw >> 10) & 3u){
    case 0:  return nearbyint(v);      /* host mode: nearest, even */
    case 1:  return floor(v);
    case 2:  return ceil(v);
    default: return trunc(v);
    }
}

/* x87 precision control (bits 8-9): 00 = 24-bit significand, 10 = 53,
 * 11 = 64.  D3D9's CreateDevice without D3DCREATE_FPU_PRESERVE leaves the
 * game thread at PC=00, so its intermediates are single precision with the
 * extended exponent range.  PC=53/64 are exactly double here. */
static inline double x87_round24(double v, uint32_t rc){
    uint64_t b; memcpy(&b, &v, 8);
    uint64_t low = b & 0x1FFFFFFFull;            /* the 29 bits below 24 */
    if (!low || ((b >> 52) & 0x7FFu) == 0x7FFu) return v;   /* exact, inf, nan */
    uint64_t t = b - low;                        /* toward zero */
    int up;
    if (rc == 0) up = low > 0x10000000ull || (low == 0x10000000ull && (t & 0x20000000ull));
    else if (rc == 3) up = 0;
    else up = (int)(b >> 63) == (rc == 1);       /* down: negatives grow; up: positives */
    if (up) t += 0x20000000ull;
    memcpy(&v, &t, 8); return v;
}
static inline double x87_pcround(uint16_t fcw, double v){
    if (__builtin_expect((fcw & 0x0F00u) == 0u, 1)){
        float f = (float)v, a = __builtin_fabsf(f);
        if (__builtin_expect(a > 0x1p-126f && a <= 0x1.fffffep127f, 1)) return (double)f;
        return x87_round24(v, 0);
    }
    if (fcw & 0x0300u) return v;                 /* PC=53/64: double */
    return x87_round24(v, (fcw >> 10) & 3u);
}
#define X87R(v) x87_pcround(c->fcw, (v))

/* ---- invalid-operation results and out-of-range conversions ----------
 * On x86 0/0 and sqrt(-x) produce "real indefinite": a quiet NaN with a
 * NEGATIVE sign.  C's result is a positive NaN. */
#define X87_INDEF (-__builtin_nan(""))

/* Two NaN operands: the x87 returns the one with the LARGER significand
 * (Intel SDM vol. 1, table 4-7). */
static inline double x87_r2_nan(double a, double b){
    if (a != a && b != b){
        uint64_t x, y; memcpy(&x, &a, 8); memcpy(&y, &b, 8);
        return (y & 0x000FFFFFFFFFFFFFull) > (x & 0x000FFFFFFFFFFFFFull) ? b : a;
    }
    if (a != a) return a;
    if (b != b) return b;
    return X87_INDEF;
}
static inline double x87_r2(double a, double b, double r){
    if (__builtin_expect(r == r, 1)) return r;
    return x87_r2_nan(a, b);
}
static inline double x87_r1(double a, double r){
    if (r == r) return r;
    if (a != a) return a;
    return X87_INDEF;
}
static inline float sse_r2f(float a, float b, float r){
    if (r == r) return r;
    if (a != a) return a;
    if (b != b) return b;
    return (float)X87_INDEF;
}
static inline float sse_r1f(float a, float r){
    if (r == r) return r;
    if (a != a) return a;
    return (float)X87_INDEF;
}
static inline double sse_r2d(double a, double b, double r){ return x87_r2(a,b,r); }
static inline double sse_r1d(double a, double r){ return x87_r1(a,r); }

/* Out of range / NaN -> "integer indefinite" (the most negative value). */
static inline uint32_t x86_cvt_i32(double v){
    if (!(v >= -2147483648.0 && v <= 2147483647.0)) return 0x80000000u;
    return (uint32_t)(int32_t)v;
}
static inline uint16_t x86_cvt_i16(double v){
    if (!(v >= -32768.0 && v <= 32767.0)) return 0x8000u;
    return (uint16_t)(int16_t)v;
}
static inline uint64_t x86_cvt_i64(double v){
    if (!(v >= -9223372036854775808.0 && v < 9223372036854775808.0))
        return 0x8000000000000000ull;
    return (uint64_t)(int64_t)v;
}

/* ---- guest memory ----------------------------------------------------- */
void mem_init(void);
void mem_commit(uint32_t addr, uint32_t size);
int  mem_load_pe(const char *path);
void mem_fault_handler_install(void);
void mem_altstack_install(void);
extern uint8_t g_cbits[1u<<15];   /* bitmap of the committed 16 KiB pages */
static inline int mem_is_committed(uint32_t addr){
    uint32_t pg = (addr) >> 14;
    return (g_cbits[pg>>3] >> (pg&7)) & 1;
}

/* ---- imports ---------------------------------------------------------- */
/* IAT slot value: 0xF0000000 | index into the runtime's import table (the
 * PE import directory order, resolved once at load). */
void import_init(void);
int  import_call(CPU *c, uint32_t index);
void import_report(void);
int  import_unimplemented(void);

/* ---- host ------------------------------------------------------------- */
extern CPU *g_cur_cpu;
extern volatile int g_guest_exited;
extern int g_guest_exit_code;
void guest_exit(int code);
uint64_t host_now_ns(void);
int host_trace_enabled(void);

#endif
