// Rendering.cpp : device resources, the D3D11 mip-mapped image pipeline, and
// the frame renderer.
//
// Linkage rules for this file:
//   * SwapChainBitmapProps has external linkage - WndProc's WM_SIZE handler
//     calls it to re-create the D2D target bitmap from the new swap-chain
//     buffer.
//   * ExtractMipAsD2DBitmap and RenderImageD3D11 are used before they are
//     defined, so they are declared file-locally below. RenderImageD3D11's
//     default argument belongs to that declaration ONLY (C2522 if it also
//     appears on the definition).

#include "Rendering.h"
#include "Fullscreen.h"   // Render tears down the slideshow black-cover window
#include "Thumbnails.h"   // Render draws the thumbnail film strip
#include "UI.h"           // Render re-fits the image via InitializeImageLayout

#include <cmath>
#include <cstring>
#include <algorithm>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

// ============================================================================
// File-local helpers that stay internal to this translation unit.
// They are declared here only because they are used before their definition
// further down this file; none of them is called from another module, so none
// of them belongs in Rendering.h. Mat4Mul, BuildImageToClip,
// CreateSmallShadowSourceViaWic, DrawVignetteBar and DrawCoverBg are defined
// before first use and need no declaration.
// ============================================================================
static ComPtr<ID2D1Bitmap> ExtractMipAsD2DBitmap(UINT targetMaxPx);
static void RenderImageD3D11(float imgW, float imgH, float opacity, bool isPrev = false);

bool LoadDefaultBackgroundFromResource()
{
    if (g_background.wicDefaultBitmap)
        return true;

    HRSRC hRes = FindResourceW(g_app.instance, MAKEINTRESOURCEW(IDR_DEFAULT_BKG_JPG), L"JPG");
    if (!hRes)
        return false;

    HGLOBAL hData = LoadResource(g_app.instance, hRes);
    if (!hData)
        return false;

    void* pData = LockResource(hData);
    DWORD size = SizeofResource(g_app.instance, hRes);

    ComPtr<IWICStream> stream;
    if (FAILED(g_devices.wicFactory->CreateStream(&stream)))
        return false;

    if (FAILED(stream->InitializeFromMemory(
        reinterpret_cast<BYTE*>(pData),
        size)))
        return false;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(g_devices.wicFactory->CreateDecoderFromStream(
        stream.Get(),
        nullptr,
        WICDecodeMetadataCacheOnLoad,
        &decoder)))
        return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)))
        return false;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(g_devices.wicFactory->CreateFormatConverter(&converter)))
        return false;

    if (FAILED(converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom)))
        return false;

    g_background.wicDefaultBitmap = converter;

    return true;
}

void DiscardDeviceResources()
{
    // D2D bitmaps
    g_view.bitmap.Reset();
    g_background.bitmap.Reset();
    g_background.defaultBitmap.Reset();
    g_devices.d2dTargetBitmap.Reset();
    g_slideshow.bgBitmap.Reset();
    g_slideshow.prevBgBitmap.Reset();
    g_slideshow.prevBitmap.Reset();
    g_anim.gpuFrames.clear();

    // Clear GPU-side thumbnail bitmaps (WIC source survives for re-upload)
    for (auto& t : g_thumb.entries)
        t.d2d.Reset();

    // Cached brushes
    g_pipeline.dimBrush.Reset();
    g_pipeline.blackBrush.Reset();

    // D2D context
    g_devices.renderTarget.Reset();

    // DXGI / D3D
    g_devices.swapChain.Reset();
    g_devices.d3dContext.Reset();
    g_devices.d3dDevice.Reset();
    g_devices.d2dDevice.Reset();
    g_background.shadowEffect.Reset();
    g_slideshow.blurEffect.Reset();
    g_background.shadowSourceBitmap.Reset();
    g_devices.renderTargetWindow = nullptr;

    // D3D11 mip image pipeline
    g_pipeline.mipTex.Reset();
    g_pipeline.srv.Reset();
    g_pipeline.vs.Reset();
    g_pipeline.ps.Reset();
    g_pipeline.il.Reset();
    g_pipeline.vb.Reset();
    g_pipeline.cb.Reset();
    g_pipeline.sampler.Reset();
    g_pipeline.blend.Reset();
    g_pipeline.rast.Reset();
    g_pipeline.ds.Reset();
    g_devices.swapRTV.Reset();
    g_pipeline.ready = false;
}

bool CreateDefaultBackgroundBitmap()
{
    if (!g_devices.renderTarget || !g_background.wicDefaultBitmap)
        return false;

    g_background.defaultBitmap.Reset();

    return SUCCEEDED(
        g_devices.renderTarget->CreateBitmapFromWicBitmap(
            g_background.wicDefaultBitmap.Get(),
            nullptr,
            g_background.defaultBitmap.GetAddressOf()
        )
    );
}

bool CreateBackgroundBitmap()
{
    if (!g_devices.renderTarget || !g_background.wicBitmap)
        return false;

    g_background.bitmap.Reset();

    HRESULT hr = g_devices.renderTarget->CreateBitmapFromWicBitmap(
        g_background.wicBitmap.Get(),
        nullptr,
        g_background.bitmap.GetAddressOf()
    );

    if (FAILED(hr))
    {
        OutputDebugString(L"CreateBackgroundBitmap failed\n");
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------
// Builds a small blurred D2D bitmap used as the slideshow background fill.
// Instead of doing an expensive CPU WIC downscale, we pull a small mip
// level directly from the already-built GPU mip chain via a staging readback
// - the GPU already downscaled the image for free as part of GenerateMips.
// Must be called outside BeginDraw/EndDraw (or before BeginDraw in Render).
// -----------------------------------------------------------------------
void CreateSlideshowBgBitmap()
{
    g_slideshow.bgBitmap.Reset();

    if (!g_devices.renderTarget || !g_slideshow.blurEffect)
        return;

    // ----- 1. Get a small source bitmap -----
    // Prefer the GPU mip chain (fast, already computed).
    // Fall back to the WIC CPU path if the mip texture is not yet available
    // (e.g. during the very first frame before RecreateImageBitmap finishes).
    ComPtr<ID2D1Bitmap> smallBmp;

    if (g_pipeline.mipTex)
    {
        // Pick a mip whose longest edge is ~200 px - enough colour detail for a blur wash.
        smallBmp = ExtractMipAsD2DBitmap(200u);
    }

    if (!smallBmp)
    {
        // CPU fallback (original path): WIC downscale to <=400 px then upload.
        if (!g_image.source || !g_devices.wicFactory) return;

        UINT fullW = 0, fullH = 0;
        if (FAILED(g_image.source->GetSize(&fullW, &fullH)) || fullW == 0 || fullH == 0)
            return;

        const UINT TARGET_MAX = 400u;
        float scaleRatio = min((float)TARGET_MAX / fullW, (float)TARGET_MAX / fullH);
        const UINT smallW = max(1u, (UINT)(fullW * scaleRatio));
        const UINT smallH = max(1u, (UINT)(fullH * scaleRatio));

        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(g_devices.wicFactory->CreateBitmapScaler(&scaler))) return;
        if (FAILED(scaler->Initialize(g_image.source.Get(), smallW, smallH,
                                       WICBitmapInterpolationModeHighQualityCubic))) return;

        ComPtr<IWICFormatConverter> conv;
        if (FAILED(g_devices.wicFactory->CreateFormatConverter(&conv))) return;
        if (FAILED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.f,
                                     WICBitmapPaletteTypeCustom))) return;

        ComPtr<ID2D1Bitmap> tmp;
        if (FAILED(g_devices.renderTarget->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &tmp)))
            return;
        smallBmp = tmp;
    }

    if (!smallBmp) return;

    D2D1_SIZE_U smallSz = smallBmp->GetPixelSize();

    // ----- 2. Create off-screen render target at the small bitmap's size -----
    D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    // ----- 3. Render the Gaussian blur into the off-screen bitmap -----
    // If the image has an EXIF rotation we must rotate the small source bitmap
    // before blurring so the background matches the displayed orientation.
    g_slideshow.blurEffect->SetInput(0, smallBmp.Get());

    ComPtr<ID2D1Image> prevTarget;
    g_devices.renderTarget->GetTarget(&prevTarget);

    // For 90/270 rotations the blurred bg needs to be portrait, not landscape -
    // create the offscreen target with swapped dimensions in that case.
    const float rotMod = fmodf(fabsf(g_view.exifRotation), 180.f);
    const bool swapDims = (rotMod > 44.f && rotMod < 136.f);
    D2D1_SIZE_U bgSz = swapDims
        ? D2D1::SizeU(smallSz.height, smallSz.width)
        : smallSz;

    ComPtr<ID2D1Bitmap1> offscreenBmp;
    if (FAILED(g_devices.renderTarget->CreateBitmap(
            bgSz, nullptr, 0, bmpProps, &offscreenBmp))) return;

    g_devices.renderTarget->SetTarget(offscreenBmp.Get());
    g_devices.renderTarget->BeginDraw();
    g_devices.renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    if (g_view.exifRotation != 0.f)
    {
        // Rotate around the centre of the (possibly swapped) offscreen target.
        const float cx = bgSz.width  * 0.5f;
        const float cy = bgSz.height * 0.5f;
        // The blur effect output origin is at (0,0) of the small source bitmap.
        // We need to centre the source in the destination, then rotate.
        const float srcCx = smallSz.width  * 0.5f;
        const float srcCy = smallSz.height * 0.5f;
        D2D1_MATRIX_3X2_F xform =
            D2D1::Matrix3x2F::Translation(-srcCx, -srcCy) *
            D2D1::Matrix3x2F::Rotation(g_view.exifRotation) *
            D2D1::Matrix3x2F::Translation(cx, cy);
        g_devices.renderTarget->SetTransform(xform);
    }

    g_devices.renderTarget->DrawImage(g_slideshow.blurEffect.Get(), D2D1::Point2F(0.f, 0.f));
    g_devices.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    g_devices.renderTarget->EndDraw();

    g_devices.renderTarget->SetTarget(prevTarget.Get());

    // ----- 4. Store -----
    g_slideshow.bgBitmap = offscreenBmp;
}


void CreateRenderTarget(HWND hWnd)
{
    if (g_devices.renderTarget)
        return;

    RECT rc;
    GetClientRect(hWnd, &rc);

    UINT width  = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;

    if (width == 0 || height == 0)
        return;

    HRESULT hr;

    // 1. Create D3D11 device
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &g_devices.d3dDevice,
        &featureLevel,
        &g_devices.d3dContext);

    if (FAILED(hr))
        return;

    // 2. Get DXGI device (local - only needed here to create the swap chain)
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = g_devices.d3dDevice.As(&dxgiDevice);
    if (FAILED(hr))
        return;

    // 3. Create D2D device + context
    hr = g_devices.d2dFactory->CreateDevice(dxgiDevice.Get(), &g_devices.d2dDevice);
    if (FAILED(hr))
        return;

    hr = g_devices.d2dDevice->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
        &g_devices.renderTarget);   // your DeviceContext
    if (FAILED(hr))
        return;

    // 4. Create DXGI factory
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr))
        return;

    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(__uuidof(IDXGIFactory2),
                            reinterpret_cast<void**>(factory.GetAddressOf()));
    if (FAILED(hr))
        return;

    // 5. Swap chain description
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width  = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;

    hr = factory->CreateSwapChainForHwnd(
        g_devices.d3dDevice.Get(),
        hWnd,
        &desc,
        nullptr,
        nullptr,
        &g_devices.swapChain);

    if (FAILED(hr))
        return;

    // 6. Create target bitmap
    ComPtr<IDXGISurface> surface;
    hr = g_devices.swapChain->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr))
        return;

    D2D1_BITMAP_PROPERTIES1 props = SwapChainBitmapProps();

    hr = g_devices.renderTarget->CreateBitmapFromDxgiSurface(
        surface.Get(),
        &props,
        &g_devices.d2dTargetBitmap);

    if (FAILED(hr))
        return;
    g_devices.renderTarget->SetTarget(g_devices.d2dTargetBitmap.Get());

    if (!g_background.shadowEffect)
    {
        HRESULT hr = g_devices.renderTarget->CreateEffect(
            CLSID_D2D1Shadow,
            &g_background.shadowEffect);

        if (SUCCEEDED(hr))
        {
            g_background.shadowEffect->SetValue(
                D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION,
                5.0f);   // softness
        }
    }

    if (!g_slideshow.blurEffect)
    {
        HRESULT hr = g_devices.renderTarget->CreateEffect(
            CLSID_D2D1GaussianBlur,
            &g_slideshow.blurEffect);

        if (SUCCEEDED(hr))
        {
            // The std-dev is in pixels of the small bitmap this effect is fed - a
            // ~200 px mip extract, or a <=400 px WIC downscale when there is no mip
            // chain - so 4 px is already a wide wash before the blurred result is
            // stretched to cover the screen.
            g_slideshow.blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 4.0f);
            // HARD: leave the effect's output at the input's bounds, which keeps the
            // wash opaque all the way to the edge. SOFT expands the output and fades
            // the added margin to transparent black - tried, and after DrawCoverBg
            // stretches this small wash to cover the screen that fade reads as a dark
            // band down two opposite edges (left/right for a 4:3 image on a 16:9
            // screen). So HARD is deliberate, not an oversight.
            g_slideshow.blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
        }
    }

    // Create cached brushes (reused every frame - avoids per-frame COM allocations)
    g_devices.renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 1.f), &g_pipeline.dimBrush);
    g_devices.renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 1.f), &g_pipeline.blackBrush);

    // Create a D3D11 RTV wrapping the swap-chain back buffer.
    // This RTV is used later to bind the back buffer as a D3D11 render target
    // in-between two D2D BeginDraw/EndDraw pairs so the D3D11 mip quad draw
    // is composited on top of the D2D background / below the D2D UI.
    {
        ComPtr<ID3D11Texture2D> backBuf;
        if (SUCCEEDED(g_devices.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuf))))
            g_devices.d3dDevice->CreateRenderTargetView(backBuf.Get(), nullptr, &g_devices.swapRTV);
    }

    // Build shaders, sampler, states for the D3D11 mip-mapped image path.
    CreateD3DImagePipeline();
}

// Returns the standard D2D1_BITMAP_PROPERTIES1 used for swap-chain target bitmaps.
D2D1_BITMAP_PROPERTIES1 SwapChainBitmapProps()
{
    return D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
}

// ============================================================
//  D3D11 mip-mapped image rendering pipeline
// ============================================================

// Embedded HLSL - vertex shader
static const char* g_imageVSHLSL = R"(
cbuffer CBTransform : register(b0)
{
    row_major float4x4 g_transform;
    float    g_opacity;
    float3   g_pad;
};
struct VSIn  { float2 pos : POSITION; float2 uv  : TEXCOORD0; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut main(VSIn v)
{
    VSOut o;
    o.pos = mul(g_transform, float4(v.pos, 0.0f, 1.0f));
    o.uv  = v.uv;
    return o;
}
)";

// Embedded HLSL - pixel shader (trilinear comes from the sampler state)
static const char* g_imagePSHLSL = R"(
Texture2D    g_texture : register(t0);
SamplerState g_sampler : register(s0);
cbuffer CBTransform : register(b0)
{
    row_major float4x4 g_transform;
    float    g_opacity;
    float3   g_pad;
};
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    float4 col = g_texture.Sample(g_sampler, uv);
    col *= g_opacity;   // premultiplied-alpha opacity
    return col;
}
)";

struct ImageVertex { float x, y, u, v; };

struct alignas(16) CBImageTransform {
    float m[16];        // row-major 4x4  image-space -> clip-space
    float opacity;
    float pad[3];
};

// Simple row-major 4x4 multiply: C = A * B
static void Mat4Mul(const float A[16], const float B[16], float C[16])
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            C[r*4+c] = 0.f;
            for (int k = 0; k < 4; ++k)
                C[r*4+c] += A[r*4+k] * B[k*4+c];
        }
}

// Build a row-major matrix that maps image-space points (x in [0,imgW], y in [0,imgH])
// directly to clip-space (NDC), accounting for pan, zoom, and rotation.
static void BuildImageToClip(float out[16],
    float imgW,    float imgH,
    float zoom,    float offsetX,  float offsetY,
    float rotDeg,
    float screenW, float screenH)
{
    const float PI = 3.14159265359f;
    float theta = rotDeg * (PI / 180.f);
    float cosT  = cosf(theta);
    float sinT  = sinf(theta);

    // Screen-space centre of the image (rotation pivot)
    float cx = offsetX + imgW * zoom * 0.5f;
    float cy = offsetY + imgH * zoom * 0.5f;

    // S: image-space -> screen-space  (x_s = x_i*zoom + offsetX)
    float S[16] = {
        zoom, 0.f,  0.f, offsetX,
        0.f,  zoom, 0.f, offsetY,
        0.f,  0.f,  1.f, 0.f,
        0.f,  0.f,  0.f, 1.f,
    };

    // R: 2-D rotation around (cx, cy) in screen-space
    float R[16] = {
         cosT, -sinT, 0.f,  cx*(1.f-cosT) + cy*sinT,
         sinT,  cosT, 0.f,  cy*(1.f-cosT) - cx*sinT,
         0.f,   0.f,  1.f,  0.f,
         0.f,   0.f,  0.f,  1.f,
    };

    // N: screen-space -> NDC
    float N[16] = {
         2.f/screenW,          0.f, 0.f, -1.f,
         0.f,         -2.f/screenH, 0.f,  1.f,
         0.f,                  0.f, 1.f,  0.f,
         0.f,                  0.f, 0.f,  1.f,
    };

    float RS[16], NRS[16];
    Mat4Mul(R, S, RS);
    Mat4Mul(N, RS, NRS);
    memcpy(out, NRS, 16 * sizeof(float));
}

// Create all device-once objects for the D3D11 image draw path.
// Called once per device creation (from CreateRenderTarget).
void CreateD3DImagePipeline()
{
    g_pipeline.ready = false;
    if (!g_devices.d3dDevice || !g_devices.d3dContext) return;

    HRESULT hr;

    // ----- Compile shaders at runtime -----
    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;

    hr = D3DCompile(g_imageVSHLSL, strlen(g_imageVSHLSL),
        "ImageVS", nullptr, nullptr, "main", "vs_4_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob)
            OutputDebugStringA((char*)errBlob->GetBufferPointer());
        return;
    }

    hr = D3DCompile(g_imagePSHLSL, strlen(g_imagePSHLSL),
        "ImagePS", nullptr, nullptr, "main", "ps_4_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob)
            OutputDebugStringA((char*)errBlob->GetBufferPointer());
        return;
    }

    hr = g_devices.d3dDevice->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_pipeline.vs);
    if (FAILED(hr)) return;

    hr = g_devices.d3dDevice->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_pipeline.ps);
    if (FAILED(hr)) return;

    // ----- Input layout: POSITION float2, TEXCOORD0 float2 -----
    D3D11_INPUT_ELEMENT_DESC ilDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_devices.d3dDevice->CreateInputLayout(
        ilDesc, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_pipeline.il);
    if (FAILED(hr)) return;

    // ----- Dynamic vertex buffer (4 vertices, updated every draw) -----
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth      = sizeof(ImageVertex) * 4;
    vbDesc.Usage          = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_devices.d3dDevice->CreateBuffer(&vbDesc, nullptr, &g_pipeline.vb);
    if (FAILED(hr)) return;

    // ----- Dynamic constant buffer -----
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth      = sizeof(CBImageTransform);
    cbDesc.Usage          = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_devices.d3dDevice->CreateBuffer(&cbDesc, nullptr, &g_pipeline.cb);
    if (FAILED(hr)) return;

    // ----- Trilinear sampler (the key to cache-friendly zoom-out) -----
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;  // trilinear
    sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.MipLODBias     = 0.0f;
    sampDesc.MaxAnisotropy  = 1;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD         = 0.f;
    sampDesc.MaxLOD         = D3D11_FLOAT32_MAX;
    hr = g_devices.d3dDevice->CreateSamplerState(&sampDesc, &g_pipeline.sampler);
    if (FAILED(hr)) return;

    // ----- Pre-multiplied alpha blend state -----
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable           = TRUE;
    blendDesc.RenderTarget[0].SrcBlend              = D3D11_BLEND_ONE;        // src already premultiplied
    blendDesc.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = g_devices.d3dDevice->CreateBlendState(&blendDesc, &g_pipeline.blend);
    if (FAILED(hr)) return;

    // ----- Rasterizer: no culling (quad is always front-facing) -----
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    hr = g_devices.d3dDevice->CreateRasterizerState(&rastDesc, &g_pipeline.rast);
    if (FAILED(hr)) return;

    // ----- Depth-stencil: no depth test -----
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable   = FALSE;
    dsDesc.StencilEnable = FALSE;
    hr = g_devices.d3dDevice->CreateDepthStencilState(&dsDesc, &g_pipeline.ds);
    if (FAILED(hr)) return;

    g_pipeline.ready = true;
}

// Upload a WIC image into a D3D11 Texture2D with a full mip chain,
// then call GenerateMips so the GPU can trilinearly filter during zoom-out.
// The full-resolution pixels are uploaded to mip 0; all lower mips are
// generated on the GPU (fast, high quality).
bool CreateMipTextureFromSource(IWICBitmapSource* wicSrc)
{
    g_pipeline.mipTex.Reset();
    g_pipeline.srv.Reset();

    if (!wicSrc || !g_devices.d3dDevice || !g_devices.d3dContext) return false;

    UINT srcW = 0, srcH = 0;
    if (FAILED(wicSrc->GetSize(&srcW, &srcH)) || srcW == 0 || srcH == 0) return false;

    // Clamp to D3D11 maximum texture dimension
    const UINT D3D_MAX = 16384u;
    float ratio = min(1.0f, min((float)D3D_MAX / srcW, (float)D3D_MAX / srcH));
    UINT texW = max(1u, (UINT)(srcW * ratio));
    UINT texH = max(1u, (UINT)(srcH * ratio));

    // Compute how many mip levels fit
    UINT mipLevels = 1u + (UINT)floorf(log2f((float)max(texW, texH)));

    // Create texture with D3D11_RESOURCE_MISC_GENERATE_MIPS so GenerateMips works.
    // We must include D3D11_BIND_RENDER_TARGET for GenerateMips to work.
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width              = texW;
    texDesc.Height             = texH;
    texDesc.MipLevels          = 0;    // 0 = allocate the full mip chain
    texDesc.ArraySize          = 1;
    texDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count   = 1;
    texDesc.Usage              = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags          = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    texDesc.MiscFlags          = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(g_devices.d3dDevice->CreateTexture2D(&texDesc, nullptr, &tex))) return false;

    // ----- Get the actual mip count that was allocated -----
    tex->GetDesc(&texDesc);   // mipLevels is now filled by the driver

    // ----- Decode (and optionally scale) the image to BGRA pixels -----
    ComPtr<IWICBitmapSource> src = wicSrc;

    // Downscale via WIC if the source exceeds our texture dimensions
    if (srcW != texW || srcH != texH)
    {
        ComPtr<IWICBitmapScaler> scaler;
        ComPtr<IWICFormatConverter> conv;
        if (SUCCEEDED(g_devices.wicFactory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(wicSrc, texW, texH,
                                         WICBitmapInterpolationModeHighQualityCubic)) &&
            SUCCEEDED(g_devices.wicFactory->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                        WICBitmapDitherTypeNone, nullptr, 0.f,
                                        WICBitmapPaletteTypeCustom)))
            src = conv;
        else
            return false;
    }
    else
    {
        // Still need BGRA pixel format
        GUID fmt = {};
        wicSrc->GetPixelFormat(&fmt);
        if (!IsEqualGUID(fmt, GUID_WICPixelFormat32bppPBGRA))
        {
            ComPtr<IWICFormatConverter> conv;
            if (SUCCEEDED(g_devices.wicFactory->CreateFormatConverter(&conv)) &&
                SUCCEEDED(conv->Initialize(wicSrc, GUID_WICPixelFormat32bppPBGRA,
                                            WICBitmapDitherTypeNone, nullptr, 0.f,
                                            WICBitmapPaletteTypeCustom)))
                src = conv;
        }
    }

    // Copy pixels from WIC
    const UINT stride  = texW * 4;
    const size_t bytes = (size_t)stride * texH;
    std::vector<BYTE> pixels(bytes);
    WICRect rc { 0, 0, (INT)texW, (INT)texH };
    if (FAILED(src->CopyPixels(&rc, stride, (UINT)bytes, pixels.data()))) return false;

    // Upload mip 0
    g_devices.d3dContext->UpdateSubresource(tex.Get(), 0, nullptr, pixels.data(), stride, 0);

    // ----- Create SRV (all mip levels) so the sampler can access them -----
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_B8G8R8A8_UNORM;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = (UINT)-1;  // all levels

    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(g_devices.d3dDevice->CreateShaderResourceView(tex.Get(), &srvDesc, &srv))) return false;

    // ----- GPU-generate all lower mip levels (very fast) -----
    g_devices.d3dContext->GenerateMips(srv.Get());

    g_pipeline.mipTex = tex;
    g_pipeline.srv    = srv;
    return true;
}

// Extract one mip level from g_pipeline.mipTex as a small D2D bitmap.
// targetMaxPx is the desired maximum dimension - we pick the smallest mip
// level whose max dimension is still >= targetMaxPx (so we never upscale).
// The GPU already computed all mip levels, so this is just a cheap readback.
static ComPtr<ID2D1Bitmap> ExtractMipAsD2DBitmap(UINT targetMaxPx)
{
    if (!g_pipeline.mipTex || !g_devices.d3dDevice || !g_devices.d3dContext || !g_devices.renderTarget)
        return nullptr;

    D3D11_TEXTURE2D_DESC texDesc = {};
    g_pipeline.mipTex->GetDesc(&texDesc);

    // Walk down the mip chain until the next level would be smaller than targetMaxPx.
    UINT mipLevel = 0;
    for (UINT m = 1; m < texDesc.MipLevels; ++m)
    {
        UINT mW = max(1u, texDesc.Width  >> m);
        UINT mH = max(1u, texDesc.Height >> m);
        if (max(mW, mH) < targetMaxPx) break;
        mipLevel = m;
    }

    UINT mipW = max(1u, texDesc.Width  >> mipLevel);
    UINT mipH = max(1u, texDesc.Height >> mipLevel);

    // Staging texture - CPU-readable copy of the chosen mip level.
    D3D11_TEXTURE2D_DESC stagDesc = {};
    stagDesc.Width              = mipW;
    stagDesc.Height             = mipH;
    stagDesc.MipLevels          = 1;
    stagDesc.ArraySize          = 1;
    stagDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagDesc.SampleDesc.Count   = 1;
    stagDesc.Usage              = D3D11_USAGE_STAGING;
    stagDesc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(g_devices.d3dDevice->CreateTexture2D(&stagDesc, nullptr, &staging)))
        return nullptr;

    g_devices.d3dContext->CopySubresourceRegion(
        staging.Get(), 0, 0, 0, 0,
        g_pipeline.mipTex.Get(),
        D3D11CalcSubresource(mipLevel, 0, texDesc.MipLevels),
        nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(g_devices.d3dContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return nullptr;

    D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap> bmp;
    g_devices.renderTarget->CreateBitmap(
        D2D1::SizeU(mipW, mipH),
        mapped.pData,
        mapped.RowPitch,
        bmpProps,
        &bmp);

    g_devices.d3dContext->Unmap(staging.Get(), 0);
    return bmp;
}

// Draw the image as a D3D11 textured quad with trilinear filtering.
// Must be called BETWEEN two D2D EndDraw/BeginDraw pairs (not inside BeginDraw).
// Uses the global g_view.zoom / g_view.offsetX / g_view.offsetY / g_view.rotationAngle to build
// the transform matrix each frame.
static void RenderImageD3D11(float imgW, float imgH, float opacity, bool /*isPrev*/)
{
    if (!g_pipeline.ready || !g_pipeline.srv || !g_devices.swapRTV) return;
    if (!g_devices.d3dDevice || !g_devices.d3dContext)                    return;
    if (opacity < 0.001f)                                  return;

    const D2D1_SIZE_F rtSize = g_devices.renderTarget->GetSize();
    const float W = rtSize.width;
    const float H = rtSize.height;

    // ----- Update constant buffer -----
    CBImageTransform cb = {};
    BuildImageToClip(cb.m, imgW, imgH,
                     g_view.zoom, g_view.offsetX, g_view.offsetY,
                     g_view.rotationAngle, W, H);
    cb.opacity = opacity;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(g_devices.d3dContext->Map(g_pipeline.cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &cb, sizeof(cb));
        g_devices.d3dContext->Unmap(g_pipeline.cb.Get(), 0);
    }

    // ----- Update dynamic vertex buffer (quad in image space) -----
    ImageVertex verts[4] = {
        { 0.f,  0.f,  0.f, 0.f },
        { imgW, 0.f,  1.f, 0.f },
        { 0.f,  imgH, 0.f, 1.f },
        { imgW, imgH, 1.f, 1.f },
    };
    if (SUCCEEDED(g_devices.d3dContext->Map(g_pipeline.vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, verts, sizeof(verts));
        g_devices.d3dContext->Unmap(g_pipeline.vb.Get(), 0);
    }

    // ----- Set pipeline state -----
    D3D11_VIEWPORT vp = {};
    vp.Width    = W;
    vp.Height   = H;
    vp.MaxDepth = 1.0f;
    g_devices.d3dContext->RSSetViewports(1, &vp);
    g_devices.d3dContext->RSSetState(g_pipeline.rast.Get());

    g_devices.d3dContext->OMSetRenderTargets(1, g_devices.swapRTV.GetAddressOf(), nullptr);
    g_devices.d3dContext->OMSetBlendState(g_pipeline.blend.Get(), nullptr, 0xFFFFFFFF);
    g_devices.d3dContext->OMSetDepthStencilState(g_pipeline.ds.Get(), 0);

    g_devices.d3dContext->IASetInputLayout(g_pipeline.il.Get());
    UINT stride = sizeof(ImageVertex), offset = 0;
    g_devices.d3dContext->IASetVertexBuffers(0, 1, g_pipeline.vb.GetAddressOf(), &stride, &offset);
    g_devices.d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    g_devices.d3dContext->VSSetShader(g_pipeline.vs.Get(), nullptr, 0);
    g_devices.d3dContext->VSSetConstantBuffers(0, 1, g_pipeline.cb.GetAddressOf());

    g_devices.d3dContext->PSSetShader(g_pipeline.ps.Get(), nullptr, 0);
    g_devices.d3dContext->PSSetShaderResources(0, 1, g_pipeline.srv.GetAddressOf());
    g_devices.d3dContext->PSSetSamplers(0, 1, g_pipeline.sampler.GetAddressOf());
    g_devices.d3dContext->PSSetConstantBuffers(0, 1, g_pipeline.cb.GetAddressOf());

    g_devices.d3dContext->Draw(4, 0);

    // ----- Unbind so D2D / the debug layer don't complain -----
    ID3D11ShaderResourceView* nullSRV = nullptr;
    g_devices.d3dContext->PSSetShaderResources(0, 1, &nullSRV);
    ID3D11RenderTargetView*   nullRTV = nullptr;
    g_devices.d3dContext->OMSetRenderTargets(0, &nullRTV, nullptr);
}

// Cheap CPU-side downscale used as the shadow-effect input for animated
// formats, where we deliberately skip the D3D11 mip chain (see
// RecreateImageBitmap). Mirrors the WIC fallback path in CreateSlideshowBgBitmap.
static ComPtr<ID2D1Bitmap> CreateSmallShadowSourceViaWic(IWICBitmapSource* wicSrc, UINT targetMaxPx)
{
    if (!wicSrc || !g_devices.wicFactory || !g_devices.renderTarget) return nullptr;

    UINT fullW = 0, fullH = 0;
    if (FAILED(wicSrc->GetSize(&fullW, &fullH)) || fullW == 0 || fullH == 0) return nullptr;

    float scaleRatio = min(1.0f, min((float)targetMaxPx / fullW, (float)targetMaxPx / fullH));
    const UINT smallW = max(1u, (UINT)(fullW * scaleRatio));
    const UINT smallH = max(1u, (UINT)(fullH * scaleRatio));

    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(g_devices.wicFactory->CreateBitmapScaler(&scaler))) return nullptr;
    if (FAILED(scaler->Initialize(wicSrc, smallW, smallH, WICBitmapInterpolationModeHighQualityCubic)))
        return nullptr;

    ComPtr<IWICFormatConverter> conv;
    if (FAILED(g_devices.wicFactory->CreateFormatConverter(&conv))) return nullptr;
    if (FAILED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                 WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom)))
        return nullptr;

    ComPtr<ID2D1Bitmap> bmp;
    if (FAILED(g_devices.renderTarget->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &bmp))) return nullptr;
    return bmp;
}

void RecreateImageBitmap()
{
    if (!g_image.source || !g_devices.renderTarget)
        return;

    g_view.bitmap.Reset();
    g_background.shadowSourceBitmap.Reset();

    // D3D11 maximum texture dimension is 16384. If the source exceeds this,
    // downscale via WIC before creating the D2D bitmap.
    const UINT D3D_MAX = 16384u;
    UINT srcW = 0, srcH = 0;
    g_image.source->GetSize(&srcW, &srcH);

    ComPtr<IWICBitmapSource> bitmapSourceForD2D = g_image.source;

    if (srcW > D3D_MAX || srcH > D3D_MAX)
    {
        float ratio = min((float)D3D_MAX / srcW, (float)D3D_MAX / srcH);
        UINT scaledW = max(1u, (UINT)(srcW * ratio));
        UINT scaledH = max(1u, (UINT)(srcH * ratio));

        ComPtr<IWICBitmapScaler> scaler;
        ComPtr<IWICFormatConverter> conv;
        if (SUCCEEDED(g_devices.wicFactory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(g_image.source.Get(), scaledW, scaledH,
                                         WICBitmapInterpolationModeHighQualityCubic)) &&
            SUCCEEDED(g_devices.wicFactory->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                        WICBitmapDitherTypeNone, nullptr, 0.f,
                                        WICBitmapPaletteTypeCustom)))
        {
            bitmapSourceForD2D = conv;
        }
    }

    HRESULT hr = g_devices.renderTarget->CreateBitmapFromWicBitmap(
        bitmapSourceForD2D.Get(),
        nullptr,
        g_view.bitmap.GetAddressOf()
    );

    if (FAILED(hr))
    {           
        OutputDebugString(L"RecreateImageBitmap failed\n");
        g_view.bitmap.Reset();
        return;
    }

    // Also build the D3D11 mip texture for cache-friendly trilinear rendering.
    // We pass the (possibly downscaled) WIC source so the texture matches the D2D bitmap size.
    if (g_pipeline.ready && !g_anim.isAnimated)
    {
        CreateMipTextureFromSource(bitmapSourceForD2D.Get());

        // Extract a ~256 px mip for use as the shadow effect input.
        // The shadow is a blurred silhouette - full resolution is wasted here.
        // This small bitmap is created once per image load and reused every frame.
        g_background.shadowSourceBitmap = ExtractMipAsD2DBitmap(256u);
    }
    else if (g_anim.isAnimated)
    {
        // Animated formats: skip the D3D11 mip chain entirely (no benefit -
        // trilinear filtering isn't worth a per-frame upload+GenerateMips
        // cost for something shown near 1:1 for a few seconds). g_pipeline.srv
        // staying null makes the render path fall back to plain D2D draw
        // automatically. Still build a *small* shadow source via cheap WIC
        // downscale so the drop shadow doesn't blur the full-res frame
        // every single render call.
        g_pipeline.mipTex.Reset();
        g_pipeline.srv.Reset();
        g_background.shadowSourceBitmap = CreateSmallShadowSourceViaWic(bitmapSourceForD2D.Get(), 256u);
    }
}

// Draw a linear-gradient vignette bar across the full width of the render target.
// darkAtBottom=true  -> transparent at topY,    dark at bottomY  (bottom bar)
// darkAtBottom=false -> dark at topY, transparent at bottomY     (top bar)
// visibility scales the overall alpha [0..1].
static void DrawVignetteBar(ID2D1DeviceContext* rt,
                             float topY, float bottomY,
                             float peakAlpha, float visibility,
                             bool darkAtBottom)
{
    if (!rt || peakAlpha < 0.001f || visibility < 0.001f) return;

    D2D1_SIZE_F sz = rt->GetSize();
    const float y0 = topY    * sz.height;
    const float y1 = bottomY * sz.height;

    // stop 0 = startPoint (y0 = topY),  stop 1 = endPoint (y1 = bottomY)
    D2D1_GRADIENT_STOP stops[2];
    if (darkAtBottom)
    {
        stops[0].color = D2D1::ColorF(0.f, 0.f, 0.f, 0.f);               // transparent at top
        stops[1].color = D2D1::ColorF(0.f, 0.f, 0.f, peakAlpha * visibility); // dark at bottom
    }
    else
    {
        stops[0].color = D2D1::ColorF(0.f, 0.f, 0.f, peakAlpha * visibility); // dark at top
        stops[1].color = D2D1::ColorF(0.f, 0.f, 0.f, 0.f);               // transparent at bottom
    }
    stops[0].position = 0.f;
    stops[1].position = 1.f;

    ComPtr<ID2D1GradientStopCollection> coll;
    if (FAILED(rt->CreateGradientStopCollection(stops, 2, &coll)) || !coll)
        return;

    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props;
    props.startPoint = D2D1::Point2F(0.f, y0);
    props.endPoint   = D2D1::Point2F(0.f, y1);

    ComPtr<ID2D1LinearGradientBrush> brush;
    if (FAILED(rt->CreateLinearGradientBrush(props, coll.Get(), &brush)) || !brush)
        return;

    rt->FillRectangle(D2D1::RectF(0.f, y0, sz.width, y1), brush.Get());
}

// Stretches a bitmap to cover the full render target at the given opacity.
static void DrawCoverBg(ID2D1Bitmap* bmp, float alpha)
{
    if (!bmp || !g_devices.renderTarget || alpha <= 0.01f) return;

    D2D1_SIZE_F sz = bmp->GetSize();
    D2D1_SIZE_F rt = g_devices.renderTarget->GetSize();

    float scaleX     = rt.width  / sz.width;
    float scaleY     = rt.height / sz.height;
    float coverScale = max(scaleX, scaleY);

    float destW = sz.width  * coverScale;
    float destH = sz.height * coverScale;
    float destX = (rt.width  - destW) * 0.5f;
    float destY = (rt.height - destH) * 0.5f;

    g_devices.renderTarget->DrawBitmap(
        bmp,
        D2D1::RectF(destX, destY, destX + destW, destY + destH),
        alpha,
        D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
}

void Render(HWND hWnd)
{
    // Only render into the currently active window to avoid accidentally
    // nuking/recreating device resources if the inactive window gets a stray WM_PAINT.
    HWND activeWindow = (g_view.isFullscreen && g_app.overlayWindow) ? g_app.overlayWindow : g_app.mainWindow;
    if (hWnd != activeWindow)
        return;

    CreateRenderTarget(hWnd);

    if (!g_devices.renderTarget || !g_devices.swapChain)
        return;

    // Recreate any missing device-dependent resources (device-loss safe).
    if (g_image.source && !g_view.bitmap)
        RecreateImageBitmap();

    // On device loss, GIF D2D bitmaps are cleared - re-upload only the frame
    // currently on screen. The rest are re-created lazily by UpdateEngine as
    // playback reaches them, same as the initial-load path.
    if (g_anim.isAnimated && !g_anim.frames.empty() && g_anim.gpuFrames.empty())
    {
        g_anim.gpuFrames.assign(g_anim.frames.size(), nullptr);

        if (g_anim.currentFrame < g_anim.frames.size())
        {
            ComPtr<ID2D1Bitmap> bmp;
            g_devices.renderTarget->CreateBitmapFromWicBitmap(g_anim.frames[g_anim.currentFrame].Get(), nullptr, &bmp);
            g_anim.gpuFrames[g_anim.currentFrame] = bmp;
            g_view.bitmap = bmp;
        }
    }

    if (!g_view.bitmap && g_background.wicDefaultBitmap && !g_background.defaultBitmap)
        CreateDefaultBackgroundBitmap();

    if (g_view.isFullscreen && g_background.wicBitmap && !g_background.bitmap)
        CreateBackgroundBitmap();

    // Slideshow blurred-bg: build lazily (off-screen render, must be before BeginDraw)
    if (g_image.source && !g_slideshow.bgBitmap && g_slideshow.blurEffect)
        CreateSlideshowBgBitmap();

    // Lazy D2D upload for one thumbnail per frame - spiral outward from the
    // current image so the visible window is always uploaded first.
    if (!g_thumb.entries.empty() && g_image.index >= 0)
    {
        const int tn  = (int)g_thumb.entries.size();
        const int cur = min(g_image.index, tn - 1);
        for (int d = 0; d < tn; ++d)
        {
            auto tryUpload = [&](int i) -> bool
            {
                if (i < 0 || i >= tn) return false;
                auto& t = g_thumb.entries[i];
                if (t.wic && !t.d2d)
                {
                    g_devices.renderTarget->CreateBitmapFromWicBitmap(t.wic.Get(), nullptr, &t.d2d);
                    return true;  // one upload per frame
                }
                return false;
            };
            if (d == 0) { if (tryUpload(cur))           break; }
            else        { if (tryUpload(cur + d) ||
                             tryUpload(cur - d))         break; }
        }
    }

    g_devices.renderTarget->BeginDraw();

    // Cache the render-target size once - used throughout this function.
    const D2D1_SIZE_F rtSize = g_devices.renderTarget->GetSize();

    // Clear background
    if (g_view.isFullscreen)
        g_devices.renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0.6f));
    else
        g_devices.renderTarget->Clear(D2D1::ColorF(0.4f, 0.4f, 0.4f));

    if (g_fullscreen.needsInit)
    {
        // Ensure resources exist
        if (g_image.source && !g_view.bitmap)
            RecreateImageBitmap();
        if (g_background.wicBitmap && !g_background.bitmap)
            CreateBackgroundBitmap();

        // Initialize layout AFTER everything exists
        InitializeImageLayout(hWnd, g_slideshow.active);

        g_fullscreen.needsInit = false;
    }

    // ---- Background ----
    if (g_slideshow.active)
    {
        // Fade out old blurred bg, fade in new one
        DrawCoverBg(g_slideshow.prevBgBitmap.Get(), 1.0f - g_slideshow.transitionAlpha);
        DrawCoverBg(g_slideshow.bgBitmap.Get(),             g_slideshow.transitionAlpha);

        // Uniform dim overlay on top of the blurred fill
        if (g_pipeline.dimBrush)
        {
            g_pipeline.dimBrush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, 0.45f * g_fullscreen.overlayAlpha));
            g_devices.renderTarget->FillRectangle(D2D1::RectF(0, 0, rtSize.width, rtSize.height), g_pipeline.dimBrush.Get());
        }
    }
    else if (g_view.isFullscreen && g_background.bitmap)
    {
        D2D1_SIZE_F size = g_background.bitmap->GetSize();

        // The stop fade runs "black in, resize at full black, black out", and this
        // swap belongs at full black with the resize - not on the first frame after
        // the slideshow stopped, which is a hard cut from the blurred wash to the
        // desktop capture. That path never sets wasInSlideshowOnExit, so the
        // cross-fade below does not cover it.
        const bool holdBlurred = g_slideshow.stopFade && g_slideshow.stopFadePendingResize &&
                                 g_slideshow.bgBitmap;

        if (holdBlurred)
        {
            DrawCoverBg(g_slideshow.bgBitmap.Get(), 1.0f);
        }
        else
        {
            g_devices.renderTarget->DrawBitmap(
                g_background.bitmap.Get(),
                D2D1::RectF(0, 0, size.width, size.height),
                1.0f
            );

            // When exiting from slideshow, cross-fade the blurred bg out on top of the
            // desktop bg that was just drawn (slides from fully visible down to invisible).
            if (g_fullscreen.pendingExit && g_fullscreen.wasInSlideshowOnExit && g_slideshow.bgBitmap)
                DrawCoverBg(g_slideshow.bgBitmap.Get(), 1.0f - g_fullscreen.exitBgFadeAlpha);
        }

        // Dim overlay: fades in on enter (driven by g_fullscreen.overlayAlpha), fades out on
        // exit (modulated by g_fullscreen.exitBgFadeAlpha so it lifts in sync with the bg fade).
        if (g_pipeline.dimBrush)
        {
            const float dimAlpha = g_fullscreen.pendingExit
                ? g_fullscreen.overlayAlpha * (1.0f - g_fullscreen.exitBgFadeAlpha)
                : g_fullscreen.overlayAlpha;
            g_pipeline.dimBrush->SetColor(D2D1::ColorF(0, 0, 0, 0.67f * dimAlpha));
            g_devices.renderTarget->FillRectangle(
                D2D1::RectF(0, 0, size.width, size.height),
                g_pipeline.dimBrush.Get());
        }
    }    
    else if (!g_view.isFullscreen && g_slideshow.bgBitmap)
    {
        DrawCoverBg(g_slideshow.bgBitmap.Get(), 1.0f);

        // Slight dim so the image on top has contrast
        if (g_pipeline.dimBrush)
        {
            g_pipeline.dimBrush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, 0.35f));
            g_devices.renderTarget->FillRectangle(D2D1::RectF(0, 0, rtSize.width, rtSize.height), g_pipeline.dimBrush.Get());
        }
    }

    // Accumulate image info needed across the three-pass render split
    D2D1_SIZE_F imgSize  = {};
    float       imgOpacity   = 1.0f;
    bool        useD3D11Image = false;

    if (g_view.bitmap)
    {        
        imgSize = g_view.bitmap->GetSize();

        // ---- Draw previous image fading out (slideshow only) ----
        if (g_slideshow.active && g_slideshow.prevBitmap && g_slideshow.transitionAlpha < 0.99f)
        {
            D2D1_SIZE_F prevSize = g_slideshow.prevBitmap->GetSize();

            D2D1::Matrix3x2F prevScale =
                D2D1::Matrix3x2F::Scale(g_slideshow.prevZoom, g_slideshow.prevZoom);
            D2D1::Matrix3x2F prevTranslate =
                D2D1::Matrix3x2F::Translation(g_slideshow.prevOffsetX, g_slideshow.prevOffsetY);
            D2D1::Matrix3x2F prevBase = prevScale * prevTranslate;

            D2D1_POINT_2F prevCenter = prevBase.TransformPoint(
                D2D1::Point2F(prevSize.width * 0.5f, prevSize.height * 0.5f));

            D2D1::Matrix3x2F prevRot =
                D2D1::Matrix3x2F::Rotation(g_slideshow.prevRotation, prevCenter);

            g_devices.renderTarget->SetTransform(prevBase * prevRot);
            g_devices.renderTarget->DrawBitmap(
                g_slideshow.prevBitmap.Get(),
                D2D1::RectF(0, 0, prevSize.width, prevSize.height),
                1.0f - g_slideshow.transitionAlpha,
                D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
            g_devices.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        }

        // ---- Compute transform for current image (shadow + mip quad share it) ----
        float imgCenterX = imgSize.width * 0.5f;
        float imgCenterY = imgSize.height * 0.5f;

        D2D1::Matrix3x2F scale =
            D2D1::Matrix3x2F::Scale(g_view.zoom, g_view.zoom);

        D2D1::Matrix3x2F translate =
            D2D1::Matrix3x2F::Translation(g_view.offsetX, g_view.offsetY);

        D2D1::Matrix3x2F baseTransform = scale * translate;
     
        D2D1_POINT_2F screenCenter =
            baseTransform.TransformPoint(D2D1::Point2F(imgCenterX, imgCenterY));

        D2D1::Matrix3x2F rotation =
            D2D1::Matrix3x2F::Rotation(g_view.rotationAngle, screenCenter);

        g_devices.renderTarget->SetTransform(baseTransform * rotation);

        imgOpacity = g_slideshow.active ? g_slideshow.transitionAlpha : 1.0f;

        // During a fade-to-black the black overlay is drawn in D2D Pass 1 and
        // flushed before the D3D11 image quad is drawn in Pass 2.  Without this
        // modulation the image renders on top of the overlay and never fades.
        // Multiplying here keeps them in sync.
        if (g_slideshow.preFade)
            imgOpacity *= (1.0f - g_slideshow.preFadeAlpha);
        if (g_slideshow.stopFade)
            imgOpacity *= (1.0f - g_slideshow.stopFadeAlpha);

        // Shadow: use the pre-extracted small mip bitmap (set once per image load).
        // The small bitmap covers the same logical image rect as the full one, so we
        // push an extra scale that maps (shadowW x shadowH) -> (imgW x imgH) before
        // applying the normal pan/zoom/rotation transform already on the context.
        // Result is visually identical to using the full bitmap - sigma=25 destroys
        // all detail - but the blur input is maybe 256x170 instead of 8000x5400.
        ID2D1Bitmap* shadowSrc = g_background.shadowSourceBitmap
                                 ? g_background.shadowSourceBitmap.Get()
                                 : (g_view.bitmap ? g_view.bitmap.Get() : nullptr);

        if (g_background.shadowEffect && shadowSrc)
        {
            D2D1_SIZE_F shSz = shadowSrc->GetSize();
            float scaleX = (shSz.width  > 0.f) ? imgSize.width  / shSz.width  : 1.f;
            float scaleY = (shSz.height > 0.f) ? imgSize.height / shSz.height : 1.f;

            // Temporarily extend the current transform with the small->full scale.
            D2D1::Matrix3x2F shadowScale =
                D2D1::Matrix3x2F::Scale(scaleX, scaleY, D2D1::Point2F(0.f, 0.f));

            g_devices.renderTarget->SetTransform(shadowScale * baseTransform * rotation);

            g_background.shadowEffect->SetInput(0, shadowSrc);
            g_devices.renderTarget->PushLayer(
                D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                    D2D1::IdentityMatrix(), imgOpacity),
                nullptr);
            g_devices.renderTarget->DrawImage(g_background.shadowEffect.Get(), D2D1::Point2F(0.0f, 0.0f));
            g_devices.renderTarget->PopLayer();

            // Restore the normal transform for anything drawn after.
            g_devices.renderTarget->SetTransform(baseTransform * rotation);
        }

        // Decide which path draws the image bitmap itself:
        //   D3D11 path  - trilinear mip filtering, cache-friendly at all zoom levels
        //   D2D fallback - used only when the mip pipeline isn't available yet,
        //                  or when the user has enabled the HQ filter option
        useD3D11Image = g_pipeline.ready && (g_pipeline.srv != nullptr) && !g_settings.useHQFilter;

        if (!useD3D11Image)
        {
            // Fallback: D2D cubic (original behaviour, kept for safety)
            g_devices.renderTarget->DrawBitmap(
                g_view.bitmap.Get(),
                D2D1::RectF(0, 0, imgSize.width, imgSize.height),
                imgOpacity,
                D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC
            );
        }

        g_devices.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    else if (g_image.showLoadError && g_devices.dwriteFactory)
    {
        // The last load failed: instead of the default background, show a
        // centered message over a plain dark panel covering the whole target.
        if (!g_view.isFullscreen)
            g_devices.renderTarget->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f));
        else if (g_pipeline.dimBrush)
        {
            g_pipeline.dimBrush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, 0.5f));
            g_devices.renderTarget->FillRectangle(D2D1::RectF(0, 0, rtSize.width, rtSize.height), g_pipeline.dimBrush.Get());
        }

        const float basePx = max(12.f, min(rtSize.width, rtSize.height) * 0.035f);
        ComPtr<IDWriteTextFormat> titleFmt, detailFmt;
        g_devices.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, basePx, L"", &titleFmt);
        g_devices.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, basePx * 0.55f, L"", &detailFmt);

        ComPtr<ID2D1SolidColorBrush> titleBrush, detailBrush;
        g_devices.renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 1.f), &titleBrush);
        g_devices.renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.75f, 0.75f, 0.75f, 1.f), &detailBrush);

        if (titleFmt && detailFmt && titleBrush && detailBrush)
        {
            titleFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            titleFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
            detailFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            detailFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

            const float margin = rtSize.width * 0.08f;
            const float midY   = rtSize.height * 0.5f;

            const std::wstring title = L"This file is corrupted and cannot be opened.";
            g_devices.renderTarget->DrawText(title.c_str(), (UINT32)title.size(), titleFmt.Get(),
                D2D1::RectF(margin, 0.f, rtSize.width - margin, midY), titleBrush.Get());

            std::wstring detail = g_image.loadAttemptName;
            if (!g_image.lastLoadError.empty())
                detail += (detail.empty() ? L"" : L"\n") + g_image.lastLoadError;
            g_devices.renderTarget->DrawText(detail.c_str(), (UINT32)detail.size(), detailFmt.Get(),
                D2D1::RectF(margin, midY + basePx * 0.4f, rtSize.width - margin, rtSize.height),
                detailBrush.Get());
        }
    }
    else if (g_background.defaultBitmap)
    {
        RECT rc;
        GetClientRect(g_app.mainWindow, &rc);

        D2D1_RECT_F destRect = D2D1::RectF(
            0.f,
            0.f,
            (float)(rc.right - rc.left),
            (float)(rc.bottom - rc.top)
        );

        g_devices.renderTarget->DrawBitmap(
            g_background.defaultBitmap.Get(),
            destRect,
            1.0f,
            D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC
        );
    }

    // Full-screen opaque black drawn on top of everything, including UI. The
    // pre-fade covers the window transition into a slideshow; the stop-fade
    // covers the overlay's rect change back out of one. Whichever is darker wins.
    {
        float blackAlpha = 0.0f;
        if (g_slideshow.preFade)
            blackAlpha = max(blackAlpha, g_slideshow.preFadeAlpha);
        if (g_slideshow.stopFade)
            blackAlpha = max(blackAlpha, g_slideshow.stopFadeAlpha);

        if (blackAlpha > 0.01f && g_pipeline.blackBrush)
        {
            g_pipeline.blackBrush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, blackAlpha));
            g_devices.renderTarget->FillRectangle(D2D1::RectF(0, 0, rtSize.width, rtSize.height), g_pipeline.blackBrush.Get());
        }
    }

    // =========================================================
    // When the D3D11 mip path is active we need three passes:
    //   Pass 1 (D2D)   - background + shadow + pre-fade  -> EndDraw
    //   Pass 2 (D3D11) - image quad, trilinear mip filter
    //   Pass 3 (D2D)   - UI buttons + text boxes          -> EndDraw -> Present
    //
    // When D3D11 is NOT needed (no image, or mip pipeline not
    // ready yet) we stay in the original single-pass model so
    // that a second BeginDraw/EndDraw pair never splits a frame
    // that has no D3D11 work between them - avoiding any
    // potential back-buffer visibility issues in flip-model
    // swap chains.
    // =========================================================

    HRESULT hr = S_OK;

    if (useD3D11Image)
    {
        // ----- Pass 1 end: flush D2D background + shadow -----
        hr = g_devices.renderTarget->EndDraw();

        if (SUCCEEDED(hr) && g_devices.swapChain)
        {
            // ----- Pass 2: D3D11 draws the mip-mapped image quad -----
            RenderImageD3D11(imgSize.width, imgSize.height, imgOpacity);

            // ----- Pass 3: D2D draws UI on top -----
            g_devices.renderTarget->BeginDraw();

            // Gradient vignette bars - only when an image is loaded.
            // Bar thickness is fixed in pixels (captured at fullscreen init); we
            // back-convert to normalized fractions for the current RT so the bars
            // never grow/shrink when the window is resized.
            if (g_view.bitmap)
            {
                const float vigRTH        = rtSize.height > 0.f ? rtSize.height : 1.f;
                const float vigTopNorm    = g_vignette.heightsCaptured ? (g_vignette.topBarHeightPx    / vigRTH) : 0.08f;
                const float vigBottomNorm = g_vignette.heightsCaptured ? (g_vignette.bottomBarHeightPx / vigRTH) : 0.14f;

                // Bottom bar: transparent at (1-vigBottomNorm) -> dark at 1.0
                DrawVignetteBar(g_devices.renderTarget.Get(),
                                1.f - vigBottomNorm, 1.f,
                                /*peakAlpha=*/0.65f,
                                g_vignette.bottomVisibility,
                                /*darkAtBottom=*/true);
                // Top bar: dark at 0.0 -> transparent at vigTopNorm
                DrawVignetteBar(g_devices.renderTarget.Get(),
                                0.f, vigTopNorm,
                                /*peakAlpha=*/0.65f,
                                g_vignette.topVisibility,
                                /*darkAtBottom=*/false);
            }

            // Update zoom text box
            {
                static float s_lastDisplayedZoom = -1.f;
                int zoomPct = int(std::round(g_view.zoom * 100));
                if (!g_textBoxes[TextBoxId::ZOOM_INPUT].IsFocused())
                {
                    if (g_view.zoom != s_lastDisplayedZoom)
                    {
                        g_textBoxes[TextBoxId::ZOOM_INPUT].SetText(std::to_wstring(zoomPct));
                        s_lastDisplayedZoom = g_view.zoom;
                    }
                }
            }

            // Thumbnail film strip (fades in/out with bottom vignette)
            DrawThumbnailStrip(g_vignette.bottomVisibility);

            for (auto& [id, btn] : g_buttons)
            {
                if (!g_view.isFullscreen && id == ButtonId::EXIT)
                    continue;
                btn.Draw(g_devices.renderTarget.Get());
            }

            for (auto& [id, txt] : g_textBoxes)
                txt.Draw(g_devices.renderTarget.Get());

            hr = g_devices.renderTarget->EndDraw();

            g_devices.swapChain->Present(1, 0);
            g_fullscreen.navPendingRender = false;  // frame delivered - next key repeat may fire

            if (g_slideshow.preFade && g_slideshow.active && g_app.overlayWindow)
            {
                DestroyBlackCoverWindow();
                g_slideshow.preFade      = false;
                g_slideshow.preFadeAlpha = 0.0f;
            }
        }
    }
    else
    {
        // ----- Single-pass: everything in one BeginDraw/EndDraw -----

        // Gradient vignette bars (same as three-pass path, pixel-fixed thickness)
        if (g_view.bitmap)
        {
                const float vigRTH        = rtSize.height > 0.f ? rtSize.height : 1.f;
                const float vigTopNorm    = g_vignette.heightsCaptured ? (g_vignette.topBarHeightPx    / vigRTH) : 0.08f;
                const float vigBottomNorm = g_vignette.heightsCaptured ? (g_vignette.bottomBarHeightPx / vigRTH) : 0.14f;

                DrawVignetteBar(g_devices.renderTarget.Get(),
                                1.f - vigBottomNorm, 1.f,
                                /*peakAlpha=*/0.65f,
                                g_vignette.bottomVisibility,
                                /*darkAtBottom=*/true);
                DrawVignetteBar(g_devices.renderTarget.Get(),
                                0.f, vigTopNorm,
                                /*peakAlpha=*/0.65f,
                                g_vignette.topVisibility,
                                /*darkAtBottom=*/false);
        }

        // Update zoom text box
        {
            static float s_lastDisplayedZoom = -1.f;
            int zoomPct = int(std::round(g_view.zoom * 100));
            if (!g_textBoxes[TextBoxId::ZOOM_INPUT].IsFocused())
            {
                if (g_view.zoom != s_lastDisplayedZoom)
                {
                    g_textBoxes[TextBoxId::ZOOM_INPUT].SetText(std::to_wstring(zoomPct));
                    s_lastDisplayedZoom = g_view.zoom;
                }
            }
        }

        // Thumbnail film strip (fades in/out with bottom vignette)
        //if (g_view.isFullscreen)
            DrawThumbnailStrip(g_vignette.bottomVisibility);

        for (auto& [id, btn] : g_buttons)
        {
            if (!g_view.isFullscreen && id == ButtonId::EXIT)
                continue;
            btn.Draw(g_devices.renderTarget.Get());
        }

        for (auto& [id, txt] : g_textBoxes)
            txt.Draw(g_devices.renderTarget.Get());

        hr = g_devices.renderTarget->EndDraw();

        if (SUCCEEDED(hr) && g_devices.swapChain)
        {
            g_devices.swapChain->Present(1, 0);
            g_fullscreen.navPendingRender = false;  // frame delivered

            if (g_slideshow.preFade && g_slideshow.active && g_app.overlayWindow)
            {
                DestroyBlackCoverWindow();
                g_slideshow.preFade      = false;
                g_slideshow.preFadeAlpha = 0.0f;
            }
        }
    }

    if (hr == D2DERR_RECREATE_TARGET)
    {
        DiscardDeviceResources();
    }
    // DiscardDeviceResources resets g_devices.renderTarget, so on device loss there is
    // no target left to set a transform on. The next WM_PAINT recreates it
    // (CreateRenderTarget runs before any drawing) and each pass sets its own
    // transform, so this only has to be skipped, not replaced.
    if (g_devices.renderTarget)
        g_devices.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
}
