#pragma once
// Decoding.h : image loading + the per-format decoders (AVIF, JXL, GIF,
// WebP, APNG) and the animated-playback glue.
//
// Read CODE_MAP.md section 6 before changing anything here - the dispatch
// order inside LoadImageD2D (AVIF -> JXL -> WebP -> GIF -> APNG -> static +
// EXIF) and the shared g_anim* state are load-bearing: every animated format
// ends up in the same frame vectors and the same playback code, only the
// per-frame decode routine differs.
//
// Only the entry points that something outside this file actually calls are
// declared here. Everything else in Decoding.cpp is deliberately file-local
// and NOT declared here:
//   * `static` helpers that stayed static: LoadFail, SniffIsobmffBrands,
//     AvifResultToWide, ConvertAvifImageToWicBitmap, AvifPeekFrameCount,
//     DecodeAvifFrames, DecodeGifCompositeFrames, DecodeWebpFrames,
//     ApngCrcTable, ApngCrc32, ApngBe32, PeekApngFrameCount,
//     DecodeApngFrames, BeginAnimatedPlayback, OpenThreadLocalWicFactory,
//     OpenThreadLocalWicDecoder, SpawnAnimDecodeThread, FinishImageLoad,
//     and the DecodeRemainingFramesFn alias they share.
//   * the anonymous-namespace compositing primitives (imin/imax/umin,
//     ReadUIntMeta, CloneBitmap, ClearRect, DecodeToPBGRA, BlendOntoCanvas),
//     which live inside one `namespace {` block and so have internal linkage.
//
// Linkage rules for this module:
//   * StopAnimDecodeThread has external linkage - WndProc's WM_DESTROY joins
//     the decode thread on shutdown.
//   * AvifOpenAndParse, DecodeAvifToWicBitmap and DecodeJxlToWicBitmap have
//     external linkage because the AVIF/JXL metadata readers in Metadata.cpp
//     reuse them. Their default arguments live HERE and only here (C2522 if
//     they also appear on the definitions).

#include "Globals.h"

#include <avif/avif.h>   // avifDecoder, needed by AvifOpenAndParse below

// ---- the one entry point everything else uses ----
// Dispatch order: AVIF -> JXL -> WebP -> GIF -> APNG -> static (+ EXIF
// orientation). Saves the outgoing view state into g_view.stateByPath[path] first,
// so returning to a file restores how you left it. Clears g_image.lastLoadError on
// entry; every failure funnels through the file-local LoadFail, so callers
// (OpenImageFile, WM_DROPFILES, WM_COPYDATA) read g_image.lastLoadError right after
// a false return.
bool LoadImageD2D(HWND hWnd, const wchar_t* filename);

// Stops (and joins) the background frame-decoding thread if one is running.
// Every LoadImageD2D call starts with this; WndProc's WM_DESTROY also calls it
// so the thread cannot be writing into g_anim.frames while the process exits.
void StopAnimDecodeThread();

// ---- shared AVIF / JXL entry points, reused by the metadata readers ----
// AvifOpenAndParse creates + parses an avifDecoder without decoding pixels;
// caller owns the result and must avifDecoderDestroy() it. Returns nullptr on
// failure, with a human-readable reason in *outReason when given.
avifDecoder* AvifOpenAndParse(const wchar_t* path, std::wstring* outReason = nullptr);

// Decode one AVIF / JXL still image into a premultiplied-alpha WIC bitmap.
// outW/outH receive the decoded size; outReason optionally carries the reason
// for a failure (the same string that ends up in g_image.lastLoadError).
bool DecodeAvifToWicBitmap(const wchar_t* path, IWICImagingFactory* wicFactory, ComPtr<IWICBitmap>& outBitmap, UINT& outW, UINT& outH, std::wstring* outReason = nullptr);
bool DecodeJxlToWicBitmap(const wchar_t* path, IWICImagingFactory* wicFactory, ComPtr<IWICBitmap>& outBitmap, UINT& outW, UINT& outH, std::wstring* outReason = nullptr);
