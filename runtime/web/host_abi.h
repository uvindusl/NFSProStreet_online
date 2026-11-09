/* The wasm side of the page contract: shared structures the C runtime
 * publishes to the page with nfsu2_register(kind, ptr, size).  The page side is
 * web/src/host/abi.ts (same numbers).  One kind per subsystem; a subsystem
 * keeps its own layout header (gpu_proto.h, audio_proto.h, ...).
 * Native builds: these are plain integer constants; wasm adds
 * __attribute__((import_module("nfsu2"), import_name(...))) */
#ifndef NFSU2_HOST_ABI_H
#define NFSU2_HOST_ABI_H
#include <stdint.h>
/* Kind values - match web/src/host/abi.ts  (HOST_KIND_* defined in gpu_proto.h) */
#define HOST_KIND_GPU    1   /* renderer control block (runtime/web/gpu_web.c) */
#define HOST_KIND_AUDIO  2   /* PCM ring + audio control (audio_web.c) */
#define HOST_KIND_INPUT  3   /* HostMailbox: keys, mouse, pad, lifecycle (input_web.c) */
#define HOST_KIND_NET    4   /* virtual LAN rings (net track) */
#define HOST_KIND_STATUS 5   /* frame/fps/timing counters for the page overlay */
#define HOST_KIND_VSYNC  6   /* vsync clock the page fills from requestAnimationFrame */
#define HOST_KIND_LAN    7   /* LAN play's virtual-LAN rings (net_lan.c; layout net_proto.h) */
/* imported from the page (web/src/host/game-worker.ts, module "nfsu2") */
/* nfsu2_register: register a shared-memory block with the page */
#if defined(__wasm__)
__attribute__((import_module("nfsu2"), import_name("register")))
int nfsu2_register(int kind, void *ptr, uint32_t size);
#else
/* Native: plain declaration - the host provides the implementation */
int nfsu2_register(int kind, void *ptr, uint32_t size);
#endif
/* now_ms: current time in milliseconds */
#if defined(__wasm__)
__attribute__((import_module("nfsu2"), import_name("now_ms")))
#endif
double nfsu2_now_ms(void);
/* blocked_ms: time spent blocked in the host (file I/O, sleeps) */
#if defined(__wasm__)
__attribute__((import_module("nfsu2"), import_name("blocked_ms")))
double nfsu2_blocked_ms(void);
#else
/* Native: plain declaration */
double nfsu2_blocked_ms(void);
#endif
#endif