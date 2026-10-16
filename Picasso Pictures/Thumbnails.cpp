// Thumbnails.cpp : the file list, the background thumbnail loader, the film
// strip draw, and the directory watcher.
//
// What this file needs that its header does not already give it:
//   * #include "Decoding.h" - the loader thread reuses DecodeAvifToWicBitmap /
//     DecodeJxlToWicBitmap for AVIF/JXL thumbnails, which WIC cannot open.
//   * <shlwapi.h> + Shlwapi.lib - StrCmpLogicalW is declared in shlwapi.h,
//     not shellapi.h.
//   * A local declaration of IsSupportedImage, which lives in the main file
//     with the rest of the file-list/navigation code.

#include "Thumbnails.h"
#include "Decoding.h"   // DecodeAvifToWicBitmap / DecodeJxlToWicBitmap (AVIF/JXL thumbnails)

#include <algorithm>    // std::sort, std::transform (and min/max with windows.h)
#include <filesystem>   // std::filesystem::path / directory_iterator
#include <shlwapi.h>    // StrCmpLogicalW (BuildImageList's Explorer-identical sort)

#pragma comment(lib, "Shlwapi.lib")

// ============================================================================
// IsSupportedImage lives in the main file, with the file-open path and the
// navigation code that share it. BuildImageList and the watcher thread both
// call it, so it is declared here rather than in a module header.
// ============================================================================
bool IsSupportedImage(const std::wstring& path);


void BuildImageList(const wchar_t* filename)
{
    // Stop the loader before touching g_image.files or g_thumb.entries to avoid races.
    StopThumbnailLoader();

    // Snapshot existing thumbnails keyed by full path BEFORE we clear g_image.files.
    // After the sorted list is rebuilt, a new file may land in the middle, shifting
    // all subsequent indices.  A naive resize() would misalign every thumbnail that
    // follows the insertion point.  Keying by path lets us restore each entry to
    // its correct new index regardless of where the new file was inserted.
    std::unordered_map<std::wstring, ThumbnailEntry> thumbSnap;
    for (int i = 0; i < (int)g_thumb.entries.size() && i < (int)g_image.files.size(); ++i)
        if (g_thumb.entries[i].wic || g_thumb.entries[i].d2d)
            thumbSnap[g_image.files[i]] = g_thumb.entries[i];

    g_image.files.clear();
    // NOTE: do NOT clear g_view.stateByPath here.  States are keyed by full path, so
    // they never collide when the folder changes, and they must survive directory-
    // change notifications (WM_APP_DIRCHANGE) that are fired after a DEL - clearing
    // them here would erase every other image's saved zoom/pan on every deletion.
    g_image.index = -1;

    std::filesystem::path p(filename);
    std::filesystem::path dir = p.parent_path();

    for (auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;

        std::wstring path = entry.path().wstring();
        if (IsSupportedImage(path))
            g_image.files.push_back(path);
    }

    // Explorer's default "Name" sort is a natural/logical sort (e.g. "img2"
    // before "img10"), not a plain character-by-character comparison - a
    // naive _wcsicmp would put "img10" before "img2" since '1' < '2'.
    // StrCmpLogicalW is the actual API Explorer uses for this, so sorting
    // with it here matches Explorer's order exactly. Comparing full paths
    // (rather than just filenames) is fine since every entry shares the
    // same parent directory prefix.
    std::sort(g_image.files.begin(), g_image.files.end(),
        [](const std::wstring& a, const std::wstring& b)
        {
            return StrCmpLogicalW(a.c_str(), b.c_str()) < 0;
        });

    for (size_t i = 0; i < g_image.files.size(); ++i)
    {
        if (_wcsicmp(g_image.files[i].c_str(), filename) == 0)
        {
            g_image.index = (int)i;
            break;
        }
    }

    // Rebuild g_thumb.entries aligned to the new sorted g_image.files, restoring any
    // already-decoded thumbnails from the snapshot.  New slots (e.g. a freshly
    // added file) start empty and will be filled by the loader thread.
    const int newN = (int)g_image.files.size();
    g_thumb.entries.assign(newN, ThumbnailEntry{});
    for (int i = 0; i < newN; ++i)
    {
        auto it = thumbSnap.find(g_image.files[i]);
        if (it != thumbSnap.end())
            g_thumb.entries[i] = it->second;
    }

    // Start watching the directory for new/deleted/renamed image files
    StartDirectoryWatcher(std::filesystem::path(filename).parent_path().wstring());
    StartThumbnailLoader();
}


// ============================================================
//  Thumbnail film strip
// ============================================================

// ============================================================
//  Directory watcher (ReadDirectoryChangesW)
// ============================================================

// The watcher's state lives here rather than in Globals.h: no other translation
// unit reads it, and the shutdown order below only holds while nothing outside
// this file can close or reassign the handle while the thread is using it.
static HANDLE      g_watchHandle    = INVALID_HANDLE_VALUE;
static std::thread g_watchThread;
// Manual-reset event that releases the thread from its wait, set by
// StopDirectoryWatcher. The thread waits on it alongside the read's own event,
// so a stop request never depends on the watch handle being closed underneath a
// pending ReadDirectoryChangesW.
static HANDLE      g_watchStopEvent = nullptr;

void StopDirectoryWatcher()
{
    if (g_watchStopEvent)
        SetEvent(g_watchStopEvent);

    // Join before anything is closed: the thread may still be inside
    // ReadDirectoryChangesW or GetOverlappedResult on the watch handle, and on
    // the stop path it cancels and drains its own pending read first.
    if (g_watchThread.joinable())
        g_watchThread.join();

    if (g_watchHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_watchHandle);
        g_watchHandle = INVALID_HANDLE_VALUE;
    }
    if (g_watchStopEvent)
    {
        CloseHandle(g_watchStopEvent);
        g_watchStopEvent = nullptr;
    }
}

void StartDirectoryWatcher(const std::wstring& dir)
{
    StopDirectoryWatcher();

    g_watchStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!g_watchStopEvent)
        return;

    g_watchHandle = CreateFileW(
        dir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (g_watchHandle == INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_watchStopEvent);
        g_watchStopEvent = nullptr;
        return;
    }

    // The thread works on its own copies of both handles. StopDirectoryWatcher
    // closes the originals only after this thread has been joined, so each copy
    // stays valid for the whole life of the thread.
    g_watchThread = std::thread([dir, watchHandle = g_watchHandle, stopEvent = g_watchStopEvent]()
    {
        // Aligned buffer for FILE_NOTIFY_INFORMATION records
        alignas(DWORD) BYTE buf[4096];
        OVERLAPPED ov = {};
        ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return;

        const HANDLE waitHandles[2] = { ov.hEvent, stopEvent };

        for (;;)
        {
            // Nothing is outstanding at this point, so a stop request can simply
            // end the loop.
            if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
                break;

            ResetEvent(ov.hEvent);

            BOOL ok = ReadDirectoryChangesW(
                watchHandle,
                buf, sizeof(buf),
                FALSE,  // non-recursive
                FILE_NOTIFY_CHANGE_FILE_NAME,  // created, deleted, renamed
                nullptr,
                &ov,
                nullptr);

            if (!ok) break;

            // Wait for the overlapped result or a stop signal
            DWORD wait = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
            if (wait != WAIT_OBJECT_0)
            {
                // A stop was requested while the read was outstanding: cancel this
                // request and wait for the cancellation to finish, so the handle is
                // not closed while the request is still in flight.
                CancelIoEx(watchHandle, &ov);
                DWORD discarded = 0;
                GetOverlappedResult(watchHandle, &ov, &discarded, TRUE);
                break;
            }

            DWORD bytesReturned = 0;
            if (!GetOverlappedResult(watchHandle, &ov, &bytesReturned, FALSE))
                break;
            if (bytesReturned == 0) continue;

            // Check whether any changed file is a supported image
            bool relevant = false;
            const FILE_NOTIFY_INFORMATION* fni =
                reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buf);
            for (;;)
            {
                std::wstring name(fni->FileName,
                                  fni->FileNameLength / sizeof(WCHAR));
                std::wstring fullPath = dir + L"\\" + name;
                if (IsSupportedImage(fullPath))
                {
                    relevant = true;
                    break;
                }
                if (fni->NextEntryOffset == 0) break;
                fni = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<const BYTE*>(fni) + fni->NextEntryOffset);
            }

            if (relevant)
            {
                // Post to main window - safe to call from any thread
                HWND target = g_app.overlayWindow ? g_app.overlayWindow : g_app.mainWindow;
                if (target)
                    PostMessage(target, WM_APP_DIRCHANGE, 0, 0);
            }
        }
        CloseHandle(ov.hEvent);
    });
}

void StopThumbnailLoader()
{
    g_thumb.loaderStop = true;
    if (g_thumb.loaderThread.joinable())
        g_thumb.loaderThread.join();
    g_thumb.loaderStop = false;
}

void StartThumbnailLoader()
{
    // Stop the thread first so there is no race when we read g_thumb.entries below.
    StopThumbnailLoader();

    const int n = (int)g_image.files.size();

    // g_thumb.entries is already aligned with g_image.files: BuildImageList snapshotted
    // the old vector BY PATH before the natural sort shifted the indices and
    // re-deposited every entry at its new index. So a slot that already holds a
    // wic bitmap is one carried over from the previous list, and a null slot is
    // one this loader has to decode. Nothing here needs the old index order.
    //
    // On the very first BuildImageList g_thumb.entries is empty, so nothing is carried
    // over and every file is decoded fresh.

    // Grow/shrink the vector to the new file count. Entries BuildImageList has
    // already re-deposited keep their bitmaps; new slots start null.
    g_thumb.entries.resize(n);  // new slots default-construct to null wic+d2d

    {
        std::lock_guard<std::mutex> lk(g_thumb.readyMutex);
        while (!g_thumb.readyQueue.empty()) g_thumb.readyQueue.pop();
    }

    if (n == 0) return;

    // Build work list: only files whose slot has no WIC bitmap yet.
    // Spiral outward from the current image so the visible window fills first.
    const int cur = max(0, min(g_image.index, n - 1));
    std::vector<int> order;
    order.reserve(n);
    for (int d = 0; d < n; ++d)
    {
        const int a = cur + d, b = cur - d;
        if (d == 0) { if (!g_thumb.entries[cur].wic) order.push_back(cur); }
        else {
            if (a < n  && !g_thumb.entries[a].wic) order.push_back(a);
            if (b >= 0 && !g_thumb.entries[b].wic) order.push_back(b);
        }
    }

    if (order.empty()) return;  // everything already loaded

    g_thumb.loaderThread = std::thread(
        [files = g_image.files, indices = std::move(order)]()
        {
            if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;

            Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) || !wic)
            { CoUninitialize(); return; }

            for (int idx : indices)
            {
                if (g_thumb.loaderStop) break;
                if (idx < 0 || idx >= (int)files.size()) continue;

                std::wstring thumbExt = std::filesystem::path(files[idx]).extension().wstring();
                std::transform(thumbExt.begin(), thumbExt.end(), thumbExt.begin(), ::towlower);
                const bool isAvifThumb = (thumbExt == L".avif");
                const bool isJxlThumb  = (thumbExt == L".jxl");

                // For AVIF/JXL there's no WIC decoder/frame object at all - decode
                // through the same bundled codecs LoadImageD2D uses, straight into
                // a full-res IWICBitmapSource, then fall into the same scale-down
                // path used for every other format below. No embedded-thumbnail
                // fast path exists for these two (libavif/libjxl don't expose one
                // through this API), so it's always the "slow path" for them -
                // still fast enough for a background thread.
                Microsoft::WRL::ComPtr<IWICBitmapDecoder>     decoder;      // WIC-native formats only
                Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;        // WIC-native formats only
                Microsoft::WRL::ComPtr<IWICBitmapSource>      sourceForScale; // what we actually scale from
                WICBitmapTransformOptions wicTransform = WICBitmapTransformRotate0;

                if (isAvifThumb || isJxlThumb)
                {
                    Microsoft::WRL::ComPtr<IWICBitmap> full;
                    UINT fw = 0, fh = 0;
                    bool decodedOk = isAvifThumb
                        ? DecodeAvifToWicBitmap(files[idx].c_str(), wic.Get(), full, fw, fh)
                        : DecodeJxlToWicBitmap(files[idx].c_str(), wic.Get(), full, fw, fh);
                    if (!decodedOk || !full) continue;
                    sourceForScale = full;
                    // wicTransform stays Rotate0 - AVIF/JXL orientation isn't applied
                    // yet anywhere in this app (see DecodeAvifToWicBitmap's comment).
                }
                else
                {
                    if (FAILED(wic->CreateDecoderFromFilename(files[idx].c_str(), nullptr,
                            GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) || !decoder)
                        continue;
                    if (g_thumb.loaderStop) break;

                    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) continue;
                    if (g_thumb.loaderStop) break;
                    sourceForScale = frame;

                    // Read EXIF orientation tag (WIC-native formats only)
                    Microsoft::WRL::ComPtr<IWICMetadataQueryReader> mqr;
                    if (SUCCEEDED(frame->GetMetadataQueryReader(&mqr)) && mqr)
                    {
                        PROPVARIANT var;
                        PropVariantInit(&var);
                        HRESULT hm = mqr->GetMetadataByName(L"/app1/ifd/{ushort=274}", &var);
                        if (FAILED(hm))
                            hm = mqr->GetMetadataByName(L"/ifd/{ushort=274}", &var);
                        if (SUCCEEDED(hm))
                        {
                            UINT ori = 0;
                            if      (var.vt == VT_UI2) ori = var.uiVal;
                            else if (var.vt == VT_UI4) ori = var.ulVal;
                            else if (var.vt == VT_I2)  ori = (UINT)var.iVal;
                            else if (var.vt == VT_I4)  ori = (UINT)var.lVal;
                            switch (ori)
                            {
                                case 3: wicTransform = WICBitmapTransformRotate180; break;
                                case 6: wicTransform = WICBitmapTransformRotate90;  break;
                                case 8: wicTransform = WICBitmapTransformRotate270; break;
                                default: break;
                            }
                        }
                        PropVariantClear(&var);
                    }
                }

                const UINT TARGET_H = 180u;
                Microsoft::WRL::ComPtr<IWICBitmap> thumb;

                // ---- Fast path: use the embedded JPEG thumbnail if it exists
                // and is at least TARGET_H pixels tall (or wide for 90/270).
                // Only WIC-native formats (frame/decoder both non-null) have this. ----
                if (frame && decoder)
                {
                    Microsoft::WRL::ComPtr<IWICBitmapSource> embedded;
                    // Try frame thumbnail first, then decoder-level thumbnail.
                    if (FAILED(frame->GetThumbnail(&embedded)) || !embedded)
                        decoder->GetThumbnail(&embedded);

                    if (embedded)
                    {
                        UINT tw = 0, th = 0;
                        embedded->GetSize(&tw, &th);
                        const UINT shortSide = min(tw, th);
                        if (shortSide >= TARGET_H)
                        {
                            // Embedded thumbnail is large enough - just convert to PBGRA.
                            Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
                            Microsoft::WRL::ComPtr<IWICBitmap> converted;
                            if (SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
                                SUCCEEDED(conv->Initialize(embedded.Get(),
                                    GUID_WICPixelFormat32bppPBGRA,
                                    WICBitmapDitherTypeNone, nullptr, 0.f,
                                    WICBitmapPaletteTypeCustom)) &&
                                SUCCEEDED(wic->CreateBitmapFromSource(conv.Get(),
                                    WICBitmapCacheOnLoad, &converted)) &&
                                converted)
                            {
                                thumb = converted;
                            }
                        }
                    }
                }
                if (g_thumb.loaderStop) break;

                // ---- Slow path: decode full source and scale down.
                // Runs for GIF/PNG/JPEG/etc. without a usable embedded thumbnail,
                // and unconditionally for AVIF/JXL. ----
                if (!thumb)
                {
                    UINT sw = 0, sh = 0;
                    sourceForScale->GetSize(&sw, &sh);
                    if (sw == 0 || sh == 0) continue;

                    // Scale using raw (pre-rotation) dimensions so the scaler
                    // never stretches; the rotator corrects orientation afterwards.
                    UINT scaleW, scaleH;
                    if (wicTransform == WICBitmapTransformRotate90 ||
                        wicTransform == WICBitmapTransformRotate270)
                    {
                        scaleH = max(1u, (UINT)((float)sh / (float)sw * TARGET_H));
                        scaleW = TARGET_H;
                    }
                    else
                    {
                        scaleW = max(1u, (UINT)((float)sw / (float)sh * TARGET_H));
                        scaleH = TARGET_H;
                    }

                    Microsoft::WRL::ComPtr<IWICBitmapScaler>    scaler;
                    Microsoft::WRL::ComPtr<IWICFormatConverter> conv;

                    if (FAILED(wic->CreateBitmapScaler(&scaler)) ||
                        FAILED(scaler->Initialize(sourceForScale.Get(), scaleW, scaleH,
                            WICBitmapInterpolationModeHighQualityCubic)) ||
                        FAILED(wic->CreateFormatConverter(&conv)) ||
                        FAILED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                            WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom)))
                        continue;
                    if (g_thumb.loaderStop) break;

                    if (FAILED(wic->CreateBitmapFromSource(conv.Get(), WICBitmapCacheOnLoad, &thumb)) || !thumb)
                        continue;
                }

                // Apply EXIF rotation on the small cached bitmap (both paths)
                if (wicTransform != WICBitmapTransformRotate0)
                {
                    Microsoft::WRL::ComPtr<IWICBitmapFlipRotator> rotator;
                    Microsoft::WRL::ComPtr<IWICBitmap> rotated;
                    if (SUCCEEDED(wic->CreateBitmapFlipRotator(&rotator)) &&
                        SUCCEEDED(rotator->Initialize(thumb.Get(), wicTransform)) &&
                        SUCCEEDED(wic->CreateBitmapFromSource(rotator.Get(), WICBitmapCacheOnLoad, &rotated)) &&
                        rotated)
                    {
                        thumb = rotated;
                    }
                }

                {
                    std::lock_guard<std::mutex> lk(g_thumb.readyMutex);
                    g_thumb.readyQueue.push({ idx, thumb });
                }
            }
            CoUninitialize();
        });
}

void DrawThumbnailStrip(float visibility)
{
    if (!g_devices.renderTarget || !g_devices.d2dFactory)         return;
    if (visibility <= 0.01f)                       return;
    if (g_thumb.entries.empty() || !g_thumb.sizeCaptured) return;
    if (g_image.index < 0)                   return;

    const D2D1_SIZE_F rtSz  = g_devices.renderTarget->GetSize();
    const float stride = g_thumb.width + g_thumb.gap;
    const int   n      = (int)g_thumb.entries.size();
    const float baseX  = rtSz.width * 0.5f + g_thumb.scrollOffset;
    const float cy     = rtSz.height - g_thumb.stripInsetFromBottomPx;  // anchored: fixed distance from bottom
    const float hw     = g_thumb.width * 0.5f;
    const float hh     = g_thumb.height * 0.5f;
    const float radius = g_thumb.height * 0.10f;

    // One cached geometry centred at the origin; we move the render target
    // transform to each thumbnail position so every slot reuses the same geometry.
    ID2D1RoundedRectangleGeometry* clipGeo =
        (!g_thumb.clipGeos.empty()) ? g_thumb.clipGeos[0].Get() : nullptr;

    // Use the base ID2D1RenderTarget pointer so PushLayer resolves to the
    // D2D1_LAYER_PARAMETERS (not D2D1_LAYER_PARAMETERS1) overload.
    ID2D1RenderTarget* rt = g_devices.renderTarget.Get();

    for (int i = 0; i < n; ++i)
    {
        const float cx = baseX + i * stride;
        if (cx + hw < 0.f || cx - hw > rtSz.width) continue;

        const D2D1_RECT_F       r  = D2D1::RectF(cx - hw, cy - hh, cx + hw, cy + hh);
        const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, radius, radius);
        const bool isCurrent = (i == g_image.index);

        if (g_thumb.entries[i].d2d && clipGeo)
        {
            // Translate the RT so the cached origin-centred geometry aligns with
            // this slot, push the clip layer, then restore the identity transform.
            rt->SetTransform(D2D1::Matrix3x2F::Translation(cx, cy));
            rt->PushLayer(
                D2D1::LayerParameters(D2D1::InfiniteRect(), clipGeo,
                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                    D2D1::IdentityMatrix(), visibility),
                nullptr);
            rt->SetTransform(D2D1::Matrix3x2F::Identity());

            // Centre-crop the bitmap to fill the slot
            const D2D1_SIZE_F bsz = g_thumb.entries[i].d2d->GetSize();
            const float bA = bsz.width  / max(1.f, bsz.height);
            const float tA = g_thumb.width   / max(1.f, g_thumb.height);
            float srcL = 0.f, srcT = 0.f, srcR = bsz.width, srcB = bsz.height;
            if (bA > tA) {
                const float nw = bsz.height * tA;
                srcL = (bsz.width  - nw) * 0.5f; srcR = srcL + nw;
            } else {
                const float nh = bsz.width / max(0.001f, tA);
                srcT = (bsz.height - nh) * 0.5f; srcB = srcT + nh;
            }

            const D2D1_RECT_F srcRect = D2D1::RectF(srcL, srcT, srcR, srcB);
            rt->DrawBitmap(g_thumb.entries[i].d2d.Get(), &r, 1.0f,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &srcRect);

            rt->PopLayer();
        }
        else
        {
            // Placeholder while thumbnail is still loading
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ph;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(0.22f, 0.22f, 0.22f, 0.8f * visibility), &ph);
            if (ph) rt->FillRoundedRectangle(&rr, ph.Get());
        }

        // Darken non-current thumbnails
        if (!isCurrent)
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dim;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(0.f, 0.f, 0.f, 0.50f * visibility), &dim);
            if (dim) rt->FillRoundedRectangle(&rr, dim.Get());
        }
    }
}
