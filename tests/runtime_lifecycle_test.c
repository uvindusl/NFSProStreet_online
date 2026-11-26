#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <pthread.h>

#define ASSERT_TRUE(expr, msg) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        exit(1); \
    } else { \
        printf("OK: %s\n", msg); \
    } \
} while(0)

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL: %s (expected %u, got %u)\n", msg, (unsigned)(b), (unsigned)(a)); \
        exit(1); \
    } else { \
        printf("OK: %s\n", msg); \
    } \
} while(0)

/* ------------------------------------------------------------------ */
/* Test 1: Window Message Queue & Pump Logic                          */
/* Mirrors lifter/runtime/win32.c implementation                      */
/* ------------------------------------------------------------------ */
#define WMQ_CAP 256
static struct { uint32_t msg, wp, lp; } s_wmq[WMQ_CAP];
static int s_wmq_head = 0, s_wmq_tail = 0;
static pthread_mutex_t s_wmq_mtx = PTHREAD_MUTEX_INITIALIZER;
static uint32_t s_hwnd = 0x00B00001u;
static uint32_t s_wndproc = 0;

static void test_wm_push(uint32_t m, uint32_t wp, uint32_t lp) {
    pthread_mutex_lock(&s_wmq_mtx);
    int next = (s_wmq_tail + 1) % WMQ_CAP;
    if (next != s_wmq_head) {
        s_wmq[s_wmq_tail].msg = m;
        s_wmq[s_wmq_tail].wp = wp;
        s_wmq[s_wmq_tail].lp = lp;
        s_wmq_tail = next;
    }
    pthread_mutex_unlock(&s_wmq_mtx);
}

static int test_wm_pop(uint32_t *m, uint32_t *wp, uint32_t *lp) {
    int ok = 0;
    pthread_mutex_lock(&s_wmq_mtx);
    if (s_wmq_head != s_wmq_tail) {
        *m = s_wmq[s_wmq_head].msg;
        *wp = s_wmq[s_wmq_head].wp;
        *lp = s_wmq[s_wmq_head].lp;
        s_wmq_head = (s_wmq_head + 1) % WMQ_CAP;
        ok = 1;
    }
    pthread_mutex_unlock(&s_wmq_mtx);
    return ok;
}

static int test_wm_peek(uint32_t *m, uint32_t *wp, uint32_t *lp) {
    int ok = 0;
    pthread_mutex_lock(&s_wmq_mtx);
    if (s_wmq_head != s_wmq_tail) {
        *m = s_wmq[s_wmq_head].msg;
        *wp = s_wmq[s_wmq_head].wp;
        *lp = s_wmq[s_wmq_head].lp;
        ok = 1;
    }
    pthread_mutex_unlock(&s_wmq_mtx);
    return ok;
}

typedef struct {
    uint32_t hwnd;
    uint32_t message;
    uint32_t wParam;
    uint32_t lParam;
    uint32_t time;
    int32_t  pt_x;
    int32_t  pt_y;
} GuestMSG;

static uint32_t test_peek_message(GuestMSG *msg, uint32_t remove_flag) {
    uint32_t m = 0, wp = 0, lp = 0;
    int remove = (remove_flag & 1u);
    int has = remove ? test_wm_pop(&m, &wp, &lp) : test_wm_peek(&m, &wp, &lp);
    if (has) {
        if (msg) {
            msg->hwnd = s_hwnd;
            msg->message = m;
            msg->wParam = wp;
            msg->lParam = lp;
        }
        return 1u;
    }
    return 0u;
}

static uint32_t test_get_message(GuestMSG *msg) {
    uint32_t m = 0, wp = 0, lp = 0;
    int has = test_wm_pop(&m, &wp, &lp);
    if (has) {
        if (msg) {
            msg->hwnd = s_hwnd;
            msg->message = m;
            msg->wParam = wp;
            msg->lParam = lp;
        }
        return (m == 0x0012u /* WM_QUIT */) ? 0u : 1u;
    }
    /* When queue empty, non-blocking fallback returns 1 so loop keeps waiting */
    return 1u;
}

static uint32_t test_register_class(int is_ex, const void *ptr, size_t valid_size) {
    uint32_t off = is_ex ? 8u : 4u;
    if (valid_size >= off + 4u) {
        uint32_t wp = *(const uint32_t *)((const char *)ptr + off);
        if (wp) {
            s_wndproc = wp;
            return 0xC001u;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test 2: Attribute Tree Crash Guard Logic                           */
/* Mirrors lifter/runtime/dispatch.c 0x00531064 guard                 */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi, eip;
} DummyCPU;

static int test_attr_guard(DummyCPU *c) {
    if (!c->edx) {
        /* edx is NULL: NIL sentinel encountered. Set eax=0 and simulate ret */
        c->eax = 0;
        c->eip = 0x005310f0u; /* return to caller */
        return 1;             /* handled */
    }
    return 0;                 /* live node: proceed */
}

/* ------------------------------------------------------------------ */
/* Test 3: COM Method Dispatch Indices                                */
/* Mirrors lifter/runtime/com.c D3D9 / D3DX Effect method routing     */
/* ------------------------------------------------------------------ */
static uint32_t test_com_dev_method(int m, const char *nm, uint32_t pPasses) {
    if (!strcmp(nm, "BeginScene") || m == 41) return 0; /* D3D_OK */
    if (!strcmp(nm, "EndScene") || m == 42) return 0;   /* D3D_OK */
    if (!strcmp(nm, "Present") || m == 17) return 0;    /* D3D_OK */
    if (!strcmp(nm, "Begin") || m == 63) {
        if (pPasses) return 1; /* 1 pass returned */
    }
    if (!strcmp(nm, "CommitChanges") || m == 65) return 0; /* D3D_OK */
    return 0xFFFFFFFFu;
}

int main(void) {
    printf("=== Running Runtime Lifecycle & Pump Tests ===\n\n");

    /* 1. Empty PeekMessage / GetMessage test */
    GuestMSG msg;
    memset(&msg, 0, sizeof(msg));
    ASSERT_EQ(test_peek_message(&msg, 1), 0u, "PeekMessageA on empty queue returns 0");
    ASSERT_EQ(test_get_message(&msg), 1u, "GetMessageA on empty queue returns 1 (keeps loop alive)");

    /* 2. Enqueue messages and verify FIFO ordering & peek vs pop */
    test_wm_push(0x0018u, 1u, 0u); /* WM_SHOWWINDOW */
    test_wm_push(0x0006u, 1u, 0u); /* WM_ACTIVATE */
    test_wm_push(0x0012u, 0u, 0u); /* WM_QUIT */

    /* Peek without removing */
    ASSERT_EQ(test_peek_message(&msg, 0), 1u, "PeekMessageA with PM_NOREMOVE finds message");
    ASSERT_EQ(msg.message, 0x0018u, "Peeked message is WM_SHOWWINDOW");

    /* Pop first message */
    ASSERT_EQ(test_peek_message(&msg, 1), 1u, "PeekMessageA with PM_REMOVE removes message");
    ASSERT_EQ(msg.message, 0x0018u, "Removed message is WM_SHOWWINDOW");

    /* Pop second message with GetMessageA */
    ASSERT_EQ(test_get_message(&msg), 1u, "GetMessageA on normal message returns 1");
    ASSERT_EQ(msg.message, 0x0006u, "GetMessageA retrieved WM_ACTIVATE");

    /* Pop WM_QUIT message with GetMessageA */
    ASSERT_EQ(test_get_message(&msg), 0u, "GetMessageA on WM_QUIT returns 0 (exit loop)");
    ASSERT_EQ(msg.message, 0x0012u, "Retrieved message is WM_QUIT");

    /* 3. RegisterClassA vs RegisterClassExA bounds check */
    struct {
        uint32_t style;
        uint32_t lpfnWndProc;
        int32_t cbClsExtra;
        int32_t cbWndExtra;
    } wndclass = { 0, 0x005A1234u, 0, 0 };

    struct {
        uint32_t cbSize;
        uint32_t style;
        uint32_t lpfnWndProc;
        int32_t cbClsExtra;
        int32_t cbWndExtra;
    } wndclassex = { sizeof(wndclassex), 0, 0x005B5678u, 0, 0 };

    /* RegisterClassA (wndproc at offset 4) */
    ASSERT_EQ(test_register_class(0, &wndclass, sizeof(wndclass)), 0xC001u, "RegisterClassA captures wndproc");
    ASSERT_EQ(s_wndproc, 0x005A1234u, "Captured wndproc from RegisterClassA matches");

    /* RegisterClassExA (wndproc at offset 8) */
    ASSERT_EQ(test_register_class(1, &wndclassex, sizeof(wndclassex)), 0xC001u, "RegisterClassExA captures wndproc");
    ASSERT_EQ(s_wndproc, 0x005B5678u, "Captured wndproc from RegisterClassExA matches");

    /* 4. Attribute tree crash site guard */
    DummyCPU cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.esi = 0x01234560u; /* node pointer */
    cpu.edx = 0x00000000u; /* child pointer = NULL (NIL sentinel) */

    int guarded = test_attr_guard(&cpu);
    ASSERT_EQ(guarded, 1, "Attribute guard intercepts NULL edx");
    ASSERT_EQ(cpu.eax, 0u, "Attribute guard sets eax=0 (valid NIL subtree)");
    ASSERT_EQ(cpu.eip, 0x005310f0u, "Attribute guard advances eip past crash site");

    /* Live child case */
    cpu.edx = 0x01234580u;
    guarded = test_attr_guard(&cpu);
    ASSERT_EQ(guarded, 0, "Attribute guard passes through non-NULL live child");

    /* 5. COM method dispatch */
    ASSERT_EQ(test_com_dev_method(41, "BeginScene", 0), 0u, "COM BeginScene (m=41) returns D3D_OK");
    ASSERT_EQ(test_com_dev_method(42, "EndScene", 0), 0u, "COM EndScene (m=42) returns D3D_OK");
    ASSERT_EQ(test_com_dev_method(17, "Present", 0), 0u, "COM Present (m=17) returns D3D_OK");
    ASSERT_EQ(test_com_dev_method(63, "Begin", 1), 1u, "COM Begin (m=63) returns 1 pass");
    ASSERT_EQ(test_com_dev_method(65, "CommitChanges", 0), 0u, "COM CommitChanges (m=65) returns D3D_OK");

    printf("\n=== ALL TESTS PASSED SUCCESSFULLY ===\n");
    return 0;
}
