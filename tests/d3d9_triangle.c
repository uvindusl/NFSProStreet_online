#include "../lifter/runtime/d3d9_hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ASSERT_OK(hr, msg) do { \
    int _hr = (hr); \
    if (_hr != D3D_OK) { \
        fprintf(stderr, "FAIL: %s (hr=0x%x)\n", msg, _hr); \
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

/* Simple vertex structure for DrawPrimitiveUP */
typedef struct {
    float x, y, z, rhw;
    uint32_t color;
} VertexXYZRHW;

int main() {
    printf("=== D3D9 HLE Triangle Test ===\n\n");
    
    /* 1. Create IDirect3D9 */
    printf("Step 1: Direct3DCreate9\n");
    struct IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    ASSERT_EQ(d3d9 != NULL, 1, "Direct3DCreate9 returns non-NULL");
    
    /* 2. GetAdapterDisplayMode */
    printf("\nStep 2: GetAdapterDisplayMode\n");
    uint32_t mode[4] = {0};
    int hr = d3d9->lpVtbl->GetAdapterDisplayMode(d3d9, D3DADAPTER_DEFAULT, mode);
    ASSERT_OK(hr, "GetAdapterDisplayMode");
    ASSERT_EQ(mode[0], 1280, "Mode width");
    ASSERT_EQ(mode[1], 720, "Mode height");
    ASSERT_EQ(mode[3], D3DFMT_X8R8G8B8, "Mode format");
    
    /* 3. CheckDeviceType */
    printf("\nStep 3: CheckDeviceType\n");
    hr = d3d9->lpVtbl->CheckDeviceType(d3d9, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, 
                                         D3DFMT_X8R8G8B8, D3DFMT_X8R8G8B8, 1);
    ASSERT_OK(hr, "CheckDeviceType");
    
    /* 4. CreateDevice */
    printf("\nStep 4: CreateDevice\n");
    D3DPRESENT_PARAMETERS pp = {0};
    pp.BackBufferWidth = 1280;
    pp.BackBufferHeight = 720;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = (void*)0x1;
    pp.Windowed = 1;
    pp.EnableAutoDepthStencil = 1;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = 0;
    
    struct IDirect3DDevice9* device = NULL;
    hr = d3d9->lpVtbl->CreateDevice(d3d9, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                     pp.hDeviceWindow, 
                                     D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                                     &pp, &device);
    ASSERT_OK(hr, "CreateDevice");
    ASSERT_EQ(device != NULL, 1, "CreateDevice returns non-NULL device");
    
    /* 5. BeginScene */
    printf("\nStep 5: BeginScene\n");
    hr = device->lpVtbl->BeginScene(device);
    ASSERT_OK(hr, "BeginScene");
    
    /* 6. Clear */
    printf("\nStep 6: Clear\n");
    hr = device->lpVtbl->Clear(device, 0, NULL, 
                                D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                                D3DCOLOR_ARGB(255, 0, 0, 128), 1.0f, 0);
    ASSERT_OK(hr, "Clear");
    
    /* 7. SetFVF */
    printf("\nStep 7: SetFVF\n");
    hr = device->lpVtbl->SetFVF(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    ASSERT_OK(hr, "SetFVF");
    
    uint32_t fvf = 0;
    hr = device->lpVtbl->GetFVF(device, &fvf);
    ASSERT_OK(hr, "GetFVF");
    ASSERT_EQ(fvf, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, "GetFVF returns correct FVF");
    
    /* 8. SetRenderState */
    printf("\nStep 8: SetRenderState\n");
    hr = device->lpVtbl->SetRenderState(device, D3DRS_ZENABLE, D3DZB_TRUE);
    ASSERT_OK(hr, "SetRenderState ZENABLE");
    hr = device->lpVtbl->SetRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE);
    ASSERT_OK(hr, "SetRenderState CULLMODE");
    hr = device->lpVtbl->SetRenderState(device, D3DRS_LIGHTING, 0);
    ASSERT_OK(hr, "SetRenderState LIGHTING");
    
    /* 9. SetTransform (View and Projection) */
    printf("\nStep 9: SetTransform\n");
    D3DXMATRIX view = {0};
    view._11 = 1; view._22 = 1; view._33 = 1; view._44 = 1;
    hr = device->lpVtbl->SetTransform(device, D3DTS_VIEW, &view);
    ASSERT_OK(hr, "SetTransform VIEW");
    
    D3DXMATRIX proj = {0};
    proj._11 = 1; proj._22 = 1; proj._33 = 1; proj._44 = 1;
    hr = device->lpVtbl->SetTransform(device, D3DTS_PROJECTION, &proj);
    ASSERT_OK(hr, "SetTransform PROJECTION");
    
    /* 10. SetViewport */
    printf("\nStep 10: SetViewport\n");
    D3DVIEWPORT9 vp = {0, 0, 1280, 720, 0.0f, 1.0f};
    hr = device->lpVtbl->SetViewport(device, &vp);
    ASSERT_OK(hr, "SetViewport");
    
    D3DVIEWPORT9 vp_out = {0};
    hr = device->lpVtbl->GetViewport(device, &vp_out);
    ASSERT_OK(hr, "GetViewport");
    ASSERT_EQ(vp_out.Width, 1280, "Viewport width");
    ASSERT_EQ(vp_out.Height, 720, "Viewport height");
    
    /* 11. CreateVertexBuffer */
    printf("\nStep 11: CreateVertexBuffer\n");
    struct IDirect3DVertexBuffer9* vb = NULL;
    hr = device->lpVtbl->CreateVertexBuffer(device, 
                                             3 * sizeof(VertexXYZRHW),
                                             D3DUSAGE_WRITEONLY,
                                             D3DFVF_XYZRHW | D3DFVF_DIFFUSE,
                                             D3DPOOL_DEFAULT,
                                             &vb, NULL);
    ASSERT_OK(hr, "CreateVertexBuffer");
    ASSERT_EQ(vb != NULL, 1, "CreateVertexBuffer returns non-NULL");
    
    /* 12. SetStreamSource */
    printf("\nStep 12: SetStreamSource\n");
    hr = device->lpVtbl->SetStreamSource(device, 0, vb, 0, sizeof(VertexXYZRHW));
    ASSERT_OK(hr, "SetStreamSource");
    
    /* 13. DrawPrimitiveUP (triangle with 3 vertices) */
    printf("\nStep 13: DrawPrimitiveUP\n");
    VertexXYZRHW vertices[3] = {
        { 640.0f,  100.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255, 255,   0,   0) },  /* Top - Red */
        { 100.0f, 600.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,   0, 255,   0) },  /* Bottom-left - Green */
        { 1180.0f, 600.0f, 0.5f, 1.0f, D3DCOLOR_ARGB(255,   0,   0, 255) },  /* Bottom-right - Blue */
    };
    
    hr = device->lpVtbl->DrawPrimitiveUP(device, 
                                          D3DPT_TRIANGLELIST, 
                                          1,  /* 1 triangle */
                                          vertices, 
                                          sizeof(VertexXYZRHW));
    ASSERT_OK(hr, "DrawPrimitiveUP");
    
    /* 14. EndScene */
    printf("\nStep 14: EndScene\n");
    hr = device->lpVtbl->EndScene(device);
    ASSERT_OK(hr, "EndScene");
    
    /* 15. Present */
    printf("\nStep 15: Present\n");
    hr = device->lpVtbl->Present(device, NULL, NULL, NULL, NULL);
    ASSERT_OK(hr, "Present");
    
    /* 16. Release resources */
    printf("\nStep 16: Release resources\n");
    
    uint32_t ref = vb->lpVtbl->Release(vb);
    printf("OK: VertexBuffer Release -> %u\n", ref);
    
    ref = device->lpVtbl->Release(device);
    printf("OK: Device Release -> %u\n", ref);
    
    ref = d3d9->lpVtbl->Release(d3d9);
    printf("OK: D3D9 Release -> %u\n", ref);
    
    printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}