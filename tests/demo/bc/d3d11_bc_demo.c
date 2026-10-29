/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Small offscreen D3D11 BC demo for the ARLinux Bionic Wine path.
 *
 * It avoids Steam, the compositor and the game while still going through
 * DXVK -> VK_LAYER_HYBRIS_compat -> libhybris -> Mali. Results are intended
 * for single-variable comparisons of HYBRIS_BC_TRANSCODE and HYBRIS_BC_FAST.
 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lostcoast_dxt_fixture.h"

#define TARGET_WIDTH 64
#define TARGET_HEIGHT 64
#define LARGE_SIZE 2048

static ID3D11Device *device;
static ID3D11DeviceContext *context;
static ID3D11Texture2D *target;
static ID3D11Texture2D *staging;
static ID3D11RenderTargetView *target_view;
static ID3D11VertexShader *texture_vs;
static ID3D11PixelShader *texture_ps;
static ID3D11PixelShader *mip_ps;
static ID3D11Buffer *mip_constant;
static unsigned checks;
static unsigned failures;
static uint32_t pixels[TARGET_WIDTH * TARGET_HEIGHT];

static void result(const char *name, int ok, const char *detail)
{
    ++checks;
    if (!ok) ++failures;
    printf("BC_DEMO_CHECK name=%s status=%s%s%s\n", name, ok ? "PASS" : "FAIL",
        detail && *detail ? " detail=" : "", detail && *detail ? detail : "");
    fflush(stdout);
}

static uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

static uint32_t hash_pixels(const uint32_t *values, size_t count)
{
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < count; ++i) {
        uint32_t value = values[i];
        for (unsigned byte = 0; byte < 4; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xFF;
            hash *= UINT32_C(16777619);
        }
    }
    return hash;
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

static ID3DBlob *compile_shader(const char *source, const char *entry, const char *profile)
{
    ID3DBlob *code = NULL;
    ID3DBlob *errors = NULL;
    HRESULT result = D3DCompile(source, strlen(source), NULL, NULL, NULL, entry, profile,
        0, 0, &code, &errors);
    if (FAILED(result)) {
        fprintf(stderr, "BC_DEMO_SHADER_FAIL entry=%s profile=%s error=%s\n", entry, profile,
            errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "unknown");
    }
    if (errors) ID3D10Blob_Release(errors);
    return code;
}

static int initialize(void)
{
    static const char *texture_source =
        "struct O { float4 position : SV_Position; float2 uv : TEXCOORD0; };\n"
        "O main(uint id : SV_VertexID) {\n"
        "  O o;\n"
        "  float2 uv = float2((id << 1) & 2, id & 2);\n"
        "  o.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
        "  o.uv = uv;\n"
        "  return o;\n"
        "}\n";
    static const char *sample_source =
        "Texture2D texture0 : register(t0);\n"
        "SamplerState sampler0 : register(s0);\n"
        "float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
        "  return texture0.Sample(sampler0, uv);\n"
        "}\n";
    static const char *sample_mip_source =
        "cbuffer Mip : register(b0) { float mip; };\n"
        "Texture2D texture0 : register(t0);\n"
        "SamplerState sampler0 : register(s0);\n"
        "float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
        "  return texture0.SampleLevel(sampler0, uv, mip);\n"
        "}\n";

    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        levels, 1, D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(hr)) {
        fprintf(stderr, "BC_DEMO_DEVICE_FAIL hr=0x%08lx\n", (unsigned long)hr);
        return 0;
    }

    ID3DBlob *vs = compile_shader(texture_source, "main", "vs_5_0");
    ID3DBlob *ps = compile_shader(sample_source, "main", "ps_5_0");
    ID3DBlob *mps = compile_shader(sample_mip_source, "main", "ps_5_0");
    if (!vs || !ps || !mps) return 0;
    hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(vs),
        ID3D10Blob_GetBufferSize(vs), NULL, &texture_vs);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(ps),
            ID3D10Blob_GetBufferSize(ps), NULL, &texture_ps);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(mps),
            ID3D10Blob_GetBufferSize(mps), NULL, &mip_ps);
    ID3D10Blob_Release(vs);
    ID3D10Blob_Release(ps);
    ID3D10Blob_Release(mps);
    if (FAILED(hr)) return 0;

    D3D11_BUFFER_DESC constants = {
        .ByteWidth = 16,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    };
    if (FAILED(ID3D11Device_CreateBuffer(device, &constants, NULL, &mip_constant))) return 0;

    D3D11_TEXTURE2D_DESC td = {
        .Width = TARGET_WIDTH,
        .Height = TARGET_HEIGHT,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc = {1, 0},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET,
    };
    if (FAILED(ID3D11Device_CreateTexture2D(device, &td, NULL, &target))) return 0;
    if (FAILED(ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target,
        NULL, &target_view))) return 0;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(ID3D11Device_CreateTexture2D(device, &td, NULL, &staging))) return 0;
    return 1;
}

static const uint32_t *read_target(void)
{
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging,
        (ID3D11Resource *)target);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0,
        D3D11_MAP_READ, 0, &mapped))) return NULL;
    for (unsigned y = 0; y < TARGET_HEIGHT; ++y)
        memcpy(pixels + y * TARGET_WIDTH,
            (const uint8_t *)mapped.pData + y * mapped.RowPitch, TARGET_WIDTH * 4);
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    return pixels;
}

static int draw_texture(ID3D11ShaderResourceView *view, unsigned mip, int use_mip_shader)
{
    D3D11_SAMPLER_DESC sampler = {
        .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
        .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
        .ComparisonFunc = D3D11_COMPARISON_NEVER,
        .MaxLOD = D3D11_FLOAT32_MAX,
    };
    ID3D11SamplerState *state;
    if (FAILED(ID3D11Device_CreateSamplerState(device, &sampler, &state))) return 0;
    const float clear[4] = {1, 0, 1, 1};
    ID3D11DeviceContext_OMSetRenderTargets(context, 1, &target_view, NULL);
    ID3D11DeviceContext_ClearRenderTargetView(context, target_view, clear);
    D3D11_VIEWPORT viewport = {0, 0, TARGET_WIDTH, TARGET_HEIGHT, 0, 1};
    ID3D11DeviceContext_RSSetViewports(context, 1, &viewport);
    ID3D11DeviceContext_IASetInputLayout(context, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(context,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(context, texture_vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(context, use_mip_shader ? mip_ps : texture_ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(context, 0, 1, &view);
    ID3D11DeviceContext_PSSetSamplers(context, 0, 1, &state);
    if (use_mip_shader) {
        float value[4] = {(float)mip, 0, 0, 0};
        ID3D11DeviceContext_UpdateSubresource(context, (ID3D11Resource *)mip_constant,
            0, NULL, value, 0, 0);
        ID3D11DeviceContext_PSSetConstantBuffers(context, 0, 1, &mip_constant);
    }
    ID3D11DeviceContext_Draw(context, 3, 0);
    ID3D11SamplerState_Release(state);
    return read_target() != NULL;
}

static void fill_color_block(uint8_t *block, unsigned bytes, uint16_t color)
{
    memset(block, 0, bytes);
    uint8_t *color_block = block + (bytes == 16 ? 8 : 0);
    memcpy(color_block, &color, sizeof(color));
    memcpy(color_block + sizeof(color), &color, sizeof(color));
    if (bytes == 16) memset(block, 0xFF, 2);
}

static int test_quadrants(DXGI_FORMAT format, const char *name,
    const uint8_t blocks[4][16], unsigned block_bytes, const uint32_t wanted[4])
{
    D3D11_TEXTURE2D_DESC td = {
        .Width = 8,
        .Height = 8,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = format,
        .SampleDesc = {1, 0},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
    };
    uint8_t packed[4 * 16] = {0};
    for (unsigned i = 0; i < 4; ++i)
        memcpy(packed + i * block_bytes, blocks[i], block_bytes);
    D3D11_SUBRESOURCE_DATA initial = {
        .pSysMem = packed,
        .SysMemPitch = block_bytes * 2,
    };
    ID3D11Texture2D *texture = NULL;
    ID3D11ShaderResourceView *view = NULL;
    HRESULT hr = ID3D11Device_CreateTexture2D(device, &td, &initial, &texture);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)texture,
            NULL, &view);
    char detail[192];
    if (FAILED(hr)) {
        snprintf(detail, sizeof(detail), "hr=0x%08lx", (unsigned long)hr);
        result(name, 0, detail);
        if (view) ID3D11ShaderResourceView_Release(view);
        if (texture) ID3D11Texture2D_Release(texture);
        return 0;
    }
    int ok = draw_texture(view, 0, 0);
    const unsigned points[4][2] = {
        {TARGET_WIDTH / 4, TARGET_HEIGHT / 4},
        {3 * TARGET_WIDTH / 4, TARGET_HEIGHT / 4},
        {TARGET_WIDTH / 4, 3 * TARGET_HEIGHT / 4},
        {3 * TARGET_WIDTH / 4, 3 * TARGET_HEIGHT / 4},
    };
    for (unsigned i = 0; ok && i < 4; ++i) {
        uint32_t actual = pixels[points[i][1] * TARGET_WIDTH + points[i][0]];
        if (!close_pixel(actual, wanted[i], 8)) {
            snprintf(detail, sizeof(detail),
                "pixel=%u actual=0x%08x want=0x%08x hash=0x%08x", i, actual,
                wanted[i], hash_pixels(pixels, TARGET_WIDTH * TARGET_HEIGHT));
            ok = 0;
        }
    }
    if (ok) {
        snprintf(detail, sizeof(detail), "hash=0x%08x",
            hash_pixels(pixels, TARGET_WIDTH * TARGET_HEIGHT));
    }
    result(name, ok, detail);
    ID3D11ShaderResourceView_Release(view);
    ID3D11Texture2D_Release(texture);
    return ok;
}

static void test_small_formats(void)
{
    uint8_t blocks[4][16];
    static const uint16_t colors[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    static const uint32_t wanted[4] = {
        0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu,
    };
    for (unsigned i = 0; i < 4; ++i)
        fill_color_block(blocks[i], 8, colors[i]);
    test_quadrants(DXGI_FORMAT_BC1_UNORM, "bc1_unorm", blocks, 8, wanted);
    test_quadrants(DXGI_FORMAT_BC1_UNORM_SRGB, "bc1_unorm_srgb", blocks, 8, wanted);

    for (unsigned i = 0; i < 4; ++i) {
        memset(blocks[i], 0, 16);
        blocks[i][0] = blocks[i][1] = 0xFF;
        fill_color_block(blocks[i] + 8, 8, colors[i]);
    }
    test_quadrants(DXGI_FORMAT_BC3_UNORM, "bc3_unorm", blocks, 16, wanted);
    test_quadrants(DXGI_FORMAT_BC3_UNORM_SRGB, "bc3_unorm_srgb", blocks, 16, wanted);

    static const uint8_t channels[4] = {255, 128, 64, 0};
    uint32_t wanted4[4];
    for (unsigned i = 0; i < 4; ++i) {
        fill_color_block(blocks[i], 8, (uint16_t)(channels[i] | (channels[i] << 8)));
        wanted4[i] = rgba(channels[i], 0, 0, 255);
    }
    test_quadrants(DXGI_FORMAT_BC4_UNORM, "bc4_unorm", blocks, 8, wanted4);

    uint32_t wanted5[4];
    for (unsigned i = 0; i < 4; ++i) {
        memset(blocks[i], 0, 16);
        blocks[i][0] = blocks[i][1] = channels[i];
        blocks[i][8] = blocks[i][9] = channels[3 - i];
        wanted5[i] = rgba(channels[i], channels[3 - i], 0, 255);
    }
    test_quadrants(DXGI_FORMAT_BC5_UNORM, "bc5_unorm", blocks, 16, wanted5);
}

static void put_bits(uint8_t *block, unsigned *bit, unsigned value, unsigned count)
{
    for (unsigned i = 0; i < count; ++i, ++*bit)
        if ((value >> i) & 1) block[*bit >> 3] |= (uint8_t)(1u << (*bit & 7));
}

static void test_bc7(void)
{
    uint8_t blocks[4][16] = {{0}};
    static const uint8_t colors[4][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255},
    };
    uint32_t wanted[4];
    for (unsigned i = 0; i < 4; ++i) {
        unsigned bit = 0;
        put_bits(blocks[i], &bit, 1u << 6, 7);
        for (unsigned channel = 0; channel < 3; ++channel) {
            put_bits(blocks[i], &bit, colors[i][channel] >> 1, 7);
            put_bits(blocks[i], &bit, colors[i][channel] >> 1, 7);
        }
        put_bits(blocks[i], &bit, 127, 7);
        put_bits(blocks[i], &bit, 127, 7);
        for (unsigned channel = 0; channel < 3; ++channel)
            put_bits(blocks[i], &bit, colors[i][channel] & 1, 1);
        wanted[i] = rgba(colors[i][0], colors[i][1], colors[i][2], 255);
    }
    test_quadrants(DXGI_FORMAT_BC7_UNORM, "bc7_unorm", blocks, 16, wanted);
}

static int create_solid_texture(DXGI_FORMAT format, unsigned size, unsigned mips,
    uint16_t color, ID3D11Texture2D **out, ID3D11ShaderResourceView **view)
{
    D3D11_TEXTURE2D_DESC td = {
        .Width = size,
        .Height = size,
        .MipLevels = mips,
        .ArraySize = 1,
        .Format = format,
        .SampleDesc = {1, 0},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
    };
    D3D11_SUBRESOURCE_DATA *initial = calloc(mips, sizeof(*initial));
    uint8_t **storage = calloc(mips, sizeof(*storage));
    if (!initial || !storage) {
        free(initial);
        free(storage);
        return 0;
    }
    unsigned block_bytes = format == DXGI_FORMAT_BC1_UNORM ? 8 : 16;
    for (unsigned mip = 0; mip < mips; ++mip) {
        unsigned width = size >> mip;
        unsigned height = size >> mip;
        if (!width) width = 1;
        if (!height) height = 1;
        unsigned row_bytes = ((width + 3) / 4) * block_bytes;
        unsigned rows = (height + 3) / 4;
        size_t bytes = (size_t)row_bytes * rows;
        storage[mip] = malloc(bytes);
        if (!storage[mip]) {
            for (unsigned i = 0; i < mip; ++i) free(storage[i]);
            free(storage);
            free(initial);
            return 0;
        }
        for (unsigned row = 0; row < rows; ++row)
            for (unsigned column = 0; column < (width + 3) / 4; ++column)
                fill_color_block(storage[mip] + row * row_bytes + column * block_bytes,
                    block_bytes, color);
        initial[mip].pSysMem = storage[mip];
        initial[mip].SysMemPitch = row_bytes;
    }
    HRESULT hr = ID3D11Device_CreateTexture2D(device, &td, initial, out);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)*out,
            NULL, view);
    for (unsigned mip = 0; mip < mips; ++mip) free(storage[mip]);
    free(storage);
    free(initial);
    return SUCCEEDED(hr);
}

static void test_large_format(DXGI_FORMAT format, const char *name, uint16_t color,
    uint32_t wanted)
{
    ID3D11Texture2D *texture = NULL;
    ID3D11ShaderResourceView *view = NULL;
    char detail[192];
    if (!create_solid_texture(format, LARGE_SIZE, 12, color, &texture, &view)) {
        snprintf(detail, sizeof(detail), "create_failed size=%u", LARGE_SIZE);
        result(name, 0, detail);
        return;
    }
    int ok = draw_texture(view, 0, 0);
    uint32_t actual = pixels[(TARGET_HEIGHT / 2) * TARGET_WIDTH + TARGET_WIDTH / 2];
    if (!close_pixel(actual, wanted, 8)) ok = 0;
    snprintf(detail, sizeof(detail), "actual=0x%08x want=0x%08x hash=0x%08x",
        actual, wanted, hash_pixels(pixels, TARGET_WIDTH * TARGET_HEIGHT));
    result(name, ok, detail);
    ID3D11ShaderResourceView_Release(view);
    ID3D11Texture2D_Release(texture);
}

static void test_mip_chain(DXGI_FORMAT format, const char *name)
{
    enum { SIZE = 64, MIPS = 3 };
    static const uint16_t colors[MIPS] = {0xF800, 0x07E0, 0x001F};
    static const uint32_t wanted[MIPS] = {
        0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u,
    };
    uint8_t storage[MIPS][64 * 64 / 2];
    D3D11_SUBRESOURCE_DATA initial[MIPS] = {{0}};
    for (unsigned mip = 0; mip < MIPS; ++mip) {
        unsigned size = SIZE >> mip;
        unsigned blocks = (size + 3) / 4;
        memset(storage[mip], 0, sizeof(storage[mip]));
        for (unsigned y = 0; y < blocks; ++y)
            for (unsigned x = 0; x < blocks; ++x)
                fill_color_block(storage[mip] + (y * blocks + x) * 8, 8,
                    colors[mip]);
        initial[mip].pSysMem = storage[mip];
        initial[mip].SysMemPitch = blocks * 8;
    }
    D3D11_TEXTURE2D_DESC td = {
        .Width = SIZE,
        .Height = SIZE,
        .MipLevels = MIPS,
        .ArraySize = 1,
        .Format = format,
        .SampleDesc = {1, 0},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
    };
    ID3D11Texture2D *texture = NULL;
    ID3D11ShaderResourceView *view = NULL;
    HRESULT hr = ID3D11Device_CreateTexture2D(device, &td, initial, &texture);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)texture,
            NULL, &view);
    char detail[192];
    if (FAILED(hr)) {
        snprintf(detail, sizeof(detail), "hr=0x%08lx", (unsigned long)hr);
        result(name, 0, detail);
        return;
    }
    int ok = 1;
    for (unsigned mip = 0; ok && mip < MIPS; ++mip) {
        ok = draw_texture(view, mip, 1);
        /* Sample the centre of texel (1, 1), not the seam at 0.5. */
        uint32_t actual = pixels[(TARGET_HEIGHT * 3 / 8) * TARGET_WIDTH +
            TARGET_WIDTH * 3 / 8];
        if (!close_pixel(actual, wanted[mip], 8)) {
            snprintf(detail, sizeof(detail),
                "mip=%u actual=0x%08x want=0x%08x", mip, actual, wanted[mip]);
            ok = 0;
        }
    }
    if (ok)
        snprintf(detail, sizeof(detail), "hash=0x%08x",
            hash_pixels(pixels, TARGET_WIDTH * TARGET_HEIGHT));
    result(name, ok, detail);
    ID3D11ShaderResourceView_Release(view);
    ID3D11Texture2D_Release(texture);
}

static uint16_t read_u16(const uint8_t *value)
{
    return (uint16_t)(value[0] | ((uint16_t)value[1] << 8));
}

static uint32_t read_u32(const uint8_t *value)
{
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
        ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static uint32_t expand_565(uint16_t color, uint8_t alpha)
{
    unsigned r = (color >> 11) & 31;
    unsigned g = (color >> 5) & 63;
    unsigned b = color & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return rgba((uint8_t)r, (uint8_t)g, (uint8_t)b, alpha);
}

static uint32_t decode_bc1_texel(const uint8_t *block, unsigned x, unsigned y)
{
    uint16_t c0 = read_u16(block);
    uint16_t c1 = read_u16(block + 2);
    uint32_t indices = read_u32(block + 4);
    unsigned index = (indices >> (2 * (y * 4 + x))) & 3;
    uint32_t palette[4];
    palette[0] = expand_565(c0, 255);
    palette[1] = expand_565(c1, 255);
    if (c0 > c1) {
        unsigned r = (2 * ((c0 >> 11) & 31) + ((c1 >> 11) & 31)) / 3;
        unsigned g = (2 * ((c0 >> 5) & 63) + ((c1 >> 5) & 63)) / 3;
        unsigned b = (2 * (c0 & 31) + (c1 & 31)) / 3;
        palette[2] = expand_565((uint16_t)((r << 11) | (g << 5) | b), 255);
        r = (((c0 >> 11) & 31) + 2 * ((c1 >> 11) & 31)) / 3;
        g = (((c0 >> 5) & 63) + 2 * ((c1 >> 5) & 63)) / 3;
        b = ((c0 & 31) + 2 * (c1 & 31)) / 3;
        palette[3] = expand_565((uint16_t)((r << 11) | (g << 5) | b), 255);
    } else {
        unsigned r = (((c0 >> 11) & 31) + ((c1 >> 11) & 31)) / 2;
        unsigned g = (((c0 >> 5) & 63) + ((c1 >> 5) & 63)) / 2;
        unsigned b = ((c0 & 31) + (c1 & 31)) / 2;
        palette[2] = expand_565((uint16_t)((r << 11) | (g << 5) | b), 255);
        palette[3] = 0;
    }
    return palette[index];
}

static void test_real_asset(void)
{
    int ok = 1;
    char mismatch[192] = "";
    for (unsigned i = 0; ok && i < bc_real_sample_block_count; ++i) {
        D3D11_TEXTURE2D_DESC td = {
            .Width = 4,
            .Height = 4,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_BC1_UNORM,
            .SampleDesc = {1, 0},
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        };
        D3D11_SUBRESOURCE_DATA initial = {
            .pSysMem = bc_real_sample_blocks + i * BC_REAL_BLOCK_BYTES,
            .SysMemPitch = BC_REAL_BLOCK_BYTES,
        };
        ID3D11Texture2D *texture = NULL;
        ID3D11ShaderResourceView *view = NULL;
        HRESULT hr = ID3D11Device_CreateTexture2D(device, &td, &initial, &texture);
        if (SUCCEEDED(hr))
            hr = ID3D11Device_CreateShaderResourceView(device,
                (ID3D11Resource *)texture, NULL, &view);
        if (FAILED(hr)) {
            snprintf(mismatch, sizeof(mismatch), "block=%u create_hr=0x%08lx",
                i, (unsigned long)hr);
            ok = 0;
            break;
        }
        ok = draw_texture(view, 0, 0);
        for (unsigned y = 0; ok && y < 4; ++y) {
            for (unsigned x = 0; x < 4; ++x) {
                uint32_t expected = decode_bc1_texel(
                    bc_real_sample_blocks + i * BC_REAL_BLOCK_BYTES, x, y);
                unsigned target_x = x * 16 + 8;
                unsigned target_y = y * 16 + 8;
                uint32_t actual = pixels[target_y * TARGET_WIDTH + target_x];
                /* BC1 endpoint interpolation has implementation-defined rounding. */
                if (!close_pixel(actual, expected, 24)) {
                    snprintf(mismatch, sizeof(mismatch),
                        "block=%u texel=%u,%u actual=0x%08x want=0x%08x",
                        i, x, y, actual, expected);
                    ok = 0;
                    break;
                }
            }
        }
        ID3D11ShaderResourceView_Release(view);
        ID3D11Texture2D_Release(texture);
    }
    char detail[192];
    snprintf(detail, sizeof(detail), "format=%s size=%ux%u source_mip=%u hash=0x%08x%s%s",
        BC_REAL_FORMAT_NAME, BC_REAL_WIDTH, BC_REAL_HEIGHT, BC_REAL_MIP,
        hash_pixels(pixels, TARGET_WIDTH * TARGET_HEIGHT),
        mismatch[0] ? " " : "", mismatch);
    result("lostcoast_dxt1_blocks", ok, detail);
}

static void destroy(void)
{
    if (target_view) ID3D11RenderTargetView_Release(target_view);
    if (staging) ID3D11Texture2D_Release(staging);
    if (target) ID3D11Texture2D_Release(target);
    if (mip_constant) ID3D11Buffer_Release(mip_constant);
    if (mip_ps) ID3D11PixelShader_Release(mip_ps);
    if (texture_ps) ID3D11PixelShader_Release(texture_ps);
    if (texture_vs) ID3D11VertexShader_Release(texture_vs);
    if (context) ID3D11DeviceContext_Release(context);
    if (device) ID3D11Device_Release(device);
}

int main(void)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER started;
    LARGE_INTEGER ended;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    printf("BC_DEMO_ENV transcode=%s fast=%s trace=%s\n",
        getenv("HYBRIS_BC_TRANSCODE") ? getenv("HYBRIS_BC_TRANSCODE") : "default",
        getenv("HYBRIS_BC_FAST") ? getenv("HYBRIS_BC_FAST") : "default",
        getenv("HYBRIS_VULKAN_TRACE") ? getenv("HYBRIS_VULKAN_TRACE") : "default");
    if (!initialize()) {
        fprintf(stderr, "BC_DEMO_INIT_FAIL\n");
        destroy();
        return 2;
    }
    test_small_formats();
    test_bc7();
    test_large_format(DXGI_FORMAT_BC1_UNORM, "bc1_large_fast", 0xF800,
        0xFF0000FFu);
    test_large_format(DXGI_FORMAT_BC3_UNORM, "bc3_large_fast", 0x07E0,
        0xFF00FF00u);
    test_mip_chain(DXGI_FORMAT_BC1_UNORM, "bc1_mip_chain");
    test_mip_chain(DXGI_FORMAT_BC1_UNORM_SRGB, "bc1_srgb_mip_chain");
    test_real_asset();
    QueryPerformanceCounter(&ended);
    double milliseconds = (double)(ended.QuadPart - started.QuadPart) * 1000.0 /
        (double)frequency.QuadPart;
    printf("BC_DEMO_SUMMARY checks=%u failures=%u milliseconds=%.3f\n",
        checks, failures, milliseconds);
    destroy();
    return failures ? 1 : 0;
}
