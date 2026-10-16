// Decoding.cpp : image loading + the per-format decoders (AVIF, JXL, GIF,
// WebP, APNG) and the animated-playback glue.
//
// The compositing primitives (imin/imax/umin, ReadUIntMeta, CloneBitmap,
// ClearRect, DecodeToPBGRA, BlendOntoCanvas) live in one `namespace {`
// block, so they have internal linkage: nothing outside this file calls them.

#include "Decoding.h"
#include "Rendering.h"   // RecreateImageBitmap (FinishImageLoad uploads through it)
#include "ViewMath.h"    // PanFromOffsets / OffsetsFromPan (saved view state)
#include "UI.h"          // UpdateTargetZoom (restoring a saved view state)

// ---- Third-party image codecs (bundled so users don't need OS/Store codecs) ----
// Get these via vcpkg (recommended):
//     vcpkg install libavif dav1d libjxl
// Both build as static libs with the x64-windows-static (or -static-md)
// triplet, so no extra DLLs need to ship alongside PicassoPictures.exe.
#include <avif/avif.h>
#include <jxl/decode.h>
#include <jxl/thread_parallel_runner.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// File-local declarations. Two reasons they are declared here:
//   * order    - FinishImageLoad is defined after LoadImageD2D, which calls it.
//   * defaults - the default arguments for the functions below live ONLY on
//     these declarations, never on the definitions (C2522 otherwise).
// AvifOpenAndParse, DecodeAvifToWicBitmap, DecodeJxlToWicBitmap and
// ConvertAvifImageToWicBitmap are deliberately NOT here: the first three are
// exported through Decoding.h (which carries their defaults) and the last is
// only ever called from inside this file with every argument spelled out.
// Re-declaring any of them here would duplicate the default (C2522).
// ============================================================================
static bool LoadFail(const wchar_t* msg, HRESULT hr = S_OK);
static UINT DecodeAvifFrames(
    const wchar_t* path,
    IWICImagingFactory* wicFactory,
    UINT startFrame,
    UINT maxFrames,
    const std::atomic<bool>* stopFlag,
    UINT* outW,
    UINT* outH,
    const std::function<void(UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)>& onFrame,
    std::wstring* outReason = nullptr);
static bool FinishImageLoad(HWND hWnd, const wchar_t* filename);

// ============================================================
//  Load-failure reporting
// ============================================================
// Every failure path in LoadImageD2D / FinishImageLoad funnels through here
// so the UI can show a specific reason instead of a generic "Failed to load
// image." g_image.lastLoadError is cleared at the top of LoadImageD2D and read by
// the caller (OpenImageFile, WM_DROPFILES, WM_COPYDATA) immediately after a
// failed call.
static bool LoadFail(const wchar_t* msg, HRESULT hr)
{
    g_image.lastLoadError = msg;
    g_image.showLoadError = true;
    if (hr != S_OK)
    {
        wchar_t buf[48];
        swprintf_s(buf, L" (HRESULT 0x%08X)", (unsigned)hr);
        g_image.lastLoadError += buf;
    }
    OutputDebugStringW((g_image.lastLoadError + L"\n").c_str());
    return false;
}

// ============================================================
//  AVIF decoding (via libavif + dav1d, statically linked)
// ============================================================
// Bundled so AVIF works with zero extra installs - no Store "AV1 Video
// Extension" required. Decodes straight into a premultiplied-alpha WIC
// bitmap so everything downstream (RecreateImageBitmap, the D3D11 mip
// path, the shadow effect) sees exactly what it already expects.
//
// NOTE: libavif's public API has shifted slightly across releases. This
// targets the mainstream 1.x API shape (avifDecoderSetIOFile /
// avifImageYUVToRGB). If your vcpkg version differs, <avif/avif.h> is the
// source of truth - the overall shape (parse -> decode frame -> convert to
// RGB -> wrap as WIC bitmap) has been stable for a long time.
//
// KNOWN LIMITATION: AVIF's irot/imir orientation transforms aren't applied
// yet (g_view.exifRotation is left at 0 for AVIF). Most AVIF exporters don't set
// them, but a file that does may appear in its untransformed orientation.
// Reads just enough of a file to report its ISOBMFF major/compatible brands
// (the 'ftyp' box). Used to diagnose "BMFF parsing failed" - the file might
// not actually be AVIF at all (wrong/misleading extension, an AVIF *image
// sequence* ('avis' brand, a different code path than the still-image
// 'avif' brand), or simply truncated/corrupted). This is a light manual
// read of the box header, not a full parser - just enough for a human to
// eyeball in the error message.
static std::wstring SniffIsobmffBrands(const wchar_t* path)
{
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return L"";

    BYTE header[128] = {};
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, header, sizeof(header), &bytesRead, nullptr);
    CloseHandle(hFile);
    if (!ok || bytesRead < 16) return L"file is too small to contain a valid ftyp box";

    auto be32 = [&](DWORD off) -> uint32_t
    {
        return (uint32_t(header[off]) << 24) | (uint32_t(header[off + 1]) << 16) |
               (uint32_t(header[off + 2]) << 8) | uint32_t(header[off + 3]);
    };
    auto tag4 = [&](DWORD off) -> std::wstring
    {
        wchar_t buf[5] = {};
        for (int i = 0; i < 4; ++i)
        {
            BYTE b = header[off + i];
            buf[i] = (b >= 0x20 && b < 0x7f) ? (wchar_t)b : L'?';
        }
        return buf;
    };

    uint32_t boxSize = be32(0);
    std::wstring boxType = tag4(4);
    if (boxType != L"ftyp")
        return L"first box is '" + boxType + L"', not 'ftyp' - this likely isn't a valid AVIF/ISOBMFF file";

    std::wstring majorBrand = tag4(8);
    std::wstring result = L"major brand '" + majorBrand + L"'";
    if (majorBrand == L"avis")
        result += L" (an AVIF image *sequence*, not a still image - not supported by this build)";
    else if (majorBrand != L"avif")
        result += L" (expected 'avif' - this file may be mislabeled or a different format entirely)";

    std::wstring compat;
    DWORD limit = min((DWORD)bytesRead, boxSize);
    limit = min(limit, (DWORD)sizeof(header));
    for (DWORD off = 16; off + 4 <= limit; off += 4)
        compat += tag4(off) + L" ";
    if (!compat.empty())
        result += L", compatible brands: " + compat;

    return result;
}

// avifResultToString() returns a narrow (const char*) string - convert to
// wide so it can flow into g_image.lastLoadError / MessageBox alongside every
// other error path in this file. Shared by every AVIF failure path below.
static std::wstring AvifResultToWide(avifResult r)
{
    const char* narrow = avifResultToString(r);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, nullptr, 0);
    if (wlen <= 0) return L"";
    std::wstring wide(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide.data(), wlen);
    if (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    return wide;
}

// Creates an avifDecoder, points it at the given file, and parses the
// container (reads its box structure and frame count, but doesn't decode
// any pixel data yet). Shared by every AVIF entry point below - the
// static-image decode, the animated per-frame decode, and the frame-count
// peek LoadImageD2D uses to choose between them all start with exactly
// this. Caller owns the returned decoder and must call avifDecoderDestroy()
// on it; returns nullptr on failure.
avifDecoder* AvifOpenAndParse(const wchar_t* path, std::wstring* outReason)
{
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0)
    {
        if (outReason) *outReason = L"invalid file path";
        return nullptr;
    }
    std::string utf8Path(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8Path.data(), utf8Len, nullptr, nullptr);

    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder)
    {
        if (outReason) *outReason = L"couldn't create AVIF decoder";
        return nullptr;
    }

    avifResult r = avifDecoderSetIOFile(decoder, utf8Path.c_str());
    if (r != AVIF_RESULT_OK)
    {
        if (outReason) *outReason = AvifResultToWide(r);
        avifDecoderDestroy(decoder);
        return nullptr;
    }

    r = avifDecoderParse(decoder);
    if (r != AVIF_RESULT_OK)
    {
        // Most common real-world cause of "some AVIF files won't open": a
        // container libavif's demuxer can't parse (truncated file, unusual
        // brand, or a feature this libavif build wasn't compiled with).
        // Sniff the ftyp box so the error names the actual problem instead
        // of just repeating libavif's generic "BMFF parsing failed".
        if (outReason)
        {
            *outReason = AvifResultToWide(r);
            std::wstring brands = SniffIsobmffBrands(path);
            if (!brands.empty())
                *outReason += L" - " + brands;
        }
        avifDecoderDestroy(decoder);
        return nullptr;
    }

    return decoder;
}

// Converts a decoded avifImage's pixels to a premultiplied-alpha WIC
// bitmap. Shared by the static (single still image) and animated
// (per-frame) AVIF decode paths below - the YUV->RGB conversion and WIC
// wrapping is identical either way; only where the avifImage comes from
// (decoder->image after avifDecoderNextImage vs. avifDecoderNthImage)
// differs.
static bool ConvertAvifImageToWicBitmap(const avifImage* image, IWICImagingFactory* wicFactory,
    ComPtr<IWICBitmap>& outBitmap, UINT& outW, UINT& outH, std::wstring* outReason = nullptr)
{
    auto fail = [&](const wchar_t* why) -> bool
    {
        if (outReason) *outReason = why;
        return false;
    };

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, image);
    rgb.format = AVIF_RGB_FORMAT_BGRA;   // byte order WIC's 32bppBGRA expects
    rgb.depth  = 8;                      // downconvert HDR bit depths to 8-bit for display

    if (avifRGBImageAllocatePixels(&rgb) != AVIF_RESULT_OK)
        return fail(L"couldn't allocate pixel buffer");

    avifResult r = avifImageYUVToRGB(image, &rgb);
    if (r != AVIF_RESULT_OK)
    {
        if (outReason) *outReason = AvifResultToWide(r);
        avifRGBImageFreePixels(&rgb);
        return false;
    }

    bool ok = false;
    ComPtr<IWICBitmap> straight;
    if (FAILED(wicFactory->CreateBitmapFromMemory(
            rgb.width, rgb.height,
            GUID_WICPixelFormat32bppBGRA,   // straight (non-premultiplied) alpha
            rgb.rowBytes,
            rgb.rowBytes * rgb.height,
            rgb.pixels,
            straight.GetAddressOf())))
    {
        fail(L"WIC couldn't wrap the decoded pixels");
    }
    else
    {
        ComPtr<IWICFormatConverter> conv;
        if (FAILED(wicFactory->CreateFormatConverter(&conv)) ||
            FAILED(conv->Initialize(straight.Get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom)))
        {
            fail(L"couldn't convert to premultiplied alpha");
        }
        else
        {
            ComPtr<IWICBitmap> cached;
            if (FAILED(wicFactory->CreateBitmapFromSource(conv.Get(), WICBitmapCacheOnLoad, &cached)))
            {
                fail(L"couldn't cache the decoded bitmap");
            }
            else
            {
                outBitmap = cached;
                outW = rgb.width;
                outH = rgb.height;
                ok = true;
            }
        }
    }

    avifRGBImageFreePixels(&rgb);
    return ok;
}

bool DecodeAvifToWicBitmap(const wchar_t* path, IWICImagingFactory* wicFactory, ComPtr<IWICBitmap>& outBitmap, UINT& outW, UINT& outH, std::wstring* outReason)
{
    avifDecoder* decoder = AvifOpenAndParse(path, outReason);
    if (!decoder)
        return false;

    bool ok = false;
    avifResult r = avifDecoderNextImage(decoder);
    if (r != AVIF_RESULT_OK)
    {
        // Most common cause here: 10/12-bit HDR content, or a AV1 profile
        // dav1d in this build doesn't support.
        if (outReason) *outReason = AvifResultToWide(r);
    }
    else
    {
        ok = ConvertAvifImageToWicBitmap(decoder->image, wicFactory, outBitmap, outW, outH, outReason);
    }

    avifDecoderDestroy(decoder);
    return ok;
}

// Parses just far enough to report an AVIF file's frame count, without
// decoding any image data, so LoadImageD2D can choose between the static
// and animated AVIF paths there. Returns 0 on any failure (including a
// perfectly ordinary still AVIF that just has one frame) - the static
// path's own DecodeAvifToWicBitmap call surfaces the real error message if
// there is one.
static UINT AvifPeekFrameCount(const wchar_t* path)
{
    avifDecoder* decoder = AvifOpenAndParse(path);
    if (!decoder)
        return 0;
    UINT count = (UINT)decoder->imageCount;
    avifDecoderDestroy(decoder);
    return count;
}

// ============================================================
//  Animated AVIF frame decode - analogous to DecodeWebpFrames above (and,
//  through it, to DecodeGifCompositeFrames), but AVIF's own thing: it's
//  decoded via libavif rather than WIC, so this opens its own avifDecoder
//  from the file path instead of taking a pre-opened IWICBitmapDecoder.
//  wicFactory is only used for the final YUV->RGB->WIC wrapping step (via
//  ConvertAvifImageToWicBitmap) and must belong to whichever thread is
//  calling this, same reasoning as DecodeGifCompositeFrames.
//
//  avifDecoderNthImage() can seek straight to any frame index - it
//  transparently decodes forward from the nearest keyframe as needed - so
//  like WebP, and unlike GIF's manual disposal replay, frames here don't
//  need to be redecoded from 0 to reach an arbitrary starting point. That's
//  what lets the background continuation for AVIF (in LoadImageD2D below)
//  start at frame 1 directly, the same as WebP.
// ============================================================
static UINT DecodeAvifFrames(
    const wchar_t* path,
    IWICImagingFactory* wicFactory,
    UINT startFrame,
    UINT maxFrames,
    const std::atomic<bool>* stopFlag,
    UINT* outW,
    UINT* outH,
    const std::function<void(UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)>& onFrame,
    std::wstring* outReason)
{
    if (outW) *outW = 0;
    if (outH) *outH = 0;

    avifDecoder* decoder = AvifOpenAndParse(path, outReason);
    if (!decoder)
        return 0;

    const UINT frameCount = (UINT)decoder->imageCount;
    if (frameCount == 0 || startFrame >= frameCount)
    {
        avifDecoderDestroy(decoder);
        return 0;
    }

    const UINT endFrame = (maxFrames == 0) ? frameCount : min(startFrame + maxFrames, frameCount);
    UINT produced = 0;

    for (UINT i = startFrame; i < endFrame; ++i)
    {
        if (stopFlag && stopFlag->load())
            break;

        if (avifDecoderNthImage(decoder, i) != AVIF_RESULT_OK)
            continue;

        ComPtr<IWICBitmap> bmp;
        UINT w = 0, h = 0;
        if (!ConvertAvifImageToWicBitmap(decoder->image, wicFactory, bmp, w, h))
            continue;

        // decoder->imageTiming is populated by the NthImage call just above
        // (duration is in seconds); AVIF delays are effectively continuous,
        // unlike GIF/WebP's integer-millisecond delays, so this is the one
        // place among the three formats that needs a unit conversion.
        UINT delayMs = (UINT)(decoder->imageTiming.duration * 1000.0 + 0.5);
        if (delayMs < 10)
            delayMs = 100;   // fallback for degenerate/zero timing

        if (produced == 0 && outW && outH)
        {
            *outW = w;
            *outH = h;
        }

        onFrame(i, bmp, delayMs);
        ++produced;
    }

    avifDecoderDestroy(decoder);
    return produced;
}

// ============================================================
//  JPEG XL decoding (via libjxl, statically linked)
// ============================================================
// Same bundling rationale as AVIF above. Reads the whole file into memory
// and drives libjxl's one-shot event loop (BASIC_INFO -> FULL_IMAGE); JXL
// files viewed in an image viewer are not expected to be large enough that
// streaming decode is worth the extra complexity.
//
// NOTE: like the AVIF path, check <jxl/decode.h> against your installed
// libjxl version if this doesn't compile as-is - a few struct/field names
// have moved between releases, though the event loop shape has been stable.
//
// KNOWN LIMITATION: only the first frame of an animated JXL is decoded
// (mirrors how AVIF is handled here; static JXL - the overwhelmingly common
// case - is unaffected).
bool DecodeJxlToWicBitmap(const wchar_t* path, IWICImagingFactory* wicFactory, ComPtr<IWICBitmap>& outBitmap, UINT& outW, UINT& outH, std::wstring* outReason)
{
    auto fail = [&](const wchar_t* why) -> bool
    {
        if (outReason) *outReason = why;
        return false;
    };

    std::vector<uint8_t> fileData;
    {
        HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return fail(L"couldn't open the file");

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0)
        {
            CloseHandle(hFile);
            return fail(L"couldn't determine file size");
        }

        fileData.resize((size_t)size.QuadPart);
        DWORD bytesRead = 0;
        BOOL readOk = ReadFile(hFile, fileData.data(), (DWORD)fileData.size(), &bytesRead, nullptr);
        CloseHandle(hFile);
        if (!readOk || bytesRead != fileData.size())
            return fail(L"couldn't read the file");
    }

    JxlDecoder* dec = JxlDecoderCreate(nullptr);
    if (!dec) return fail(L"couldn't create JXL decoder");

    bool ok = false;
    std::vector<uint8_t> pixels;
    JxlBasicInfo info{};

    void* runner = JxlThreadParallelRunnerCreate(
        nullptr, (size_t)max(1u, std::thread::hardware_concurrency()));
    if (runner)
        JxlDecoderSetParallelRunner(dec, JxlThreadParallelRunner, runner);

    if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) == JXL_DEC_SUCCESS)
    {
        JxlDecoderSetInput(dec, fileData.data(), fileData.size());
        JxlDecoderCloseInput(dec);

        JxlPixelFormat format = { 4, JXL_TYPE_UINT8, JXL_LITTLE_ENDIAN, 0 }; // RGBA8, straight alpha

        std::wstring loopFailReason;
        for (;;)
        {
            JxlDecoderStatus status = JxlDecoderProcessInput(dec);

            if (status == JXL_DEC_ERROR)
            {
                loopFailReason = L"the decoder reported a parse/decode error";
                break;
            }

            if (status == JXL_DEC_BASIC_INFO)
            {
                if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS)
                {
                    loopFailReason = L"couldn't read basic image info";
                    break;
                }
                continue;
            }

            if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER)
            {
                size_t bufferSize = 0;
                if (JxlDecoderImageOutBufferSize(dec, &format, &bufferSize) != JXL_DEC_SUCCESS)
                {
                    loopFailReason = L"couldn't determine output buffer size";
                    break;
                }
                pixels.resize(bufferSize);
                if (JxlDecoderSetImageOutBuffer(dec, &format, pixels.data(), pixels.size()) != JXL_DEC_SUCCESS)
                {
                    loopFailReason = L"couldn't set output buffer";
                    break;
                }
                continue;
            }

            if (status == JXL_DEC_FULL_IMAGE)
            {
                // First (or only, for a static image) frame decoded - stop here.
                ok = (info.xsize > 0 && info.ysize > 0 && !pixels.empty());
                if (!ok) loopFailReason = L"decoded frame had no pixel data";
                break;
            }

            if (status == JXL_DEC_SUCCESS)
            {
                loopFailReason = L"stream ended before a full image was produced";
                break;
            }
        }

        if (!ok && outReason && !loopFailReason.empty())
            *outReason = loopFailReason;
    }

    if (runner)
        JxlThreadParallelRunnerDestroy(runner);
    JxlDecoderDestroy(dec);

    if (!ok)
    {
        if (outReason && outReason->empty())
            *outReason = L"decode failed";
        return false;
    }

    ComPtr<IWICBitmap> straight;
    if (FAILED(wicFactory->CreateBitmapFromMemory(
            info.xsize, info.ysize,
            GUID_WICPixelFormat32bppRGBA,
            info.xsize * 4,
            (UINT)pixels.size(),
            pixels.data(),
            straight.GetAddressOf())))
        return fail(L"WIC couldn't wrap the decoded pixels");

    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wicFactory->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(straight.Get(), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom)))
        return fail(L"couldn't convert to premultiplied alpha");

    ComPtr<IWICBitmap> cached;
    if (FAILED(wicFactory->CreateBitmapFromSource(conv.Get(), WICBitmapCacheOnLoad, &cached)))
        return fail(L"couldn't cache the decoded bitmap");

    outBitmap = cached;
    outW = info.xsize;
    outH = info.ysize;
    return true;
}

// ============================================================
//  Shared primitives for the animated-format frame decoders
//  (GIF, WebP, APNG).
//
//  These used to be duplicated as local lambdas inside each
//  Decode*Frames function (and ReadUIntMeta a third time inside
//  LoadImageD2D). APNG added a fourth copy and the bodies were
//  already byte-for-byte identical, which is exactly how subtle
//  drift begins - a premultiply-invariant fix or a blend rounding
//  tweak landing in one path but not the others, so the formats
//  silently composite differently. Hoisting them to a single copy
//  guarantees every format's compositing is identical by
//  construction.
//
//  None of these hold state. The WIC factory is passed explicitly
//  at the call sites that need it (CloneBitmap, DecodeToPBGRA) so
//  the "factory must belong to the calling thread" rule stays
//  visible at each use instead of being hidden in a lambda capture.
// ============================================================
namespace {

// Integer min/max. Lowercase names mirror the originals and sidestep the
// windows.h min/max function-like macros, which would mangle a qualified
// std::min/std::max call.
inline int   imin(int a, int b)   { return a < b ? a : b; }
inline int   imax(int a, int b)   { return a > b ? a : b; }
inline UINT  umin(UINT a, UINT b) { return a < b ? a : b; }

// Reads one metadata value via a WIC query reader and coerces the
// PROPVARIANT integer types WIC actually hands back into a UINT. Shared by
// the GIF/WebP/APNG decoders and by LoadImageD2D's EXIF-orientation read.
bool ReadUIntMeta(IWICMetadataQueryReader* reader, const wchar_t* name, UINT& outVal)
{
    if (!reader) return false;
    PROPVARIANT var;
    PropVariantInit(&var);
    HRESULT h = reader->GetMetadataByName(name, &var);
    if (FAILED(h)) { PropVariantClear(&var); return false; }
    UINT v = 0;
    switch (var.vt)
    {
        case VT_UI1: v = var.bVal;  break;
        case VT_UI2: v = var.uiVal; break;
        case VT_UI4: v = var.ulVal; break;
        case VT_I2:  v = (var.iVal < 0) ? 0u : (UINT)var.iVal; break;
        case VT_I4:  v = (var.lVal < 0) ? 0u : (UINT)var.lVal; break;
        default: PropVariantClear(&var); return false;
    }
    PropVariantClear(&var);
    outVal = v;
    return true;
}

// Copies a WIC source into an independent, cached IWICBitmap on the
// calling thread's factory.
ComPtr<IWICBitmap> CloneBitmap(IWICImagingFactory* wicFactory, IWICBitmapSource* src)
{
    ComPtr<IWICBitmap> clone;
    if (src && wicFactory &&
        SUCCEEDED(wicFactory->CreateBitmapFromSource(src, WICBitmapCacheOnLoad, clone.GetAddressOf())))
        return clone;
    return nullptr;
}

// Zero-fills a rectangular region of a WIC bitmap (transparent premultiplied).
void ClearRect(IWICBitmap* bmp, const WICRect& rc)
{
    if (!bmp) return;
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(bmp->Lock(&rc, WICBitmapLockWrite, lock.GetAddressOf())) || !lock) return;
    UINT cb = 0; BYTE* data = nullptr; UINT stride = 0;
    if (FAILED(lock->GetDataPointer(&cb, &data)) || !data) return;
    if (FAILED(lock->GetStride(&stride)) || stride == 0) return;
    for (UINT y = 0; y < (UINT)rc.Height; ++y)
        memset(data + (size_t)y * stride, 0, (size_t)rc.Width * 4);
}

// Decodes a WIC source (optionally a sub-rectangle) into premultiplied
// BGRA pixels, enforcing the premultiplied invariant (RGB zeroed under
// A==0) so transparent pixels never carry stray color.
bool DecodeToPBGRA(IWICImagingFactory* wicFactory,
                   IWICBitmapSource* src,
                   const WICRect* optionalCopyRect,
                   std::vector<BYTE>& outPixels,
                   UINT& outW, UINT& outH, UINT& outStride)
{
    if (!src || !wicFactory) return false;
    UINT w = 0, h = 0;
    if (FAILED(src->GetSize(&w, &h)) || w == 0 || h == 0) return false;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory->CreateFormatConverter(converter.GetAddressOf())) || !converter)
        return false;
    if (FAILED(converter->Initialize(src, GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom)))
        return false;

    WICRect rc = { 0, 0, (INT)w, (INT)h };
    if (optionalCopyRect)
    {
        int cx = imax(0, optionalCopyRect->X);
        int cy = imax(0, optionalCopyRect->Y);
        int cw = imin(optionalCopyRect->Width,  (int)w - cx);
        int ch = imin(optionalCopyRect->Height, (int)h - cy);
        if (cw <= 0 || ch <= 0) return false;
        rc = { cx, cy, cw, ch };
    }
    const UINT copyW = (UINT)rc.Width, copyH = (UINT)rc.Height;
    const UINT stride = copyW * 4;
    const size_t bufSize = (size_t)stride * copyH;
    outPixels.assign(bufSize, 0);
    if (FAILED(converter->CopyPixels(optionalCopyRect ? &rc : nullptr,
            stride, (UINT)bufSize, outPixels.data())))
        return false;

    for (size_t p = 0; p + 3 < outPixels.size(); p += 4)
        if (outPixels[p + 3] == 0)
            outPixels[p] = outPixels[p + 1] = outPixels[p + 2] = 0;

    outW = copyW; outH = copyH; outStride = stride;
    return true;
}

// Composites a premultiplied BGRA source rectangle onto a WIC canvas at
// (dstX,dstY), clipped to the canvas. blendOver=true does source-over
// (the GIF / APNG BLEND_OP_OVER model); blendOver=false copies the source
// pixels verbatim including alpha (APNG BLEND_OP_SOURCE). Where the two
// modes overlap they share identical source-over math.
void BlendOntoCanvas(IWICBitmap* canvas, int dstX, int dstY,
                     const BYTE* src, UINT srcW, UINT srcH, UINT srcStride,
                     bool blendOver)
{
    if (!canvas || !src || srcW == 0 || srcH == 0) return;
    UINT canvasW = 0, canvasH = 0;
    if (FAILED(canvas->GetSize(&canvasW, &canvasH)) || canvasW == 0 || canvasH == 0) return;

    int x0 = dstX, y0 = dstY, x1 = dstX + (int)srcW, y1 = dstY + (int)srcH;
    if (x1 <= 0 || y1 <= 0 || x0 >= (int)canvasW || y0 >= (int)canvasH) return;
    int clipX0 = imax(0, x0), clipY0 = imax(0, y0);
    int clipX1 = imin((int)canvasW, x1), clipY1 = imin((int)canvasH, y1);
    const UINT drawW = (UINT)(clipX1 - clipX0), drawH = (UINT)(clipY1 - clipY0);
    const UINT srcOffX = (UINT)(clipX0 - x0), srcOffY = (UINT)(clipY0 - y0);

    WICRect lockRc{ clipX0, clipY0, (INT)drawW, (INT)drawH };
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(canvas->Lock(&lockRc, WICBitmapLockWrite, lock.GetAddressOf())) || !lock) return;
    UINT cb = 0; BYTE* dst = nullptr; UINT dstStride = 0;
    if (FAILED(lock->GetDataPointer(&cb, &dst)) || !dst) return;
    if (FAILED(lock->GetStride(&dstStride)) || dstStride == 0) return;

    for (UINT y = 0; y < drawH; ++y)
    {
        BYTE* dRow = dst + (size_t)y * dstStride;
        const BYTE* sRow = src + (size_t)(y + srcOffY) * srcStride + (size_t)srcOffX * 4;
        for (UINT x = 0; x < drawW; ++x)
        {
            const BYTE sb = sRow[x*4+0], sg = sRow[x*4+1], sr = sRow[x*4+2], sa = sRow[x*4+3];
            if (!blendOver)   // BLEND_OP_SOURCE: copy verbatim, alpha included
            {
                dRow[x*4+0]=sb; dRow[x*4+1]=sg; dRow[x*4+2]=sr; dRow[x*4+3]=sa;
                continue;
            }
            if (sa == 0) continue;
            if (sa == 255)
            {
                dRow[x*4+0]=sb; dRow[x*4+1]=sg; dRow[x*4+2]=sr; dRow[x*4+3]=255;
                continue;
            }
            const UINT inv = 255u - sa;
            const UINT db = dRow[x*4+0];
            const UINT dg = dRow[x*4+1];
            const UINT dr = dRow[x*4+2];
            const UINT da = dRow[x*4+3];
            dRow[x*4+0] = (BYTE)(sb + (db * inv + 127) / 255);
            dRow[x*4+1] = (BYTE)(sg + (dg * inv + 127) / 255);
            dRow[x*4+2] = (BYTE)(sr + (dr * inv + 127) / 255);
            dRow[x*4+3] = (BYTE)(sa + (da * inv + 127) / 255);
        }
    }
}

} // anonymous namespace


// ============================================================
//  GIF frame decode/composite. It runs on the main thread (frame 0 only, for
//  an immediate first paint) and on the background continuation thread (the
//  rest of the frames). GIF disposal methods make this inherently sequential:
//  each frame's starting canvas state depends on how the previous frame was
//  disposed, so frames cannot be decoded out of order or in parallel - the
//  only choice is which thread does the decoding.
//
//  The compositing primitives it uses (ClearRect / DecodeToPBGRA /
//  BlendOntoCanvas / CloneBitmap / ReadUIntMeta) are the shared copies in
//  the block above - identical to the ones WebP and APNG use. wicFactory
//  and decoder must both belong to whichever thread is calling this (WIC
//  objects aren't safely shared across the apartment boundary - same reason
//  the thumbnail loader creates its own per-thread factory).
//  maxFrames=0 means "decode all frames"; pass 1 to get just frame 0.
//  Returns the number of frames actually produced (onFrame was called that
//  many times).
// ============================================================
static UINT DecodeGifCompositeFrames(
    IWICImagingFactory* wicFactory,
    IWICBitmapDecoder* decoder,
    UINT frameCount,
    UINT maxFrames,
    const std::atomic<bool>* stopFlag,
    UINT* outCanvasW,
    UINT* outCanvasH,
    const std::function<void(UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)>& onFrame)
{
    if (outCanvasW) *outCanvasW = 0;
    if (outCanvasH) *outCanvasH = 0;
    if (!wicFactory || !decoder || frameCount == 0)
        return 0;

    // Logical screen size (canvas size)
    UINT canvasW = 0, canvasH = 0;
    {
        Microsoft::WRL::ComPtr<IWICMetadataQueryReader> decMeta;
        if (SUCCEEDED(decoder->GetMetadataQueryReader(decMeta.GetAddressOf())) && decMeta)
        {
            ReadUIntMeta(decMeta.Get(), L"/logscrdesc/Width",  canvasW);
            ReadUIntMeta(decMeta.Get(), L"/logscrdesc/Height", canvasH);
        }
    }
    if (canvasW == 0 || canvasH == 0)
    {
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> f0;
        if (SUCCEEDED(decoder->GetFrame(0, f0.GetAddressOf())) && f0)
            f0->GetSize(&canvasW, &canvasH);
    }
    if (canvasW == 0 || canvasH == 0)
        return 0;

    if (outCanvasW) *outCanvasW = canvasW;
    if (outCanvasH) *outCanvasH = canvasH;

    Microsoft::WRL::ComPtr<IWICBitmap> canvas;
    HRESULT hr = wicFactory->CreateBitmap(
        canvasW, canvasH, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, canvas.GetAddressOf());
    if (FAILED(hr) || !canvas)
        return 0;

    ClearRect(canvas.Get(), WICRect{ 0, 0, (INT)canvasW, (INT)canvasH });
    UINT prevDisposal = 0;
    WICRect prevFrameRect{ 0,0,0,0 };
    Microsoft::WRL::ComPtr<IWICBitmap> savedCanvasForDisposal3;

    const UINT limit = (maxFrames == 0) ? frameCount : min(maxFrames, frameCount);
    UINT produced = 0;

    for (UINT i = 0; i < limit; ++i)
    {
        if (stopFlag && stopFlag->load())
            break;

        if (i > 0)
        {
            if (prevDisposal == 2)
            {
                if (prevFrameRect.Width > 0 && prevFrameRect.Height > 0)
                    ClearRect(canvas.Get(), prevFrameRect);
            }
            else if (prevDisposal == 3 && savedCanvasForDisposal3)
            {
                canvas = savedCanvasForDisposal3;
                savedCanvasForDisposal3.Reset();
            }
        }

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(i, frame.GetAddressOf())) || !frame)
            continue;

        UINT left = 0, top = 0;
        UINT w = 0, h = 0;
        UINT disposal = 0;
        UINT delayMs = 100;
        UINT frameW = 0, frameH = 0;
        frame->GetSize(&frameW, &frameH);
        Microsoft::WRL::ComPtr<IWICMetadataQueryReader> meta;

        if (SUCCEEDED(frame->GetMetadataQueryReader(meta.GetAddressOf())) && meta)
        {
            ReadUIntMeta(meta.Get(), L"/imgdesc/Left",   left);
            ReadUIntMeta(meta.Get(), L"/imgdesc/Top",    top);
            ReadUIntMeta(meta.Get(), L"/imgdesc/Width",  w);
            ReadUIntMeta(meta.Get(), L"/imgdesc/Height", h);
            ReadUIntMeta(meta.Get(), L"/grctlext/Disposal", disposal);
            UINT delay10ms = 0;
            if (ReadUIntMeta(meta.Get(), L"/grctlext/Delay", delay10ms))
                delayMs = delay10ms * 10;
        }

        if (delayMs < 10) delayMs = 100;
        if (w == 0) w = frameW;
        if (h == 0) h = frameH;
        if (left >= canvasW || top >= canvasH)
            continue;

        const UINT dstW = umin(w, canvasW - left);
        const UINT dstH = umin(h, canvasH - top);
        if (dstW == 0 || dstH == 0)
            continue;

        WICRect frameRect{ (INT)left, (INT)top, (INT)dstW, (INT)dstH };

        if (disposal == 3)
            savedCanvasForDisposal3 = CloneBitmap(wicFactory, canvas.Get());
        else
            savedCanvasForDisposal3.Reset();

        std::vector<BYTE> src;
        UINT srcW = 0, srcH = 0, srcStride = 0;
        WICRect copyRect{ 0,0,(INT)frameW,(INT)frameH };
        const WICRect* copyRectPtr = nullptr;
        if (frameW >= left + dstW && frameH >= top + dstH &&
            (frameW != dstW || frameH != dstH) && (left != 0 || top != 0))
        {
            copyRect = WICRect{ (INT)left, (INT)top, (INT)dstW, (INT)dstH };
            copyRectPtr = &copyRect;
        }

        if (!DecodeToPBGRA(wicFactory, frame.Get(), copyRectPtr, src, srcW, srcH, srcStride))
            continue;
        const UINT drawW = umin(dstW, srcW);
        const UINT drawH = umin(dstH, srcH);

        BlendOntoCanvas(canvas.Get(), (int)left, (int)top, src.data(), drawW, drawH, srcStride, /*blendOver=*/true);

        auto composedFrame = CloneBitmap(wicFactory, canvas.Get());
        if (!composedFrame)
            continue;

        prevDisposal = disposal;
        prevFrameRect = frameRect;

        onFrame(i, composedFrame, delayMs);
        ++produced;
    }

    return produced;
}

// ============================================================
//  WebP frame decode - analogous to DecodeGifCompositeFrames above, but
//  simpler: WIC composites WebP animation frames internally (unlike GIF,
//  where this app has to manually replay disposal methods), so each frame
//  is independent and decode can start at any index without needing to
//  replay anything before it. That's why the background continuation for
//  WebP (in LoadImageD2D below) starts at frame 1 directly, instead of
//  redoing frame 0 the way the GIF continuation has to.
//
//  Like the GIF path it uses the shared DecodeToPBGRA / ReadUIntMeta
//  primitives above. wicFactory and decoder must both belong to whichever
//  thread is calling this, same reasoning as DecodeGifCompositeFrames.
//  maxFrames=0 means "decode through the end"; outW/outH report the first
//  decoded frame's dimensions (frame 0 unless startFrame skips it).
// ============================================================
static UINT DecodeWebpFrames(
    IWICImagingFactory* wicFactory,
    IWICBitmapDecoder* decoder,
    UINT frameCount,
    UINT startFrame,
    UINT maxFrames,
    const std::atomic<bool>* stopFlag,
    UINT* outW,
    UINT* outH,
    const std::function<void(UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)>& onFrame)
{
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    if (!wicFactory || !decoder || frameCount == 0 || startFrame >= frameCount)
        return 0;

    const UINT endFrame = (maxFrames == 0) ? frameCount : min(startFrame + maxFrames, frameCount);
    UINT produced = 0;

    for (UINT i = startFrame; i < endFrame; ++i)
    {
        if (stopFlag && stopFlag->load())
            break;

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(i, frame.GetAddressOf())) || !frame)
            continue;

        UINT delayMs = 100; // WebP delays are milliseconds; fallback = 100ms

        Microsoft::WRL::ComPtr<IWICMetadataQueryReader> meta;
        if (SUCCEEDED(frame->GetMetadataQueryReader(meta.GetAddressOf())) && meta)
        {
            UINT v = 0;

            // WIC's WebP ANMF metadata exposes WICWebpAnmfFrameDuration
            // (property id 1) in milliseconds. Different systems/SDKs have
            // used slightly different query-reader paths, so try the common
            // forms and fall back gracefully if none are present.
            if (ReadUIntMeta(meta.Get(), L"/ANMF/{uint=1}", v) ||
                ReadUIntMeta(meta.Get(), L"/anmf/{uint=1}", v) ||
                ReadUIntMeta(meta.Get(), L"/ANMF/FrameDuration", v) ||
                ReadUIntMeta(meta.Get(), L"/anmf/FrameDuration", v) ||
                ReadUIntMeta(meta.Get(), L"/webpanmf/{uint=1}", v) ||
                ReadUIntMeta(meta.Get(), L"/webpanmf/FrameDuration", v))
            {
                delayMs = v;
            }
        }

        if (delayMs < 10)
            delayMs = 100;

        std::vector<BYTE> pixels;
        UINT w = 0, h = 0, stride = 0;
        if (!DecodeToPBGRA(wicFactory, frame.Get(), nullptr, pixels, w, h, stride))
            continue;

        Microsoft::WRL::ComPtr<IWICBitmap> cached;
        HRESULT hrFrame = wicFactory->CreateBitmapFromMemory(
            w, h, GUID_WICPixelFormat32bppPBGRA, stride,
            (UINT)pixels.size(), pixels.data(), cached.GetAddressOf());

        if (FAILED(hrFrame) || !cached)
            continue;

        if (produced == 0 && outW && outH)
        {
            *outW = w;
            *outH = h;
        }

        onFrame(i, cached, delayMs);
        ++produced;
    }

    return produced;
}

// ============================================================
//  APNG (animated PNG) - parsed directly from the chunk stream.
//  WIC's PNG decoder only understands the default (first) frame,
//  so like the raw PNG-text / WebP-XMP readers above, we walk the
//  chunk list ourselves: acTL/fcTL give frame geometry, delays and
//  disposal/blend ops, and each frame's raw PNG scanline data
//  (IDAT for frame 0, fdAT for the rest) is repackaged into a
//  minimal single-frame PNG in memory and handed to WIC to inflate
//  and unfilter - no zlib dependency of our own. Compositing then
//  follows APNG's dispose/blend ops, which mirror GIF's disposal
//  methods (plus an extra "source" blend), so the playback glue
//  is identical to the GIF path.
//
//  wicFactory must belong to whichever thread is calling this, same
//  reasoning as DecodeGifCompositeFrames.
// ============================================================

static const UINT32* ApngCrcTable()
{
    static UINT32 table[256];
    static bool init = false;
    if (!init)
    {
        for (UINT32 n = 0; n < 256; ++n)
        {
            UINT32 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        init = true;
    }
    return table;
}

static UINT32 ApngCrc32(const BYTE* data, size_t len)
{
    const UINT32* table = ApngCrcTable();
    UINT32 crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static UINT32 ApngBe32(const BYTE* p)
{
    return (UINT32(p[0]) << 24) | (UINT32(p[1]) << 16) | (UINT32(p[2]) << 8) | UINT32(p[3]);
}

// Reads just enough of a PNG's chunk stream to report its APNG frame
// count (from acTL), without reading any pixel data. Returns 0 for a
// plain static PNG. Used by LoadImageD2D to choose between the static
// and animated PNG paths, mirroring AvifPeekFrameCount's role for AVIF.
static UINT PeekApngFrameCount(const wchar_t* path)
{
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    BYTE sig[8];
    DWORD br = 0;
    if (!ReadFile(hFile, sig, 8, &br, nullptr) || br != 8 ||
        memcmp(sig, "\x89PNG\r\n\x1A\n", 8) != 0)
    {
        CloseHandle(hFile);
        return 0;
    }

    UINT frames = 0;
    BYTE hdr[8];
    while (ReadFile(hFile, hdr, 8, &br, nullptr) && br == 8)
    {
        UINT32 len = ApngBe32(hdr);
        char type[5] = {};
        memcpy(type, hdr + 4, 4);

        if (strcmp(type, "IEND") == 0) break;

        // acTL is the only chunk whose contents matter here, and only its first
        // 4 bytes (num_frames), so its payload is read partially and every
        // other chunk is stepped over without reading it at all.
        UINT32 payloadRead = 0;
        if (strcmp(type, "acTL") == 0 && len >= 4)
        {
            BYTE numFrames[4];
            if (!ReadFile(hFile, numFrames, 4, &br, nullptr) || br != 4)
                break;
            frames      = ApngBe32(numFrames);
            payloadRead = 4;
        }

        // A chunk is length + type + payload + CRC, so the next one starts
        // after whatever is left of the payload plus the 4-byte CRC. Seeking
        // keeps this walk proportional to the chunk count, not the file size.
        LARGE_INTEGER skip{};
        skip.QuadPart = static_cast<LONGLONG>(len - payloadRead) + 4;
        if (!SetFilePointerEx(hFile, skip, nullptr, FILE_CURRENT))
            break;

        // acTL always precedes the first IDAT, so once pixel data starts
        // we already have the count.
        if (strcmp(type, "IDAT") == 0) break;
    }

    CloseHandle(hFile);
    return frames;
}

struct ApngFrameInfo
{
    UINT32 w = 0, h = 0, x = 0, y = 0;
    UINT32 delayNum = 0, delayDen = 0;
    BYTE disposeOp = 0;   // 0 none, 1 background, 2 previous
    BYTE blendOp   = 0;   // 0 source, 1 over
};

static UINT DecodeApngFrames(
    const wchar_t* path,
    IWICImagingFactory* wicFactory,
    UINT startFrame,
    UINT maxFrames,
    const std::atomic<bool>* stopFlag,
    UINT* outCanvasW,
    UINT* outCanvasH,
    const std::function<void(UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)>& onFrame)
{
    if (outCanvasW) *outCanvasW = 0;
    if (outCanvasH) *outCanvasH = 0;
    if (!wicFactory) return 0;

    // ---- 1. Walk the chunk stream: metadata + per-frame compressed data ----
    struct ApngParsed
    {
        UINT32 canvasW = 0, canvasH = 0;
        BYTE bitDepth = 0, colorType = 0, interlace = 0;
        std::vector<ApngFrameInfo> frames;
        std::vector<std::vector<BYTE>> frameData;     // concatenated IDAT/fdAT payloads
        std::vector<std::vector<BYTE>> ancillary;     // PLTE/tRNS/gAMA/... verbatim
    };
    ApngParsed p;

    {
        HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return 0;

        BYTE sig[8];
        DWORD br = 0;
        if (!ReadFile(hFile, sig, 8, &br, nullptr) || br != 8 ||
            memcmp(sig, "\x89PNG\r\n\x1A\n", 8) != 0)
        {
            CloseHandle(hFile);
            return 0;
        }

        bool sawAcTL = false;
        BYTE hdr[8];
        while (ReadFile(hFile, hdr, 8, &br, nullptr) && br == 8)
        {
            UINT32 len = ApngBe32(hdr);
            char type[5] = {};
            memcpy(type, hdr + 4, 4);

            if (strcmp(type, "IEND") == 0) break;

            std::vector<BYTE> data(len);
            if (len > 0 && (!ReadFile(hFile, data.data(), len, &br, nullptr) || br != len))
                break;
            SetFilePointerEx(hFile, { 4, 0 }, nullptr, FILE_CURRENT);   // skip CRC

                        if (strcmp(type, "IHDR") == 0 && len >= 13)
            {
                p.canvasW = ApngBe32(data.data());
                p.canvasH = ApngBe32(data.data() + 4);
                p.bitDepth  = data[8];
                p.colorType = data[9];
                p.interlace = data[12];
            }
            else if (strcmp(type, "acTL") == 0 && len >= 4)
            {
                sawAcTL = true;
                p.frameData.resize(ApngBe32(data.data()));
            }
            else if (strcmp(type, "fcTL") == 0 && len >= 26)
            {
                ApngFrameInfo f;
                // The sequence number is deliberately NOT used for indexing:
                // it increments once per fcTL *and* per fdAT chunk (0,1,2,3...),
                // while frame indices are 0,1,2... Conflating the two drops
                // every frame after the first. Chunks always appear in
                // increasing sequence order, so the frame an fdAT belongs to
                // is simply the most recent fcTL.
                f.w = ApngBe32(data.data() + 4);
                f.h = ApngBe32(data.data() + 8);
                f.x = ApngBe32(data.data() + 12);
                f.y = ApngBe32(data.data() + 16);
                f.delayNum = (UINT32(data[20]) << 8) | data[21];
                f.delayDen = (UINT32(data[22]) << 8) | data[23];
                f.disposeOp = data[24];
                f.blendOp   = data[25];
                p.frames.push_back(f);
                if (p.frames.size() > p.frameData.size())
                    p.frameData.resize(p.frames.size());
            }
            else if (strcmp(type, "IDAT") == 0)
            {
                // Frame 0 (the default image) lives in IDAT.
                if (!p.frameData.empty())
                {
                    auto& d = p.frameData[0];
                    d.insert(d.end(), data.begin(), data.end());
                }
            }
            else if (strcmp(type, "fdAT") == 0 && len >= 4)
            {
                // Belongs to the most recently seen fcTL; appending handles
                // frames split across multiple fdAT chunks.
                if (!p.frames.empty())
                {
                    auto& d = p.frameData[p.frames.size() - 1];
                    d.insert(d.end(), data.begin() + 4, data.end());
                }
            }
            else if (strcmp(type, "acTL") != 0 && strcmp(type, "fcTL") != 0)
            {
                // Re-emit ancillary chunks (PLTE, tRNS, gAMA, sRGB, pHYs, ...)
                // verbatim in the synthetic per-frame PNGs, preserving order.
                std::vector<BYTE> chunk(8 + len + 4);
                memcpy(chunk.data(), hdr, 8);
                memcpy(chunk.data() + 8, data.data(), len);
                std::vector<BYTE> crcIn(4 + len);
                memcpy(crcIn.data(), type, 4);
                memcpy(crcIn.data() + 4, data.data(), len);
                UINT32 crc = ApngCrc32(crcIn.data(), crcIn.size());
                chunk[8 + len + 0] = (BYTE)(crc >> 24);
                chunk[8 + len + 1] = (BYTE)(crc >> 16);
                chunk[8 + len + 2] = (BYTE)(crc >> 8);
                chunk[8 + len + 3] = (BYTE)(crc);
                p.ancillary.push_back(std::move(chunk));
            }
        }
        CloseHandle(hFile);

        if (!sawAcTL || p.frames.empty() || p.canvasW == 0 || p.canvasH == 0)
            return 0;
    }

    if (outCanvasW) *outCanvasW = p.canvasW;
    if (outCanvasH) *outCanvasH = p.canvasH;

    // ---- 2. Synthetic-frame decode (the only APNG-specific decode step;
    // the actual inflate/unfilter and premultiply-invariant handling is the
    // shared DecodeToPBGRA used by GIF and WebP too). ----
    auto DecodeSyntheticFrame = [&](const ApngFrameInfo& f,
                                   const std::vector<BYTE>& compressed,
                                   std::vector<BYTE>& outPixels,
                                   UINT& outW, UINT& outH, UINT& outStride) -> bool
    {
        // Build a minimal valid single-frame PNG: signature, IHDR with this
        // frame's dimensions (global bit depth/color type), copied ancillary
        // chunks, one IDAT with the frame's data, IEND. WIC does the inflate
        // and unfiltering; we only compute CRCs for the chunks we synthesize.
        auto appendChunk = [](std::vector<BYTE>& out, const char* type,
                              const BYTE* data, size_t len)
        {
            BYTE lenBE[4] = { (BYTE)(len >> 24), (BYTE)(len >> 16), (BYTE)(len >> 8), (BYTE)len };
            out.insert(out.end(), lenBE, lenBE + 4);
            out.insert(out.end(), type, type + 4);
            if (len) out.insert(out.end(), data, data + len);
            std::vector<BYTE> crcIn(4 + len);
            memcpy(crcIn.data(), type, 4);
            if (len) memcpy(crcIn.data() + 4, data, len);
            UINT32 crc = ApngCrc32(crcIn.data(), crcIn.size());
            BYTE crcBE[4] = { (BYTE)(crc >> 24), (BYTE)(crc >> 16), (BYTE)(crc >> 8), (BYTE)crc };
            out.insert(out.end(), crcBE, crcBE + 4);
        };

        std::vector<BYTE> png;
        png.insert(png.end(), (const BYTE*)"\x89PNG\r\n\x1A\n", (const BYTE*)"\x89PNG\r\n\x1A\n" + 8);
        BYTE ihdr[13] = {};
        auto putBE = [](BYTE* d, UINT32 v){ d[0]=(BYTE)(v>>24); d[1]=(BYTE)(v>>16); d[2]=(BYTE)(v>>8); d[3]=(BYTE)v; };
        putBE(ihdr, f.w); putBE(ihdr + 4, f.h);
        ihdr[8] = p.bitDepth; ihdr[9] = p.colorType;
        ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = p.interlace;
        appendChunk(png, "IHDR", ihdr, 13);
        for (auto& c : p.ancillary)
            png.insert(png.end(), c.begin(), c.end());
        appendChunk(png, "IDAT", compressed.data(), compressed.size());
        appendChunk(png, "IEND", nullptr, 0);

        ComPtr<IWICStream> stream;
        if (FAILED(wicFactory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromMemory(png.data(), (DWORD)png.size())))
            return false;
        ComPtr<IWICBitmapDecoder> dec;
        if (FAILED(wicFactory->CreateDecoderFromStream(
            stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &dec)))
            return false;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(dec->GetFrame(0, &frame))) return false;

        // Same PBGRA decode + premultiply-invariant cleanup the GIF/WebP
        // paths use.
        return DecodeToPBGRA(wicFactory, frame.Get(), nullptr, outPixels, outW, outH, outStride);
    };

    // ---- 3. Replay + composite. Disposal makes frames sequential: always
    // process from 0, but only publish frames >= startFrame (the caller's
    // onFrame wrapper skips index 0 for the background thread, exactly like
    // the GIF path). Compositing uses the shared ClearRect / BlendOntoCanvas
    // / CloneBitmap primitives. ----
    ComPtr<IWICBitmap> canvas;
    if (FAILED(wicFactory->CreateBitmap(p.canvasW, p.canvasH,
            GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, canvas.GetAddressOf())) || !canvas)
        return 0;
    ClearRect(canvas.Get(), WICRect{ 0, 0, (INT)p.canvasW, (INT)p.canvasH });

    const UINT total = (UINT)p.frames.size();
    const UINT endFrame = (maxFrames == 0) ? total : umin(total, startFrame + maxFrames);
    UINT prevDispose = 0;
    WICRect prevRect{ 0, 0, 0, 0 };
    ComPtr<IWICBitmap> savedForDispose2;
    UINT produced = 0;

    for (UINT i = 0; i < endFrame; ++i)
    {
        if (stopFlag && stopFlag->load()) break;

        if (i > 0)
        {
            if (prevDispose == 1)   // APNG_DISPOSE_OP_BACKGROUND
            {
                if (prevRect.Width > 0 && prevRect.Height > 0)
                    ClearRect(canvas.Get(), prevRect);
            }
            else if (prevDispose == 2 && savedForDispose2)   // APNG_DISPOSE_OP_PREVIOUS
            {
                canvas = savedForDispose2;
                savedForDispose2.Reset();
            }
        }

        const ApngFrameInfo& f = p.frames[i];
        if (f.w == 0 || f.h == 0 || f.x >= p.canvasW || f.y >= p.canvasH)
            continue;

        std::vector<BYTE> px; UINT pw = 0, ph = 0, ps = 0;
        if (!DecodeSyntheticFrame(f, p.frameData[i], px, pw, ph, ps))
            continue;

        if (f.disposeOp == 2)
            savedForDispose2 = CloneBitmap(wicFactory, canvas.Get());
        else
            savedForDispose2.Reset();

        // blendOp 1 = OVER (src-over), 0 = SOURCE (verbatim copy).
        BlendOntoCanvas(canvas.Get(), (int)f.x, (int)f.y, px.data(), pw, ph, ps,
                        f.blendOp == 1);

        prevDispose = f.disposeOp;
        prevRect = WICRect{ (INT)f.x, (INT)f.y,
                            (INT)umin(f.w, p.canvasW - f.x),
                            (INT)umin(f.h, p.canvasH - f.y) };

        if (i >= startFrame)
        {
            auto composed = CloneBitmap(wicFactory, canvas.Get());
            if (!composed) continue;
            UINT delayMs = (f.delayDen > 0)
                ? (UINT)((double)f.delayNum / f.delayDen * 1000.0 + 0.5) : 100;
            if (delayMs < 10) delayMs = 100;
            onFrame(i, composed, delayMs);
            ++produced;
        }
    }

    return produced;
}



// Stops (and joins) the background GIF/WebP/AVIF-frame-decoding thread, if
// one is running. Every LoadImageD2D call starts with this - the thread it
// might stop belongs to whatever image was previously loaded, and must not
// be left writing into g_anim.frames/g_anim.frameDelays after those vectors
// get reassigned for the new image.
void StopAnimDecodeThread()
{
    g_anim.decodeStop = true;
    if (g_anim.decodeThread.joinable())
        g_anim.decodeThread.join();
    g_anim.decodeStop = false;
}

// ============================================================
//  Shared animated-image loading glue (GIF + WebP + AVIF)
// ------------------------------------------------------------
//  All three formats follow the exact same shape once their frame-0 is
//  decoded: publish the shared g_anim* state, then hand the remaining
//  frames off to a background thread. Only the actual per-frame decode
//  call differs (DecodeGifCompositeFrames vs. DecodeWebpFrames vs.
//  DecodeAvifFrames, and where each one needs to start), so that's the one
//  thing callers still provide themselves, as a small lambda - including
//  opening whatever thread-local decode resources their format needs
//  (OpenThreadLocalWicDecoder for GIF/WebP; DecodeAvifFrames opens its own
//  avifDecoder internally, so AVIF only needs OpenThreadLocalWicFactory).
// ============================================================

// Publishes g_anim.frames[0] (already decoded by the caller) as the current
// frame and marks the image as animated. Common tail of all three formats'
// synchronous frame-0 decode step.
static void BeginAnimatedPlayback(UINT firstFrameW, UINT firstFrameH)
{
    g_anim.isAnimated = true;
    g_anim.currentFrame = 0;
    g_image.source = g_anim.frames[0];
    g_anim.lastFrameTime = GetTickCount64();
    g_image.width = (int)firstFrameW;
    g_image.height = (int)firstFrameH;
    g_anim.framesReadyUpTo.store(1);
}

// Opens a fresh WIC factory for use on a background thread. WIC objects
// aren't safely shared across the apartment boundary (see
// DecodeGifCompositeFrames's comment), so every background continuation
// below creates its own rather than reusing the main thread's g_devices.wicFactory.
static bool OpenThreadLocalWicFactory(ComPtr<IWICImagingFactory>& outFactory)
{
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&outFactory))) && outFactory;
}

// Same, plus a WIC decoder for the given file - what the GIF and WebP
// background continuations need (AVIF doesn't: DecodeAvifFrames opens its
// own avifDecoder from the path instead, and only needs the factory).
static bool OpenThreadLocalWicDecoder(const std::wstring& path,
    ComPtr<IWICImagingFactory>& outFactory, ComPtr<IWICBitmapDecoder>& outDecoder)
{
    if (!OpenThreadLocalWicFactory(outFactory))
        return false;
    return SUCCEEDED(outFactory->CreateDecoderFromFilename(path.c_str(), nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnLoad, &outDecoder)) && outDecoder;
}

// Signature every format's "decode the rest of the frames" callback must
// match: given the total frame count, decode whatever frames remain and
// publish each one via onFrame.
using DecodeRemainingFramesFn = std::function<UINT(
    UINT frameCount,
    const std::atomic<bool>* stopFlag,
    const std::function<void(UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)>& onFrame)>;

// Spawns the background thread that decodes every animation frame not
// already handled by the synchronous frame-0 decode. This is the part that
// used to be duplicated almost line-for-line between the GIF and WebP
// branches of LoadImageD2D (and would have been a third time over for
// AVIF): initialize COM for the thread, run decodeRestFn, and publish each
// frame into the shared g_anim* vectors as it arrives.
static void SpawnAnimDecodeThread(UINT frameCount, DecodeRemainingFramesFn decodeRestFn)
{
    g_anim.decodeThread = std::thread([frameCount, decodeRestFn]()
    {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
            return;

        decodeRestFn(frameCount, &g_anim.decodeStop,
            [](UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)
            {
                if (index < g_anim.frames.size())
                {
                    g_anim.frames[index] = bmp;
                    g_anim.frameDelays[index] = delayMs;
                    // Frames are produced strictly in order by this single
                    // thread, so it's always safe to publish index+1 here.
                    g_anim.framesReadyUpTo.store((int)(index + 1));
                }
            });

        CoUninitialize();
    });
}

bool LoadImageD2D(HWND hWnd, const wchar_t* filename)
{
    g_image.lastLoadError.clear();
    g_image.showLoadError = false;
    g_image.loadAttemptName = (filename && *filename)
        ? std::filesystem::path(filename).filename().wstring()
        : std::wstring();

    if (!g_devices.wicFactory || !filename || !*filename)
        return LoadFail(L"Internal error: the image system hasn't finished initializing.");

    // Stop any background GIF-frame decode left over from the previously
    // loaded image before we touch g_anim.frames/g_anim.frameDelays below - see
    // StopAnimDecodeThread's comment.
    StopAnimDecodeThread();

    // ---- Slideshow: snapshot current bitmap + bg before we clobber them ----
    if (g_slideshow.active && g_view.bitmap)
    {
        g_slideshow.prevBitmap         = g_view.bitmap;
        g_slideshow.prevBgBitmap = g_slideshow.bgBitmap;
        g_slideshow.prevZoom         = g_view.zoom;
        g_slideshow.prevOffsetX         = g_view.offsetX;
        g_slideshow.prevOffsetY         = g_view.offsetY;
        g_slideshow.prevRotation     = g_view.rotationAngle;

        // Reset so Render recreates it for the new image
        g_slideshow.bgBitmap.Reset();

        // Kick off the fade-in
        g_slideshow.transitionAlpha = 0.0f;
        g_slideshow.targetAlpha     = 1.0f;
    }
        
    // ------------------------------------------------------------
    // Save previous image state
    // ------------------------------------------------------------
    if (!g_image.path.empty() && g_view.bitmap)
    {
    
        D2D1_SIZE_F imgSize = g_view.bitmap->GetSize();

        float panX = 0.f, panY = 0.f;
        float targetPanX = 0.f, targetPanY = 0.f;

        if (PanFromOffsets(hWnd, g_view.zoom, g_view.offsetX, g_view.offsetY,
                           imgSize.width, imgSize.height, panX, panY) &&
            PanFromOffsets(hWnd, g_view.targetZoom, g_view.targetOffsetX, g_view.targetOffsetY,
                           imgSize.width, imgSize.height, targetPanX, targetPanY))
        {
            g_view.stateByPath[g_image.path] = {
                g_view.zoom, panX, panY,
                g_view.targetZoom, targetPanX, targetPanY,
                g_view.rotationAngle, g_view.targetRotationAngle
            };
        }
    }
    
    // Clear previous state
    g_view.bitmap.Reset();
    g_image.source.Reset();
    g_anim.frames.clear();
    g_anim.frameDelays.clear();
    g_anim.gpuFrames.clear();
    g_anim.isAnimated = false;
    g_anim.currentFrame = 0;
    g_anim.lastFrameTime = 0;
    g_slideshow.bgBitmap.Reset();

    std::wstring extLower = std::filesystem::path(filename).extension().wstring();
    std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::towlower);

    // --------------------------------------------------------------
    // AVIF / JPEG XL - decoded ourselves (bundled libavif/dav1d and
    // libjxl), bypassing WIC's decoder registry entirely. This is what
    // lets these formats work with no OS/Store codec installed.
    // --------------------------------------------------------------
    if (extLower == L".avif")
    {
        // Peek the frame count first so we know whether this is a still
        // image or an animated AVIF (an 'avis'-brand image *sequence*);
        // parsing just the container structure is cheap - the actual
        // per-frame decode work happens in whichever path we take below.
        const UINT avifFrameCount = AvifPeekFrameCount(filename);

        if (avifFrameCount > 1)
        {
            // Animated AVIF - same shape as the animated-WebP branch further
            // down (see the comment there): decode frame 0 synchronously so
            // the window opens immediately, then hand the rest to a
            // background thread via SpawnAnimDecodeThread. AVIF's
            // avifDecoderNthImage() can seek to any frame directly, the same
            // as WebP, so the continuation starts right at frame 1.
            g_anim.frames.assign(avifFrameCount, nullptr);
            g_anim.frameDelays.assign(avifFrameCount, 100);
            g_anim.framesReadyUpTo.store(0);

            UINT frameW = 0, frameH = 0;
            std::wstring reason;
            UINT produced = DecodeAvifFrames(
                filename, g_devices.wicFactory.Get(), /*startFrame=*/0, /*maxFrames=*/1, nullptr,
                &frameW, &frameH,
                [&](UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)
                {
                    if (index < g_anim.frames.size())
                    {
                        g_anim.frames[index] = bmp;
                        g_anim.frameDelays[index] = delayMs;
                    }
                }, &reason);

            if (produced == 0 || !g_anim.frames[0])
                return LoadFail((L"No animation frames could be decoded from this AVIF file: " + reason).c_str());

            BeginAnimatedPlayback(frameW, frameH);
            g_view.exifRotation = 0.f;  // AVIF irot/imir transforms aren't applied yet - see DecodeAvifToWicBitmap

            std::wstring filePathCopy = filename;
            SpawnAnimDecodeThread(avifFrameCount,
                [filePathCopy](UINT fc, const std::atomic<bool>* stopFlag,
                                const std::function<void(UINT, ComPtr<IWICBitmap>, UINT)>& onFrame) -> UINT
                {
                    ComPtr<IWICImagingFactory> wic;
                    if (!OpenThreadLocalWicFactory(wic))
                        return 0;
                    // Starts at frame 1 (not 0) - same reasoning as WebP.
                    return DecodeAvifFrames(filePathCopy.c_str(), wic.Get(), /*startFrame=*/1, /*maxFrames=*/0,
                                             stopFlag, nullptr, nullptr, onFrame);
                });

            return FinishImageLoad(hWnd, filename);
        }

        // Static AVIF (single image)
        ComPtr<IWICBitmap> bmp;
        UINT w = 0, h = 0;
        std::wstring reason;
        if (!DecodeAvifToWicBitmap(filename, g_devices.wicFactory.Get(), bmp, w, h, &reason))
            return LoadFail((L"This AVIF file couldn't be decoded: " + reason).c_str());
        g_image.source = bmp;
        g_image.width   = (int)w;
        g_image.height  = (int)h;
        g_view.exifRotation = 0.f;  // AVIF irot/imir transforms aren't applied yet - see DecodeAvifToWicBitmap
        return FinishImageLoad(hWnd, filename);
    }
    else if (extLower == L".jxl")
    {
        ComPtr<IWICBitmap> bmp;
        UINT w = 0, h = 0;
        std::wstring reason;
        if (!DecodeJxlToWicBitmap(filename, g_devices.wicFactory.Get(), bmp, w, h, &reason))
            return LoadFail((L"This JPEG XL file couldn't be decoded: " + reason).c_str());
        g_image.source = bmp;
        g_image.width   = (int)w;
        g_image.height  = (int)h;
        g_view.exifRotation = 0.f;
        return FinishImageLoad(hWnd, filename);
    }

    // --------------------------------------------
    // Decode container (everything else still goes through WIC, as before)
    // --------------------------------------------

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = g_devices.wicFactory->CreateDecoderFromFilename(
        filename,
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        decoder.GetAddressOf());

    if (FAILED(hr) || !decoder)
    {
        if (hr == WINCODEC_ERR_COMPONENTNOTFOUND)
            return LoadFail(L"No decoder is registered for this file type on this system.", hr);
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
            return LoadFail(L"The file could not be found.", hr);
        if (hr == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION))
            return LoadFail(L"The file is in use by another program and can't be read right now.", hr);
        return LoadFail(L"This file couldn't be opened - it may be corrupted or not a valid image.", hr);
    }

    UINT frameCount = 0;
    hr = decoder->GetFrameCount(&frameCount);

    if (FAILED(hr) || frameCount == 0)
        return LoadFail(L"This image file has no readable frames.", hr);

    GUID container = {};
    decoder->GetContainerFormat(&container);

    // GUID_ContainerFormatWebp is only present in newer Windows SDKs, so use
    // a local GUID constant to keep the project buildable with older SDKs too.
    static const GUID kContainerFormatWebp =
        { 0xe094b0e2, 0x67f2, 0x45b3, { 0xb0, 0xea, 0x11, 0x53, 0x37, 0xca, 0x7c, 0xf3 } };

    const bool isGifContainer  = IsEqualGUID(container, GUID_ContainerFormatGif);
    const bool isWebpContainer = IsEqualGUID(container, kContainerFormatWebp);

    // --------------------------------------------
    // Metadata helper
    // --------------------------------------------
    // ReadUIntMeta and the compositing primitives (ClearRect /
    // BlendOntoCanvas / DecodeToPBGRA / CloneBitmap) used to be duplicated
    // as local lambdas here and inside each Decode*Frames function. They now
    // live once in the shared block above DecodeGifCompositeFrames, so the
    // EXIF-orientation read below and every animated decoder use the
    // identical PROPVARIANT coercion and identical compositing math.

    // ============================================================
    // Animated WebP / animated GIF
    // ------------------------------------------------------------
    // (Animated AVIF is handled earlier, in the .avif branch above - it
    // bypasses WIC's decoder registry entirely the same way static AVIF
    // does, so it can't share the WIC decoder this function opens below.
    // It still goes through the exact same BeginAnimatedPlayback /
    // SpawnAnimDecodeThread glue as these two, though - see the comment on
    // that section for why all three end up looking so similar.)
    //
    // Both branches below follow the same shape (assign the shared g_anim*
    // vectors, decode frame 0 synchronously so the window opens right away,
    // then call SpawnAnimDecodeThread for the rest) via BeginAnimatedPlayback
    // and SpawnAnimDecodeThread above. What's left here is genuinely
    // format-specific: which Decode*Frames function to call, and - because
    // WIC composites WebP frames internally while GIF frames must be
    // replayed through prior frames' disposal state - where each one needs
    // to start decoding from.
    // ============================================================
    // The PNG branch below needs the APNG frame count twice: to decide whether
    // the file is animated at all, and to size the frame vectors. One walk of
    // the chunk list fills in both.
    UINT apngCount = 0;

    if (isWebpContainer && frameCount > 1)
    {
        g_anim.frames.assign(frameCount, nullptr);
        g_anim.frameDelays.assign(frameCount, 100);
        g_anim.framesReadyUpTo.store(0);

        // Decode just frame 0 synchronously, on the main thread, using the
        // decoder we already have open - this is what lets the window open
        // immediately.
        UINT frameW = 0, frameH = 0;
        UINT produced = DecodeWebpFrames(
            g_devices.wicFactory.Get(), decoder.Get(), frameCount, /*startFrame=*/0, /*maxFrames=*/1, nullptr,
            &frameW, &frameH,
            [&](UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)
            {
                if (index < g_anim.frames.size())
                {
                    g_anim.frames[index] = bmp;
                    g_anim.frameDelays[index] = delayMs;
                }
            });

        if (produced == 0 || !g_anim.frames[0])
            return LoadFail(L"No animation frames could be decoded from this WebP file.");

        BeginAnimatedPlayback(frameW, frameH);

        std::wstring filePathCopy = filename;
        SpawnAnimDecodeThread(frameCount,
            [filePathCopy](UINT fc, const std::atomic<bool>* stopFlag,
                            const std::function<void(UINT, ComPtr<IWICBitmap>, UINT)>& onFrame) -> UINT
            {
                ComPtr<IWICImagingFactory> wic;
                ComPtr<IWICBitmapDecoder> localDecoder;
                if (!OpenThreadLocalWicDecoder(filePathCopy, wic, localDecoder))
                    return 0;
                // Starts at frame 1 (not 0) - unlike GIF, WebP frames are
                // independent, so there's no state to rebuild by redoing frame 0.
                return DecodeWebpFrames(wic.Get(), localDecoder.Get(), fc, /*startFrame=*/1, /*maxFrames=*/0,
                                         stopFlag, nullptr, nullptr, onFrame);
            });
    }
    else if (isGifContainer && frameCount > 1)
    {
        g_anim.frames.assign(frameCount, nullptr);
        g_anim.frameDelays.assign(frameCount, 100);
        g_anim.framesReadyUpTo.store(0);

        // Decode just frame 0 synchronously, on the main thread, using the
        // decoder we already have open - this is what lets the window open
        // immediately instead of stalling on the whole animation.
        UINT canvasW = 0, canvasH = 0;
        UINT produced = DecodeGifCompositeFrames(
            g_devices.wicFactory.Get(), decoder.Get(), frameCount, /*maxFrames=*/1, nullptr,
            &canvasW, &canvasH,
            [&](UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)
            {
                if (index < g_anim.frames.size())
                {
                    g_anim.frames[index] = bmp;
                    g_anim.frameDelays[index] = delayMs;
                }
            });

        if (produced == 0 || !g_anim.frames[0])
            return LoadFail(L"No frames could be decoded from this GIF.");

        BeginAnimatedPlayback(canvasW, canvasH);

        // Unlike WebP, the background thread here has to redecode from frame
        // 0 to rebuild identical canvas/disposal state before it can
        // continue past where the synchronous pass left off (see
        // DecodeGifCompositeFrames's comment) - so its onFrame callback
        // skips republishing frame 0, which the main thread already
        // published above via BeginAnimatedPlayback. The one-frame repeat is
        // cheap; sharing the WIC canvas object across threads instead would
        // not be safe.
        SpawnAnimDecodeThread(frameCount,
            [filePathCopy = std::wstring(filename)](UINT fc, const std::atomic<bool>* stopFlag,
                const std::function<void(UINT, ComPtr<IWICBitmap>, UINT)>& onFrame) -> UINT
            {
                ComPtr<IWICImagingFactory> wic;
                ComPtr<IWICBitmapDecoder> localDecoder;
                if (!OpenThreadLocalWicDecoder(filePathCopy, wic, localDecoder))
                    return 0;
                return DecodeGifCompositeFrames(wic.Get(), localDecoder.Get(), fc, /*maxFrames=*/0, stopFlag,
                    nullptr, nullptr,
                    [&](UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)
                    {
                        if (index == 0) return;  // main thread already published frame 0
                        onFrame(index, bmp, delayMs);
                    });
            });
    }
    else if (IsEqualGUID(container, GUID_ContainerFormatPng) &&
             (apngCount = PeekApngFrameCount(filename)) > 1)
    {
        // Animated PNG. WIC reports frameCount == 1 for APNG because it
        // only understands the default image, so we detected the real
        // count via the acTL chunk. Like GIF (and unlike WebP/AVIF),
        // disposal can depend on prior frames, so the background thread
        // replays from 0 and skips republishing frame 0.
        g_anim.frames.assign(apngCount, nullptr);
        g_anim.frameDelays.assign(apngCount, 100);
        g_anim.framesReadyUpTo.store(0);

        UINT canvasW = 0, canvasH = 0;
        UINT produced = DecodeApngFrames(
            filename, g_devices.wicFactory.Get(), /*startFrame=*/0, /*maxFrames=*/1, nullptr,
            &canvasW, &canvasH,
            [&](UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)
            {
                if (index < g_anim.frames.size())
                {
                    g_anim.frames[index] = bmp;
                    g_anim.frameDelays[index] = delayMs;
                }
            });

        if (produced == 0 || !g_anim.frames[0])
            return LoadFail(L"No animation frames could be decoded from this APNG file.");

        BeginAnimatedPlayback(canvasW, canvasH);
        g_view.exifRotation = 0.f;   // PNG has no EXIF orientation

        SpawnAnimDecodeThread(apngCount,
            [filePathCopy = std::wstring(filename)](UINT fc, const std::atomic<bool>* stopFlag,
                const std::function<void(UINT, ComPtr<IWICBitmap>, UINT)>& onFrame) -> UINT
            {
                ComPtr<IWICImagingFactory> wic;
                if (!OpenThreadLocalWicFactory(wic))
                    return 0;
                // startFrame=1, but internally replays from 0 to rebuild
                // canvas state; frame 0 is never republished.
                return DecodeApngFrames(filePathCopy.c_str(), wic.Get(), /*startFrame=*/1,
                                        /*maxFrames=*/0, stopFlag, nullptr, nullptr, onFrame);
            });
    }
    // Static image (or non-GIF/WebP multi-frame): load frame 0
    // ============================================================
    else
    {
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.GetAddressOf());

        if (FAILED(hr) || !frame)
            return LoadFail(L"Couldn't read the image frame from this file.", hr);

        // ---- Read EXIF Orientation tag (tag 274) ----
        // Map the 8 EXIF orientation values to clockwise rotation degrees.
        // Values 5-8 also involve a flip but we only apply the rotation component
        // since the render pipeline doesn't support flipping.
        //   1 = normal          -> 0 deg
        //   3 = upside-down     -> 180 deg
        //   6 = 90 deg CW shot     -> 90 deg   (phone held 90 deg CCW)
        //   8 = 90 deg CCW shot    -> 270 deg  (phone held 90 deg CW)
        {
            ComPtr<IWICMetadataQueryReader> mqr;
            if (SUCCEEDED(frame->GetMetadataQueryReader(mqr.GetAddressOf())) && mqr)
            {
                UINT orientation = 0;
                if (ReadUIntMeta(mqr.Get(), L"/app1/ifd/{ushort=274}", orientation) ||
                    ReadUIntMeta(mqr.Get(), L"/ifd/{ushort=274}",      orientation))
                {
                    switch (orientation)
                    {
                        case 3: g_view.exifRotation =  180.f; break;
                        case 6: g_view.exifRotation =   90.f; break;
                        case 8: g_view.exifRotation =  270.f; break;
                        default:g_view.exifRotation =    0.f; break;
                    }
                }
                else
                {
                    g_view.exifRotation = 0.f;
                }
            }
            else
            {
                g_view.exifRotation = 0.f;
            }
        }

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        hr = g_devices.wicFactory->CreateFormatConverter(converter.GetAddressOf());

        if (FAILED(hr) || !converter)
            return LoadFail(L"Couldn't create a color-format converter for this image.", hr);

        hr = converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0f,
            WICBitmapPaletteTypeCustom);

        if (FAILED(hr))
            return LoadFail(L"This image's pixel format isn't supported.", hr);

        Microsoft::WRL::ComPtr<IWICBitmap> cached;
        hr = g_devices.wicFactory->CreateBitmapFromSource(
            converter.Get(),
            WICBitmapCacheOnLoad,
            cached.GetAddressOf());

        if (FAILED(hr) || !cached)
            return LoadFail(L"Couldn't cache the decoded image in memory.", hr);

        // Enforce premultiplied invariant for fully transparent pixels.
        // (Prevents speckle/noise if RGB is non-zero under A==0.)
        {
            UINT w = 0, h = 0;

            if (SUCCEEDED(cached->GetSize(&w, &h)) && w && h)
            {
                WICRect rc{ 0,0,(INT)w,(INT)h };
                Microsoft::WRL::ComPtr<IWICBitmapLock> lock;
                g_image.width  = (int)w;
                g_image.height = (int)h;
                if (SUCCEEDED(cached->Lock(&rc, WICBitmapLockWrite, lock.GetAddressOf())) && lock)
                {
                    UINT cb = 0; BYTE* data = nullptr; UINT stride = 0;
                    if (SUCCEEDED(lock->GetDataPointer(&cb, &data)) && data &&
                        SUCCEEDED(lock->GetStride(&stride)) && stride)
                    {
                        for (UINT y = 0; y < h; ++y)
                        {
                            BYTE* row = data + (size_t)y * stride;
                            for (UINT x = 0; x < w; ++x)
                            {
                                BYTE* px = row + x * 4;
                                if (px[3] == 0)
                                    px[0] = px[1] = px[2] = 0;
                            }
                        }
                    }
                }
            }
        }

        g_image.source = cached;
    }

    return FinishImageLoad(hWnd, filename);
}

// ============================================================
//  Shared "tail" of an image load - runs after g_image.source (and, for
//  animated images, g_anim.frames) has been populated by either the WIC path
//  or the AVIF/JXL paths above. Uploads to the GPU, restores any saved
//  view state, and updates the filename label.
// ============================================================
static bool FinishImageLoad(HWND hWnd, const wchar_t* filename)
{
    // Create Direct2D Bitmap for the *current device*
    RecreateImageBitmap();

    // Only frame 0 gets uploaded here (RecreateImageBitmap() just above already
    // built it, since g_image.source points at g_anim.frames[0] for animated
    // formats). Every other frame is uploaded lazily, one at a time, by
    // UpdateEngine as playback reaches it - this is what lets a large/long
    // animated GIF, WebP, or AVIF display its first frame immediately instead of
    // blocking on every frame hitting the GPU up front.
    if (g_anim.isAnimated && g_devices.renderTarget)
    {
        g_anim.gpuFrames.assign(g_anim.frames.size(), nullptr);
        if (!g_anim.gpuFrames.empty())
            g_anim.gpuFrames[0] = g_view.bitmap;   // already built above, no need to re-upload
    }

    // ------------------------------------------------------------
    // Restore state for new image (if exists)
    // ------------------------------------------------------------
    std::wstring newPath = filename;

    auto it = g_view.stateByPath.find(newPath);
            
    if (it != g_view.stateByPath.end() && g_view.bitmap)
    {
        

        const ImageViewState& s = it->second;

        g_view.zoom = s.zoom;
        UpdateTargetZoom(s.targetZoom);

        D2D1_SIZE_F imgSize = g_view.bitmap->GetSize();

        // Convert stored pan back into absolute offsets for THIS render target.
        // When in fullscreen the overlay is the render target; always use it
        // here so that callers passing g_app.mainWindow (e.g. the Open button
        // callback or WM_COPYDATA) still get offsets in overlay coordinates.
        HWND panWnd = (g_view.isFullscreen && g_app.overlayWindow && IsWindow(g_app.overlayWindow))
                      ? g_app.overlayWindow : hWnd;

        OffsetsFromPan(panWnd, g_view.zoom, imgSize.width, imgSize.height,
                       s.panX, s.panY, g_view.offsetX, g_view.offsetY);

        OffsetsFromPan(panWnd, g_view.targetZoom, imgSize.width, imgSize.height,
                       s.targetPanX, s.targetPanY, g_view.targetOffsetX, g_view.targetOffsetY);

        g_view.rotationAngle = s.rotation;
        g_view.targetRotationAngle = s.targetRotation;

        g_view.restoredStateThisLoad = true;
    }
    else
    {
        g_view.restoredStateThisLoad = false;
        // Apply EXIF orientation so the image appears upright immediately.
        // GIFs never carry EXIF so g_view.exifRotation will be 0 for them.
        g_view.rotationAngle  = g_view.exifRotation;
        g_view.targetRotationAngle = g_view.exifRotation;
    }
    // Update current path
    g_image.path = newPath;
    size_t slash = g_image.path.find_last_of(L"\\/");

    if (slash != std::wstring::npos)
        g_image.fileName = g_image.path.substr(slash + 1);
    else
        g_image.fileName = g_image.path;

    // Use the full path here, not g_image.fileName (the bare name).
    // When the app is launched from a Windows Explorer *search* results window the
    // working directory is NOT set to the file's folder, so file_size(bare_name)
    // would throw a filesystem_error and crash the process before anything is shown.
    std::error_code ec;
    uintmax_t bytes = std::filesystem::file_size(g_image.path, ec);
    if (ec) bytes = 0;
    wchar_t buffer[64];
    if (bytes < 1024)
        swprintf(buffer, 64, L"%llu B", bytes);
    else if (bytes < 1024 * 1024)
        swprintf(buffer, 64, L"%.1f KB", bytes / 1024.0);
    else
        swprintf(buffer, 64, L"%.2f MB", bytes / (1024.0 * 1024.0));


    std::wstring text =
        g_image.fileName +
        L" (" +
        std::to_wstring(g_image.width) +
        L" x " +
        std::to_wstring(g_image.height) +
        L", " +
        buffer +
        L")";

    g_textBoxes[TextBoxId::FILE_NAME].SetText(text);

    if (!g_view.bitmap)
        return LoadFail(L"The image decoded, but couldn't be uploaded to the graphics device.");

    return true;
}
