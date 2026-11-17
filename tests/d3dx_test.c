#include "../lifter/runtime/d3d9_hle.h"
#include "../lifter/runtime/d3dx9_hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#define ASSERT_TRUE(expr, msg) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        exit(1); \
    } else { \
        printf("OK: %s\n", msg); \
    } \
} while(0)

#define ASSERT_FLOAT_EQ(a, b, msg) do { \
    float diff = fabsf((a) - (b)); \
    if (diff > 1e-5f) { \
        fprintf(stderr, "FAIL: %s (expected %f, got %f, diff %f)\n", msg, (double)(b), (double)(a), (double)diff); \
        exit(1); \
    } else { \
        printf("OK: %s\n", msg); \
    } \
} while(0)

#define ASSERT_MATRIX_EQ(m1, m2, msg) do { \
    const float* a = (const float*)(m1); \
    const float* b = (const float*)(m2); \
    int ok = 1; \
    for (int i = 0; i < 16; i++) { \
        if (fabsf(a[i] - b[i]) > 1e-5f) { \
            fprintf(stderr, "FAIL: %s at [%d] (expected %f, got %f)\n", msg, i, (double)b[i], (double)a[i]); \
            ok = 0; \
            break; \
        } \
    } \
    if (ok) printf("OK: %s\n", msg); \
    else exit(1); \
} while(0)

/* Identity matrix */
static D3DXMATRIX IDENTITY = {
    1,0,0,0,
    0,1,0,0,
    0,0,1,0,
    0,0,0,1
};

/* Test matrix A */
static D3DXMATRIX MAT_A = {
    1, 2, 3, 4,
    5, 6, 7, 8,
    9, 10, 11, 12,
    13, 14, 15, 16
};

/* Test matrix B */
static D3DXMATRIX MAT_B = {
    16, 15, 14, 13,
    12, 11, 10, 9,
    8, 7, 6, 5,
    4, 3, 2, 1
};

/* Expected A * B */
static D3DXMATRIX MAT_A_MUL_B = {
    80, 70, 60, 50,
    240, 214, 188, 162,
    400, 358, 316, 274,
    560, 502, 444, 386
};

/* Test vector */
static D3DXVECTOR3 VEC3 = { 1.0f, 2.0f, 3.0f };
static D3DXVECTOR4 VEC4 = { 1.0f, 2.0f, 3.0f, 4.0f };

/* Translation matrix */
static D3DXMATRIX TRANS_MAT = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    10, 20, 30, 1
};

/* Scale matrix */
static D3DXMATRIX SCALE_MAT = {
    2, 0, 0, 0,
    0, 3, 0, 0,
    0, 0, 4, 0,
    0, 0, 0, 1
};

void test_matrix_multiply() {
    printf("\n=== Testing D3DXMatrixMultiply ===\n");
    
    D3DXMATRIX result;
    
    /* Test A * B */
    D3DXMatrixMultiply(&result, &MAT_A, &MAT_B);
    ASSERT_MATRIX_EQ(&result, &MAT_A_MUL_B, "A * B");
    
    /* Test identity */
    D3DXMatrixMultiply(&result, &MAT_A, &IDENTITY);
    ASSERT_MATRIX_EQ(&result, &MAT_A, "A * I = A");
    
    D3DXMatrixMultiply(&result, &IDENTITY, &MAT_A);
    ASSERT_MATRIX_EQ(&result, &MAT_A, "I * A = A");
    
    /* Test in-place (pOut == pM1) */
    D3DXMATRIX temp = MAT_A;
    D3DXMatrixMultiply(&temp, &temp, &IDENTITY);
    ASSERT_MATRIX_EQ(&temp, &MAT_A, "in-place A * I");
    
    /* Test in-place (pOut == pM2) */
    temp = MAT_B;
    D3DXMatrixMultiply(&temp, &IDENTITY, &temp);
    ASSERT_MATRIX_EQ(&temp, &MAT_B, "in-place I * B");
}

void test_matrix_multiply_transpose() {
    printf("\n=== Testing D3DXMatrixMultiplyTranspose ===\n");
    
    D3DXMATRIX result;
    D3DXMATRIX expected;
    
    /* (A * B)^T = transpose(A * B) */
    D3DXMatrixMultiply(&expected, &MAT_A, &MAT_B);
    D3DXMatrixTranspose(&expected, &expected);
    
    D3DXMatrixMultiplyTranspose(&result, &MAT_A, &MAT_B);
    ASSERT_MATRIX_EQ(&result, &expected, "(A * B)^T");
}

void test_matrix_transpose() {
    printf("\n=== Testing D3DXMatrixTranspose ===\n");
    
    D3DXMATRIX result;
    
    /* Transpose of A */
    D3DXMatrixTranspose(&result, &MAT_A);
    
    D3DXMATRIX expected = {
        1, 5, 9, 13,
        2, 6, 10, 14,
        3, 7, 11, 15,
        4, 8, 12, 16
    };
    ASSERT_MATRIX_EQ(&result, &expected, "transpose(A)");
    
    /* Double transpose = original */
    D3DXMatrixTranspose(&result, &result);
    ASSERT_MATRIX_EQ(&result, &MAT_A, "transpose(transpose(A)) = A");
    
    /* Transpose identity = identity */
    D3DXMatrixTranspose(&result, &IDENTITY);
    ASSERT_MATRIX_EQ(&result, &IDENTITY, "transpose(I) = I");
}

void test_matrix_inverse() {
    printf("\n=== Testing D3DXMatrixInverse ===\n");
    
    D3DXMATRIX result;
    float det;
    
    /* Inverse of identity = identity */
    D3DXMatrixInverse(&result, &det, &IDENTITY);
    ASSERT_FLOAT_EQ(det, 1.0f, "det(I) = 1");
    ASSERT_MATRIX_EQ(&result, &IDENTITY, "inverse(I) = I");
    
    /* Inverse of translation matrix */
    D3DXMATRIX inv_trans;
    D3DXMatrixInverse(&inv_trans, &det, &TRANS_MAT);
    ASSERT_FLOAT_EQ(det, 1.0f, "det(translation) = 1");
    
    D3DXMATRIX expected_trans = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        -10, -20, -30, 1
    };
    ASSERT_MATRIX_EQ(&inv_trans, &expected_trans, "inverse(translation)");
    
    /* Verify T * T^-1 = I */
    D3DXMatrixMultiply(&result, &TRANS_MAT, &inv_trans);
    ASSERT_MATRIX_EQ(&result, &IDENTITY, "T * T^-1 = I");
    
    /* Inverse of scale matrix */
    D3DXMATRIX inv_scale;
    D3DXMatrixInverse(&inv_scale, &det, &SCALE_MAT);
    ASSERT_FLOAT_EQ(det, 24.0f, "det(scale) = 24");
    
    D3DXMATRIX expected_scale = {
        0.5f, 0, 0, 0,
        0, 1.0f/3.0f, 0, 0,
        0, 0, 0.25f, 0,
        0, 0, 0, 1
    };
    ASSERT_MATRIX_EQ(&inv_scale, &expected_scale, "inverse(scale)");
    
    /* Verify S * S^-1 = I */
    D3DXMatrixMultiply(&result, &SCALE_MAT, &inv_scale);
    ASSERT_MATRIX_EQ(&result, &IDENTITY, "S * S^-1 = I");
    
    /* Singular matrix (zero determinant) */
    D3DXMATRIX singular = {0};
    D3DXMatrixInverse(&result, &det, &singular);
    ASSERT_FLOAT_EQ(det, 0.0f, "det(singular) = 0");
    /* Result should be zero matrix */
    for (int i = 0; i < 16; i++) {
        ASSERT_FLOAT_EQ(((float*)&result)[i], 0.0f, "singular inverse = zero");
    }
}

void test_vec3_transform() {
    printf("\n=== Testing D3DXVec3Transform ===\n");
    
    D3DXVECTOR4 result;
    
    /* Transform by identity */
    D3DXVec3Transform(&result, &VEC3, &IDENTITY);
    ASSERT_FLOAT_EQ(result.x, 1.0f, "vec3 * I x");
    ASSERT_FLOAT_EQ(result.y, 2.0f, "vec3 * I y");
    ASSERT_FLOAT_EQ(result.z, 3.0f, "vec3 * I z");
    ASSERT_FLOAT_EQ(result.w, 1.0f, "vec3 * I w");
    
    /* Transform by translation */
    D3DXVec3Transform(&result, &VEC3, &TRANS_MAT);
    ASSERT_FLOAT_EQ(result.x, 11.0f, "vec3 * T x");
    ASSERT_FLOAT_EQ(result.y, 22.0f, "vec3 * T y");
    ASSERT_FLOAT_EQ(result.z, 33.0f, "vec3 * T z");
    ASSERT_FLOAT_EQ(result.w, 1.0f, "vec3 * T w");
    
    /* Transform by scale */
    D3DXVec3Transform(&result, &VEC3, &SCALE_MAT);
    ASSERT_FLOAT_EQ(result.x, 2.0f, "vec3 * S x");
    ASSERT_FLOAT_EQ(result.y, 6.0f, "vec3 * S y");
    ASSERT_FLOAT_EQ(result.z, 12.0f, "vec3 * S z");
    ASSERT_FLOAT_EQ(result.w, 1.0f, "vec3 * S w");
}

void test_vec4_transform() {
    printf("\n=== Testing D3DXVec4Transform ===\n");
    
    D3DXVECTOR4 result;
    
    /* Transform by identity */
    D3DXVec4Transform(&result, &VEC4, &IDENTITY);
    ASSERT_FLOAT_EQ(result.x, 1.0f, "vec4 * I x");
    ASSERT_FLOAT_EQ(result.y, 2.0f, "vec4 * I y");
    ASSERT_FLOAT_EQ(result.z, 3.0f, "vec4 * I z");
    ASSERT_FLOAT_EQ(result.w, 4.0f, "vec4 * I w");
    
    /* Transform by translation (w=1 for points, w=0 for vectors) */
    D3DXVECTOR4 vec4_point = {1, 2, 3, 1};
    D3DXVec4Transform(&result, &vec4_point, &TRANS_MAT);
    ASSERT_FLOAT_EQ(result.x, 11.0f, "vec4(point) * T x");
    ASSERT_FLOAT_EQ(result.y, 22.0f, "vec4(point) * T y");
    ASSERT_FLOAT_EQ(result.z, 33.0f, "vec4(point) * T z");
    ASSERT_FLOAT_EQ(result.w, 1.0f, "vec4(point) * T w");
    
    D3DXVECTOR4 vec4_vector = {1, 2, 3, 0};
    D3DXVec4Transform(&result, &vec4_vector, &TRANS_MAT);
    ASSERT_FLOAT_EQ(result.x, 1.0f, "vec4(vector) * T x");
    ASSERT_FLOAT_EQ(result.y, 2.0f, "vec4(vector) * T y");
    ASSERT_FLOAT_EQ(result.z, 3.0f, "vec4(vector) * T z");
    ASSERT_FLOAT_EQ(result.w, 0.0f, "vec4(vector) * T w");
}

void test_shader_profiles() {
    printf("\n=== Testing Shader Profiles ===\n");
    
    void* device = (void*)0x12345678;
    
    const char* vs_profile = D3DXGetVertexShaderProfile(device);
    ASSERT_TRUE(strcmp(vs_profile, "vs_2_0") == 0, "vertex shader profile = vs_2_0");
    
    const char* ps_profile = D3DXGetPixelShaderProfile(device);
    ASSERT_TRUE(strcmp(ps_profile, "ps_2_0") == 0, "pixel shader profile = ps_2_0");
}

void test_effect_stubs() {
    printf("\n=== Testing Effect Stubs ===\n");
    
    void* device = (void*)0x12345678;
    void* effect = NULL;
    void* pool = NULL;
    void* error = NULL;
    
    int hr = D3DXCreateEffectFromResourceA(device, NULL, "test.fx", NULL, NULL, 0, NULL, &effect, &error);
    ASSERT_TRUE(hr == D3D_OK, "D3DXCreateEffectFromResourceA returns D3D_OK");
    ASSERT_TRUE(effect != NULL, "effect pointer is non-NULL");
    ASSERT_TRUE(error == NULL, "error buffer is NULL");
    
    hr = D3DXCreateEffectPool(&pool);
    ASSERT_TRUE(hr == D3D_OK, "D3DXCreateEffectPool returns D3D_OK");
    ASSERT_TRUE(pool != NULL, "pool pointer is non-NULL");
}

void test_texture_stubs() {
    printf("\n=== Testing Texture Stubs ===\n");
    
    void* device = (void*)0x12345678;
    void* cube_tex = NULL;
    void* vol_tex = NULL;
    
    int hr = D3DXCreateCubeTextureFromFileInMemory(device, "data", 4, &cube_tex);
    ASSERT_TRUE(hr == D3D_OK, "D3DXCreateCubeTextureFromFileInMemory returns D3D_OK");
    ASSERT_TRUE(cube_tex != NULL, "cube texture pointer is non-NULL");
    
    hr = D3DXCreateVolumeTextureFromFileInMemory(device, "data", 4, &vol_tex);
    ASSERT_TRUE(hr == D3D_OK, "D3DXCreateVolumeTextureFromFileInMemory returns D3D_OK");
    ASSERT_TRUE(vol_tex != NULL, "volume texture pointer is non-NULL");
}

void test_surface_stubs() {
    printf("\n=== Testing Surface Stubs ===\n");
    
    void* buf = NULL;
    void* surf = (void*)0x12345678;
    
    int hr = D3DXSaveSurfaceToFileA("test.bmp", 0, surf, NULL, NULL);
    ASSERT_TRUE(hr == D3D_OK, "D3DXSaveSurfaceToFileA returns D3D_OK");
    
    hr = D3DXSaveSurfaceToFileInMemory(&buf, 0, surf, NULL, NULL);
    ASSERT_TRUE(hr == D3D_OK, "D3DXSaveSurfaceToFileInMemory returns D3D_OK");
    ASSERT_TRUE(buf != NULL, "buffer pointer is non-NULL");
}

int main() {
    printf("=== D3DX9 HLE Tests ===\n");
    
    test_matrix_multiply();
    test_matrix_multiply_transpose();
    test_matrix_transpose();
    test_matrix_inverse();
    test_vec3_transform();
    test_vec4_transform();
    test_shader_profiles();
    test_effect_stubs();
    test_texture_stubs();
    test_surface_stubs();
    
    printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}