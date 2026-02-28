// Picasso Pictures.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "resource.h"

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
#include <filesystem>
#include <dwmapi.h>

#define MAX_LOADSTRING 100

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

// Global Variables:
HINSTANCE               hInst;                              // current instance
WCHAR                   szTitle[MAX_LOADSTRING];            // The title bar text
WCHAR                   szWindowClass[MAX_LOADSTRING];      // the main window class name
ID2D1Factory*           g_d2dFactory = nullptr;
ID2D1HwndRenderTarget*  g_renderTarget = nullptr;
IWICImagingFactory*     g_wicFactory = nullptr;
ID2D1Bitmap*            g_d2dBitmap = nullptr;
float                   g_zoom = 1.0f;                      // Current zoom level
float                   g_offsetX = 0.0f;                   // Pan offset X
float                   g_offsetY = 0.0f;                   // Pan offset Y
bool                    g_isDragging = false;
POINT                   g_lastMouse = {};
DWORD                   g_lastZoomTime = 0;
float                   g_targetZoom = 1.0f;
float                   g_zoomDisplayAlpha = 0.0f;
bool                    g_showZoomDisplay = false;
float                   g_targetOffsetX = 0.0f;
float                   g_targetOffsetY = 0.0f;
bool                    g_isFullscreen = false;
ID2D1Bitmap*            g_backgroundBitmap = nullptr;
IWICBitmapSource*       g_wicBitmapSource = nullptr;
bool                    g_needsFullscreenInit = false;
IWICBitmap*             g_wicBackground = nullptr;
float                   g_overlayAlpha = 0.0f;
HWND                    g_overlayWindow = nullptr;
HWND                    g_mainWindow = nullptr;
bool                    g_isExiting = false;
std::vector<std::wstring> g_imageFiles;
int                     g_currentImageIndex = -1;
std::wstring            g_currentFilePath;
IDWriteFactory*         g_dwriteFactory = nullptr;
IDWriteTextFormat*      g_textFormat = nullptr;
DWORD                   g_lastFrameTime = 0;
DWORD                   g_zoomHideStartTime = 0;
bool                    g_launchedWithFile = false;
D2D1_RECT_F             g_exitButtonRect = {};
float                   g_smooth = 0.065f;
bool                    g_pendingPreserveView;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
bool                LoadImageD2D(HWND hWnd, const wchar_t* filename);
void                CreateRenderTarget(HWND hWnd);
void                RecreateImageBitmap();
void                CaptureDesktop(HWND hWnd);

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
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

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

void UpdateEngine(float dt)
{
    DWORD now = GetTickCount();

    if (g_showZoomDisplay && now - g_lastZoomTime > 800)
        g_showZoomDisplay = false;

    const float fadeSpeed = 3.0f;

    if (g_showZoomDisplay)
    {
        g_zoomDisplayAlpha += fadeSpeed * dt;
        if (g_zoomDisplayAlpha > 1.0f)
            g_zoomDisplayAlpha = 1.0f;
    }
    else
    {
        g_zoomDisplayAlpha -= fadeSpeed * dt;
        if (g_zoomDisplayAlpha < 0.0f)
            g_zoomDisplayAlpha = 0.0f;
    }

    float overlayTarget = g_isExiting ? 0.0f : 1.0f;
    g_overlayAlpha += (overlayTarget - g_overlayAlpha) * 0.15f;

    g_zoom     += (g_targetZoom     - g_zoom)     * g_smooth;
    g_offsetX  += (g_targetOffsetX  - g_offsetX)  * g_smooth;
    g_offsetY  += (g_targetOffsetY  - g_offsetY)  * g_smooth;

    HWND renderWindow = g_isFullscreen && g_overlayWindow
        ? g_overlayWindow
        : g_mainWindow;

    InvalidateRect(renderWindow, nullptr, FALSE);

    if (g_isExiting &&
        fabs(g_zoom - g_targetZoom) < 0.0005f &&
        g_overlayAlpha < 0.01f)
    {
        if (g_overlayWindow)
        {
            DestroyWindow(g_overlayWindow);
            g_overlayWindow = nullptr;
        }

        PostQuitMessage(0);
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    if (lpCmdLine && wcslen(lpCmdLine) > 0)
    {
        g_launchedWithFile = true;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitialize(nullptr);

    D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        &g_d2dFactory
    );

    CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&g_wicFactory)
    );

    DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&g_dwriteFactory)
    );

    // Create text format (font settings)
    g_dwriteFactory->CreateTextFormat(
        L"Segoe UI",            // Font
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        28.0f,                  // Font size
        L"",
        &g_textFormat
    );

    // Center alignment
    g_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_PICASSOPICTURES, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    RegisterOverlayClass(hInstance);

    // Create window (loads image inside)
    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }
    if (lpCmdLine && wcslen(lpCmdLine) > 0)
    {
        wchar_t filePath[MAX_PATH];
        wcscpy_s(filePath, lpCmdLine);

        if (filePath[0] == L'"')
        {
            size_t len = wcslen(filePath);
            filePath[len - 1] = 0;
            memmove(filePath, filePath + 1, (len - 1) * sizeof(wchar_t));
        }

        HWND mainWindow = FindWindow(szWindowClass, nullptr);
        if (mainWindow)
            LoadImageD2D(mainWindow, filePath);
    }
    HACCEL hAccelTable = LoadAccelerators(
        hInstance,
        MAKEINTRESOURCE(IDC_PICASSOPICTURES)
    );

    MSG msg = {};
    DWORD lastTime = GetTickCount();

    while (true)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return (int)msg.wParam;

            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        DWORD now = GetTickCount();
        float dt = (now - lastTime) / 1000.0f;
        lastTime = now;

        if (dt > 0.03f)
            dt = 0.03f;

        UpdateEngine(dt);
    }


    return (int)msg.wParam;
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
           ext == L".webp";

}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable
   
    CaptureDesktop(g_overlayWindow);
    CaptureDesktop(g_mainWindow);

    HWND hWnd = CreateWindowExW(
        0,
        szWindowClass,
        szTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        800,
        600,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    g_mainWindow = hWnd;

    DragAcceptFiles(hWnd, TRUE);

    // Center window on screen at startup
    RECT rc;
    GetWindowRect(hWnd, &rc);

    int windowWidth  = rc.right - rc.left;
    int windowHeight = rc.bottom - rc.top;

    int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    int posX = (screenWidth  - windowWidth)  / 2;
    int posY = (screenHeight - windowHeight) / 2;


    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(
        MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST),
        &mi
    );
 
    SetWindowPos(
        hWnd,
        nullptr,
        posX,
        posY,
        0,
        0,
        SWP_NOSIZE | SWP_NOZORDER
    );

   if (!hWnd)
   {
      return FALSE;
   }

    if (!g_launchedWithFile)
    {
        ShowWindow(hWnd, nCmdShow);
        UpdateWindow(hWnd);
    }
    else
    {
        ShowWindow(hWnd, SW_HIDE);
    }

    g_lastFrameTime = GetTickCount();

    return TRUE;
}

HWND CreateOverlayWindow(HWND parent)
{
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(
        MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST),
        &mi);

    RECT rc = mi.rcWork;

    HWND overlay = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        L"OverlayWindowClass",
        L"",
        WS_POPUP,
        rc.left,
        rc.top,
        rc.right - rc.left,
        rc.bottom - rc.top,
        parent,
        nullptr,
        hInst,
        nullptr);

    ShowWindow(overlay, SW_SHOW);
    UpdateWindow(overlay);

    if (g_renderTarget)
    {
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }

    return overlay;
}

void SetZoomCentered(float newZoom, HWND hWnd, bool instant = false)
{
    if (!g_d2dBitmap) return;

    RECT rc;
    GetClientRect(hWnd, &rc);
    float windowWidth  = (float)(rc.right - rc.left);
    float windowHeight = (float)(rc.bottom - rc.top);

    D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

    if (instant)
    {
        // Hard center in window
        g_zoom = newZoom;
        g_targetZoom = newZoom;

        g_offsetX = (windowWidth  - imgSize.width  * newZoom) / 2.0f;
        g_offsetY = (windowHeight - imgSize.height * newZoom) / 2.0f;

        g_targetOffsetX = g_offsetX;
        g_targetOffsetY = g_offsetY;

        return;
    }

    // Normal animated behavior (preserve center)
    float centerX = g_offsetX + imgSize.width  * g_zoom / 2.0f;
    float centerY = g_offsetY + imgSize.height * g_zoom / 2.0f;

    g_targetOffsetX = centerX - (imgSize.width * newZoom) / 2.0f;
    g_targetOffsetY = centerY - (imgSize.height * newZoom) / 2.0f;

    g_targetZoom = newZoom;
}

void CaptureDesktop(HWND hWnd)
{
    if (g_wicBackground)
    {
        g_wicBackground->Release();
        g_wicBackground = nullptr;
    }

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(
        MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST),
        &mi);

    RECT rc = mi.rcWork;

    int width  = rc.right  - rc.left;
    int height = rc.bottom - rc.top;

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    HBITMAP hBitmap = CreateCompatibleBitmap(screenDC, width, height);
    SelectObject(memDC, hBitmap);

    BitBlt(memDC, 0, 0, width, height,
           screenDC, rc.left, rc.top, SRCCOPY);

    g_wicFactory->CreateBitmapFromHBITMAP(
        hBitmap,
        nullptr,
        WICBitmapIgnoreAlpha,
        &g_wicBackground);

    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

void FitToWindow(HWND hWnd)
{
    if (!g_d2dBitmap) return;

    RECT rc;
    GetClientRect(hWnd, &rc);

    float windowWidth  = (float)(rc.right - rc.left);
    float windowHeight = (float)(rc.bottom - rc.top);

    D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

    float scaleX = windowWidth  / imgSize.width;
    float scaleY = windowHeight / imgSize.height;

    float scale = min(scaleX, scaleY);

    SetZoomCentered(scale, hWnd);

    InvalidateRect(hWnd, nullptr, FALSE);
}

void FitWindowToImage(HWND hWnd)
{
    if (!g_d2dBitmap)
        return;

    D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

    // Desired CLIENT size
    RECT rc = {};
    rc.left   = 0;
    rc.top    = 0;
    rc.right  = (LONG)imgSize.width;
    rc.bottom = (LONG)imgSize.height;

    // Convert client size to full window size
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

    // Keep current window position
    RECT current;
    GetWindowRect(hWnd, &current);

    SetWindowPos(
        hWnd,
        nullptr,
        current.left,
        current.top,
        rc.right - rc.left,
        rc.bottom - rc.top,
        SWP_NOZORDER
    );

    // Reset zoom to 1:1
    SetZoomCentered(1.0f, hWnd, true);

    InvalidateRect(hWnd, nullptr, FALSE);
}

bool CreateBackgroundBitmap()
{
    // We must have a render target
    if (!g_renderTarget)
        return false;

    // We must have a captured WIC bitmap source
    if (!g_wicBackground)
        return false;

    // Release old background bitmap if it exists
    if (g_backgroundBitmap)
    {
        g_backgroundBitmap->Release();
        g_backgroundBitmap = nullptr;
    }

    HRESULT hr = g_renderTarget->CreateBitmapFromWicBitmap(
        g_wicBackground,
        nullptr,
        &g_backgroundBitmap
    );

    if (FAILED(hr))
    {
        OutputDebugString(L"CreateBackgroundBitmap failed\n");
        return false;
    }

    return true;
}

void EnterFullscreen(bool preserveView, bool needsDelay)
{
    // Prevent duplicate first-stage calls
    if (g_isFullscreen && needsDelay)
        return;
    // ----------------------------------------
    // STAGE 1 — hide window & wait for DWM
    // ----------------------------------------
    if (needsDelay)
    {
        g_isFullscreen = true;

        SetWindowLong(
            g_mainWindow,
            GWL_EXSTYLE,
            GetWindowLong(g_mainWindow, GWL_EXSTYLE) | WS_EX_LAYERED
        );

        SetLayeredWindowAttributes(
            g_mainWindow,
            0,
            0,
            LWA_ALPHA
        );

        UpdateWindow(g_mainWindow);

        // Give DWM time to remove dialog + window
        SetTimer(g_mainWindow, 8888, 120, nullptr);

        // Store whether we preserve view
        g_pendingPreserveView = preserveView;
        return;
    }

    // ----------------------------------------
    // STAGE 2 — actual fullscreen creation
    // ----------------------------------------

    CaptureDesktop(g_mainWindow);

    if (g_renderTarget)
    {
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }

    g_overlayWindow = CreateOverlayWindow(g_mainWindow);

    CreateRenderTarget(g_overlayWindow);
    CreateBackgroundBitmap();
    RecreateImageBitmap();

    g_overlayAlpha = 0.0f;

    if (!preserveView)
    {
        g_needsFullscreenInit = true;
    }
    else
    {
        g_needsFullscreenInit = false;

        // Convert image position from main window → screen
        POINT pt = { (LONG)g_offsetX, (LONG)g_offsetY };
        ClientToScreen(g_mainWindow, &pt);
        ScreenToClient(g_overlayWindow, &pt);

        g_targetZoom = g_zoom;

        g_offsetX = (float)pt.x;
        g_offsetY = (float)pt.y;

        g_targetOffsetX = g_offsetX;
        g_targetOffsetY = g_offsetY;
    }

    InvalidateRect(g_overlayWindow, nullptr, FALSE);
}

void ExitFullscreen()
{
    if (!g_isFullscreen)
        return;

    g_zoom      = g_targetZoom;
    g_offsetX   = g_targetOffsetX;
    g_offsetY   = g_targetOffsetY;
    D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

    RECT overlayClient;
    GetClientRect(g_overlayWindow, &overlayClient);
    // Get overlay window screen rect
    RECT overlayRect;
    GetWindowRect(g_overlayWindow, &overlayRect);

    float overlayWidth  = (float)(overlayClient.right  - overlayClient.left);
    float overlayHeight = (float)(overlayClient.bottom - overlayClient.top);

    float imgLeft   = g_offsetX;
    float imgTop    = g_offsetY;
    float imgRight  = g_offsetX + imgSize.width  * g_zoom;
    float imgBottom = g_offsetY + imgSize.height * g_zoom;

    // Clamp to visible region
    float visibleLeft   = max(0.0f, imgLeft);
    float visibleTop    = max(0.0f, imgTop);
    float offsetCorrectionX = imgLeft - visibleLeft;
    float offsetCorrectionY = imgTop  - visibleTop;

    float visibleRight  = min(overlayWidth,  imgRight);
    float visibleBottom = min(overlayHeight, imgBottom);

    float clientWidth  = visibleRight  - visibleLeft;
    float clientHeight = visibleBottom - visibleTop;

    // Adjust new position using visible region
    int newX = overlayRect.left + (int)visibleLeft;
    int newY = overlayRect.top  + (int)visibleTop;

    g_isFullscreen = false;
    g_needsFullscreenInit = false;
    g_overlayAlpha = 1.0f;

    if (g_backgroundBitmap)
    {
        g_backgroundBitmap->Release();
        g_backgroundBitmap = nullptr;
    }

    if (g_overlayWindow)
    {
        DestroyWindow(g_overlayWindow);
        g_overlayWindow = nullptr;
    }

    if (g_renderTarget)
    {
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }

    // Remove layered style FIRST (but don't mess with redraw logic)
    LONG ex = GetWindowLong(g_mainWindow, GWL_EXSTYLE);
    SetWindowLong(
        g_mainWindow,
        GWL_EXSTYLE,
        ex & ~WS_EX_LAYERED
    );

    // Calculate window rect BEFORE showing
    RECT adj = { 0,0,(LONG)clientWidth,(LONG)clientHeight };
    AdjustWindowRect(&adj, WS_OVERLAPPEDWINDOW, TRUE);

    int nonClientOffsetY = -adj.top;
    int nonClientOffsetX = -adj.left;

    // Resize window BEFORE showing
    SetWindowPos(
        g_mainWindow,
        nullptr,
        newX - nonClientOffsetX,
        newY - nonClientOffsetY,
        adj.right - adj.left,
        adj.bottom - adj.top,
        SWP_NOZORDER | SWP_NOACTIVATE
    );

    // Snap animation fully to final state
    g_offsetX = offsetCorrectionX;
    g_offsetY = offsetCorrectionY;

    g_targetOffsetX = g_offsetX;
    g_targetOffsetY = g_offsetY;

    // Also snap zoom target
    g_targetZoom = g_zoom;

    // Now show window

    UpdateWindow(g_mainWindow);
    // Recreate D2D AFTER window is visible

    CreateRenderTarget(g_mainWindow);
    RecreateImageBitmap();

    InvalidateRect(g_mainWindow, nullptr, FALSE);
    ShowWindow(g_mainWindow, SW_SHOW);
}

void CreateRenderTarget(HWND hWnd)
{
    if (!g_renderTarget)
    {
        RECT rc;
        GetClientRect(hWnd, &rc);

        D2D1_SIZE_U size = D2D1::SizeU(
            rc.right - rc.left,
            rc.bottom - rc.top
        );

        D2D1_RENDER_TARGET_PROPERTIES props =
            D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    D2D1_ALPHA_MODE_IGNORE
                )
            );

        props.dpiX = 96.0f;
        props.dpiY = 96.0f;

        g_d2dFactory->CreateHwndRenderTarget(
            props,
            D2D1::HwndRenderTargetProperties(hWnd, size),
            &g_renderTarget
        );
    }
}

void RecreateImageBitmap()
{
    if (!g_wicBitmapSource || !g_renderTarget)
        return;

    if (g_d2dBitmap)
    {
        g_d2dBitmap->Release();
        g_d2dBitmap = nullptr;
    }

    g_renderTarget->CreateBitmapFromWicBitmap(
        g_wicBitmapSource,
        nullptr,
        &g_d2dBitmap
    );
}

void InitializeImageLayout(HWND hWnd, bool hard = false)
{
    if (!g_d2dBitmap) return;

    RECT rc;
    GetClientRect(hWnd, &rc);

    float windowWidth  = (float)(rc.right - rc.left);
    float windowHeight = (float)(rc.bottom - rc.top);

    D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

    float scale = 1.0f;

    if (hard)
        g_overlayAlpha = 1.0f;
    else
        g_overlayAlpha = 0.0f;

    // If image is larger than screen, scale down to ~92% of screen
    if (imgSize.width > windowWidth || imgSize.height > windowHeight)
    {
        float scaleX = (windowWidth  * 0.92f) / imgSize.width;
        float scaleY = (windowHeight * 0.92f) / imgSize.height;

        scale = min(scaleX, scaleY);
    }

    float windowCenterX = windowWidth / 2.0f;
    float windowCenterY = windowHeight / 2.0f;

    if (hard)
    {
        g_zoom = scale;          // Hard clamp to final scale
        g_targetZoom = scale;

        // Target offsets for final scale
        g_targetOffsetX = windowCenterX - (imgSize.width  * g_targetZoom) / 2.0f;
        g_targetOffsetY = windowCenterY - (imgSize.height * g_targetZoom) / 2.0f;

        // Start offsets at final position since we're hard centering
        g_offsetX = g_targetOffsetX ;
        g_offsetY = g_targetOffsetY;
    }
    else
    {
        g_zoom = 0.05f;          // Start tiny
        g_targetZoom = scale;    // Animate to final scale

        // Start offsets so that image center is centered at window center at tiny zoom
        g_offsetX = windowCenterX - (imgSize.width  * g_zoom) / 2.0f;
        g_offsetY = windowCenterY - (imgSize.height * g_zoom) / 2.0f;

        // Target offsets for final scale
        g_targetOffsetX = windowCenterX - (imgSize.width  * g_targetZoom) / 2.0f;
        g_targetOffsetY = windowCenterY - (imgSize.height * g_targetZoom) / 2.0f;
    }
}

void Render(HWND hWnd)
{

    CreateRenderTarget(hWnd);
    g_renderTarget->BeginDraw();

    // Always clear first
    g_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 1.0f));

    if (g_isFullscreen)
    {
        // Semi-transparent black over live desktop
        g_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0.6f));
    }
    else
    {
        g_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 1.0f));
    }


    if (g_needsFullscreenInit)
    {
        RecreateImageBitmap();

        if (g_wicBackground)
        {
            if (g_backgroundBitmap)
            {
                g_backgroundBitmap->Release();
                g_backgroundBitmap = nullptr;
            }

            g_renderTarget->CreateBitmapFromWicBitmap(
                g_wicBackground,
                nullptr,
                &g_backgroundBitmap
            );
        }

        // Initialize layout AFTER everything exists
        InitializeImageLayout(hWnd);

        g_needsFullscreenInit = false;
    }


    // Draw dimmed desktop background
    if (g_isFullscreen && g_backgroundBitmap)
    {
        D2D1_SIZE_F size = g_backgroundBitmap->GetSize();

        g_renderTarget->DrawBitmap(
            g_backgroundBitmap,
            D2D1::RectF(0, 0, size.width, size.height),
            1.0f
        );

        ID2D1SolidColorBrush* brush = nullptr;
        g_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0, 0, 0, 0.55f * g_overlayAlpha),
            &brush
        );

        g_renderTarget->FillRectangle(
            D2D1::RectF(0, 0, size.width, size.height),
            brush
        );

        brush->Release();
    }
    
    // Draw image
    if (g_d2dBitmap)
    {
        D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

        D2D1::Matrix3x2F transform =
            D2D1::Matrix3x2F::Scale(g_zoom, g_zoom) *
            D2D1::Matrix3x2F::Translation(g_offsetX, g_offsetY);

        g_renderTarget->SetTransform(transform);

        g_renderTarget->DrawBitmap(
            g_d2dBitmap,
            D2D1::RectF(0, 0, imgSize.width, imgSize.height),
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
        );

        g_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    }


    if (g_zoomDisplayAlpha > 0.01f)
    {
        wchar_t text[32];
        swprintf_s(text, L"%.0f%%", g_zoom * 100.0f);

        ID2D1SolidColorBrush* bgBrush = nullptr;
        ID2D1SolidColorBrush* textBrush = nullptr;

        g_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.15f, 0.15f, 0.15f, g_zoomDisplayAlpha),
            &bgBrush);

        g_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 1.0f, 1.0f, g_zoomDisplayAlpha),
            &textBrush);

        RECT rc;
        GetClientRect(hWnd, &rc);

        float boxWidth = 120;
        float boxHeight = 60;

        float centerX = (rc.right - rc.left) / 2.0f;
        float centerY = (rc.bottom - rc.top) / 2.0f;

        D2D1_ROUNDED_RECT rounded =
            D2D1::RoundedRect(
                D2D1::RectF(
                    centerX - boxWidth / 2,
                    centerY - boxHeight / 2,
                    centerX + boxWidth / 2,
                    centerY + boxHeight / 2),
                12.0f,
                12.0f);

        g_renderTarget->FillRoundedRectangle(rounded, bgBrush);

        D2D1_RECT_F textRect =
            D2D1::RectF(
                centerX - boxWidth / 2,
                centerY - boxHeight / 2,
                centerX + boxWidth / 2,
                centerY + boxHeight / 2);

        g_renderTarget->DrawTextW(
            text,
            (UINT32)wcslen(text),
            g_textFormat,
            textRect,
            textBrush
        );


        bgBrush->Release();
        textBrush->Release();
    }

    HRESULT hr = g_renderTarget->EndDraw();

    if (hr == D2DERR_RECREATE_TARGET)
    {
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }


    /*
    float size = 30.0f;

    D2D1_SIZE_F rtSize = g_renderTarget->GetSize();

    g_exitButtonRect =
        D2D1::RectF(
            rtSize.width - size - 10,
            10,
            rtSize.width - 10,
            10 + size
        );

    ID2D1SolidColorBrush* brush = nullptr;
    g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1, 1, 1, 0.7f),
        &brush
    );

    g_renderTarget->DrawRectangle(
        g_exitButtonRect,
        brush,
        2.0f
    );

    g_renderTarget->DrawTextW(
        L"X",
        1,
        g_textFormat,
        g_exitButtonRect,
        brush
    );

    brush->Release();
    */
}

bool LoadImageD2D(HWND hWnd, const wchar_t* filename)
{
    // If we already have a bitmap loaded, release it
    if (g_d2dBitmap)
    {
        g_d2dBitmap->Release();
        g_d2dBitmap = nullptr;
    }

    // Make sure render target exists
    CreateRenderTarget(hWnd);

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    // Create decoder from file
    HRESULT hr = g_wicFactory->CreateDecoderFromFilename(
        filename,
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder
    );

    if (FAILED(hr)) return false;

    g_currentFilePath = filename;
    g_imageFiles.clear();
    g_currentImageIndex = -1;

    std::filesystem::path p(filename);
    std::filesystem::path dir = p.parent_path();

    int index = 0;

    for (auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;

        std::wstring path = entry.path().wstring();

        if (IsSupportedImage(path))
        {
            g_imageFiles.push_back(path);

            if (entry.path() == p)
                g_currentImageIndex = index;

            index++;
        }
    }

    // Get first image frame
    decoder->GetFrame(0, &frame);

    // Create format converter
    g_wicFactory->CreateFormatConverter(&converter);

    // Convert to 32-bit BGRA format required by Direct2D
    converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom
    );

    // Store WIC source permanently
    if (g_wicBitmapSource)
    {
        g_wicBitmapSource->Release();
        g_wicBitmapSource = nullptr;
    }

    g_wicBitmapSource = converter;
    g_wicBitmapSource->AddRef();

    // Create D2D bitmap
    g_renderTarget->CreateBitmapFromWicBitmap(
        g_wicBitmapSource,
        nullptr,
        &g_d2dBitmap
    );

    // Release temporary COM objects
    converter->Release();
    frame->Release();
    decoder->Release();

    // Force redraw
    //InvalidateRect(hWnd, nullptr, FALSE);
    return g_d2dBitmap != nullptr;
}

bool OpenImageFile(HWND hWnd)
{
    // Buffer that will receive the selected file path
    wchar_t fileName[MAX_PATH] = { 0 };

    // Structure used by the Windows file open dialog
    OPENFILENAME ofn = {};

    ofn.lStructSize = sizeof(ofn);          // Required size
    ofn.hwndOwner = hWnd;                  // Parent window
    ofn.lpstrFile = fileName;              // Output buffer
    ofn.nMaxFile = MAX_PATH;               // Buffer size

    // File filter (double-null terminated!)
    ofn.lpstrFilter =
        L"All Supported Images (*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff;*.webp)\0"
        L"*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff;*.webp\0"
        L"All Files (*.*)\0*.*\0";

    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;
    ofn.lpstrDefExt = L"jpg";

    // Show dialog
    if (!GetOpenFileName(&ofn))
        return false;   // User cancelled

    // Load image using Direct2D/WIC
    if (!LoadImageD2D(hWnd, fileName))
    {
        MessageBox(hWnd, L"Failed to load image.", L"Error", MB_ICONERROR);
        return false;
    }
    EnterFullscreen(false, true);

    return true;
}

bool ZoomIntoImage(HWND hWnd, short delta, POINT pt)
{
    if (g_isExiting || !g_d2dBitmap)
        return false;

    g_showZoomDisplay = true;
    g_lastZoomTime = GetTickCount();

    float zoomStep = delta / 120.0f;
    float zoomAmount = powf(1.1f, zoomStep);
    float newTargetZoom = g_targetZoom * zoomAmount;

    if (newTargetZoom < 0.05f) newTargetZoom = 0.05f;
    if (newTargetZoom > 50.0f) newTargetZoom = 50.0f;
    
    ScreenToClient(hWnd, &pt);

    D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

    // Check if mouse is over the image
    bool overImage =
        pt.x >= g_offsetX &&
        pt.x <= g_offsetX + imgSize.width * g_zoom &&
        pt.y >= g_offsetY &&
        pt.y <= g_offsetY + imgSize.height * g_zoom;

    if (overImage)
    {
        // Mouse is over the image: zoom relative to cursor
        float imageX = (pt.x - g_offsetX) / g_zoom;
        float imageY = (pt.y - g_offsetY) / g_zoom;

        g_targetOffsetX = pt.x - imageX * newTargetZoom;
        g_targetOffsetY = pt.y - imageY * newTargetZoom;
    }
    else
    {
        // Mouse outside: zoom relative to current image center
        float centerX = g_offsetX + imgSize.width  * g_zoom / 2.0f;
        float centerY = g_offsetY + imgSize.height * g_zoom / 2.0f;

        g_targetOffsetX = centerX - (imgSize.width * newTargetZoom) / 2.0f;
        g_targetOffsetY = centerY - (imgSize.height * newTargetZoom) / 2.0f;
    }

    g_targetZoom = newTargetZoom;

    return true;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_ERASEBKGND:
    {
        return 1; // Prevent background erase (no flicker)
    }
    break;

    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;

        wchar_t filePath[MAX_PATH];
        DragQueryFile(hDrop, 0, filePath, MAX_PATH);

        DragFinish(hDrop);

        LoadImageD2D(hWnd, filePath);
        EnterFullscreen(false, true);
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
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
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
        if (g_renderTarget)
        {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);

            g_renderTarget->Resize(D2D1::SizeU(width, height));
        }
    }
    break;
       
    case WM_MOUSEWHEEL:
    {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        ZoomIntoImage(hWnd, delta, pt);
    }
    break;

    case WM_LBUTTONDOWN:
    {
        g_isDragging = true;

        SetCapture(hWnd);

        g_lastMouse.x = GET_X_LPARAM(lParam);
        g_lastMouse.y = GET_Y_LPARAM(lParam);

    }
    break;

    case WM_MOUSEMOVE:
    {
        if (g_isDragging && (wParam & MK_LBUTTON))
        {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);

            float dx = (float)(pt.x - g_lastMouse.x);
            float dy = (float)(pt.y - g_lastMouse.y);

            g_offsetX += dx;
            g_offsetY += dy;

            // Keep animation targets aligned
            g_targetOffsetX = g_offsetX;
            g_targetOffsetY = g_offsetY;

            g_lastMouse = pt;

            HWND renderWindow = g_isFullscreen && g_overlayWindow
                ? g_overlayWindow
                : hWnd;

            InvalidateRect(renderWindow, nullptr, FALSE);
        }
    }
    break;

    case WM_LBUTTONUP:
    {
        g_isDragging = false;
        ReleaseCapture();
    }
    break;

    case WM_LBUTTONDBLCLK:
    {
        if (!g_d2dBitmap) break;

        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

        bool overImage =
            pt.x >= g_offsetX &&
            pt.x <= g_offsetX + imgSize.width * g_zoom &&
            pt.y >= g_offsetY &&
            pt.y <= g_offsetY + imgSize.height * g_zoom;

        if (!g_isFullscreen && overImage)
        {
            EnterFullscreen(true, true); // preserve zoom & position
        }
        else if (g_isFullscreen)
        {
            if (overImage)
            {
                g_lastZoomTime = GetTickCount();
                g_showZoomDisplay = true;
                SetZoomCentered(1.0f, g_overlayWindow);
            }
            else
            {
                ExitFullscreen();
            }
        }
    }
    break;

    case WM_TIMER:
    {
        if (wParam == 8888)
        {
            KillTimer(hWnd, 8888);
            EnterFullscreen(g_pendingPreserveView, false);
        }
    }
    break;

    case WM_KEYDOWN:
    {
        switch (wParam)
        {
            case VK_ESCAPE:
            {
                if (!g_d2dBitmap)
                {
                    PostQuitMessage(0);
                    return 0;
                }

                D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();
                float centerX = g_offsetX + imgSize.width  * g_zoom / 2.0f;
                float centerY = g_offsetY + imgSize.height * g_zoom / 2.0f;

                g_targetOffsetX = centerX - (imgSize.width * 0.05f) / 2.0f;
                g_targetOffsetY = centerY - (imgSize.height * 0.05f) / 2.0f;
                g_smooth = 0.18f;
                g_targetZoom = 0.0005f;
                g_showZoomDisplay = false;
                g_isExiting = true;

                return 0;
            }

            case VK_UP:
            {
                short delta = 250;
                POINT pt;
                pt.x = 0;
                pt.y = 0;
                ZoomIntoImage(hWnd, delta, pt);
                return 0;
            }
            
            case VK_DOWN:
            {
                short delta = -250;
                POINT pt;
                pt.x = 0;
                pt.y = 0;
                ZoomIntoImage(hWnd, delta, pt);
                return 0;
            }

            case VK_RIGHT:
            {
                if (!g_imageFiles.empty())
                {
                    g_currentImageIndex =
                        (g_currentImageIndex + 1) % g_imageFiles.size();

                    g_showZoomDisplay = false;
                    LoadImageD2D(hWnd, g_imageFiles[g_currentImageIndex].c_str());
                    InitializeImageLayout(hWnd, true);

                }

                return 0;
            }

            case VK_LEFT:
            {
                if (!g_imageFiles.empty())
                {
                    g_currentImageIndex--;

                    if (g_currentImageIndex < 0)
                        g_currentImageIndex =
                            (int)g_imageFiles.size() - 1;

                    g_showZoomDisplay = false;
                    LoadImageD2D(hWnd, g_imageFiles[g_currentImageIndex].c_str());
                    InitializeImageLayout(hWnd, true);
                }

                return 0;
            }
        }
    }
    break;
    
    case WM_DESTROY:
    {
        if (hWnd == g_mainWindow)
        {
            if (g_d2dBitmap) g_d2dBitmap->Release();
            if (g_renderTarget) g_renderTarget->Release();
            if (g_d2dFactory) g_d2dFactory->Release();
            if (g_wicFactory) g_wicFactory->Release();
            if (g_textFormat)     g_textFormat->Release();
            if (g_dwriteFactory)  g_dwriteFactory->Release();

            CoUninitialize();
            PostQuitMessage(0);
        }
    }
    break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
