// Picasso Pictures.cpp : Defines the entry point for the application.
//

// MAX_LOADSTRING, framework.h, the control/command IDs, the shared types and
// every global are now in Globals.h (defined in Globals.cpp). Globals.h
// defines MAX_LOADSTRING and includes framework.h itself.
#include "Globals.h"
#include "ViewMath.h"
#include "Fullscreen.h"
#include "Rendering.h"
#include "Decoding.h"
#include "Thumbnails.h"
#include "UI.h"
#include "Metadata.h"
#include "Dialogs.h"
#include "resource.h"
#include "AnimatedButton.h"
#include "UITextBox.h"
#include <windows.h>
#include <objidl.h>
#include <commdlg.h>
#include <algorithm>
#include <windowsx.h>
#include <d2d1.h>
#include <wincodec.h>
#include <vector>
#include <string>
#include <dwrite.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#include <filesystem>
#include <dwmapi.h>
#include <uxtheme.h>
#include <wrl/client.h>
#include <cmath>
#include <functional>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl.h>
#include <d2d1effects.h>
#include <unordered_map>
#include <d3dcompiler.h>

// ---- Third-party image codecs (bundled so users don't need OS/Store codecs) ----
// Get these via vcpkg (recommended):
//     vcpkg install libavif dav1d libjxl
// Both build as static libs with the x64-windows-static (or -static-md)
// triplet, so no extra DLLs need to ship alongside PicassoPictures.exe.
// Library names below match the mainstream vcpkg port output as of this
// writing - if your build reports different .lib names (this has shifted
// across libavif/libjxl releases), check vcpkg's install output and adjust
// the #pragma comment lines to match.
#include <avif/avif.h>
#include <jxl/decode.h>
#include <jxl/thread_parallel_runner.h>


#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "d3dcompiler.lib")


using Microsoft::WRL::ComPtr;

// (ImageViewState, the control/command IDs and every global variable are
//  declared in Globals.h, with their single definition site in Globals.cpp.)


// Forward declarations of functions included in this code module:
static std::wstring GetInitialFileFromCommandLine();
int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow);
void EnableDarkTitleBar(HWND hwnd);
ATOM MyRegisterClass(HINSTANCE hInstance);
void UpdateEngine(float dt);
bool IsSupportedImage(const std::wstring& path);
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);

// SetZoomCentered / FitToWindowRelative / InitializeMenuButtons /
// InitializeButtons / InitializeImageInfoLabel / UpdateTargetZoom are declared
// in UI.h. Their default arguments (preserveCenter = true, hard = false) live
// on those declarations ONLY - repeating them on the UI.cpp definitions is
// C2522.

// EnterFullscreen / ExitFullscreen / CompleteExitFullscreen and the slideshow
// + black-cover declarations are in Fullscreen.h. IsOwnedByOurWindow and
// FullscreenExitSuppressor are in Globals.h, so the fullscreen-exit
// suppression contract travels with the counter it guards.

// LoadImageD2D and the AVIF/JXL decode entry points are declared in
// Decoding.h; the rest of the loader region is private to Decoding.cpp.
bool OpenImageFile(HWND hWnd);
void OpenNextImage(HWND hWnd);
void OpenPrevImage(HWND hWnd);
void DeleteCurrentImage(HWND hWnd, bool permanent);

// ShowImageMetadata / ShowImageProperties and the whole reader region are in
// Metadata.h; the 27 `static` helpers inside it keep internal linkage in
// Metadata.cpp and are declared nowhere else.

// ZoomIntoImage / MakeZoomVisible and InitializeImageLayout are in UI.h.

// BuildImageList, Start/StopThumbnailLoader, DrawThumbnailStrip and the
// Start/StopDirectoryWatcher pair are in Thumbnails.h. IsSupportedImage stays
// here: it is a file-type predicate shared by the file-open path and the
// navigation code, so Thumbnails.cpp declares it locally rather than the
// other way round.
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// AssociateFileTypes and About (the file-association writer and the one proc
// behind BOTH the About box and the Keyboard-shortcuts box) are declared in
// Dialogs.h. About is handed straight to four DialogBox() calls in WndProc
// below, so its CALLBACK linkage has to stay exactly as declared there.

static std::wstring GetInitialFileFromCommandLine();

static std::wstring GetInitialFileFromCommandLine()
{
    // Use CommandLineToArgvW so quoting/spaces are handled correctly.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring path;
    if (argv && argc > 1 && argv[1])
        path = argv[1];
    if (argv)
        LocalFree(argv);
    return path;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Get initial file from the command line.
    std::wstring initialFile = GetInitialFileFromCommandLine();
    g_app.launchedWithFile = !initialFile.empty();
    // --- Load the strings early so we can find the existing window by class name ---
    LoadStringW(hInstance, IDS_APP_TITLE, g_app.title, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_PICASSOPICTURES, g_app.windowClass, MAX_LOADSTRING);

    // --- Single-instance check using a named mutex ---
    // Use a stable name for the mutex (change GUID-like part if you want a different token)
    static const wchar_t* MUTEX_NAME = L"Local\\PicassoPictures_SingleInstance_v1";

    HANDLE hMutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);

    if (hMutex == nullptr)
    {
        // Can't create mutex - proceed as normal (or optionally exit)
    }
    else
    {
        DWORD lastErr = GetLastError();
        if (lastErr == ERROR_ALREADY_EXISTS || lastErr == ERROR_ACCESS_DENIED)
        {
            // Another instance probably exists. Try to find its window and send the filename.
            HWND hOtherWnd = FindWindowW(g_app.windowClass, nullptr);
                        if (!hOtherWnd)
            {
                MessageBoxW(nullptr, L"Mutex exists but window not found!", L"Race condition!", MB_OK);
            }
            if (hOtherWnd)
            {

                // If we have a file, send it
                if (!initialFile.empty())
                {
                    COPYDATASTRUCT cds;
                    cds.dwData = 1;
                    cds.cbData = static_cast<DWORD>((initialFile.size() + 1) * sizeof(wchar_t));
                    cds.lpData = (PVOID)initialFile.c_str();

                    DWORD_PTR result = 0;
                    SendMessageTimeoutW(
                        hOtherWnd,
                        WM_COPYDATA,
                        0,
                        (LPARAM)&cds,
                        SMTO_ABORTIFHUNG | SMTO_NORMAL,
                        2000,
                        &result);
                }

                // Bring existing window to foreground
                if (IsIconic(hOtherWnd))
                    ShowWindow(hOtherWnd, SW_RESTORE);

                DWORD currentThread = GetCurrentThreadId();
                DWORD targetThread = GetWindowThreadProcessId(hOtherWnd, nullptr);

                AttachThreadInput(currentThread, targetThread, TRUE);

                SetForegroundWindow(hOtherWnd);
                SetFocus(hOtherWnd);
                SetActiveWindow(hOtherWnd);

                AttachThreadInput(currentThread, targetThread, FALSE);

                CloseHandle(hMutex);
                return 0;
            }
        }
    }

    // --- Normal initialization continues here ---

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Restore persisted settings from registry
    {
        DWORD hqVal = 0, cb = sizeof(hqVal);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\PicassoPictures",
                     L"HQFilter", RRF_RT_REG_DWORD, nullptr, &hqVal, &cb);
        g_settings.useHQFilter = (hqVal != 0);

        DWORD fitVal = 0; cb = sizeof(fitVal);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\PicassoPictures",
                     L"FitToScreen", RRF_RT_REG_DWORD, nullptr, &fitVal, &cb);
        g_settings.openFitToScreen = (fitVal != 0);
    }

    if (FAILED(CoInitialize(nullptr)))
    {
        if (hMutex) CloseHandle(hMutex);
        return FALSE;
    }
    D2D1_FACTORY_OPTIONS options = {};
    #ifdef _DEBUG
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
    #endif

    if (FAILED(D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),
        &options,
        reinterpret_cast<void**>(g_devices.d2dFactory.GetAddressOf()))))
    {
        CoUninitialize();
        if (hMutex) CloseHandle(hMutex);
        return FALSE;
    }

    if (FAILED(CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(g_devices.wicFactory.GetAddressOf()))))
    {
        CoUninitialize();
        if (hMutex) CloseHandle(hMutex);
        return FALSE;
    }

    LoadDefaultBackgroundFromResource();

    if (FAILED(DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_devices.dwriteFactory.GetAddressOf()))))
    {
        CoUninitialize();
        if (hMutex) CloseHandle(hMutex);
        return FALSE;
    }
  
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int refreshRate = GetMonitorRefreshRate(GetDesktopWindow());
    g_view.smooth = g_view.smooth * 60.0f / (float)refreshRate;

    // Make overlay box 2.3% of screen height
    float boxHeight = screenHeight * 0.023f;

    // Font ~50% of that
    float fontSize = boxHeight * 0.5f;

    if (FAILED(g_devices.dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSize,
        L"",
        g_devices.textFormat.GetAddressOf())))
    {
        CoUninitialize();
        if (hMutex) CloseHandle(hMutex);
        return FALSE;
    }

    // Center alignment
    g_devices.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_devices.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // The same two strings again, into the same buffers: the earlier pair exists so
    // the single-instance check could match the class name with FindWindow.
    LoadStringW(hInstance, IDS_APP_TITLE, g_app.title, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_PICASSOPICTURES, g_app.windowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    RegisterOverlayClass(hInstance);
    RegisterBlackCoverClass(hInstance);

    // Create window
    if (!InitInstance(hInstance, nCmdShow))
    {
        CoUninitialize();
        if (hMutex) CloseHandle(hMutex);
        return FALSE;
    }
    if (!g_devices.renderTarget)
        CreateRenderTarget(g_app.mainWindow);
    // Main menu:
    SetMenu(g_app.mainWindow, nullptr);
    InitializeMenuButtons();

    // If we have an initial file, load it and enter fullscreen immediately.
    if (!initialFile.empty())
    {
        BuildImageList(initialFile.c_str());
        LoadImageD2D(g_app.mainWindow, initialFile.c_str());
        EnterFullscreen(false, true);
    }

    HACCEL hAccelTable = LoadAccelerators(
        hInstance,
        MAKEINTRESOURCE(IDC_PICASSOPICTURES)
    );

    MSG msg = {};
    ULONGLONG lastTime = GetTickCount64();

    // Main loop
    while (true)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                goto shutdown;

            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        ULONGLONG now = GetTickCount64();
        float dt = (now - lastTime) / 1000.0f;
        lastTime = now;

        if (dt > 0.03f)
            dt = 0.03f;

        UpdateEngine(dt);
    }

shutdown:
    // Cleanup
    if (hAccelTable) DestroyAcceleratorTable(hAccelTable);
    CoUninitialize();
    if (hMutex) CloseHandle(hMutex);
    return (int)msg.wParam;
}


void EnableDarkTitleBar(HWND hwnd)
{
    BOOL useDark = TRUE;
    DwmSetWindowAttribute(
        hwnd,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &useDark,
        sizeof(useDark));

    // Dark menu bar via UxTheme ordinals (Windows 10 1903+, stable but undocumented).
    // Loaded dynamically so nothing breaks on older systems.
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (ux)
    {
        // Ordinal 135 - SetPreferredAppMode(1 = AllowDark)
        using FnSetPreferredAppMode = int (WINAPI*)(int);
        if (auto fn = reinterpret_cast<FnSetPreferredAppMode>(
                GetProcAddress(ux, MAKEINTRESOURCEA(135))))
            fn(1);

        // Ordinal 133 - AllowDarkModeForWindow(hwnd, true)
        using FnAllowDark = bool (WINAPI*)(HWND, bool);
        if (auto fn = reinterpret_cast<FnAllowDark>(
                GetProcAddress(ux, MAKEINTRESOURCEA(133))))
            fn(hwnd, true);

        // Ordinal 136 - FlushMenuThemes() - applies changes immediately
        using FnFlushMenuThemes = void (WINAPI*)();
        if (auto fn = reinterpret_cast<FnFlushMenuThemes>(
                GetProcAddress(ux, MAKEINTRESOURCEA(136))))
            fn();

        FreeLibrary(ux);
    }

    SetWindowTheme(hwnd, L"Explorer", nullptr);
    SendMessage(hwnd, WM_THEMECHANGED, 0, 0);
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PICASSOPICTURES));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = nullptr;
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_PICASSOPICTURES);
    wcex.lpszClassName  = g_app.windowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}


void UpdateEngine(float dt)
{
    if (g_anim.isAnimated && !g_anim.frames.empty())
    {
        ULONGLONG now = GetTickCount64();
        if (now - g_anim.lastFrameTime >= g_anim.frameDelays[g_anim.currentFrame])
        {
            const UINT nextFrame = (g_anim.currentFrame + 1) % g_anim.frames.size();
            const UINT readyCount = (UINT)max(0, g_anim.framesReadyUpTo.load());

            // While the background decode thread is still working through a
            // large animated image (GIF, WebP, APNG or AVIF), don't advance past
            // the last frame it's actually produced yet (this also blocks
            // wrapping around to frame 0 before the tail end of a long
            // animation has been decoded even once). Just hold on the
            // current frame and check again next tick - once fully caught
            // up, readyCount == frame count and this condition is always
            // true, i.e. normal playback resumes.
            if (nextFrame < readyCount || readyCount >= g_anim.frames.size())
            {
                g_anim.currentFrame = nextFrame;

                // Already uploaded? Just swap the pointer - no CPU->GPU work.
                if (g_anim.currentFrame < g_anim.gpuFrames.size() && g_anim.gpuFrames[g_anim.currentFrame])
                {
                    g_view.bitmap = g_anim.gpuFrames[g_anim.currentFrame];
                    g_pipeline.srv.Reset();  // animated formats never carry a mip SRV
                }
                else
                {
                    // First time reaching this frame: decode/upload it now, and
                    // cache the result so it's never re-uploaded on later loops.
                    g_image.source = g_anim.frames[g_anim.currentFrame];
                    RecreateImageBitmap();

                    if (g_anim.currentFrame < g_anim.gpuFrames.size())
                        g_anim.gpuFrames[g_anim.currentFrame] = g_view.bitmap;
                }
            }

            g_anim.lastFrameTime = now;
        }
    }

    if (std::fabs(g_view.targetRotationAngle - g_view.rotationAngle) > 0.01f)
    {
        float delta = g_view.targetRotationAngle - g_view.rotationAngle;
        float step = g_view.rotationSpeed * dt;
        if (std::fabs(step) > std::fabs(delta))
            step = delta;
        else
            step *= (delta > 0 ? 1.f : -1.f);

        g_view.rotationAngle += step;
    }
    float overlayTarget = g_app.isExiting ? 0.0f : 1.0f;
    g_fullscreen.overlayAlpha += (overlayTarget - g_fullscreen.overlayAlpha) * 0.15f * dt * 60.0f;

    // ---- Slideshow pre-fade (current view -> black, then window transition) ----
    // g_slideshow.preFade stays true until Render() confirms the overlay's first Present().
    if (g_slideshow.preFade && !g_slideshow.active)
    {
        // Phase 1: fade the current render target to black
        g_slideshow.preFadeAlpha += SLIDESHOW_FADE_STEP * dt * 60.0f;

        if (g_slideshow.preFadeAlpha > 0.995f)
        {
            g_slideshow.preFadeAlpha = 1.0f;

            // Phase 2: screen is provably black in D2D - now raise the cover
            // window and flush DWM so it is physically on screen before we
            // destroy the render target.
            CreateBlackCoverWindow();

            if (g_view.isFullscreen) ExitFullscreen(/*immediate=*/true);
            g_slideshow.active          = true;
            UpdateSlideshowButton();
            g_slideshow.prevBitmap.Reset();
            g_slideshow.prevBgBitmap.Reset();
            g_slideshow.transitionAlpha = 0.0f;
            g_slideshow.targetAlpha     = 1.0f;
            EnterFullscreen(false, true);
            SetTimer(g_app.mainWindow, static_cast<UINT_PTR>(TimerId::SLIDESHOW), SLIDESHOW_INTERVAL_MS, nullptr);
            // g_slideshow.preFade remains true - Render() clears it after first Present().
        }
    }

    // ---- Slideshow stop fade (slideshow -> plain fullscreen) ----
    // Black in, rect change at full black, black out. Only runs when the
    // slideshow was started from fullscreen; the windowed case goes through the
    // normal fullscreen exit fade instead.
    if (g_slideshow.stopFade)
    {
        // If the screen is already on its way out of fullscreen, drop the resize
        // and let the black fade out on its own - re-fitting the image would undo
        // the exit animation that is now driving the view.
        if (g_app.isExiting || g_fullscreen.pendingExit)
            g_slideshow.stopFadePendingResize = false;

        if (g_slideshow.stopFadePendingResize)
        {
            g_slideshow.stopFadeAlpha += SLIDESHOW_FADE_STEP * dt * 60.0f;

            if (g_slideshow.stopFadeAlpha >= 1.0f)
            {
                g_slideshow.stopFadeAlpha = 1.0f;
                ResizeOverlayToWorkArea();
                g_slideshow.stopFadePendingResize = false;
            }
        }
        else
        {
            g_slideshow.stopFadeAlpha -= SLIDESHOW_FADE_STEP * dt * 60.0f;

            if (g_slideshow.stopFadeAlpha <= 0.0f)
            {
                g_slideshow.stopFadeAlpha = 0.0f;
                g_slideshow.stopFade      = false;
            }
        }
    }

    // ---- Fullscreen exit fade (desktop bg fades in, dim lifts, then teardown) ----
    if (g_fullscreen.pendingExit && g_view.isFullscreen)
    {
        g_fullscreen.exitBgFadeAlpha += 0.04f * dt * 60.0f;

        if (g_fullscreen.exitBgFadeAlpha > 0.995f)
        {
            g_fullscreen.exitBgFadeAlpha       = 1.0f;
            g_fullscreen.pendingExit = false;
            CompleteExitFullscreen();
        }
    }

    // ---- Slideshow cross-fade (image-to-image transition) ----
    if (g_slideshow.active && g_slideshow.transitionAlpha < g_slideshow.targetAlpha)
    {
        g_slideshow.transitionAlpha += 0.02f * dt * 60.0f;
        g_slideshow.transitionAlpha += (1.0f - g_slideshow.transitionAlpha) * (1.0f - std::powf(1.0f - 0.005f, dt * 60.0f));

        if (g_slideshow.transitionAlpha > 0.995f)
        {
            g_slideshow.transitionAlpha = 1.0f;
            g_slideshow.prevBitmap.Reset();
            g_slideshow.prevBgBitmap.Reset();
        }
    }

    // ---- Thumbnail strip: drain loader queue (one upload per frame) ----
    {
        std::lock_guard<std::mutex> lk(g_thumb.readyMutex);
        if (!g_thumb.readyQueue.empty())
        {
            auto& [idx, bmp] = g_thumb.readyQueue.front();
            if (idx >= 0 && idx < (int)g_thumb.entries.size())
                g_thumb.entries[idx].wic = std::move(bmp);
            g_thumb.readyQueue.pop();
        }
    }

    // Animate thumbnail scroll offset
    if (g_thumb.sizeCaptured)
    {
        if (!g_thumb.freeScroll && !g_thumb.dragging)
        {
            // Normal mode: keep the strip centered on the current image.
            g_thumb.targetOffset = -(g_image.index * (g_thumb.width + g_thumb.gap));
        }
        // During an active drag g_thumb.scrollOffset is updated directly in
        // WM_MOUSEMOVE (zero-lag direct manipulation); skip the spring step so
        // the strip tracks the finger exactly.  After release the momentum target
        // is already set and the spring provides natural deceleration.
        if (!g_thumb.dragging)
        {
            g_thumb.scrollOffset += (g_thumb.targetOffset - g_thumb.scrollOffset) * g_view.smooth;
        }
    }

    g_view.zoom     += (g_view.targetZoom     - g_view.zoom)     * g_view.smooth;
    g_view.offsetX  += (g_view.targetOffsetX  - g_view.offsetX)  * g_view.smooth;
    g_view.offsetY  += (g_view.targetOffsetY  - g_view.offsetY)  * g_view.smooth;

    HWND renderWindow = (g_view.isFullscreen && g_app.overlayWindow)
        ? g_app.overlayWindow
        : g_app.mainWindow;

    // Update button animations
    for (auto& [id, btn] : g_buttons)
        btn.Update(dt);

    for (auto& [id, txt] : g_textBoxes)
        txt.UpdateVisibility(dt, 0.08f);

    // Smooth-animate vignette visibility (same falloff as the buttons' visibility,
    // 0.08 per 60 Hz frame, rescaled by dt so the fade takes the same real time at
    // any refresh rate - see SmoothAlpha).
    const float vignetteAlpha = SmoothAlpha(0.08f, dt);
    g_vignette.topVisibility    += (g_vignette.topTarget    - g_vignette.topVisibility)    * vignetteAlpha;
    g_vignette.bottomVisibility += (g_vignette.bottomTarget - g_vignette.bottomVisibility) * vignetteAlpha;

    InvalidateRect(renderWindow, nullptr, FALSE);

    if (g_app.isExiting &&
        std::fabs(g_view.zoom - g_view.targetZoom) < 0.0005f &&
        g_fullscreen.overlayAlpha < 0.01f)
    {
        // Destroy the main window rather than only posting a quit message:
        // WM_DESTROY is what stops the three background threads and empties the
        // containers holding COM objects, it destroys the overlay along with it,
        // and it posts WM_QUIT itself. Quitting with the windows still alive
        // leaves those threads joinable when the process's static destructors
        // run, and a joinable std::thread destructing calls std::terminate -
        // that is the 0xc0000409 seen on exit. The teardown used to be gated on
        // g_app.overlayWindow, so an exit with no overlay (deleting the last
        // file in a folder while windowed, for instance) skipped all of it.
        //
        // g_app.mainWindow is cleared only AFTER DestroyWindow returns: the
        // WM_DESTROY guard is `hWnd == g_app.mainWindow`, so clearing it first
        // would make that handler skip the whole teardown. By the time it
        // returns, WM_DESTROY has already destroyed and cleared the overlay.
        if (g_app.mainWindow)
        {
            DestroyWindow(g_app.mainWindow);
            g_app.mainWindow = nullptr;
        }
        else if (g_app.overlayWindow)
        {
            DestroyWindow(g_app.overlayWindow);
            g_app.overlayWindow = nullptr;
            PostQuitMessage(0);
        }
        else
        {
            PostQuitMessage(0);
        }
    }
}

bool IsSupportedImage(const std::wstring& path)
{
    std::wstring ext = std::filesystem::path(path).extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    return ext == L".jpg"  ||
           ext == L".jpeg" ||
           ext == L".png"  ||
           ext == L".bmp"  ||
           ext == L".gif"  ||
           ext == L".tif"  ||
           ext == L".tiff" ||
           ext == L".webp" ||
           ext == L".avif" ||   // Decoded via bundled libavif/dav1d - no OS/Store extension required
           ext == L".jxl";      // Decoded via bundled libjxl - no OS/Store extension required
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    g_app.instance = hInstance; // Store instance handle in our global variable

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    // The one process-wide UI scale, set before any widget exists so it is never the
    // 1.0f default. Fullscreen stage 2 overwrites it with the monitor actually being
    // fullscreened (Fullscreen.cpp), which is what lets a tooltip on a different
    // monitor re-measure itself. Tooltips are the only thing that reads this today:
    // every widget sizes itself from its own Layout::uiPixelScale snapshot instead.
    g_view.uiPixelScale = (float)min(screenW, screenH);

    // Use 50% of screen height
    int windowHeight = screenH / 2;

    // Default background aspect ratio (3:2)
    float aspect = 1536.0f / 1024.0f;

    // Compute width from height
    int windowWidth = (int)(windowHeight * aspect);

    HWND hWnd = CreateWindowExW(
        0,
        g_app.windowClass,
        g_app.title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowWidth,
        windowHeight,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hWnd)
        return FALSE;

    EnableDarkTitleBar(hWnd);
    g_app.mainWindow = hWnd;
    DragAcceptFiles(hWnd, TRUE);

    // Center window on screen at startup
    RECT rc;
    GetWindowRect(hWnd, &rc);

    windowWidth  = rc.right - rc.left;
    windowHeight = rc.bottom - rc.top;

    int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    int posX = (screenWidth  - windowWidth)  / 2;
    int posY = (screenHeight - windowHeight) / 2;

    SetWindowPos(
        hWnd,
        nullptr,
        posX,
        posY,
        0,
        0,
        SWP_NOSIZE | SWP_NOZORDER
    );

    if (!g_app.launchedWithFile)
    {
        ShowWindow(hWnd, nCmdShow);
        UpdateWindow(hWnd);
    }
    else
    {
        // We'll enter fullscreen once the command-line file loads successfully.
        ShowWindow(hWnd, SW_HIDE);
    }

    g_devices.renderTargetWindow = nullptr; // will be set once render target is created
    return TRUE;
}


bool OpenImageFile(HWND hWnd)
{
    // Buffer that will receive the selected file path
    wchar_t fileName[MAX_PATH] = { 0 };

    // Structure used by the Windows file open dialog
    OPENFILENAME ofn = {};

    ofn.lStructSize = sizeof(ofn);         // Required size
    ofn.hwndOwner = hWnd;                  // Parent window
    ofn.lpstrFile = fileName;              // Output buffer
    ofn.nMaxFile = MAX_PATH;               // Buffer size

    // File filter (double-null terminated!)
    ofn.lpstrFilter =
        L"All Supported Images (*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.avif;*.jxl)\0"
        L"*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.avif;*.jxl\0"
        L"All Files (*.*)\0*.*\0";

    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;
    ofn.lpstrDefExt = L"jpg";

    // In fullscreen the overlay is the real render target.  Use it as the
    // dialog owner so that focus-change messages target the overlay. This
    // also makes the dialog an owned window of g_app.overlayWindow, which
    // IsOwnedByOurWindow() recognizes in WM_ACTIVATE - no manual suppression
    // needed here anymore.
    if (g_view.isFullscreen && g_app.overlayWindow && IsWindow(g_app.overlayWindow))
        ofn.hwndOwner = g_app.overlayWindow;

    bool got = GetOpenFileName(&ofn) != FALSE;

    if (!got)
        return false;   // User cancelled

    // Load image using Direct2D/WIC
    if (!LoadImageD2D(hWnd, fileName))
    {
        std::wstring msg = g_image.lastLoadError.empty()
            ? L"Failed to load image."
            : g_image.lastLoadError;
        MessageBox(hWnd, msg.c_str(), L"Error", MB_ICONERROR);
        return false;
    }
    BuildImageList(fileName);

    if (g_view.isFullscreen)
    {
        // Already fullscreen - reset layout using the overlay window so that
        // g_view.offsetX/Y are computed against the full-screen client rect.
        // Using g_app.mainWindow here (e.g. when called from the button callback)
        // would give wrong coordinates and cause the overImage check in
        // WM_LBUTTONUP to fail, exiting fullscreen on the next click.
        HWND layoutWnd = (g_app.overlayWindow && IsWindow(g_app.overlayWindow))
                         ? g_app.overlayWindow : hWnd;
        InitializeImageLayout(layoutWnd, true);
    }
    else
    {
        EnterFullscreen();
    }
    return true;
}

void OpenPrevImage(HWND hWnd)
{
    if (!g_image.files.empty() && g_image.index > 0)
    {
        g_thumb.freeScroll = false;  // resume auto-centering on the new image
        g_image.index--;
        LoadImageD2D(hWnd, g_image.files[g_image.index].c_str());
        InitializeImageLayout(hWnd, true);
    }
}

void OpenNextImage(HWND hWnd)
{
    if (!g_image.files.empty() &&
        g_image.index < (int)g_image.files.size() - 1)
    {
        g_thumb.freeScroll = false;  // resume auto-centering on the new image
        g_image.index++;
        LoadImageD2D(hWnd, g_image.files[g_image.index].c_str());
        InitializeImageLayout(hWnd, true);
    }
}


void DeleteCurrentImage(HWND hWnd, bool permanent)
{
    if (g_image.index < 0 || g_image.index >= (int)g_image.files.size())
        return;

    std::wstring fileToDelete = g_image.files[g_image.index];
    bool success = false;

    if (permanent)
    {
        success = std::filesystem::remove(fileToDelete);
    }
    else
    {
        SHFILEOPSTRUCTW op = {};
        std::wstring from = fileToDelete + L'\0';
        op.wFunc  = FO_DELETE;
        op.pFrom  = from.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
        success = (SHFileOperationW(&op) == 0);
    }

    if (success)
    {
        g_view.stateByPath.erase(fileToDelete);

        StopThumbnailLoader();

        struct ThumbSnap { Microsoft::WRL::ComPtr<IWICBitmap> wic;
                           Microsoft::WRL::ComPtr<ID2D1Bitmap> d2d; };
        std::unordered_map<std::wstring, ThumbSnap> snap;
        for (int si = 0; si < (int)g_thumb.entries.size() &&
                         si < (int)g_image.files.size(); ++si)
            if (g_thumb.entries[si].wic || g_thumb.entries[si].d2d)
                snap[g_image.files[si]] = { g_thumb.entries[si].wic, g_thumb.entries[si].d2d };

        g_image.files.erase(g_image.files.begin() + g_image.index);

        const int newN = (int)g_image.files.size();
        g_thumb.entries.assign(newN, ThumbnailEntry{});
        for (int si = 0; si < newN; ++si)
        {
            auto it = snap.find(g_image.files[si]);
            if (it != snap.end())
            {
                g_thumb.entries[si].wic = it->second.wic;
                g_thumb.entries[si].d2d = it->second.d2d;
            }
        }

        if (g_image.files.empty())
        {
            InitFullScreenExit();
            return;
        }

        if (g_image.index >= (int)g_image.files.size())
            g_image.index = (int)g_image.files.size() - 1;

        LoadImageD2D(hWnd, g_image.files[g_image.index].c_str());
        InitializeImageLayout(hWnd, true);
        StartThumbnailLoader();
    }
}


// Set by WM_LBUTTONDOWN when a button acted on the press and read by the matching
// WM_LBUTTONUP, so one click cannot both press a button and count as "clicked
// outside the image". Only WndProc touches it, so it stays out of Globals.h.
static bool g_buttonClaimedClick = false;

// The film-strip row: the band of client y values the strip occupies, measured
// from the same pixel-fixed anchor DrawThumbnailStrip uses. WM_LBUTTONDOWN claims
// the row to start a drag/click gesture, and WM_LBUTTONDBLCLK has to recognize it
// too: the second press of a double-click arrives as DBLCLK rather than another
// WM_LBUTTONDOWN, so a handler that does not know about the row lets that press
// fall through to the image branches.
static bool PointInStripRow(float y, HWND hWnd)
{
    if (!g_thumb.sizeCaptured)
        return false;

    RECT rc;
    GetClientRect(hWnd, &rc);
    const float rtH     = (float)(rc.bottom - rc.top);
    const float stripCY = rtH - g_thumb.stripInsetFromBottomPx;
    return y >= stripCY - g_thumb.height * 0.5f &&
           y <= stripCY + g_thumb.height * 0.5f;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{

    switch (message)
    {
    case WM_ERASEBKGND:
    {
        return 1;
    }
    break;

    case WM_COPYDATA:
    {
        PCOPYDATASTRUCT pcds = (PCOPYDATASTRUCT)lParam;
        if (pcds && pcds->dwData == 1 && pcds->lpData)
        {
            const wchar_t* incomingPath = reinterpret_cast<const wchar_t*>(pcds->lpData);
            if (incomingPath && incomingPath[0])
            {
                // Open the image in the existing instance
                BuildImageList(incomingPath);
                if (LoadImageD2D(g_app.mainWindow, incomingPath))
                {
                    // Use existing logic to enter fullscreen and display
                    EnterFullscreen(true, true);
                    // Bring ourselves to front (safe way)
                    if (IsIconic(hWnd))
                        ShowWindow(hWnd, SW_RESTORE);

                    SetForegroundWindow(hWnd);
                    SetActiveWindow(hWnd);
                }
                else
                {
                    std::wstring msg = g_image.lastLoadError.empty()
                        ? L"Failed to load image."
                        : g_image.lastLoadError;
                    MessageBox(hWnd, msg.c_str(), L"Error", MB_ICONERROR);
                }
            }
        }
        return 0;
    }
    break;


    case WM_ACTIVATE:
    {
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            // lParam is the HWND becoming active. If it's one of our own
            // owned windows (About box, metadata viewer, a common dialog,
            // etc.) this isn't a real focus loss - see IsOwnedByOurWindow.
            HWND becoming = reinterpret_cast<HWND>(lParam);
            bool ownWindowTookFocus = becoming && IsOwnedByOurWindow(becoming);

            if (g_view.isFullscreen && g_fullscreen.initDone &&
                g_fullscreen.suppressExitDepth == 0 && !ownWindowTookFocus)
            {
                // Post rather than call directly - ExitFullscreen destroys the
                // overlay and calls SetFocus while Windows is mid-focus-change,
                // which leaves the main window visually active but without real
                // keyboard focus. Deferring lets the OS finish first.
                PostMessage(g_app.mainWindow, WM_APP_EXITFULLSCREEN, 0, 0);
            }
        }
        else
        {
            g_fullscreen.navPendingRender = false;
        }
    }
    break;

    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;

        wchar_t filePath[MAX_PATH];
        DragQueryFile(hDrop, 0, filePath, MAX_PATH);

        DragFinish(hDrop);

        if (LoadImageD2D(hWnd, filePath))
        {
            BuildImageList(filePath);
            EnterFullscreen();
        }
        else
        {
            std::wstring msg = g_image.lastLoadError.empty()
                ? L"Failed to load image."
                : g_image.lastLoadError;
            MessageBox(hWnd, msg.c_str(), L"Error", MB_ICONERROR);
        }
    }
    break;

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        // Parse the menu selections:
        switch (wmId)
        {
        case IDM_OPEN:
            OpenImageFile(hWnd);
            break;
        case IDM_ABOUT:
            // DialogBox is owned by hWnd, so IsOwnedByOurWindow() in
            // WM_ACTIVATE recognizes it automatically - no manual
            // suppression needed.
            DialogBox(g_app.instance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        case ID_HELP_COMMANDS:
            DialogBox(g_app.instance, MAKEINTRESOURCE(IDD_COMMANDBOX), hWnd, About);
            break;

        case static_cast<int>(MenuCommandId::SHORTCUTS):
            DialogBox(g_app.instance, MAKEINTRESOURCE(IDD_COMMANDBOX), hWnd, About);
            break;

        case static_cast<int>(MenuCommandId::ABOUT):
            DialogBox(g_app.instance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;

        case static_cast<int>(MenuCommandId::HQ_FILTER):
        {
            // Pure registry write, no window involved - never needed
            // suppression in the first place.
            g_settings.useHQFilter = !g_settings.useHQFilter;
            DWORD val = g_settings.useHQFilter ? 1 : 0;
            HKEY hKey;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\PicassoPictures",
                                0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
            {
                RegSetValueExW(hKey, L"HQFilter", 0, REG_DWORD,
                               reinterpret_cast<const BYTE*>(&val), sizeof(val));
                RegCloseKey(hKey);
            }
            break;
        }

        case static_cast<int>(MenuCommandId::FIT_TO_SCREEN):
        {
            // Pure registry write, no window involved - never needed
            // suppression in the first place.
            g_settings.openFitToScreen = !g_settings.openFitToScreen;
            DWORD val = g_settings.openFitToScreen ? 1 : 0;
            HKEY hKey;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\PicassoPictures",
                                0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
            {
                RegSetValueExW(hKey, L"FitToScreen", 0, REG_DWORD,
                               reinterpret_cast<const BYTE*>(&val), sizeof(val));
                RegCloseKey(hKey);
            }
            break;
        }

        case static_cast<int>(MenuCommandId::ASSOCIATE):
            // AssociateFileTypes shows a TaskDialogIndirect owned by hWnd -
            // recognized automatically by IsOwnedByOurWindow().
            AssociateFileTypes(hWnd);
            break;

        case static_cast<int>(ContextMenuId::COPY):
        {
            if (!g_view.bitmap || !g_image.source || g_image.path.empty()) break;

            UINT srcW = 0, srcH = 0;
            g_image.source->GetSize(&srcW, &srcH);

            BITMAPINFOHEADER bi = {};
            bi.biSize        = sizeof(bi);
            bi.biWidth       = (LONG)srcW;
            bi.biHeight      = -(LONG)srcH;  // top-down
            bi.biPlanes      = 1;
            bi.biBitCount    = 32;
            bi.biCompression = BI_RGB;

            size_t pixelBytes = (size_t)srcW * srcH * 4;
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + pixelBytes);
            if (!hMem) break;

            BYTE* pBuf = (BYTE*)GlobalLock(hMem);
            if (!pBuf)
            {
                // The block was never locked, so it is freed rather than unlocked.
                GlobalFree(hMem);
                break;
            }
            memcpy(pBuf, &bi, sizeof(bi));
            BYTE* pDst = pBuf + sizeof(BITMAPINFOHEADER);

            ComPtr<IWICFormatConverter> conv;
            if (SUCCEEDED(g_devices.wicFactory->CreateFormatConverter(&conv)) &&
                SUCCEEDED(conv->Initialize(g_image.source.Get(),
                    GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone,
                    nullptr, 0.f, WICBitmapPaletteTypeCustom)))
            {
                conv->CopyPixels(nullptr, srcW * 4, (UINT)pixelBytes, pDst);
            }
            GlobalUnlock(hMem);

            // SetClipboardData takes ownership of the block when it succeeds, and the
            // clipboard then owns it from that moment. On any failure - OpenClipboard
            // refusing, or SetClipboardData rejecting the format - the block is still
            // ours, and without this it leaks whole (header plus every pixel), one
            // block per failed copy on a path that can be repeated at will.
            if (OpenClipboard(hWnd))
            {
                EmptyClipboard();
                if (!SetClipboardData(CF_DIB, hMem))
                    GlobalFree(hMem);
                CloseClipboard();
            }
            else
            {
                GlobalFree(hMem);
            }
            break;
        }

        case static_cast<int>(ContextMenuId::DELETE_FILE):
        {
            bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            DeleteCurrentImage(hWnd, shiftHeld);
            break;
        }

        case static_cast<int>(ContextMenuId::OPEN_FOLDER):
        {
            if (g_image.path.empty()) break;
            PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(g_image.path.c_str());
            if (pidl)
            {
                SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
                ILFree(pidl);
            }
            break;
        }

        case static_cast<int>(ContextMenuId::PROPERTIES):
            // ShowImageProperties's dialog is owned by hWnd - recognized
            // automatically by IsOwnedByOurWindow(), same as "View
            // metadata" below.
            ShowImageProperties(hWnd);
            break;

        case static_cast<int>(ContextMenuId::METADATA):
            // ShowImageMetadata's viewer window is owned by hWnd -
            // recognized automatically by IsOwnedByOurWindow().
            ShowImageMetadata(hWnd);
            break;

        case static_cast<int>(ContextMenuId::WALLPAPER):
        {
            // Both MessageBoxW calls below are owned by hWnd, so WM_ACTIVATE's
            // IsOwnedByOurWindow() recognizes them and no FullscreenExitSuppressor is
            // needed. Nothing in this block opens a suppression scope, so none of its
            // early `break`s can leave fullscreen-exit permanently disabled.
            if (g_image.path.empty()) break;
            auto ext = g_image.path.substr(g_image.path.rfind(L'.'));
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            static const std::vector<std::wstring> supported = {
                L".jpg", L".jpeg", L".png", L".bmp", L".gif"
            };
            if (std::find(supported.begin(), supported.end(), ext) == supported.end())
            {
                MessageBoxW(hWnd, L"This file format is not supported as a wallpaper.",
                            L"Set as wallpaper", MB_ICONINFORMATION);
                break;
            }
            SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0,
                (PVOID)g_image.path.c_str(),
                SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            MessageBoxW(hWnd,
                (L"\"" + g_image.fileName + L"\" set as wallpaper.").c_str(),
                L"Set as wallpaper", MB_ICONINFORMATION);
            break;
        }

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        Render(hWnd);
        EndPaint(hWnd, &ps);
    }
    break;

    case WM_SIZE:
    {
        if (!g_devices.swapChain || !g_devices.renderTarget)
            break;

        UINT width  = LOWORD(lParam);
        UINT height = HIWORD(lParam);

        if (width == 0 || height == 0)
            break;

        // Unbind current D2D target and D3D11 RTV before ResizeBuffers -
        // both wrap the old back buffer which is about to be released.
        g_devices.renderTarget->SetTarget(nullptr);
        g_devices.d2dTargetBitmap.Reset();
        g_devices.swapRTV.Reset();   // must release before ResizeBuffers

        HRESULT hr = g_devices.swapChain->ResizeBuffers(
            0,
            width,
            height,
            DXGI_FORMAT_UNKNOWN,
            0);

        if (FAILED(hr))
            break;

        // Recreate D2D target bitmap and D3D11 RTV from the new back buffer.
        ComPtr<IDXGISurface> surface;
        ComPtr<ID3D11Texture2D> backBuf;
        if (SUCCEEDED(g_devices.swapChain->GetBuffer(0, IID_PPV_ARGS(&surface))) &&
            SUCCEEDED(g_devices.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuf))))
        {
            D2D1_BITMAP_PROPERTIES1 props = SwapChainBitmapProps();

            if (SUCCEEDED(
                g_devices.renderTarget->CreateBitmapFromDxgiSurface(
                    surface.Get(),
                    &props,
                    &g_devices.d2dTargetBitmap)))
            {
                g_devices.renderTarget->SetTarget(g_devices.d2dTargetBitmap.Get());
                for (auto& [id, btn] : g_buttons)
                    btn.UpdateLayout(g_devices.renderTarget.Get());

                for (auto& [key, textbox] : g_textBoxes)
                    textbox.UpdateLayout(g_devices.renderTarget.Get());
            }

            // Recreate the D3D11 RTV that RenderImageD3D11 binds as render target
            if (g_devices.d3dDevice)
                g_devices.d3dDevice->CreateRenderTargetView(backBuf.Get(), nullptr, &g_devices.swapRTV);
        }

        if (g_view.bitmap)
        {
            if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED)
            {
                // Recenter image for new window size
                SetZoomCentered(g_view.zoom, hWnd, true, false);
            }
        }
    }
    break;

    case WM_RBUTTONUP:
    {
        if (g_image.path.empty()) break;

        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING,    static_cast<UINT_PTR>(ContextMenuId::COPY),        L"Copy image");
        AppendMenuW(hMenu, MF_STRING,    static_cast<UINT_PTR>(ContextMenuId::DELETE_FILE),      L"Delete");
        AppendMenuW(hMenu, MF_SEPARATOR, 0,                    nullptr);
        AppendMenuW(hMenu, MF_STRING,    static_cast<UINT_PTR>(ContextMenuId::OPEN_FOLDER), L"Open containing folder");
        AppendMenuW(hMenu, MF_STRING,    static_cast<UINT_PTR>(ContextMenuId::PROPERTIES),  L"Properties");
        AppendMenuW(hMenu, MF_STRING,    static_cast<UINT_PTR>(ContextMenuId::METADATA),    L"View metadata");
        AppendMenuW(hMenu, MF_SEPARATOR, 0,                    nullptr);
        AppendMenuW(hMenu, MF_STRING,    static_cast<UINT_PTR>(ContextMenuId::WALLPAPER),   L"Set as wallpaper");

        POINT pt;
        GetCursorPos(&pt);
        // Same reasoning as the hamburger menu: TrackPopupMenu's internal
        // menu window isn't one of our owned windows, and SetForegroundWindow
        // is what fires WA_INACTIVE - so this needs an explicit suppression
        // scope around both calls.
        {
            FullscreenExitSuppressor guard;
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
        }
        DestroyMenu(hMenu);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (!g_view.bitmap)
            break;
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT pt;
        GetCursorPos(&pt);
        ZoomIntoImage(hWnd, delta, &pt);
        MakeZoomVisible(hWnd);
    }
    break;

    case WM_LBUTTONDOWN:
    {
        float x = static_cast<float>(GET_X_LPARAM(lParam));
        float y = static_cast<float>(GET_Y_LPARAM(lParam));

        // Cleared here so the flag can only describe the press this release follows.
        g_buttonClaimedClick = false;
        for (auto& [id, btn] : g_buttons)
        {
            if (btn.OnMouseDown(x, y))
            {
                // A button acts on the press (AnimatedButton::OnMouseDown runs the
                // callback), so the matching WM_LBUTTONUP must not read this same
                // click as "clicked outside the image" - that would exit fullscreen
                // right after the button did its job.
                g_buttonClaimedClick = true;
                return 0;  // STOP - button handled it
            }
        }

        for (auto& [id, box] : g_textBoxes)
        {
            if (box.OnMouseDown(x, y))
            {
                // Same claim as a button. Without it, WM_LBUTTONUP below reads this
                // click as "clicked outside the image" and exits fullscreen - which
                // is exactly what happens when the zoom readout (screen centre) does
                // not happen to be over the image, e.g. after a cursor-anchored wheel
                // zoom has panned the image away from the centre.
                g_buttonClaimedClick = true;
                return 0;  // STOP - text box handled it
            }
        }
        // Thumbnail strip interaction.
        // The entire strip row is claimed on mouse-down so the user can drag to
        // pan without accidentally starting an image-pan drag.  Whether the
        // gesture ends as a click (open image) or a drag (pan with momentum) is
        // decided in WM_LBUTTONUP.
        if (g_thumb.sizeCaptured)
        {
            RECT rc2; GetClientRect(hWnd, &rc2);
            const float rtW      = (float)(rc2.right  - rc2.left);

            if (PointInStripRow(y, hWnd))
            {
                // Identify which slot (if any) the cursor is over - we'll need
                // this if the gesture turns out to be a click, not a drag.
                // The hit half-width is expanded by half the gap on each side so
                // clicks in the gaps between thumbnails register on the nearest
                // one, leaving no dead zone and no overlap.
                const float baseX    = rtW * 0.5f + g_thumb.scrollOffset;
                const float stride   = g_thumb.width + g_thumb.gap;
                const float hitHalfW = g_thumb.width * 0.5f + g_thumb.gap * 0.5f;
                const int   n        = (int)g_thumb.entries.size();
                g_thumb.dragHitIndex = -1;
                for (int i = 0; i < n; ++i)
                {
                    const float cx = baseX + i * stride;
                    if (x >= cx - hitHalfW && x <= cx + hitHalfW)
                    {
                        g_thumb.dragHitIndex = i;
                        break;
                    }
                }

                g_thumb.dragging   = true;
                g_thumb.freeScroll = true;   // suspend auto-centering immediately
                g_thumb.dragStartX = x;
                g_thumb.dragLastX  = x;
                g_thumb.velocity   = 0.f;
                SetCapture(hWnd);
                return 0;
            }
        }

        g_view.isDragging = true;
        SetCapture(hWnd);
        g_view.lastMouse.x = static_cast<LONG>(x);
        g_view.lastMouse.y = static_cast<LONG>(y);
        g_view.mouseFromDown = g_view.lastMouse;
    }
    break;
    
    case WM_LBUTTONUP:
    {
        float x = (float)GET_X_LPARAM(lParam);
        float y = (float)GET_Y_LPARAM(lParam);

        // ---- Resolve thumbnail strip drag ----------------------------------------
        if (g_thumb.dragging)
        {
            g_thumb.dragging = false;
            g_view.isDragging    = false;  // ensure image-drag state is never left stale
            ReleaseCapture();

            const float totalMove = fabsf(x - g_thumb.dragStartX);
            if (totalMove < 5.f)
            {
                // Gesture was a click: open the thumbnail that was under the cursor.
                // Clicking the current image or a gap just snaps the strip back.
                if (g_thumb.dragHitIndex >= 0 &&
                    g_thumb.dragHitIndex < (int)g_image.files.size() &&
                    g_thumb.dragHitIndex != g_image.index)
                {
                    g_image.index = g_thumb.dragHitIndex;
                    LoadImageD2D(hWnd, g_image.files[g_thumb.dragHitIndex].c_str());
                    InitializeImageLayout(hWnd, true);  // reset zoom/pan to fit, matching OpenNextImage/OpenPrevImage
                    RestartSlideshowTimer();
                }
                g_thumb.freeScroll = false;  // snap strip back to current image
            }
            else
            {
                // Gesture was a drag: apply momentum.
                g_thumb.targetOffset = g_thumb.scrollOffset + g_thumb.velocity * 25.f;
                // Clamp the momentum target so a fast fling cannot overshoot
                // the first or last thumbnail, and spring back any rubber-band overshoot.
                if (g_thumb.sizeCaptured && !g_image.files.empty())
                {
                    const float step   = g_thumb.width + g_thumb.gap;
                    const float minOff = -(static_cast<float>(g_image.files.size() - 1) * step);
                    const float maxOff = 0.f;
                    if (g_thumb.targetOffset > maxOff) g_thumb.targetOffset = maxOff;
                    if (g_thumb.targetOffset < minOff) g_thumb.targetOffset = minOff;
                }
                // g_thumb.freeScroll stays true so UpdateEngine does not overwrite
                // the momentum target until the user navigates to another image.
            }
            return 0;
        }
        // --------------------------------------------------------------------------

        // A release must ALWAYS end the button press, whatever the image state is.
        // Buttons act on the press, so if the release is skipped the button keeps
        // m_pressed / m_holdActive set and its auto-repeat runs free, gated only by
        // m_mouseOverSelf: hovering off pauses it, hovering back resumes it with
        // nobody holding the mouse. Navigating onto a file that fails to load clears
        // g_view.bitmap (LoadImageD2D resets it before decoding), which used to make
        // the guard below swallow the release entirely.
        for (auto& [id, btn] : g_buttons)
        {
            btn.OnMouseUp(x, y);
        }

        if (!g_view.bitmap) break;

        g_view.isDragging = false;
        ReleaseCapture();
        
        bool overImage = IsPointOverImage(static_cast<float>(x), static_cast<float>(y));

        bool wasReallyDragging = (abs(x - g_view.mouseFromDown.x) > 5) || (abs(y - g_view.mouseFromDown.y) > 5);

        // A press that a button already acted on is not a click on the background.
        const bool buttonClaimed = g_buttonClaimedClick;
        g_buttonClaimedClick = false;

        if (g_view.isFullscreen && !overImage && !wasReallyDragging && !buttonClaimed)
        {
            ExitFullscreen();
        }

        // Handle incomplete rotations
        SetTimer(hWnd, static_cast<UINT_PTR>(TimerId::KILL_ROTATION), 120, nullptr);
    }
    break;

    case WM_MOUSEMOVE:
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        RECT rc;
        GetClientRect(hWnd, &rc);
        float windowWidth   = (float)(rc.right - rc.left);
        float windowHeight  = (float)(rc.bottom - rc.top);
        for (auto& [id, btn] : g_buttons)
            btn.UpdateProximity(static_cast<float>(pt.x), static_cast<float>(pt.y), windowWidth, windowHeight);

        for (auto& [id, txt] : g_textBoxes)
            txt.UpdateProximity(static_cast<float>(pt.x), static_cast<float>(pt.y), windowWidth, windowHeight);

        // Vignette activation zones: top 20 % for top bar, bottom 20 % for bottom bar.
        // Active while an image is loaded, and also while the most recent load failed:
        // the film strip is the only way back out of the error screen, so it has to
        // stay reachable when there is no bitmap. The buttons are proximity-driven and
        // are not gated on an image, so they are unaffected either way.
        if (g_view.bitmap || g_image.showLoadError)
        {
            // Use pixel-fixed activation thresholds (captured at fullscreen init);
            // fall back to normalized 20% if not yet captured.
            const float topThresh    = g_vignette.heightsCaptured
                                           ? g_vignette.topActivationPx
                                           : windowHeight * 0.20f;
            const float bottomThresh = g_vignette.heightsCaptured
                                           ? (windowHeight - g_vignette.bottomActivationPx)
                                           : windowHeight * 0.80f;
            g_vignette.topTarget    = ((float)pt.y <= topThresh)    ? 1.0f : 0.0f;
            // Keep the bottom vignette (and the thumbnail strip it drives) visible
            // for as long as the strip is still moving under momentum so it never
            // disappears mid-coast.
            const bool stripCoasting = g_thumb.freeScroll &&
                                       !g_thumb.dragging &&
                                       g_thumb.sizeCaptured &&
                                       fabsf(g_thumb.targetOffset - g_thumb.scrollOffset) > 1.0f;
            g_vignette.bottomTarget = (((float)pt.y >= bottomThresh) || stripCoasting) ? 1.0f : 0.0f;
        }
        else
        {
            g_vignette.topTarget    = 0.0f;
            g_vignette.bottomTarget = 0.0f;
        }


        // ---- Strip drag: pan the filmstrip directly, tracking velocity ----
        if (g_thumb.dragging && (wParam & MK_LBUTTON))
        {
            const float rawDx = (float)pt.x - g_thumb.dragLastX;

            // Past an edge the strip still tracks the cursor exactly: the
            // factor below is 1.0, so there is no resistance. This branch is
            // where a resistance factor would go.
            float dx = rawDx;
            if (g_thumb.sizeCaptured && !g_image.files.empty())
            {
                const float step   = g_thumb.width + g_thumb.gap;
                const float minOff = -(static_cast<float>(g_image.files.size() - 1) * step);
                const float maxOff = 0.f;
                const bool pastMax = g_thumb.scrollOffset > maxOff && rawDx > 0.f;
                const bool pastMin = g_thumb.scrollOffset < minOff && rawDx < 0.f;
                if (pastMax || pastMin)
                    dx = rawDx * 1.0f;
            }

            g_thumb.scrollOffset += dx;
            g_thumb.targetOffset  = g_thumb.scrollOffset;
            g_thumb.velocity      = g_thumb.velocity * 0.65f + dx * 0.35f;
            g_thumb.dragLastX     = (float)pt.x;

            // Hard cap on the stretch: beyond this far past the boundary the
            // offset stops moving. The multiplier is 100 client widths, far
            // more than a drag can reach, so the cap is a safety net rather
            // than a visible limit.
            if (g_thumb.sizeCaptured && !g_image.files.empty())
            {
                const float step        = g_thumb.width + g_thumb.gap;
                const float minOff      = -(static_cast<float>(g_image.files.size() - 1) * step);
                const float maxOff      = 0.f;
                RECT rcRB; GetClientRect(hWnd, &rcRB);
                const float maxOvershoot = 100.0f*(float)(rcRB.right - rcRB.left);// * 0.5f;

                if (g_thumb.scrollOffset > maxOff + maxOvershoot)
                {
                    g_thumb.scrollOffset = maxOff + maxOvershoot;
                    g_thumb.targetOffset = g_thumb.scrollOffset;
                }
                else if (g_thumb.scrollOffset < minOff - maxOvershoot)
                {
                    g_thumb.scrollOffset = minOff - maxOvershoot;
                    g_thumb.targetOffset = g_thumb.scrollOffset;
                }
            }
        }

        if (g_view.isDragging && (wParam & MK_LBUTTON))
        {
            float dx = (float)(pt.x - g_view.lastMouse.x);
            float dy = (float)(pt.y - g_view.lastMouse.y);

            g_view.offsetX += dx;
            g_view.offsetY += dy;

            // Keep animation targets aligned
            g_view.targetOffsetX = g_view.offsetX;
            g_view.targetOffsetY = g_view.offsetY;

            g_view.lastMouse = pt;

            HWND renderWindow = (g_view.isFullscreen && g_app.overlayWindow)
                ? g_app.overlayWindow
                : hWnd;
        }
    }
    break;

    case WM_LBUTTONDBLCLK:
    {
        float x = static_cast<float>(GET_X_LPARAM(lParam));
        float y = static_cast<float>(GET_Y_LPARAM(lParam));

        for (auto& [id, btn] : g_buttons)
        {
            if (btn.OnMouseDown(x, y))
            {
                // Same claim as WM_LBUTTONDOWN: the release that follows this DBLCLK
                // must not read the gesture as "clicked outside the image".
                g_buttonClaimedClick = true;
                return 0;  // STOP - button handled it
            }
        }

        for (auto& [id, box] : g_textBoxes)
        {
            if (box.OnMouseDown(x, y))
            {
                g_buttonClaimedClick = true;
                return 0;  // STOP - text box handled it
            }
        }

        // The strip claims its whole row on the first press, so it claims this press
        // as well. Nothing else happens here: the first press already started the
        // gesture and the release after this message resolves it as a click or a
        // drag. Swallowing the press keeps a double-click on a thumbnail - or on any
        // part of the strip row - from also reaching the image branches below, which
        // toggled the image's fit/1:1 zoom whenever the image showed behind the slot.
        if (PointInStripRow(y, hWnd))
        {
            g_buttonClaimedClick = true;
            return 0;  // STOP - strip row handled it
        }

        // No image loaded (the most recent load failed): a double-click on empty space
        // opens the file dialog so the user can pick another file. This has to come
        // AFTER the widget loops above - while the load-error screen is up, a
        // double-click that landed on a button opened the dialog instead of acting on
        // the button, because the second click of a rapid pair arrives as DBLCLK and
        // this branch used to run before the buttons saw it.
        if (!g_view.bitmap)
        {
            OpenImageFile(hWnd);
            break;
        }

        bool overImage = IsPointOverImage(x, y);

        if (!g_view.isFullscreen && overImage)
        {
            EnterFullscreen(true, true); // preserve zoom & position
        }
        else if (g_view.isFullscreen && g_app.overlayWindow)
        {
            // The overlay window is required: these two functions take the window
            // whose client rect defines the fit, and during fullscreen stage 1
            // (the 150 ms before the overlay exists) isFullscreen is already true
            // while the overlay is still null.
            if (overImage)
            {
                if (g_view.targetZoom == 1.0f)
                    FitToWindowRelative(g_app.overlayWindow, 0.96f, false);
                else
                    SetZoomCentered(1.0f, g_app.overlayWindow, false, false);
            }
        }
    }
    break;
    
    case WM_TIMER:
    {
        if (wParam == static_cast<UINT_PTR>(TimerId::ENTER_FULL_SCREEN))
        {
            KillTimer(hWnd, static_cast<UINT_PTR>(TimerId::ENTER_FULL_SCREEN));
            // Run stage 2 only if the delayed entry is still wanted. CompleteExitFullscreen
            // kills this timer, but a WM_TIMER already sitting in the queue survives
            // KillTimer, so without this an exit that happened inside the 150 ms gap
            // would still raise an overlay for a view that is no longer fullscreen.
            if (g_view.isFullscreen && !g_fullscreen.pendingExit && !g_app.isExiting)
                EnterFullscreen(g_fullscreen.pendingPreserveView, false);
        }
        if (wParam == static_cast<UINT_PTR>(TimerId::KILL_ROTATION))
        {
            // Handle incomplete rotations:
            KillTimer(hWnd, static_cast<UINT_PTR>(TimerId::KILL_ROTATION));
            if (g_view.rotationAngle < g_view.targetRotationAngle)
            {
                g_view.targetRotationAngle = std::ceil(g_view.rotationAngle / 90.0f) * 90.0f;
            }
            else
            {
                g_view.targetRotationAngle = std::floor(g_view.rotationAngle / 90.0f) * 90.0f;
            }
        }
        if (wParam == static_cast<UINT_PTR>(TimerId::ZOOM_DISPLAY))
            {
                KillTimer(hWnd, static_cast<UINT_PTR>(TimerId::ZOOM_DISPLAY));
                g_textBoxes[TextBoxId::ZOOM_INPUT].SetForcedVisibility(false);
        }
        if (wParam == static_cast<UINT_PTR>(TimerId::SLIDESHOW))
        {
            if (g_slideshow.active && !g_image.files.empty())
            {
                // Advance only once the current transition has settled (> 80 %)
                // so rapid fires don't pile up if the system is under load.
                if (g_slideshow.transitionAlpha > 0.8f)
                {
                    OpenNextImage(g_app.overlayWindow ? g_app.overlayWindow : g_app.mainWindow);
                    InitializeImageLayout(g_app.overlayWindow ? g_app.overlayWindow : g_app.mainWindow, true);
                }
            }
        }
    }
    break;

    case WM_CHAR:
    {
        for (auto& [id, box] : g_textBoxes)
            box.OnChar((wchar_t)wParam);
    }
    break;

    case WM_KEYDOWN:
    {   
        RestartSlideshowTimer();

        // If any textbox is focused, let it handle the key
        for (auto& [id, box] : g_textBoxes)
        {
            if (box.IsFocused())
            {
                box.OnKeyDown(wParam);
                return 0;  // STOP. Do NOT process global shortcuts.
            }
        }
        
        switch (wParam)
        {
            case VK_DELETE:
            {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                DeleteCurrentImage(hWnd, shiftHeld);
                return 0;
            } 
            case VK_ESCAPE:
            {
                if (!g_view.bitmap)
                {
                    // Destroy the window rather than posting a quit message:
                    // WM_DESTROY is what runs the teardown - the three background
                    // threads, the device resources, the containers holding COM
                    // objects. Quitting the loop from here skips all of it and leaves
                    // those globals to release after COM has gone away.
                    DestroyWindow(g_app.mainWindow);
                    return 0;
                }
                if (g_slideshow.active)
                {
                    ExitSlideshowMode();
                    return 0;
                }
                if (g_view.isFullscreen)
                {
                    InitFullScreenExit();
                    return 0;
                }
                else
                {
                    // Windowed Esc quits, but through DestroyWindow so that WM_DESTROY
                    // runs the same teardown every other exit path gets.
                    DestroyWindow(g_app.mainWindow);
                }
            }
            break;
            case 0x46: // 'F' key - ignore auto-repeat to prevent toggle glitches
            {
                if (lParam & (1 << 30)) break;  // auto-repeat, skip
                if (!g_view.bitmap)
                {
                    break;
                }
                else if (g_view.isFullscreen)
                {
                    ExitFullscreen();
                }
                else
                {
                    EnterFullscreen(true, true);
                }
                return 0;
            }

            case 0x4F: // 'O' key
                OpenImageFile(hWnd);
            break;

            case 0x57: // 'W' key
            case VK_UP:
            {
                ZoomIntoImage(hWnd, 250, nullptr);
                MakeZoomVisible(hWnd);
                return 0;
            }

            case 0x53: // 'S' key
            case VK_DOWN:
            {
                ZoomIntoImage(hWnd, -250, nullptr);
                MakeZoomVisible(hWnd);
                return 0;
            }

            case 0x41: // 'A' key
            case VK_LEFT:
            {
                if (g_fullscreen.navPendingRender) break;
                g_fullscreen.navPendingRender = true;
                OpenPrevImage(hWnd);
                return 0;
            }

            case 0x44: // 'D' key
            case VK_SPACE:
            case VK_RIGHT:
            {
                if (g_fullscreen.navPendingRender) break;
                g_fullscreen.navPendingRender = true;
                OpenNextImage(hWnd);
                return 0;
            }

            case 0x51: // 'Q' key
            {
                if (g_view.bitmap)
                {
                    g_view.targetRotationAngle -= 90.0f;
                }
                return 0;
            }

            case 0x45: // 'E' key
            {
                if (g_view.bitmap)
                {
                    g_view.targetRotationAngle += 90.0f;
                }
                return 0;
            }

            case VK_F5: // F5 - toggle slideshow mode
            {
                if (!g_view.bitmap) break;
                if (g_slideshow.active)
                    ExitSlideshowMode();
                else
                    EnterSlideshowMode();
                return 0;
            }
        }
    }
    break;

    case WM_KEYUP:
    {
        // Handle incomplete rotations
        SetTimer(hWnd, static_cast<UINT_PTR>(TimerId::KILL_ROTATION), 120, nullptr);
    }
    break;
    
    case WM_APP_EXITFULLSCREEN:
    {
        // Re-check the suppress depth here, not just at post-time. This
        // message is posted (queued), so by the time it's actually
        // dispatched - which can happen well after the WM_ACTIVATE
        // (WA_INACTIVE) that posted it, e.g. once a modal popup menu's
        // internal message pump gets around to it - an intervening guard
        // (hamburger menu, etc.) may have opened a new suppression scope.
        // Without this check the stale post fires anyway and exits
        // fullscreen out from under whatever UI is currently suppressing it.
        if (g_view.isFullscreen && g_fullscreen.suppressExitDepth == 0)
            ExitFullscreen();
        return 0;
    }
    break;
 
    case WM_APP_DIRCHANGE:
    {
        // A supported image file was created, deleted, or renamed in the
        // current directory. Rebuild the file list, preserving the current
        // image if it still exists, or clamping to the nearest valid index.
        if (!g_image.path.empty())
        {
            const std::wstring currentPath = g_image.path;
            BuildImageList(currentPath.c_str());

            // Nothing left to look at: the folder really does have no supported
            // images any more - all of them deleted or moved out in Explorer. (A
            // folder that cannot be read at all never reaches this point:
            // BuildImageList keeps the previous list and returns early, so a
            // sleeping drive or a dropped network share cannot end the program.)
            // Same treatment as deleting the last picture in the folder: quit
            // rather than sit on a stale bitmap with an empty film strip.
            if (g_image.files.empty())
            {
                InitFullScreenExit();
                return 0;
            }

            // BuildImageList sets g_image.index to -1 if the current
            // file no longer exists; clamp to a valid image in that case.
            if (g_image.index < 0 && !g_image.files.empty())
            {
                g_image.index = 0;
                LoadImageD2D(hWnd, g_image.files[0].c_str());
                InitializeImageLayout(hWnd, true);
            }
        }
        return 0;
    }
    break;

    case WM_DESTROY:
    {
        if (hWnd == g_app.mainWindow)
        {
            // Ensure overlay is cleaned up too.
            if (g_app.overlayWindow)
            {
                DestroyWindow(g_app.overlayWindow);
                g_app.overlayWindow = nullptr;
            }

            StopDirectoryWatcher();
            StopThumbnailLoader();
            StopAnimDecodeThread();
            DiscardDeviceResources();

            // Empty the containers that still hold COM objects the discard above did
            // not touch, while the factories that created them are still alive -
            // CoUninitialize happens later, in wWinMain, once the message loop has
            // exited. These are globals: anything left in them is
            // destroyed only after main returns, releasing into a COM runtime that has
            // already gone away. That is the crash on exit - 0xc0000005 inside ~vector
            // -> ~ComPtr::InternalRelease.
            // g_thumb.entries keeps the CPU-side IWICBitmap thumbnails and g_thumb
            // .clipGeos the factory-created slot geometries; DiscardDeviceResources
            // touches neither, and both only exist after fullscreen stage 2 - which is
            // why closing the windowed UI never crashed and closing fullscreen always
            // did. g_textBoxes / g_buttons are deliberately not in this list: the
            // widgets' only COM members are DWrite objects (their factory, text format
            // and tooltip layout), never a WIC bitmap or an object made by the device
            // context, so nothing device-dependent is left behind there.
            g_thumb.entries.clear();
            g_thumb.clipGeos.clear();
            while (!g_thumb.readyQueue.empty()) g_thumb.readyQueue.pop();
            g_anim.frames.clear();
            g_anim.gpuFrames.clear();

            g_background.wicDefaultBitmap.Reset();
            g_background.wicBitmap.Reset();
            g_image.source.Reset();
            g_devices.wicFactory.Reset();
            g_devices.d2dFactory.Reset();
            g_devices.textFormat.Reset();
            g_devices.dwriteFactory.Reset();
            // No CoUninitialize here on purpose: CoInitialize runs once in wWinMain and
            // its matching CoUninitialize is on the shutdown label after the message
            // loop. This handler runs while that loop is still alive, so uninitializing
            // COM here would unbalance the pair - the loop's own call would be the
            // second one.
            PostQuitMessage(0);
        }
    }
    break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
