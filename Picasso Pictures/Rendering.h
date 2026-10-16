#pragma once
// Rendering.h : device resources, the D3D11 mip-mapped image pipeline, and
// the frame renderer.
//
// Owns everything that has to be thrown away and rebuilt when the D3D device
// is lost or the render target changes size, plus the per-frame draw.
//
// Read the control-flow notes in CODE_MAP.md section 4 before changing
// anything here - the sequencing is load-bearing:
//   * Render() runs in THREE passes when the D3D11 mip path is active
//     (D2D bg+shadow -> EndDraw -> D3D11 quad -> D2D overlays -> EndDraw ->
//     Present), and deliberately in ONE pass otherwise, so a frame with no
//     D3D11 work is never split across two targets.
//   * RenderImageD3D11 must run *between* D2D EndDraw/BeginDraw pairs.
//   * DiscardDeviceResources() deliberately does NOT touch the g_wic*
//     sources - that is what lets the desktop capture survive a
//     fullscreen <-> slideshow round trip.
//   * SwapChainBitmapProps() is shared by CreateRenderTarget and WndProc's
//     WM_SIZE handler; both must describe the target bitmap identically.
//
// Linkage rules for this module:
//   * SwapChainBitmapProps has external linkage - WndProc's WM_SIZE calls it.
//   * Mat4Mul, BuildImageToClip, ExtractMipAsD2DBitmap, RenderImageD3D11,
//     CreateSmallShadowSourceViaWic, DrawVignetteBar and DrawCoverBg are all
//     called only from inside this file, so they stay `static` in
//     Rendering.cpp and are deliberately NOT declared here.
//   * The embedded HLSL (g_imageVSHLSL / g_imagePSHLSL) and the ImageVertex /
//     CBImageTransform PODs are likewise Rendering.cpp-private.

#include "Globals.h"

// ---- default / captured background bitmaps ----
// LoadDefaultBackgroundFromResource reads the embedded JPG resource
// (IDR_DEFAULT_BKG_JPG) into g_background.wicDefaultBitmap; the two Create*Bitmap
// functions turn a device-independent WIC source into a device bitmap.
bool LoadDefaultBackgroundFromResource();
bool CreateDefaultBackgroundBitmap();
bool CreateBackgroundBitmap();

// ---- device / render-target lifecycle ----
// CreateRenderTarget is idempotent: it returns immediately if a target
// already exists. DiscardDeviceResources resets every GPU object but leaves
// the WIC sources and the effects' input wiring alone.
void DiscardDeviceResources();
void CreateRenderTarget(HWND hWnd);

// The one set of properties used for every swap-chain target bitmap.
D2D1_BITMAP_PROPERTIES1 SwapChainBitmapProps();

// ---- D3D11 mip-mapped image path ----
// CreateD3DImagePipeline builds the device-once objects (shaders, sampler,
// dynamic VB/CB, blend/raster/depth states) and sets g_pipeline.ready.
// CreateMipTextureFromSource uploads one WIC image into a full mip chain and
// calls GenerateMips on the GPU. RecreateImageBitmap is the pair that
// re-uploads the current image (and its mip chain + shadow source) after a
// device loss, a fullscreen switch or a new file.
void CreateD3DImagePipeline();
bool CreateMipTextureFromSource(IWICBitmapSource* wicSrc);
void RecreateImageBitmap();

// ---- slideshow background ----
// Mip-extract (~200 px) from the live GPU mip chain, then CLSID_D2D1GaussianBlur,
// with a CPU WIC downscale fallback when there is no mip chain (animated
// images skip the mip pipeline on purpose). Must run outside BeginDraw/EndDraw.
void CreateSlideshowBgBitmap();

// ---- the frame ----
// Bails unless hWnd is the active window, lazily re-creates whatever device
// resources a device loss dropped, then draws. See CODE_MAP.md section 4 for
// the pass order.
void Render(HWND hWnd);
