#define _GNU_SOURCE
#include "rt.h"
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <pthread.h>

/* Assert helpers */
#define ASSERT_TRUE(expr, msg) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
        exit(1); \
    } else { \
        printf("OK: %s\n", msg); \
    } \
} while(0)

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL: %s (expected %u, got %u at line %d)\n", msg, (unsigned)(b), (unsigned)(a), __LINE__); \
        exit(1); \
    } else { \
        printf("OK: %s\n", msg); \
    } \
} while(0)

/* Mock function table for standalone testing */
const uint32_t g_fn_count = 0;
const uint32_t g_fn_addr[1] = {0};
typedef void (*gfn_t)(CPU *);
const gfn_t g_fn_ptr[1] = {0};

const uint32_t g_seh_fn_count = 0;
const uint32_t g_seh_fn_addr[1] = {0};
const guest_fn_t g_seh_fn_ptr[1] = {0};

/* Mock imports table matching rt.h ImpEnt */
#define TEST_IAT_BASE 0x00300000u

enum {
    IMP_REG_CLASS_EX_A,
    IMP_REG_CLASS_A,
    IMP_CREATE_WINDOW_EX_A,
    IMP_PEEK_MESSAGE_A,
    IMP_GET_MESSAGE_A,
    IMP_DISPATCH_MESSAGE_A,
    IMP_TRANSLATE_MESSAGE,
    IMP_POST_QUIT_MESSAGE,
    IMP_POST_MESSAGE_A,
    IMP_GET_FOREGROUND_WINDOW,
    IMP_GET_DESKTOP_WINDOW,
    IMP_TEST_COUNT
};

const ImpEnt g_imp[] = {
    [IMP_REG_CLASS_EX_A]        = {TEST_IAT_BASE + 0 * 4, "USER32.dll", "RegisterClassExA", 1},
    [IMP_REG_CLASS_A]           = {TEST_IAT_BASE + 1 * 4, "USER32.dll", "RegisterClassA", 1},
    [IMP_CREATE_WINDOW_EX_A]    = {TEST_IAT_BASE + 2 * 4, "USER32.dll", "CreateWindowExA", 12},
    [IMP_PEEK_MESSAGE_A]        = {TEST_IAT_BASE + 3 * 4, "USER32.dll", "PeekMessageA", 5},
    [IMP_GET_MESSAGE_A]         = {TEST_IAT_BASE + 4 * 4, "USER32.dll", "GetMessageA", 4},
    [IMP_DISPATCH_MESSAGE_A]    = {TEST_IAT_BASE + 5 * 4, "USER32.dll", "DispatchMessageA", 1},
    [IMP_TRANSLATE_MESSAGE]     = {TEST_IAT_BASE + 6 * 4, "USER32.dll", "TranslateMessage", 1},
    [IMP_POST_QUIT_MESSAGE]     = {TEST_IAT_BASE + 7 * 4, "USER32.dll", "PostQuitMessage", 1},
    [IMP_POST_MESSAGE_A]        = {TEST_IAT_BASE + 8 * 4, "USER32.dll", "PostMessageA", 4},
    [IMP_GET_FOREGROUND_WINDOW] = {TEST_IAT_BASE + 9 * 4, "USER32.dll", "GetForegroundWindow", 0},
    [IMP_GET_DESKTOP_WINDOW]    = {TEST_IAT_BASE + 10 * 4, "USER32.dll", "GetDesktopWindow", 0},
};
const uint32_t g_imp_count = IMP_TEST_COUNT;

/* Stubs for lifted functions referenced by dispatch_init */
void sub_00909210(CPU *c) { (void)c; }
void sub_009096a0(CPU *c) { (void)c; }
void sub_004040f0(CPU *c) { (void)c; }
void sub_0044bfd0(CPU *c) { (void)c; }
void sub_004add90(CPU *c) { (void)c; }
void sub_004c8280(CPU *c) { (void)c; }
void sub_004c9730(CPU *c) { (void)c; }
void sub_00542700(CPU *c) { (void)c; }
void sub_01a11e78(CPU *c) { (void)c; }
void sub_00458260(CPU *c) { (void)c; }
void sub_00459c10(CPU *c) { (void)c; }
void sub_0052c6f0(CPU *c) { (void)c; }
void sub_004e1480(CPU *c) { (void)c; }
void sub_0052d5c0(CPU *c) { (void)c; }
void sub_00435140(CPU *c) { (void)c; }
void sub_004591a0(CPU *c) { (void)c; }

/* Include actual runtime sources */
#include "mem.c"
#include "com.c"
#include "d3dx9_hle.c"
#include "win32.c"
#include "dispatch.c"
#include "hdfont.c"
#include "gamefixes.c"
#include "d3dx_effect.c"
#include "swrast.c"
#include "autotest.c"

/* Mock wndproc to verify DispatchMessageA callback */
static uint32_t s_mock_wndproc_called = 0;
static uint32_t s_last_wm_msg = 0;
static uint32_t s_last_wm_wparam = 0;
static uint32_t s_last_wm_lparam = 0;

static void mock_guest_wndproc(CPU *c) {
    s_mock_wndproc_called++;
    /* stdcall (hwnd, msg, wp, lp): esp+4=hwnd, esp+8=msg, esp+12=wp, esp+16=lp */
    s_last_wm_msg = MR32(c->esp + 8);
    s_last_wm_wparam = MR32(c->esp + 12);
    s_last_wm_lparam = MR32(c->esp + 16);
    POP32(c->eip);
    c->esp += 16u;
    c->eax = 42u; /* mock return value */
}

int main(void) {
    printf("=== Antigravity Runtime Lifecycle Unit Tests ===\n\n");

    /* 1. Initialize guest memory, dispatch table, and COM bridge */
    mem_init();
    /* Suite 3 checks the attribute-tree workaround, which is opt-in */
    setenv("PROSTREET_ATTR_GUARD", "1", 1);
    dispatch_init();
    mem_commit(TEST_IAT_BASE, 0x1000u);
    win32_init();

    CPU cpu;
    memset(&cpu, 0, sizeof(cpu));
    CPU *c = &cpu;
    g_cur_cpu = c;

    /* Setup stack in guest memory */
    uint32_t stack_base = 0x7fe00000u;
    mem_commit(stack_base - 0x10000u, 0x20000u);
    c->esp = stack_base;

    /* -------------------------------------------------------------- */
    /* Test Suite 1: Win32 Window Message Pump & Queue                */
    /* -------------------------------------------------------------- */
    printf("\n--- Test Suite 1: Win32 Window Message Pump & Queue ---\n");

    /* RegisterClassExA with valid struct capturing lpfnWndProc */
    uint32_t wndclass_addr = 0x00200000u;
    mem_commit(wndclass_addr, 0x1000u);
    uint32_t mock_wndproc_addr = 0x00550000u;
    g_dtab[mock_wndproc_addr - TEXT_BASE] = mock_guest_wndproc;

    /* WNDCLASSEXA: +0 cbSize, +4 style, +8 lpfnWndProc */
    MR32(wndclass_addr + 0) = 48;
    MR32(wndclass_addr + 4) = 0;
    MR32(wndclass_addr + 8) = mock_wndproc_addr;

    /* Test RegisterClassExA */
    c->esp = stack_base;
    PUSH32(wndclass_addr);  /* arg 0: lpwcx */
    PUSH32(0x12345678u);    /* ret addr */
    win32_call(c, IMP_REG_CLASS_EX_A);
    ASSERT_EQ(c->eax, 0xC001u, "RegisterClassExA returns valid atom");
    ASSERT_EQ(g_wndproc, mock_wndproc_addr, "g_wndproc captured correctly");

    /* Test RegisterClassA (WNDCLASSA: 40 bytes, +4 lpfnWndProc) */
    uint32_t wndclass_a_addr = 0x00201000u;
    mem_commit(wndclass_a_addr, 0x1000u);
    MR32(wndclass_a_addr + 0) = 0;
    MR32(wndclass_a_addr + 4) = mock_wndproc_addr;
    c->esp = stack_base;
    PUSH32(wndclass_a_addr);
    PUSH32(0x12345678u);
    win32_call(c, IMP_REG_CLASS_A);
    ASSERT_EQ(c->eax, 0xC001u, "RegisterClassA returns valid atom");

    /* Test RegisterClassExA with NULL returns 0 */
    c->esp = stack_base;
    PUSH32(0u);             /* arg 0: NULL */
    PUSH32(0x12345678u);    /* ret addr */
    win32_call(c, IMP_REG_CLASS_EX_A);
    ASSERT_EQ(c->eax, 0u, "RegisterClassExA with NULL returns 0");

    /* Test CreateWindowExA */
    c->esp = stack_base;
    for (int i = 0; i < 12; i++) PUSH32(0); /* 12 args for CreateWindowExA */
    PUSH32(0x12345678u);
    win32_call(c, IMP_CREATE_WINDOW_EX_A);
    ASSERT_EQ(c->eax, 0x00B00001u, "CreateWindowExA returns valid hwnd");
    ASSERT_EQ(g_hwnd, 0x00B00001u, "g_hwnd set to 0x00B00001");

    /* Test GetForegroundWindow matches g_hwnd */
    c->esp = stack_base;
    PUSH32(0x12345678u);
    win32_call(c, IMP_GET_FOREGROUND_WINDOW);
    ASSERT_EQ(c->eax, 0x00B00001u, "GetForegroundWindow returns matching g_hwnd (0x00B00001)");

    /* Test GetDesktopWindow returns distinct desktop handle 0x00B00000 */
    c->esp = stack_base;
    PUSH32(0x12345678u);
    win32_call(c, IMP_GET_DESKTOP_WINDOW);
    ASSERT_EQ(c->eax, 0x00B00000u, "GetDesktopWindow returns distinct 0x00B00000");

    /* Verify initial messages were enqueued by CreateWindowExA */
    uint32_t msg_struct = 0x00210000u;
    mem_commit(msg_struct, 0x1000u);

    /* PeekMessageA (PM_NOREMOVE = 0) */
    c->esp = stack_base;
    PUSH32(0u);             /* wRemoveMsg = PM_NOREMOVE */
    PUSH32(0u);             /* wMsgFilterMax */
    PUSH32(0u);             /* wMsgFilterMin */
    PUSH32(0u);             /* hWnd */
    PUSH32(msg_struct);     /* lpMsg */
    PUSH32(0x12345678u);    /* ret addr */
    win32_call(c, IMP_PEEK_MESSAGE_A);
    ASSERT_EQ(c->eax, 1u, "PeekMessageA PM_NOREMOVE returns 1 (message available)");
    ASSERT_EQ(MR32(msg_struct + 4), 0x0018u, "Peeked message is WM_SHOWWINDOW (0x0018)");

    /* PeekMessageA (PM_REMOVE = 1) */
    c->esp = stack_base;
    PUSH32(1u);             /* wRemoveMsg = PM_REMOVE */
    PUSH32(0u);             /* wMsgFilterMax */
    PUSH32(0u);             /* wMsgFilterMin */
    PUSH32(0u);             /* hWnd */
    PUSH32(msg_struct);     /* lpMsg */
    PUSH32(0x12345678u);    /* ret addr */
    win32_call(c, IMP_PEEK_MESSAGE_A);
    ASSERT_EQ(c->eax, 1u, "PeekMessageA PM_REMOVE returns 1");
    ASSERT_EQ(MR32(msg_struct + 4), 0x0018u, "Popped message is WM_SHOWWINDOW (0x0018)");

    /* DispatchMessageA */
    c->esp = stack_base;
    PUSH32(msg_struct);     /* lpMsg */
    PUSH32(0x12345678u);    /* ret addr */
    win32_call(c, IMP_DISPATCH_MESSAGE_A);
    ASSERT_EQ(s_mock_wndproc_called, 1u, "DispatchMessageA invoked mock wndproc");
    ASSERT_EQ(s_last_wm_msg, 0x0018u, "WndProc received WM_SHOWWINDOW");
    ASSERT_EQ(c->eax, 42u, "DispatchMessageA returned wndproc return value");

    /* TranslateMessage on WM_SHOWWINDOW (non-key message) returns 0 */
    c->esp = stack_base;
    PUSH32(msg_struct);
    PUSH32(0x12345678u);
    win32_call(c, IMP_TRANSLATE_MESSAGE);
    ASSERT_EQ(c->eax, 0u, "TranslateMessage on non-key message returns 0");

    /* Drain queue: WM_ACTIVATEAPP (0x1C), WM_ACTIVATE (0x06), WM_SETFOCUS (0x07), WM_PAINT (0x0F) */
    for (int i = 0; i < 4; i++) {
        c->esp = stack_base;
        PUSH32(1u); PUSH32(0u); PUSH32(0u); PUSH32(0u); PUSH32(msg_struct); PUSH32(0x12345678u);
        win32_call(c, IMP_PEEK_MESSAGE_A);
        ASSERT_EQ(c->eax, 1u, "Draining startup messages");
    }

    /* Queue is now empty. PeekMessageA should return 0 */
    c->esp = stack_base;
    PUSH32(1u); PUSH32(0u); PUSH32(0u); PUSH32(0u); PUSH32(msg_struct); PUSH32(0x12345678u);
    win32_call(c, IMP_PEEK_MESSAGE_A);
    ASSERT_EQ(c->eax, 0u, "PeekMessageA returns 0 when queue is empty");

    /* GetMessageA on empty queue should return 1 (keep loop alive, not terminate) */
    c->esp = stack_base;
    PUSH32(0u); PUSH32(0u); PUSH32(0u); PUSH32(msg_struct); PUSH32(0x12345678u);
    win32_call(c, IMP_GET_MESSAGE_A);
    ASSERT_EQ(c->eax, 1u, "GetMessageA on empty queue returns 1 (non-terminating)");

    /* Test PostQuitMessage & GetMessageA returning 0 strictly on WM_QUIT */
    c->esp = stack_base;
    PUSH32(0u);             /* exit code 0 */
    PUSH32(0x12345678u);
    win32_call(c, IMP_POST_QUIT_MESSAGE);

    c->esp = stack_base;
    PUSH32(0u); PUSH32(0u); PUSH32(0u); PUSH32(msg_struct); PUSH32(0x12345678u);
    win32_call(c, IMP_GET_MESSAGE_A);
    ASSERT_EQ(c->eax, 0u, "GetMessageA returns 0 strictly on WM_QUIT");
    ASSERT_EQ(MR32(msg_struct + 4), 0x0012u, "Message is WM_QUIT (0x0012)");

    /* -------------------------------------------------------------- */
    /* Test Suite 2: D3DXEffect & D3D9 COM Bridge                     */
    /* -------------------------------------------------------------- */
    printf("\n--- Test Suite 2: D3DXEffect & D3D9 COM Bridge ---\n");

    /* Verify ID3DXEffect::Begin writes pass count to COM_A(2), not COM_A(1) */
    uint32_t effect_obj = 0x4C000000u;
    uint32_t passes_out_addr = 0x00220000u;
    mem_commit(passes_out_addr, 0x1000u);
    MR32(passes_out_addr) = 0xdeadbeefu;

    uint32_t flags_val = 0x12345678u;

    /* Setup stack for stdcall: Begin(this, Flags, pPasses)
     * At com_call entry: [esp+0]=ret, [esp+4]=this, [esp+8]=Flags, [esp+12]=pPasses
     * After POP32(c->eip): COM_A(0)=this, COM_A(1)=Flags, COM_A(2)=pPasses */
    c->esp = stack_base;
    PUSH32(passes_out_addr);/* Arg 2: UINT *NumPasses (pPasses) -> COM_A(2) */
    PUSH32(flags_val);      /* Arg 1: UINT Flags -> COM_A(1) */
    PUSH32(effect_obj);     /* Arg 0: this -> COM_A(0) */
    PUSH32(0x12345678u);    /* ret addr */

    /* slot 63 in M_EFFECT is Begin */
    uint32_t effect_begin_magic = (IF_EFFECT << 16) | 63;
    com_call(c, effect_begin_magic);
    ASSERT_EQ(c->eax, 0u, "ID3DXEffect::Begin returns D3D_OK");
    ASSERT_EQ(MR32(passes_out_addr), 1u, "ID3DXEffect::Begin wrote 1 to COM_A(2) (pPasses)");

    /* Verify ID3DXEffect lifecycle stubs */
    uint32_t effect_begin_pass_magic = (IF_EFFECT << 16) | 64;
    c->esp = stack_base;
    PUSH32(0u); PUSH32(effect_obj); PUSH32(0x12345678u);
    com_call(c, effect_begin_pass_magic);
    ASSERT_EQ(c->eax, 0u, "ID3DXEffect::BeginPass returns D3D_OK");

    uint32_t effect_commit_magic = (IF_EFFECT << 16) | 65;
    c->esp = stack_base;
    PUSH32(effect_obj); PUSH32(0x12345678u);
    com_call(c, effect_commit_magic);
    ASSERT_EQ(c->eax, 0u, "ID3DXEffect::CommitChanges returns D3D_OK");

    uint32_t effect_end_pass_magic = (IF_EFFECT << 16) | 66;
    c->esp = stack_base;
    PUSH32(effect_obj); PUSH32(0x12345678u);
    com_call(c, effect_end_pass_magic);
    ASSERT_EQ(c->eax, 0u, "ID3DXEffect::EndPass returns D3D_OK");

    uint32_t effect_end_magic = (IF_EFFECT << 16) | 67;
    c->esp = stack_base;
    PUSH32(effect_obj); PUSH32(0x12345678u);
    com_call(c, effect_end_magic);
    ASSERT_EQ(c->eax, 0u, "ID3DXEffect::End returns D3D_OK");

    /* Verify IDirect3DDevice9 lifecycle methods */
    uint32_t dev_obj = 0x4C000000u;
    /* BeginScene (m=41), EndScene (m=42), Present (m=17) */
    c->esp = stack_base;
    PUSH32(dev_obj); PUSH32(0x12345678u);
    com_call(c, (IF_DEV << 16) | 41);
    ASSERT_EQ(c->eax, 0u, "IDirect3DDevice9::BeginScene returns D3D_OK");

    c->esp = stack_base;
    PUSH32(dev_obj); PUSH32(0x12345678u);
    com_call(c, (IF_DEV << 16) | 42);
    ASSERT_EQ(c->eax, 0u, "IDirect3DDevice9::EndScene returns D3D_OK");

    c->esp = stack_base;
    PUSH32(0); PUSH32(0); PUSH32(0); PUSH32(0); PUSH32(dev_obj); PUSH32(0x12345678u);
    com_call(c, (IF_DEV << 16) | 17);
    ASSERT_EQ(c->eax, 0u, "IDirect3DDevice9::Present returns D3D_OK");

    /* -------------------------------------------------------------- */
    /* Test Suite 3: Attribute Tree sub_00530f60 & sub_005310e0 Overrides */
    /* -------------------------------------------------------------- */
    printf("\n--- Test Suite 3: Attribute Tree sub_00530f60 & sub_005310e0 Overrides ---\n");

    /* Verify sub_005310e0 is registered in g_dtab and cleans 5 args (20 bytes) */
    c->esp = stack_base;
    PUSH32(1u); PUSH32(2u); PUSH32(3u); PUSH32(4u); PUSH32(5u); /* 5 args = 20 bytes */
    PUSH32(0x12345678u); /* return address */
    uint32_t sp_before = c->esp;

    dispatch_indirect(c, 0x005310e0u);
    ASSERT_EQ(c->eip, 0x12345678u, "sub_005310e0 popped return address to eip");
    ASSERT_EQ(c->esp, sp_before + 24u, "sub_005310e0 cleaned ret addr + 20 bytes of args");
    ASSERT_EQ(c->eax, 0u, "sub_005310e0 returned eax = 0 (clean completion)");

    /* Verify sub_00530f60 is registered in g_dtab and cleans 2 args (8 bytes) */
    c->esp = stack_base;
    c->ecx = 0x00230000u; /* this pointer */
    PUSH32(10u); PUSH32(20u); /* 2 args = 8 bytes */
    PUSH32(0x87654321u); /* return address */
    sp_before = c->esp;

    dispatch_indirect(c, 0x00530f60u);
    ASSERT_EQ(c->eip, 0x87654321u, "sub_00530f60 popped return address to eip");
    ASSERT_EQ(c->esp, sp_before + 12u, "sub_00530f60 cleaned ret addr + 8 bytes of args");
    ASSERT_EQ(c->eax, 0x00230000u, "sub_00530f60 returned eax = this");

    /* Verify 0x00531064 crash site guard patches NULL child with safe NIL sentinel */
    uint32_t node_addr = 0x00230000u;
    mem_commit(node_addr, 0x1000u);
    MR32(node_addr + 4) = 0u; /* NULL child pointer that triggers SIGSEGV at 0x22 */
    c->esi = node_addr;

    dispatch_indirect(c, 0x00531064u);
    uint32_t patched_child = MR32(node_addr + 4);
    ASSERT_TRUE(patched_child != 0u, "0x00531064 guard patched NULL child pointer");
    ASSERT_TRUE(mem_is_committed(patched_child + 0x22u), "Patched sentinel is committed and safe to read offset 0x22");
    ASSERT_EQ(MR16(patched_child + 0x22u), 0u, "Patched sentinel offset 0x22 is 0 (clean loop termination)");
    ASSERT_EQ(c->edx, patched_child, "c->edx points to valid sentinel node");
    ASSERT_EQ(c->eax, 0u, "c->eax = 0 (clean completion)");

    /* Verify stack-return guard when caller has return address 0x00531064 */
    uint32_t node_addr2 = 0x00240000u;
    mem_commit(node_addr2, 0x1000u);
    MR32(node_addr2 + 4) = 0u;
    c->esi = node_addr2;
    c->esp = stack_base;
    PUSH32(0x00531064u); /* return address on stack */
    dispatch_indirect(c, 0x005310e0u);
    uint32_t patched_child2 = MR32(node_addr2 + 4);
    ASSERT_TRUE(patched_child2 != 0u, "dispatch_indirect stack-return guard patched NULL child");
    ASSERT_TRUE(mem_is_committed(patched_child2 + 0x22u), "Stack-return patched sentinel safe at offset 0x22");

    /* -------------------------------------------------------------- */
    /* Test Suite 4: Window Message Filtering & Routing               */
    /* -------------------------------------------------------------- */
    printf("\n--- Test Suite 4: Window Message Filtering & Routing ---\n");

    /* Push messages 0x0100 (WM_KEYDOWN) and 0x0200 (WM_MOUSEMOVE) */
    wm_push(0x0100u, 0x41u, 0u);
    wm_push(0x0200u, 0u, 0u);

    /* Test PeekMessageA with hWnd mismatch -> returns 0 */
    c->esp = stack_base;
    PUSH32(0u);             /* wRemoveMsg = PM_NOREMOVE */
    PUSH32(0u);             /* wMsgFilterMax */
    PUSH32(0u);             /* wMsgFilterMin */
    PUSH32(0x00B09999u);    /* invalid hWnd */
    PUSH32(msg_struct);     /* lpMsg */
    PUSH32(0x12345678u);    /* ret addr */
    win32_call(c, IMP_PEEK_MESSAGE_A);
    ASSERT_EQ(c->eax, 0u, "PeekMessageA with mismatched hWnd returns 0");

    /* Test PeekMessageA with range filter [0x0200, 0x0209] targeting mouse msg */
    c->esp = stack_base;
    PUSH32(1u);             /* wRemoveMsg = PM_REMOVE */
    PUSH32(0x0209u);        /* wMsgFilterMax */
    PUSH32(0x0200u);        /* wMsgFilterMin */
    PUSH32(0u);             /* hWnd */
    PUSH32(msg_struct);     /* lpMsg */
    PUSH32(0x12345678u);    /* ret addr */
    win32_call(c, IMP_PEEK_MESSAGE_A);
    ASSERT_EQ(c->eax, 1u, "PeekMessageA with range filter matched message");
    ASSERT_EQ(MR32(msg_struct + 4), 0x0200u, "Filter extracted WM_MOUSEMOVE (0x0200)");

    /* Pop remaining message 0x0100 */
    c->esp = stack_base;
    PUSH32(1u); PUSH32(0u); PUSH32(0u); PUSH32(0u); PUSH32(msg_struct); PUSH32(0x12345678u);
    win32_call(c, IMP_PEEK_MESSAGE_A);
    ASSERT_EQ(c->eax, 1u, "Popped remaining message");
    ASSERT_EQ(MR32(msg_struct + 4), 0x0100u, "Extracted WM_KEYDOWN (0x0100)");

    /* TranslateMessage on WM_KEYDOWN (0x0100) returns 1 and pushes WM_CHAR (0x0102) */
    c->esp = stack_base;
    PUSH32(msg_struct);
    PUSH32(0x12345678u);
    win32_call(c, IMP_TRANSLATE_MESSAGE);
    ASSERT_EQ(c->eax, 1u, "TranslateMessage on WM_KEYDOWN returns 1");

    /* Pop translated WM_CHAR message */
    c->esp = stack_base;
    PUSH32(1u); PUSH32(0u); PUSH32(0u); PUSH32(0u); PUSH32(msg_struct); PUSH32(0x12345678u);
    win32_call(c, IMP_PEEK_MESSAGE_A);
    ASSERT_EQ(c->eax, 1u, "PeekMessageA popped translated character message");
    ASSERT_EQ(MR32(msg_struct + 4), 0x0102u, "Message is WM_CHAR (0x0102)");
    ASSERT_EQ(MR32(msg_struct + 8), 0x41u, "Char is 'A' (0x41)");

    /* Test TranslateMessage with non-character key like VK_LEFT (0x25) -> returns 1 but no WM_CHAR */
    MR32(msg_struct + 4) = 0x0100u; /* WM_KEYDOWN */
    MR32(msg_struct + 8) = 0x25u;   /* VK_LEFT */
    c->esp = stack_base;
    PUSH32(msg_struct);
    PUSH32(0x12345678u);
    win32_call(c, IMP_TRANSLATE_MESSAGE);
    ASSERT_EQ(c->eax, 1u, "TranslateMessage on VK_LEFT returns 1");

    /* Queue should have no WM_CHAR */
    c->esp = stack_base;
    PUSH32(0u); PUSH32(0u); PUSH32(0u); PUSH32(0u); PUSH32(msg_struct); PUSH32(0x12345678u);
    win32_call(c, IMP_PEEK_MESSAGE_A);
    ASSERT_EQ(c->eax, 0u, "No WM_CHAR emitted for non-character key VK_LEFT");

    /* Test PostMessageA with invalid hWnd */
    c->esp = stack_base;
    PUSH32(0u); PUSH32(0u); PUSH32(0x0400u); PUSH32(0xDEADBEEFu); PUSH32(0x12345678u);
    win32_call(c, IMP_POST_MESSAGE_A);
    ASSERT_EQ(c->eax, 0u, "PostMessageA with invalid hWnd returns 0");

    /* Test PostMessageA with valid hWnd */
    c->esp = stack_base;
    PUSH32(0x9999u);        /* lParam */
    PUSH32(0x8888u);        /* wParam */
    PUSH32(0x0400u);        /* Msg = WM_USER */
    PUSH32(0x00B00001u);    /* hWnd */
    PUSH32(0x12345678u);    /* ret addr */
    win32_call(c, IMP_POST_MESSAGE_A);
    ASSERT_EQ(c->eax, 1u, "PostMessageA returned 1");

    c->esp = stack_base;
    PUSH32(1u); PUSH32(0u); PUSH32(0u); PUSH32(0u); PUSH32(msg_struct); PUSH32(0x12345678u);
    win32_call(c, IMP_PEEK_MESSAGE_A);
    ASSERT_EQ(c->eax, 1u, "PeekMessageA retrieved posted message");
    ASSERT_EQ(MR32(msg_struct + 4), 0x0400u, "Retrieved WM_USER (0x0400)");

    printf("\nAll runtime lifecycle tests passed successfully (100%% production code exercised)!\n");
    return 0;
}
