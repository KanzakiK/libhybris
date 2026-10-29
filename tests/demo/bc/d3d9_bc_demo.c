/* SPDX-License-Identifier: Apache-2.0 */
/* Minimal D3D9 DXT sampling check for the Bionic Wine -> DXVK path. */
#define COBJMACROS
#define DIRECT3D_VERSION 0x0900
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TARGET_SIZE 64

static IDirect3D9Ex *direct3d;
static IDirect3DDevice9Ex *device;
static IDirect3DTexture9 *render_texture;
static IDirect3DSurface9 *render_target;
static IDirect3DSurface9 *staging;
static IDirect3DVertexShader9 *vertex_shader;
static IDirect3DPixelShader9 *pixel_shader;
static IDirect3DVertexDeclaration9 *declaration;
static unsigned checks;
static unsigned failures;
static uint32_t pixels[TARGET_SIZE * TARGET_SIZE];

static void result(const char *name, int ok, const char *detail)
{
    ++checks;
    if (!ok) ++failures;
    printf("BC_DEMO9_CHECK name=%s status=%s%s%s\n", name, ok ? "PASS" : "FAIL",
        detail && *detail ? " detail=" : "", detail && *detail ? detail : "");
    fflush(stdout);
}

static int close_pixel(uint32_t actual, uint32_t expected, unsigned tolerance)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        int a = (int)((actual >> shift) & 0xFF);
        int e = (int)((expected >> shift) & 0xFF);
        if (abs(a - e) > (int)tolerance) return 0;
    }
    return 1;
}

static ID3DBlob *compile_shader(const char *source, const char *entry,
    const char *profile)
{
    ID3DBlob *code = NULL;
    ID3DBlob *errors = NULL;
    HRESULT result = D3DCompile(source, strlen(source), NULL, NULL, NULL, entry,
        profile, 0, 0, &code, &errors);
    if (FAILED(result))
        fprintf(stderr, "BC_DEMO9_SHADER_FAIL profile=%s error=%s\n", profile,
            errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "unknown");
    if (errors) ID3D10Blob_Release(errors);
    return code;
}

static int initialize(void)
{
    HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &direct3d);
    if (FAILED(hr)) {
        fprintf(stderr, "BC_DEMO9_DIRECT3D_FAIL hr=0x%08lx\n", (unsigned long)hr);
        return 0;
    }
    D3DPRESENT_PARAMETERS parameters = {
        .BackBufferWidth = TARGET_SIZE,
        .BackBufferHeight = TARGET_SIZE,
        .BackBufferFormat = D3DFMT_A8R8G8B8,
        .BackBufferCount = 1,
        .SwapEffect = D3DSWAPEFFECT_DISCARD,
        .Windowed = TRUE,
        .PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE,
    };
    hr = IDirect3D9Ex_CreateDeviceEx(direct3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
        GetDesktopWindow(), D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
        &parameters, NULL, &device);
    if (FAILED(hr)) {
        fprintf(stderr, "BC_DEMO9_DEVICE_FAIL hr=0x%08lx\n", (unsigned long)hr);
        return 0;
    }
    hr = IDirect3DDevice9Ex_CreateTexture(device, TARGET_SIZE, TARGET_SIZE, 1,
        D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
        &render_texture, NULL);
    if (SUCCEEDED(hr))
        hr = IDirect3DTexture9_GetSurfaceLevel(render_texture, 0, &render_target);
    if (SUCCEEDED(hr))
        hr = IDirect3DDevice9Ex_CreateOffscreenPlainSurface(device, TARGET_SIZE,
            TARGET_SIZE, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &staging, NULL);
    if (FAILED(hr)) return 0;

    static const char *vertex_source =
        "struct VS_IN { float3 pos : POSITION; float2 uv : TEXCOORD0; };\n"
        "struct VS_OUT { float4 pos : POSITION; float2 uv : TEXCOORD0; };\n"
        "VS_OUT main(VS_IN input) {\n"
        "  VS_OUT output;\n"
        "  output.pos = float4(input.pos, 1);\n"
        "  output.uv = input.uv;\n"
        "  return output;\n"
        "}\n";
    static const char *pixel_source =
        "sampler2D color_sampler : register(s0);\n"
        "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
        "  return tex2D(color_sampler, uv);\n"
        "}\n";
    ID3DBlob *vs = compile_shader(vertex_source, "main", "vs_3_0");
    ID3DBlob *ps = compile_shader(pixel_source, "main", "ps_3_0");
    if (!vs || !ps) return 0;
    hr = IDirect3DDevice9Ex_CreateVertexShader(device,
        (const DWORD *)ID3D10Blob_GetBufferPointer(vs), &vertex_shader);
    if (SUCCEEDED(hr))
        hr = IDirect3DDevice9Ex_CreatePixelShader(device,
            (const DWORD *)ID3D10Blob_GetBufferPointer(ps), &pixel_shader);
    ID3D10Blob_Release(vs);
    ID3D10Blob_Release(ps);
    if (FAILED(hr)) return 0;

    static const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END(),
    };
    hr = IDirect3DDevice9Ex_CreateVertexDeclaration(device, elements, &declaration);
    return SUCCEEDED(hr);
}

static int read_target(void)
{
    HRESULT hr = IDirect3DDevice9Ex_GetRenderTargetData(device, render_target, staging);
    if (FAILED(hr)) return 0;
    D3DLOCKED_RECT locked;
    hr = IDirect3DSurface9_LockRect(staging, &locked, NULL, D3DLOCK_READONLY);
    if (FAILED(hr)) return 0;
    for (unsigned y = 0; y < TARGET_SIZE; ++y)
        memcpy(pixels + y * TARGET_SIZE,
            (const uint8_t *)locked.pBits + y * locked.Pitch, TARGET_SIZE * 4);
    IDirect3DSurface9_UnlockRect(staging);
    return 1;
}

static int draw_texture(IDirect3DTexture9 *texture)
{
    static const struct {
        float x, y, z, u, v;
    } vertices[] = {
        {-1, 1, 0, 0, 0},
        {1, 1, 0, 1, 0},
        {-1, -1, 0, 0, 1},
        {1, -1, 0, 1, 1},
    };
    HRESULT hr = IDirect3DDevice9Ex_SetRenderTarget(device, 0, render_target);
    if (SUCCEEDED(hr))
        hr = IDirect3DDevice9Ex_Clear(device, 0, NULL, D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(255, 0, 255), 1, 0);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9Ex_BeginScene(device);
    if (SUCCEEDED(hr)) {
        IDirect3DDevice9Ex_SetVertexDeclaration(device, declaration);
        IDirect3DDevice9Ex_SetVertexShader(device, vertex_shader);
        IDirect3DDevice9Ex_SetPixelShader(device, pixel_shader);
        IDirect3DDevice9Ex_SetTexture(device, 0, (IDirect3DBaseTexture9 *)texture);
        IDirect3DDevice9Ex_SetSamplerState(device, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        IDirect3DDevice9Ex_SetSamplerState(device, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        IDirect3DDevice9Ex_SetSamplerState(device, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        IDirect3DDevice9Ex_SetSamplerState(device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        IDirect3DDevice9Ex_SetSamplerState(device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        hr = IDirect3DDevice9Ex_DrawPrimitiveUP(device, D3DPT_TRIANGLESTRIP, 2,
            vertices, sizeof(vertices[0]));
        IDirect3DDevice9Ex_EndScene(device);
    }
    return SUCCEEDED(hr) && read_target();
}

static IDirect3DTexture9 *create_block_texture(D3DFORMAT format, unsigned bytes,
    const uint8_t blocks[4][16])
{
    IDirect3DTexture9 *texture = NULL;
    HRESULT hr = IDirect3DDevice9Ex_CreateTexture(device, 8, 8, 1, D3DUSAGE_DYNAMIC,
        format, D3DPOOL_DEFAULT, &texture, NULL);
    if (FAILED(hr)) return NULL;
    D3DLOCKED_RECT locked;
    hr = IDirect3DTexture9_LockRect(texture, 0, &locked, NULL, D3DLOCK_DISCARD);
    if (FAILED(hr)) {
        IDirect3DTexture9_Release(texture);
        return NULL;
    }
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 2; ++x)
            memcpy((uint8_t *)locked.pBits + y * locked.Pitch + x * bytes,
                blocks[y * 2 + x], bytes);
    IDirect3DTexture9_UnlockRect(texture, 0);
    return texture;
}

static void fill_constant_block(uint8_t *block, unsigned bytes, uint16_t color)
{
    memset(block, 0, bytes);
    uint8_t *color_block = block + (bytes == 16 ? 8 : 0);
    memcpy(color_block, &color, 2);
    memcpy(color_block + 2, &color, 2);
}

static void test_format(D3DFORMAT format, const char *name, unsigned bytes)
{
    static const uint16_t colors[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    static const uint32_t wanted[4] = {
        0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFFFFu,
    };
    uint8_t blocks[4][16] = {{0}};
    for (unsigned i = 0; i < 4; ++i) {
        fill_constant_block(blocks[i], bytes, colors[i]);
        if (format == D3DFMT_DXT3) memset(blocks[i], 0xFF, 8);
        else if (format == D3DFMT_DXT5) memset(blocks[i], 0xFF, 2);
    }
    IDirect3DTexture9 *texture = create_block_texture(format, bytes, blocks);
    char detail[192];
    if (!texture) {
        result(name, 0, "CreateTexture or LockRect failed");
        return;
    }
    int ok = draw_texture(texture);
    const unsigned points[4][2] = {
        {TARGET_SIZE / 4, TARGET_SIZE / 4},
        {3 * TARGET_SIZE / 4, TARGET_SIZE / 4},
        {TARGET_SIZE / 4, 3 * TARGET_SIZE / 4},
        {3 * TARGET_SIZE / 4, 3 * TARGET_SIZE / 4},
    };
    for (unsigned i = 0; ok && i < 4; ++i) {
        uint32_t actual = pixels[points[i][1] * TARGET_SIZE + points[i][0]];
        if (!close_pixel(actual, wanted[i], 10)) {
            snprintf(detail, sizeof(detail),
                "pixel=%u actual=0x%08x want=0x%08x", i, actual, wanted[i]);
            ok = 0;
        }
    }
    result(name, ok, ok ? "4 block colours match" : detail);
    IDirect3DTexture9_Release(texture);
}

static void test_large_dxt1(void)
{
    enum { SIZE = 2048 };
    IDirect3DTexture9 *texture = NULL;
    HRESULT hr = IDirect3DDevice9Ex_CreateTexture(device, SIZE, SIZE, 1,
        D3DUSAGE_DYNAMIC, D3DFMT_DXT1, D3DPOOL_DEFAULT, &texture, NULL);
    if (FAILED(hr)) {
        result("dxt1_large_fast", 0, "CreateTexture failed");
        return;
    }
    D3DLOCKED_RECT locked;
    hr = IDirect3DTexture9_LockRect(texture, 0, &locked, NULL, D3DLOCK_DISCARD);
    if (FAILED(hr)) {
        result("dxt1_large_fast", 0, "LockRect failed");
        IDirect3DTexture9_Release(texture);
        return;
    }
    uint8_t block[8];
    fill_constant_block(block, sizeof(block), 0xF800);
    for (unsigned y = 0; y < SIZE / 4; ++y)
        for (unsigned x = 0; x < SIZE / 4; ++x)
            memcpy((uint8_t *)locked.pBits + y * locked.Pitch + x * 8, block, 8);
    IDirect3DTexture9_UnlockRect(texture, 0);
    int ok = draw_texture(texture);
    uint32_t actual = pixels[(TARGET_SIZE / 2) * TARGET_SIZE + TARGET_SIZE / 2];
    ok = ok && close_pixel(actual, 0xFFFF0000u, 10);
    char detail[96];
    snprintf(detail, sizeof(detail), "actual=0x%08x want=0xffff0000", actual);
    result("dxt1_large_fast", ok, detail);
    IDirect3DTexture9_Release(texture);
}

static void destroy(void)
{
    if (declaration) IDirect3DVertexDeclaration9_Release(declaration);
    if (pixel_shader) IDirect3DPixelShader9_Release(pixel_shader);
    if (vertex_shader) IDirect3DVertexShader9_Release(vertex_shader);
    if (staging) IDirect3DSurface9_Release(staging);
    if (render_target) IDirect3DSurface9_Release(render_target);
    if (render_texture) IDirect3DTexture9_Release(render_texture);
    if (device) IDirect3DDevice9Ex_Release(device);
    if (direct3d) IDirect3D9Ex_Release(direct3d);
}

int main(void)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER started;
    LARGE_INTEGER ended;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    printf("BC_DEMO9_ENV transcode=%s fast=%s trace=%s\n",
        getenv("HYBRIS_BC_TRANSCODE") ? getenv("HYBRIS_BC_TRANSCODE") : "default",
        getenv("HYBRIS_BC_FAST") ? getenv("HYBRIS_BC_FAST") : "default",
        getenv("HYBRIS_VULKAN_TRACE") ? getenv("HYBRIS_VULKAN_TRACE") : "default");
    if (!initialize()) {
        fprintf(stderr, "BC_DEMO9_INIT_FAIL\n");
        destroy();
        return 2;
    }
    test_format(D3DFMT_DXT1, "dxt1", 8);
    test_format(D3DFMT_DXT3, "dxt3", 16);
    test_format(D3DFMT_DXT5, "dxt5", 16);
    test_large_dxt1();
    QueryPerformanceCounter(&ended);
    double milliseconds = (double)(ended.QuadPart - started.QuadPart) * 1000.0 /
        (double)frequency.QuadPart;
    printf("BC_DEMO9_SUMMARY checks=%u failures=%u milliseconds=%.3f\n",
        checks, failures, milliseconds);
    destroy();
    return failures ? 1 : 0;
}
