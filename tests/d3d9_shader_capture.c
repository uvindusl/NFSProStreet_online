/* d3d9_shader_capture.c - exercise the D3D9 HLE shader-object path.
 *
 * CreateVertexShader / CreatePixelShader now keep an exact copy of the
 * compiled D3D9 bytecode the guest hands over, so the runtime can translate it
 * to WGSL (or feed the CPU interpreter) instead of discarding it.  This test
 * checks:
 *   - a synthetic vs_2_0 / ps_2_0 blob yields a non-NULL handle
 *   - SetVertexShader / GetVertexShader and the pixel-shader pair round-trip
 *   - GetFunction returns the bytecode and length we passed in
 *   - (optional) real ProStreet blobs from a directory given as argv[1]
 *
 * Build: tests/build_d3d9_shader_capture.sh
 * Run:   tests/build/d3d9_shader_capture [shader-dir]
 */
#include "../lifter/runtime/d3d9_hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_OK(hr, msg) do { \
    int _hr = (hr); \
    if (_hr != D3D_OK) { fprintf(stderr, "FAIL: %s (hr=0x%x)\n", msg, _hr); exit(1); } \
    else printf("OK: %s\n", msg); \
} while (0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); exit(1); } \
    else printf("OK: %s\n", msg); \
} while (0)

/* vs_2_0: dcl_position v0 ; end  */
static const uint32_t g_vs[] = {
    0xFFFE0200u,
    0x0200001Fu, 0x80000000u, 0x900F0000u,   /* dcl_position v0 */
    0x0000FFFFu,                              /* end */
};

/* ps_2_0: mov r0, c0 ; end */
static const uint32_t g_ps[] = {
    0xFFFF0200u,
    0x02000001u, 0x800F0000u, 0xA00F0000u,   /* mov r0, c0 */
    0x0000FFFFu,                              /* end */
};

static struct IDirect3DDevice9* make_device(void) {
    struct IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d9) return NULL;
    D3DPRESENT_PARAMETERS pp = {0};
    pp.BackBufferWidth = 1280;
    pp.BackBufferHeight = 720;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = (void*)0x1;
    pp.Windowed = 1;
    struct IDirect3DDevice9* dev = NULL;
    int hr = d3d9->lpVtbl->CreateDevice(d3d9, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                        pp.hDeviceWindow,
                                        D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                        &pp, &dev);
    if (hr != D3D_OK) return NULL;
    return dev;
}

/* Verify that GetFunction returns exactly the bytes and length we created. */
static void check_get_function(struct IDirect3DDevice9* dev, void* shader,
                               const uint32_t* expect, uint32_t nbytes, const char* label) {
    uint32_t size = 0;
    int hr = dev->lpVtbl->GetVertexShader(dev, NULL); /* no-op, keeps the device used */
    (void)hr;
    /* GetFunction lives on the shader object's vtable (slot 4). */
    typedef struct { const void* f[5]; } ShaderVtbl;
    const ShaderVtbl* vt = (const ShaderVtbl*)(*(const void**)shader);
    int (*getfn)(void*, void*, uint32_t*) = (int (*)(void*, void*, uint32_t*))vt->f[4];

    hr = getfn(shader, NULL, &size);
    ASSERT_OK(hr, label);
    ASSERT_TRUE(size == nbytes, "GetFunction reports the bytecode length");
    uint8_t* buf = malloc(size ? size : 1);
    hr = getfn(shader, buf, &size);
    ASSERT_OK(hr, "GetFunction copies bytecode");
    ASSERT_TRUE(memcmp(buf, expect, nbytes) == 0, "GetFunction bytecode round-trips");
    free(buf);
}

int main(int argc, char** argv) {
    printf("=== D3D9 HLE Shader Capture Test ===\n\n");
    struct IDirect3DDevice9* dev = make_device();
    ASSERT_TRUE(dev != NULL, "device created");

    printf("\n-- synthetic vs_2_0 --\n");
    void* vs = NULL;
    ASSERT_OK(dev->lpVtbl->CreateVertexShader(dev, g_vs, &vs), "CreateVertexShader");
    ASSERT_TRUE(vs != NULL, "vertex shader handle non-NULL");
    ASSERT_OK(dev->lpVtbl->SetVertexShader(dev, vs), "SetVertexShader");
    void* got = NULL;
    ASSERT_OK(dev->lpVtbl->GetVertexShader(dev, &got), "GetVertexShader");
    ASSERT_TRUE(got == vs, "GetVertexShader returns the bound shader");
    check_get_function(dev, vs, g_vs, sizeof g_vs, "GetFunction(vs)");

    printf("\n-- synthetic ps_2_0 --\n");
    void* ps = NULL;
    ASSERT_OK(dev->lpVtbl->CreatePixelShader(dev, g_ps, &ps), "CreatePixelShader");
    ASSERT_TRUE(ps != NULL, "pixel shader handle non-NULL");
    ASSERT_OK(dev->lpVtbl->SetPixelShader(dev, ps), "SetPixelShader");
    got = NULL;
    ASSERT_OK(dev->lpVtbl->GetPixelShader(dev, &got), "GetPixelShader");
    ASSERT_TRUE(got == ps, "GetPixelShader returns the bound shader");

    /* Rejecting garbage is better than accepting it. */
    printf("\n-- invalid bytecode --\n");
    uint32_t junk[4] = { 0xDEADBEEFu, 0x12345678u, 0u, 0u };
    void* bad = (void*)0x1;
    int hr = dev->lpVtbl->CreateVertexShader(dev, junk, &bad);
    ASSERT_TRUE(hr != D3D_OK, "CreateVertexShader rejects non-shader bytes");
    ASSERT_TRUE(bad == NULL, "failed create leaves handle NULL");

    if (argc > 1) {
        printf("\n-- real ProStreet blobs from %s --\n", argv[1]);
        const char* names[] = { "vs_2_0_001.bin", "ps_2_0_001.bin",
                                "vs_3_0_001.bin", "ps_3_0_001.bin", "vs_1_1_001.bin" };
        for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
            char path[512];
            snprintf(path, sizeof path, "%s/%s", argv[1], names[i]);
            FILE* f = fopen(path, "rb");
            if (!f) { printf("skip: %s not found\n", names[i]); continue; }
            static uint8_t blob[1 << 16];
            size_t n = fread(blob, 1, sizeof blob, f);
            fclose(f);
            void* sh = NULL;
            int r;
            if (strncmp(names[i], "vs", 2) == 0)
                r = dev->lpVtbl->CreateVertexShader(dev, blob, &sh);
            else
                r = dev->lpVtbl->CreatePixelShader(dev, blob, &sh);
            ASSERT_OK(r, names[i]);
            ASSERT_TRUE(sh != NULL, "real blob yields a handle");
        }
    }

    printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}
