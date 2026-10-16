// Fullscreen.cpp : fullscreen / overlay / slideshow lifecycle.
//
// Linkage rules for this file:
//   * CompleteExitFullscreen has external linkage because UpdateEngine, which
//     calls it once the exit fade finishes, lives in the main file.
//   * EnterFullscreen's default arguments belong to Fullscreen.h alone; the
//     definition here must not repeat them (C2522).

#include "Fullscreen.h"
#include "ViewMath.h"
#include "Rendering.h"
#include "UI.h"        // InitializeButtons / InitializeImageInfoLabel / UpdateTargetZoom / UpdateSlideshowButton

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// ============================================================================
// Cross-TU declaration for a function that stays in the main file: WndProc.
// The overlay window class reuses the main window's proc, so this TU needs the
// declaration and no module header owns it.
// ============================================================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

ATOM RegisterOverlayClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = {};

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszClassName = L"OverlayWindowClass";

    return RegisterClassExW(&wcex);
}

// A plain black popup window used to hide D3D device recreation.
// No D2D involved - just a black HBRUSH painted by DefWindowProc.
void RegisterBlackCoverClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex   = {};
    wcex.cbSize        = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc   = DefWindowProcW;
    wcex.hInstance     = hInstance;
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszClassName = L"BlackCoverClass";
    RegisterClassExW(&wcex);
}

void CreateBlackCoverWindow()
{
    if (g_app.blackCoverWindow) return;

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromWindow(g_app.mainWindow, MONITOR_DEFAULTTONEAREST), &mi);
    RECT rc = mi.rcMonitor;

    g_app.blackCoverWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"BlackCoverClass", L"", WS_POPUP,
        rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, g_app.instance, nullptr);

    if (g_app.blackCoverWindow)
    {
        ShowWindow(g_app.blackCoverWindow, SW_SHOWNOACTIVATE);
        UpdateWindow(g_app.blackCoverWindow);
        // Block until DWM has composited this window onto the screen.
        // Without this, the cover may not be visible before we destroy the render target.
        DwmFlush();
    }
}

void DestroyBlackCoverWindow()
{
    if (g_app.blackCoverWindow)
    {
        DestroyWindow(g_app.blackCoverWindow);
        g_app.blackCoverWindow = nullptr;
    }
}
HWND CreateOverlayWindow(HWND parent)
{
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(
        MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST),
        &mi);

    // Slideshow covers the taskbar; normal fullscreen only covers the work area.
    RECT rc = g_slideshow.active ? mi.rcMonitor : mi.rcWork;

    DWORD exStyle = WS_EX_TOOLWINDOW;
    if (g_slideshow.active)
        exStyle |= WS_EX_TOPMOST;

    HWND overlay = CreateWindowEx(
        exStyle,
        L"OverlayWindowClass",
        L"",
        WS_POPUP,
        rc.left,
        rc.top,
        rc.right - rc.left,
        rc.bottom - rc.top,
        parent,
        nullptr,
        g_app.instance,
        nullptr);

    // Important: don't force an immediate paint here. We'll show it once device resources are ready.
    return overlay;
}
void CaptureDesktop(HWND hWnd)
{
    g_background.wicBitmap.Reset();

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(
        MonitorFromWindow(hWnd ? hWnd : g_app.mainWindow, MONITOR_DEFAULTTONEAREST),
        &mi);

    RECT rc = mi.rcWork;

    int width  = rc.right  - rc.left;
    int height = rc.bottom - rc.top;

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    HBITMAP hBitmap = CreateCompatibleBitmap(screenDC, width, height);
    HGDIOBJ old = SelectObject(memDC, hBitmap);

    BitBlt(memDC, 0, 0, width, height,
           screenDC, rc.left, rc.top, SRCCOPY);

    SelectObject(memDC, old);

    g_devices.wicFactory->CreateBitmapFromHBITMAP(
        hBitmap,
        nullptr,
        WICBitmapIgnoreAlpha,
        g_background.wicBitmap.GetAddressOf());

    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}
void EnterSlideshowMode()
{
    if (g_slideshow.active || g_slideshow.preFade) return;
    // Only start the fade-to-black. UpdateEngine handles everything else once
    // the screen is fully black, so no window transitions happen visibly.
    // This is the only point that still sees the pre-slideshow window state, so
    // record here whether stopping the slideshow should go back to fullscreen.
    g_slideshow.fromFullscreen = g_view.isFullscreen;
    g_slideshow.preFade      = true;
    g_slideshow.preFadeAlpha = 0.0f;
}

void ExitSlideshowMode()
{
    DestroyBlackCoverWindow();

    if (g_slideshow.preFade)
    {
        // Cancelled mid-fade before the slideshow actually started.
        g_slideshow.preFade      = false;
        g_slideshow.preFadeAlpha = 0.0f;
        return;
    }

    if (!g_slideshow.active) return;

    KillTimer(g_app.mainWindow, static_cast<UINT_PTR>(TimerId::SLIDESHOW));

    // Clear transition state first so Render doesn't try to use
    // bitmaps that are about to be discarded.
    g_slideshow.prevBitmap.Reset();
    g_slideshow.prevBgBitmap.Reset();
    g_slideshow.transitionAlpha = 1.0f;

    g_slideshow.active = false;
    UpdateSlideshowButton();

    // A slideshow that started in fullscreen goes back to fullscreen instead of
    // to the windowed view. That is only a rect change on the live overlay, so
    // fade through black and let UpdateEngine apply it at the darkest moment.
    if (g_slideshow.fromFullscreen && g_app.overlayWindow)
    {
        g_slideshow.fromFullscreen        = false;
        g_slideshow.stopFade              = true;
        g_slideshow.stopFadePendingResize = true;
        g_slideshow.stopFadeAlpha         = 0.0f;
        return;
    }

    g_fullscreen.wasInSlideshowOnExit = true;   // tell the exit fade to cross-fade blurred->desktop bg
    ExitFullscreen();
}

void RestartSlideshowTimer()
{
    if (!g_slideshow.active) return;

    KillTimer(g_app.mainWindow, static_cast<UINT_PTR>(TimerId::SLIDESHOW));
    SetTimer(g_app.mainWindow, static_cast<UINT_PTR>(TimerId::SLIDESHOW), SLIDESHOW_INTERVAL_MS, nullptr);
}

// A slideshow covers the whole monitor; plain fullscreen stops at the work area.
// Called once per stop fade, while the stop fade has the screen fully black, so
// the swap-chain rebuild this resize triggers is never seen. WM_SIZE does the
// buffer work; Render has already switched to the desktop-capture background
// because g_slideshow.active is false by the time this runs.
void ResizeOverlayToWorkArea()
{
    if (!g_app.overlayWindow)
        return;

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromWindow(g_app.overlayWindow, MONITOR_DEFAULTTONEAREST), &mi);

    SetWindowPos(g_app.overlayWindow,
                 HWND_NOTOPMOST,
                 mi.rcWork.left,
                 mi.rcWork.top,
                 mi.rcWork.right  - mi.rcWork.left,
                 mi.rcWork.bottom - mi.rcWork.top,
                 SWP_NOACTIVATE);

    // The slide was fitted to the whole monitor; re-fit for the smaller work
    // area instead of leaving the bottom of the image under the taskbar.
    InitializeImageLayout(g_app.overlayWindow, true);
}

void EnterFullscreen(bool preserveView, bool needsDelay)
{
    // Prevent duplicate first-stage calls
    if (g_view.isFullscreen && needsDelay) return;

    // ----------------------------------------
    // STAGE 1 - make main window fully transparent and wait for DWM
    // ----------------------------------------
    if (needsDelay)
    {
        g_view.isFullscreen = true;
        SetWindowLong  (g_app.mainWindow, 
                        GWL_EXSTYLE,
                        GetWindowLong(g_app.mainWindow, GWL_EXSTYLE) | WS_EX_LAYERED
                        );

        SetLayeredWindowAttributes(g_app.mainWindow, 0, 0, LWA_ALPHA);
        UpdateWindow(g_app.mainWindow);

        SetTimer(g_app.mainWindow, static_cast<UINT_PTR>(TimerId::ENTER_FULL_SCREEN), 150, nullptr);
        g_fullscreen.pendingPreserveView = preserveView;
        return;
    }

    // If we are preserving, compute whether the current view is "centered"
    // using pan (imageCenter - windowCenter). pan==0 means centered for any window size.
    float panX = 0.f, panY = 0.f;
    float targetPanX = 0.f, targetPanY = 0.f;
    bool havePan = false;
    bool viewIsCentered = false;

    if (preserveView && g_view.bitmap)
    {
        D2D1_SIZE_F imgSizeMain = g_view.bitmap->GetSize();

        havePan =
            PanFromOffsets(g_app.mainWindow, g_view.zoom, g_view.offsetX, g_view.offsetY,
                           imgSizeMain.width, imgSizeMain.height, panX, panY) &&
            PanFromOffsets(g_app.mainWindow, g_view.targetZoom, g_view.targetOffsetX, g_view.targetOffsetY,
                           imgSizeMain.width, imgSizeMain.height, targetPanX, targetPanY);

        if (havePan)
        {
            constexpr float EPS = 1.0f; // pixels
            viewIsCentered =
                (fabsf(panX) < EPS && fabsf(panY) < EPS &&
                 fabsf(targetPanX) < EPS && fabsf(targetPanY) < EPS);
        }
    }

    // ----------------------------------------
    // STAGE 2 - actual fullscreen overlay creation
    // ----------------------------------------
    // Capture the desktop behind the window - but only when transitioning from
    // windowed to fullscreen. When entering slideshow mode the screen is already
    // covered by the black cover window, so capturing now would overwrite
    // g_background.wicBitmap with a black frame. The original desktop capture from the
    // initial fullscreen entry is still alive in g_background.wicBitmap (DiscardDeviceResources
    // does not touch it) and will be re-uploaded below by CreateBackgroundBitmap().
    if (!g_slideshow.active)
        CaptureDesktop(g_app.mainWindow);

    // Switching render targets -> discard device resources
    DiscardDeviceResources();

    g_app.overlayWindow = CreateOverlayWindow(g_app.mainWindow);

    // Create device resources for overlay now
    CreateRenderTarget(g_app.overlayWindow);

    if (g_devices.renderTarget)
    {
        D2D1_SIZE_F size = g_devices.renderTarget->GetSize();
        g_view.uiPixelScale = min(size.width, size.height);

        // Capture vignette bar heights in pixels from this overlay's render target.
        // This runs only when a fullscreen overlay is created, never on a windowed
        // resize, so the physical thickness stays fixed while the user resizes the
        // window but follows the monitor when fullscreen is entered on another one.
        // Draw code back-converts these to normalized fractions of the current RT
        // height (see Render), which is what keeps the bars the same size on screen.
        if (size.height > 0.f)
        {
            g_vignette.topBarHeightPx     = size.height * 0.08f;  // topY=0.00->bottomY=0.08
            g_vignette.bottomBarHeightPx  = size.height * 0.14f;  // topY=0.86->bottomY=1.00
            // Activation zone: OffsetFromStart for top, OffsetFromEnd for bottom
            g_vignette.topActivationPx    = size.height * 0.20f;  // show when mouseY <= this
            g_vignette.bottomActivationPx = size.height * 0.20f;  // show when mouseY >= windowH - this
            g_vignette.heightsCaptured    = true;
        }

        // Thumbnail strip dimensions, from the same render target as the vignettes,
        // so a different monitor gets its own slot sizes. The clip geometry is
        // dropped here because it is built from g_thumb.height below; it is otherwise
        // never cleared, and a D2D geometry outlives the device it was drawn with.
        if (size.height > 0.f)
        {
            g_thumb.height            = size.height * 0.08f;
            g_thumb.width            = g_thumb.height;            // square slots
            g_thumb.gap          = g_thumb.height    * 0.12f;
            g_thumb.stripInsetFromBottomPx = size.height * (1.0f - 0.855f);  // mirrors vignette anchor pattern
            g_thumb.sizeCaptured = true;
            g_thumb.clipGeos.clear();

            // Snap scroll immediately so there is no slide-in on first open
            g_thumb.targetOffset = -(g_image.index * (g_thumb.width + g_thumb.gap));
            g_thumb.scrollOffset = g_thumb.targetOffset;
        }

        // Rebuild per-slot clip geometries whenever thumb dimensions are freshly captured.
        // Slots share the same shape so we only need one; index them by position later.
        // We create exactly one geometry (reused for every slot via the same RR dimensions).
        if (g_thumb.sizeCaptured && g_thumb.clipGeos.empty() && g_devices.d2dFactory)
        {
            const float radius = g_thumb.height * 0.10f;
            // Placeholder rect centred at origin - position is irrelevant because
            // we transform the render target before pushing the layer.
            const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
                D2D1::RectF(-g_thumb.width * 0.5f, -g_thumb.height * 0.5f,
                             g_thumb.width * 0.5f,  g_thumb.height * 0.5f),
                radius, radius);
            Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> geo;
            if (SUCCEEDED(g_devices.d2dFactory->CreateRoundedRectangleGeometry(rr, &geo)))
                g_thumb.clipGeos.push_back(geo);
        }
    }

    CreateBackgroundBitmap();
    RecreateImageBitmap();
    InitializeButtons();
    InitializeImageInfoLabel();
        

    g_fullscreen.overlayAlpha = 0.0f;

    if (!preserveView)
    {
        g_fullscreen.needsInit = true;
    }
    else
    {
        g_fullscreen.needsInit = false;

        if (viewIsCentered && havePan && g_view.bitmap)
        {
            // Center-relative preservation:
            // recompute absolute offsets for the new render target from pan.
            D2D1_SIZE_F imgSize = g_view.bitmap->GetSize();

            OffsetsFromPan(g_app.overlayWindow, g_view.zoom,
                           imgSize.width, imgSize.height,
                           panX, panY, g_view.offsetX, g_view.offsetY);

            OffsetsFromPan(g_app.overlayWindow, g_view.targetZoom,
                           imgSize.width, imgSize.height,
                           targetPanX, targetPanY, g_view.targetOffsetX, g_view.targetOffsetY);
        }
        else
        {
            // Screen-anchored preservation (your original behavior):
            // Convert image position from main window -> screen -> overlay client
            POINT pt = { (LONG)g_view.offsetX, (LONG)g_view.offsetY };
            ClientToScreen(g_app.mainWindow, &pt);
            ScreenToClient(g_app.overlayWindow, &pt);

            UpdateTargetZoom(g_view.zoom);
            g_view.offsetX = (float)pt.x;
            g_view.offsetY = (float)pt.y;
            g_view.targetOffsetX = g_view.offsetX;
            g_view.targetOffsetY = g_view.offsetY;
        }
    }

    ShowWindow(g_app.overlayWindow, SW_SHOW);
    UpdateWindow(g_app.overlayWindow);

    // Any drag that was in progress on the main window before the overlay took
    // over is now stale - clear all drag state so it cannot bleed through.
    g_view.isDragging    = false;
    g_thumb.dragging = false;
    ReleaseCapture();

    g_fullscreen.initDone = true;
    g_devices.renderTargetWindow = g_app.overlayWindow;
}

// Inner teardown - called either immediately (immediate=true path) or by
// UpdateEngine once the exit-fade animation has finished.
void CompleteExitFullscreen()
{
    // Already cleaned up guard (e.g. double-call from slideshow timer race)
    if (!g_view.isFullscreen)
        return;

    if (!g_view.bitmap)
    {
        // No bitmap - bail out safely.
        g_view.isFullscreen = false;
        if (g_app.overlayWindow)
        {
            DestroyWindow(g_app.overlayWindow);
            g_app.overlayWindow = nullptr;
        }
        g_fullscreen.wasInSlideshowOnExit = false;
        return;
    }

    D2D1_SIZE_F imgSize = g_view.bitmap->GetSize();

    RECT overlayClient;
    GetClientRect(g_app.overlayWindow, &overlayClient);

    RECT overlayRect;
    GetWindowRect(g_app.overlayWindow, &overlayRect);

    float overlayWidth  = (float)(overlayClient.right  - overlayClient.left);
    float overlayHeight = (float)(overlayClient.bottom - overlayClient.top);

    // The image is rendered rotated around its unrotated centre.
    // g_view.offsetX/Y is the top-left of the *unrotated* rectangle, so the
    // visual (post-rotation) bounding box must be derived from the centre
    // and the effective (possibly swapped) screen dimensions.
    float fitW = imgSize.width;
    float fitH = imgSize.height;
    {
        const float rotMod = fmodf(fabsf(g_view.rotationAngle), 180.f);
        if (rotMod > 44.f && rotMod < 136.f)
            std::swap(fitW, fitH);
    }
    // Rotation pivot (screen-space centre of the image, invariant to rotation)
    float cx = g_view.offsetX + imgSize.width  * g_view.zoom * 0.5f;
    float cy = g_view.offsetY + imgSize.height * g_view.zoom * 0.5f;

    float imgLeft   = cx - fitW * g_view.zoom * 0.5f;
    float imgTop    = cy - fitH * g_view.zoom * 0.5f;
    float imgRight  = cx + fitW * g_view.zoom * 0.5f;
    float imgBottom = cy + fitH * g_view.zoom * 0.5f;

    float visibleLeft   = max(0.0f, imgLeft);
    float visibleTop    = max(0.0f, imgTop);
    float offsetCorrectionX = imgLeft - visibleLeft;
    float offsetCorrectionY = imgTop  - visibleTop;

    float visibleRight  = min(overlayWidth,  imgRight);
    float visibleBottom = min(overlayHeight, imgBottom);

    float clientWidth  = visibleRight  - visibleLeft;
    float clientHeight = visibleBottom - visibleTop;

    const float MIN_CLIENT_W = 800.0f;
    const float MIN_CLIENT_H = 600.0f;
    
    float oldClientW = clientWidth;
    float oldClientH = clientHeight;

    clientWidth  = max(clientWidth,  MIN_CLIENT_W);
    clientHeight = max(clientHeight, MIN_CLIENT_H);
    float extraW = clientWidth  - oldClientW;
    float extraH = clientHeight - oldClientH;

    visibleLeft -= extraW * 0.5f;
    visibleTop  -= extraH * 0.5f;

    offsetCorrectionX += extraW * 0.5f;
    offsetCorrectionY += extraH * 0.5f;

    int newX = overlayRect.left + (int)visibleLeft;
    int newY = overlayRect.top  + (int)visibleTop;

    g_view.isFullscreen = false;
    g_fullscreen.needsInit = false;
    g_fullscreen.overlayAlpha = 1.0f;
    g_fullscreen.wasInSlideshowOnExit = false;
    g_fullscreen.exitBgFadeAlpha      = 0.f;

    if (g_app.overlayWindow)
    {
        DestroyWindow(g_app.overlayWindow);
        g_app.overlayWindow = nullptr;
    }

    DiscardDeviceResources();

    RECT adj = { 0, 0, (LONG)clientWidth, (LONG)clientHeight };
    AdjustWindowRect(&adj, WS_OVERLAPPEDWINDOW, FALSE);

    int nonClientOffsetY = -adj.top;
    int nonClientOffsetX = -adj.left;

    int winX = newX - nonClientOffsetX;
    int winY = newY - nonClientOffsetY;
    int winW = adj.right  - adj.left;
    int winH = adj.bottom - adj.top;

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromPoint({ newX, newY }, MONITOR_DEFAULTTONEAREST), &mi);
    RECT& wa = mi.rcWork;

    if (winX + winW > wa.right)  winX = wa.right  - winW;
    if (winY + winH > wa.bottom) winY = wa.bottom - winH;
    if (winX < wa.left)          winX = wa.left;
    if (winY < wa.top)           winY = wa.top;

    SetWindowPos(
        g_app.mainWindow,
        nullptr,
        winX, winY, winW, winH,
        SWP_NOZORDER | SWP_NOACTIVATE
    );

    int preClampWinX = winX;
    int preClampWinY = winY;

    if (winX + winW > wa.right)  winX = wa.right  - winW;
    if (winY + winH > wa.bottom) winY = wa.bottom - winH;
    if (winX < wa.left)          winX = wa.left;
    if (winY < wa.top)           winY = wa.top;

    int clampShiftX = winX - preClampWinX;
    int clampShiftY = winY - preClampWinY;

    // offsetCorrectionX/Y is the visual (rotated) image left/top in the new
    // client rect.  g_view.offsetX/Y is the *unrotated* top-left, which is offset
    // from the visual edge by half the difference between unrotated and rotated
    // dimensions.  For non-rotated images fitW==imgW so the adjustment is zero.
    float cx_new = offsetCorrectionX - (float)clampShiftX + fitW * g_view.zoom * 0.5f;
    float cy_new = offsetCorrectionY - (float)clampShiftY + fitH * g_view.zoom * 0.5f;
    g_view.offsetX = cx_new - imgSize.width  * g_view.zoom * 0.5f;
    g_view.offsetY = cy_new - imgSize.height * g_view.zoom * 0.5f;

    g_view.targetOffsetX = g_view.offsetX;
    g_view.targetOffsetY = g_view.offsetY;

    UpdateTargetZoom(g_view.zoom);

    CreateRenderTarget(g_app.mainWindow);
    RecreateImageBitmap();

    LONG ex = GetWindowLong(g_app.mainWindow, GWL_EXSTYLE);
    if (ex & WS_EX_LAYERED)
    {
        SetLayeredWindowAttributes(g_app.mainWindow, 0, 255, LWA_ALPHA);
        SetWindowLong(g_app.mainWindow, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    }

    ShowWindow(g_app.mainWindow, SW_SHOW);
    RedrawWindow(g_app.mainWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

    for (auto& [id, btn] : g_buttons)
        btn.UpdateLayout(g_devices.renderTarget.Get());

    for (auto& [key, textbox] : g_textBoxes)
        textbox.UpdateLayout(g_devices.renderTarget.Get());

    g_fullscreen.initDone = false;
    g_devices.renderTargetWindow = g_app.mainWindow;
    if (g_slideshow.active)
    {
        ExitSlideshowMode();
    }
}

void ExitFullscreen(bool immediate)
{
    if (!g_view.isFullscreen)
        return;

    // Kill the zoom-display timer and hide the textbox in case it was visible.
    // Safe to do immediately regardless of the fade path.
    KillTimer(g_app.mainWindow, static_cast<UINT_PTR>(TimerId::ZOOM_DISPLAY));
    if (!g_textBoxes.empty())
        g_textBoxes[TextBoxId::ZOOM_INPUT].SetForcedVisibility(false);

    // If there is no image, or an immediate exit is requested (e.g. slideshow
    // entry which has already faded to black), skip the fade entirely.
    if (immediate || !g_view.bitmap)
    {
        g_fullscreen.pendingExit = false;
        g_fullscreen.exitBgFadeAlpha       = 0.f;
        CompleteExitFullscreen();
        return;
    }

    // Already fading - ignore re-entrant calls.
    if (g_fullscreen.pendingExit)
        return;

    // Start the deferred exit fade: UpdateEngine animates g_fullscreen.exitBgFadeAlpha
    // 0->1 then calls CompleteExitFullscreen() when it reaches 1.
    g_fullscreen.pendingExit = false; // will be set true below; clear first for safety
    g_fullscreen.exitBgFadeAlpha       = 0.f;
    g_fullscreen.pendingExit = true;
}
void InitFullScreenExit()
{
    D2D1_SIZE_F imgSize = g_view.bitmap->GetSize();
    float centerX = g_view.offsetX + imgSize.width  * g_view.zoom / 2.0f;
    float centerY = g_view.offsetY + imgSize.height * g_view.zoom / 2.0f;
    
    g_view.targetOffsetX = centerX - (imgSize.width * 0.05f) / 2.0f;
    g_view.targetOffsetY = centerY - (imgSize.height * 0.05f) / 2.0f;
    
    int refreshRate = GetMonitorRefreshRate(GetDesktopWindow());
    g_view.smooth = 2*0.18f * 60.0f / (float)refreshRate;
    g_view.targetZoom = 0.0005f;
    g_app.isExiting = true;
}
