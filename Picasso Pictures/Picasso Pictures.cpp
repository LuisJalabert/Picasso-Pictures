// Picasso Pictures.cpp : Defines the entry point for the application.
//

#define MAX_LOADSTRING 100

#include "framework.h"
#include "resource.h"
#include "AnimatedButton.h"
#include "UITextBox.h"
#include <chrono>
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
#include <uxtheme.h>
#include <wrl/client.h>
#include <cmath>
#include <functional>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl.h>
#include <d2d1effects.h>
#include <unordered_map>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "uxtheme.lib")

using Microsoft::WRL::ComPtr;

struct ImageViewState {
    float zoom;
    float panX;        // image-center minus window-center, in pixels
    float panY;
    float targetZoom;
    float targetPanX;
    float targetPanY;
    float rotation;
    float targetRotation;
};

// Control IDs for text boxes
int TEXTBOX_FILE_NAME = 0;
int TEXTBOX_ZOOM_INPUT = 1;

// Control IDs for buttons

int BUTTON_ZOOM_11 = 0;
int BUTTON_ZOOM_IN = 1;
int BUTTON_ZOOM_OUT = 2;
int BUTTON_ROTATE_LEFT = 3;
int BUTTON_ROTATE_RIGHT = 4;
int BUTTON_PREVIOUS = 5;
int BUTTON_NEXT = 6;
int BUTTON_EXIT = 7;
int BUTTON_SLIDESHOW = 8;
int BUTTON_OPEN = 9;
int BUTTON_HELP = 10;

// Global Variables:
HINSTANCE                                           hInst;                              // current instance
WCHAR                                               szTitle[MAX_LOADSTRING];            // The title bar text
WCHAR                                               szWindowClass[MAX_LOADSTRING];      // the main window class name
ComPtr<ID2D1Factory1>                               g_d2dFactory;
HWND                                                g_renderTargetWindow = nullptr;
ComPtr<IWICImagingFactory>                          g_wicFactory;
ComPtr<ID2D1Bitmap>                                 g_d2dBitmap;
ComPtr<IWICBitmapSource>                            g_wicBitmapSource;                  // Cached, device-independent WIC source for current image
float                                               g_zoom = 1.0f;                      // Current zoom level
float                                               g_offsetX = 0.0f;                   // Pan offset X
float                                               g_offsetY = 0.0f;                   // Pan offset Y
bool                                                g_isDragging = false;
POINT                                               g_lastMouse = {};
POINT                                               g_mouseFromDown = {};
float                                               g_targetZoom = 1.0f;
float                                               g_targetOffsetX = 0.0f;
float                                               g_targetOffsetY = 0.0f;
bool                                                g_isFullscreen = false;
float                                               g_uiPixelScale = 1.0f;
float                                               g_imageRotationAngle = 0.0f;
float                                               g_targetRotationAngle = 0.0f;       // New rotation target
float                                               g_rotationSpeed = 300.0f;           // degrees per second
ComPtr<ID2D1Bitmap>                                 g_backgroundBitmap;                 // Device-dependent background bitmap
ComPtr<IWICBitmap>                                  g_wicBackground;                    // Device-independent captured background (WIC)
ComPtr<IWICBitmapSource>                            g_wicDefaultBackground;
ComPtr<ID2D1Bitmap>                                 g_defaultBackgroundBitmap;
bool                                                g_needsFullscreenInit = false;
float                                               g_overlayAlpha = 0.0f;
HWND                                                g_overlayWindow = nullptr;
HWND                                                g_mainWindow = nullptr;
bool                                                g_isExiting = false;
bool                                                g_fullScreenInitDone = false;
std::vector<std::wstring>                           g_imageFiles;
int                                                 g_currentImageIndex = -1;
std::wstring                                        g_currentFilePath;
std::wstring                                        g_currentFileName;
int                                                 g_imageWidth  = 0;
int                                                 g_imageHeight = 0;
std::unordered_map<std::wstring, ImageViewState>    g_imageStates;
bool                                                g_restoredStateThisLoad = false;
ComPtr<IDWriteFactory>                              g_dwriteFactory;
ComPtr<IDWriteTextFormat>                           g_textFormat;
LONG                                                g_lastFrameTime = 0;
DWORD                                               g_zoomHideStartTime = 0;
bool                                                g_launchedWithFile = false;
D2D1_RECT_F                                         g_exitButtonRect = {};
float                                               g_smooth = 0.13f;
bool                                                g_pendingPreserveView = false;
std::vector<ComPtr<IWICBitmapSource>>               g_gifFrames;
DWORD                                               g_lastGifFrameTime = 0;
std::vector<UINT>                                   g_gifFrameDelays;
UINT                                                g_currentGifFrame = 0;
bool                                                g_isAnimatedGif = false;
ComPtr<ID3D11Device>                                g_d3dDevice;
ComPtr<ID3D11DeviceContext>                         g_d3dContext;
ComPtr<IDXGIDevice>                                 g_dxgiDevice;
ComPtr<ID2D1Device>                                 g_d2dDevice;
ComPtr<ID2D1DeviceContext>                          g_renderTarget;
ComPtr<IDXGISwapChain1>                             g_swapChain;
ComPtr<ID2D1Bitmap1>                                g_d2dTargetBitmap;
ComPtr<ID2D1Effect>                                 g_shadowEffect;
std::unordered_map<int, UITextBox>                  g_textBoxes;
std::unordered_map<int, AnimatedButton>             g_buttons;

// ---- Slideshow mode ----
bool                                                g_isSlideshowMode            = false;
ComPtr<ID2D1Bitmap>                                 g_slideshowBgBitmap;         // 25 %-size blurred bg for current image
ComPtr<ID2D1Bitmap>                                 g_prevSlideshowBgBitmap;     // blurred bg fading out
ComPtr<ID2D1Bitmap>                                 g_prevD2DBitmap;             // image bitmap fading out
ComPtr<ID2D1Effect>                                 g_blurEffect;                // reusable Gaussian blur effect
float                                               g_slideshowTransitionAlpha   = 1.0f;  // 0 = show prev, 1 = show new
float                                               g_slideshowTargetAlpha       = 1.0f;
float                                               g_prevImageZoom              = 1.0f;
float                                               g_prevImageOffX              = 0.0f;
float                                               g_prevImageOffY              = 0.0f;
float                                               g_prevImageRotation          = 0.0f;
constexpr UINT_PTR                                  SLIDESHOW_TIMER_ID           = 999;
constexpr UINT                                      SLIDESHOW_INTERVAL_MS        = 5000;  // ms between pictures
bool                                                g_slideshowPreFade           = false; // true from button-press until first overlay Present()
float                                               g_slideshowPreFadeAlpha      = 0.0f;  // 0=current view visible, 1=fully black
HWND                                                g_blackCoverWindow           = nullptr;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
ATOM                RegisterOverlayClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

static std::wstring GetInitialFileFromCommandLine();
int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow);
int GetMonitorRefreshRate(HWND hWnd);
bool LoadDefaultBackgroundFromResource();
void EnableDarkTitleBar(HWND hwnd);
ATOM MyRegisterClass(HINSTANCE hInstance);
ATOM RegisterOverlayClass(HINSTANCE hInstance);
void DiscardDeviceResources();
void UpdateEngine(float dt);
bool IsSupportedImage(const std::wstring& path);
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
HWND CreateOverlayWindow(HWND parent);
bool CreateDefaultBackgroundBitmap();
void SetZoomCentered(float newZoom, HWND hWnd, bool instant, bool preserveCenter);
void CaptureDesktop(HWND hWnd);
void FitToWindowRelative(HWND hWnd, float zoom, bool preserveCenter);
bool CreateBackgroundBitmap();
void InitializeMenuButtons();
void InitializeButtons();
void InitializeImageInfoLabel();
void UpdateTargetZoom(float newZoom);
void EnterFullscreen(bool preserveView, bool needsDelay);
void ExitFullscreen();
void EnterSlideshowMode();
void ExitSlideshowMode();
void CreateSlideshowBgBitmap();
void RegisterBlackCoverClass(HINSTANCE hInstance);
void CreateBlackCoverWindow();
void DestroyBlackCoverWindow();
void CreateRenderTarget(HWND hWnd);
void RecreateImageBitmap();
void InitializeImageLayout(HWND hWnd, bool hard);
void Render(HWND hWnd);
bool LoadImageD2D(HWND hWnd, const wchar_t* filename);
void BuildImageList(const wchar_t* filename);
bool OpenImageFile(HWND hWnd);
void OpenNextImage(HWND hWnd);
void OpenPrevImage(HWND hWnd);
bool ZoomIntoImage(HWND hWnd, short delta, POINT* optionalPt);
void MakeZoomVisible(HWND hWnd);
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

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
    g_launchedWithFile = !initialFile.empty();
    // --- Load the strings early so we can find the existing window by class name ---
    // (these load the same values you load later; moving them here lets FindWindow use them)
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_PICASSOPICTURES, szWindowClass, MAX_LOADSTRING);

    // --- Single-instance check using a named mutex ---
    // Use a stable name for the mutex (change GUID-like part if you want a different token)
    static const wchar_t* MUTEX_NAME = L"Local\\PicassoPictures_SingleInstance_v1";

    HANDLE hMutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);

    if (hMutex == nullptr)
    {
        // Can't create mutex — proceed as normal (or optionally exit)
    }
    else
    {
        DWORD lastErr = GetLastError();
        if (lastErr == ERROR_ALREADY_EXISTS || lastErr == ERROR_ACCESS_DENIED)
        {
            // Another instance probably exists. Try to find its window and send the filename.
            HWND hOtherWnd = FindWindowW(szWindowClass, nullptr);
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
        reinterpret_cast<void**>(g_d2dFactory.GetAddressOf()))))
    {
        CoUninitialize();
        if (hMutex) CloseHandle(hMutex);
        return FALSE;
    }

    if (FAILED(CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(g_wicFactory.GetAddressOf()))))
    {
        CoUninitialize();
        if (hMutex) CloseHandle(hMutex);
        return FALSE;
    }

    LoadDefaultBackgroundFromResource();

    if (FAILED(DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_dwriteFactory.GetAddressOf()))))
    {
        CoUninitialize();
        if (hMutex) CloseHandle(hMutex);
        return FALSE;
    }
  
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int refreshRate = GetMonitorRefreshRate(GetDesktopWindow());
    g_smooth = g_smooth / ((float)(refreshRate) / 60.0f);

    // Make overlay box 2.3% of screen height
    float boxHeight = screenHeight * 0.023f;

    // Font ~50% of that
    float fontSize = boxHeight * 0.5f;

    if (FAILED(g_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSize,
        L"",
        g_textFormat.GetAddressOf())))
    {
        CoUninitialize();
        if (hMutex) CloseHandle(hMutex);
        return FALSE;
    }

    // Center alignment
    g_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Initialize global strings (already loaded above but we keep this for consistency)
    // (If you prefer, you can remove the earlier LoadString calls and keep these here.)
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_PICASSOPICTURES, szWindowClass, MAX_LOADSTRING);
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
    if (!g_renderTarget)
        CreateRenderTarget(g_mainWindow);
    // Main menu:
    SetMenu(g_mainWindow, nullptr);
    InitializeMenuButtons();

    // If we have an initial file, load it and enter fullscreen immediately.
    if (!initialFile.empty())
    {
        BuildImageList(initialFile.c_str());
        LoadImageD2D(g_mainWindow, initialFile.c_str());
        EnterFullscreen(false, true);
    }

    HACCEL hAccelTable = LoadAccelerators(
        hInstance,
        MAKEINTRESOURCE(IDC_PICASSOPICTURES)
    );

    MSG msg = {};
    LONG lastTime = GetTickCount64();

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

        LONG now = GetTickCount64();
        float dt = (now - lastTime) / 1000.0f;
        lastTime = now;

        if (dt > 0.03f)
            dt = 0.03f;

        UpdateEngine(dt);
    }

shutdown:
    // Cleanup
    CoUninitialize();
    if (hMutex) CloseHandle(hMutex);
    return (int)msg.wParam;
}

static bool GetClientSizeF(HWND hWnd, float& outW, float& outH)
{
    RECT rc{};
    if (!GetClientRect(hWnd, &rc)) return false;
    outW = float(rc.right - rc.left);
    outH = float(rc.bottom - rc.top);
    return outW > 0.f && outH > 0.f;
}

static bool PanFromOffsets(HWND hWnd, float zoom, float offsetX, float offsetY, float imgW, float imgH, float& outPanX, float& outPanY)
{
    float winW = 0.f, winH = 0.f;
    if (!GetClientSizeF(hWnd, winW, winH) || zoom <= 0.00001f) return false;

    const float imgCenterX = offsetX + (imgW * zoom * 0.5f);
    const float imgCenterY = offsetY + (imgH * zoom * 0.5f);

    outPanX = imgCenterX - (winW * 0.5f);
    outPanY = imgCenterY - (winH * 0.5f);
    return true;
}

static bool OffsetsFromPan(HWND hWnd, float zoom, float imgW, float imgH, float panX, float panY, float& outOffsetX, float& outOffsetY)
{
    float winW = 0.f, winH = 0.f;
    if (!GetClientSizeF(hWnd, winW, winH) || zoom <= 0.00001f) return false;

    outOffsetX = (winW * 0.5f) + panX - (imgW * zoom * 0.5f);
    outOffsetY = (winH * 0.5f) + panY - (imgH * zoom * 0.5f);
    return true;
}

int GetMonitorRefreshRate(HWND hWnd)
{
    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);

    MONITORINFOEX monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfo(hMonitor, &monitorInfo);

    DEVMODE dm = {};
    dm.dmSize = sizeof(dm);

    if (EnumDisplaySettings(monitorInfo.szDevice,
                            ENUM_CURRENT_SETTINGS,
                            &dm))
    {
        int refreshRate = dm.dmDisplayFrequency;
        return refreshRate;
    }
    return 60; // Default to 60 Hz if we can't get the actual refresh rate
}

bool LoadDefaultBackgroundFromResource()
{
    if (g_wicDefaultBackground)
        return true;

    HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCE(IDR_DEFAULT_BKG_JPG), L"JPG");
    if (!hRes)
        return false;

    HGLOBAL hData = LoadResource(hInst, hRes);
    if (!hData)
        return false;

    void* pData = LockResource(hData);
    DWORD size = SizeofResource(hInst, hRes);

    ComPtr<IWICStream> stream;
    if (FAILED(g_wicFactory->CreateStream(&stream)))
        return false;

    if (FAILED(stream->InitializeFromMemory(
        reinterpret_cast<BYTE*>(pData),
        size)))
        return false;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(g_wicFactory->CreateDecoderFromStream(
        stream.Get(),
        nullptr,
        WICDecodeMetadataCacheOnLoad,
        &decoder)))
        return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)))
        return false;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(g_wicFactory->CreateFormatConverter(&converter)))
        return false;

    if (FAILED(converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom)))
        return false;

    g_wicDefaultBackground = converter;

    return true;
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
        // Ordinal 135 — SetPreferredAppMode(1 = AllowDark)
        using FnSetPreferredAppMode = int (WINAPI*)(int);
        if (auto fn = reinterpret_cast<FnSetPreferredAppMode>(
                GetProcAddress(ux, MAKEINTRESOURCEA(135))))
            fn(1);

        // Ordinal 133 — AllowDarkModeForWindow(hwnd, true)
        using FnAllowDark = bool (WINAPI*)(HWND, bool);
        if (auto fn = reinterpret_cast<FnAllowDark>(
                GetProcAddress(ux, MAKEINTRESOURCEA(133))))
            fn(hwnd, true);

        // Ordinal 136 — FlushMenuThemes() — applies changes immediately
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

// A plain black popup window used to hide D3D device recreation.
// No D2D involved — just a black HBRUSH painted by DefWindowProc.
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
    if (g_blackCoverWindow) return;

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromWindow(g_mainWindow, MONITOR_DEFAULTTONEAREST), &mi);
    RECT rc = mi.rcMonitor;

    g_blackCoverWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"BlackCoverClass", L"", WS_POPUP,
        rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr);

    if (g_blackCoverWindow)
    {
        ShowWindow(g_blackCoverWindow, SW_SHOWNOACTIVATE);
        UpdateWindow(g_blackCoverWindow);
        // Block until DWM has composited this window onto the screen.
        // Without this, the cover may not be visible before we destroy the render target.
        DwmFlush();
    }
}

void DestroyBlackCoverWindow()
{
    if (g_blackCoverWindow)
    {
        DestroyWindow(g_blackCoverWindow);
        g_blackCoverWindow = nullptr;
    }
}

void DiscardDeviceResources()
{
    // D2D bitmaps
    g_d2dBitmap.Reset();
    g_backgroundBitmap.Reset();
    g_defaultBackgroundBitmap.Reset();
    g_d2dTargetBitmap.Reset();
    g_slideshowBgBitmap.Reset();
    g_prevSlideshowBgBitmap.Reset();
    g_prevD2DBitmap.Reset();

    // D2D context
    g_renderTarget.Reset();

    // DXGI / D3D
    g_swapChain.Reset();
    g_d3dContext.Reset();
    g_d3dDevice.Reset();
    g_d2dDevice.Reset();
    g_shadowEffect.Reset();
    g_blurEffect.Reset();
    g_renderTargetWindow = nullptr;
}

void UpdateEngine(float dt)
{
    LONG now = GetTickCount64();

    if (g_isAnimatedGif && !g_gifFrames.empty())
    {
        if (now - g_lastGifFrameTime >= g_gifFrameDelays[g_currentGifFrame])
        {
            g_currentGifFrame =
                (g_currentGifFrame + 1) % g_gifFrames.size();

            g_wicBitmapSource = g_gifFrames[g_currentGifFrame];
            RecreateImageBitmap();

            g_lastGifFrameTime = now;
        }
    }

    if (std::fabs(g_targetRotationAngle - g_imageRotationAngle) > 0.01f)
    {
        float delta = g_targetRotationAngle - g_imageRotationAngle;
        float step = g_rotationSpeed * dt;
        if (std::fabs(step) > std::fabs(delta))
            step = delta;
        else
            step *= (delta > 0 ? 1.f : -1.f);

        g_imageRotationAngle += step;
    }
    float overlayTarget = g_isExiting ? 0.0f : 1.0f;
    g_overlayAlpha += (overlayTarget - g_overlayAlpha) * 0.15f;

    // ---- Slideshow pre-fade (current view → black, then window transition) ----
    // g_slideshowPreFade stays true until Render() confirms the overlay's first Present().
    if (g_slideshowPreFade && !g_isSlideshowMode)
    {
        // Phase 1: fade the current render target to black (slow ease-out)
        g_slideshowPreFadeAlpha += (1.0f - g_slideshowPreFadeAlpha) * 0.04f;

        if (g_slideshowPreFadeAlpha > 0.995f)
        {
            g_slideshowPreFadeAlpha = 1.0f;

            // Phase 2: screen is provably black in D2D — now raise the cover
            // window and flush DWM so it is physically on screen before we
            // destroy the render target.
            CreateBlackCoverWindow();

            if (g_isFullscreen) ExitFullscreen();
            g_isSlideshowMode          = true;
            g_prevD2DBitmap.Reset();
            g_prevSlideshowBgBitmap.Reset();
            g_slideshowTransitionAlpha = 0.0f;
            g_slideshowTargetAlpha     = 1.0f;
            EnterFullscreen(false, true);
            SetTimer(g_mainWindow, SLIDESHOW_TIMER_ID, SLIDESHOW_INTERVAL_MS, nullptr);
            // g_slideshowPreFade remains true — Render() clears it after first Present().
        }
    }

    // ---- Slideshow cross-fade (image-to-image transition) ----
    if (g_isSlideshowMode && g_slideshowTransitionAlpha < g_slideshowTargetAlpha)
    {
        g_slideshowTransitionAlpha +=
            (g_slideshowTargetAlpha - g_slideshowTransitionAlpha) * 0.02f;

        if (g_slideshowTransitionAlpha > 0.995f)
        {
            g_slideshowTransitionAlpha = 1.0f;
            g_prevD2DBitmap.Reset();
            g_prevSlideshowBgBitmap.Reset();
        }
    }

    g_zoom     += (g_targetZoom     - g_zoom)     * g_smooth;
    g_offsetX  += (g_targetOffsetX  - g_offsetX)  * g_smooth;
    g_offsetY  += (g_targetOffsetY  - g_offsetY)  * g_smooth;

    HWND renderWindow = (g_isFullscreen && g_overlayWindow)
        ? g_overlayWindow
        : g_mainWindow;

    // Update button animations
    static auto previousTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - previousTime).count();
    previousTime = currentTime;
    for (auto& [id, btn] : g_buttons)
        btn.Update(deltaTime);

    for (auto& [id, txt] : g_textBoxes)
        txt.UpdateVisibility();

    InvalidateRect(renderWindow, nullptr, FALSE);

    if (g_isExiting &&
        std::fabs(g_zoom - g_targetZoom) < 0.0005f &&
        g_overlayAlpha < 0.01f)
    {
        if (g_overlayWindow)
        {
            DestroyWindow(g_mainWindow);
            g_mainWindow = nullptr;
            DestroyWindow(g_overlayWindow);
            g_overlayWindow = nullptr;
        }

        PostQuitMessage(0);
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
           ext == L".webp";
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // Store instance handle in our global variable

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    // Use 50% of screen height
    int windowHeight = screenH / 2;

    // Default background aspect ratio (3:2)
    float aspect = 1536.0f / 1024.0f;

    // Compute width from height
    int windowWidth = (int)(windowHeight * aspect);

    HWND hWnd = CreateWindowExW(
        0,
        szWindowClass,
        szTitle,
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
    g_mainWindow = hWnd;
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

    if (!g_launchedWithFile)
    {
        ShowWindow(hWnd, nCmdShow);
        UpdateWindow(hWnd);
    }
    else
    {
        // We'll enter fullscreen once the command-line file loads successfully.
        ShowWindow(hWnd, SW_HIDE);
    }

    g_lastFrameTime = GetTickCount64();
    return TRUE;
}

HWND CreateOverlayWindow(HWND parent)
{
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(
        MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST),
        &mi);

    // Slideshow covers the taskbar; normal fullscreen only covers the work area.
    RECT rc = g_isSlideshowMode ? mi.rcMonitor : mi.rcWork;

    DWORD exStyle = WS_EX_TOOLWINDOW;
    if (g_isSlideshowMode)
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
        hInst,
        nullptr);

    // Important: don't force an immediate paint here. We'll show it once device resources are ready.
    return overlay;
}

bool CreateDefaultBackgroundBitmap()
{
    if (!g_renderTarget || !g_wicDefaultBackground)
        return false;

    g_defaultBackgroundBitmap.Reset();

    return SUCCEEDED(
        g_renderTarget->CreateBitmapFromWicBitmap(
            g_wicDefaultBackground.Get(),
            nullptr,
            g_defaultBackgroundBitmap.GetAddressOf()
        )
    );
}

void SetZoomCentered(float newZoom, HWND hWnd, bool instant, bool preserveCenter)
{
    if (!g_d2dBitmap) return;

    RECT rc;
    GetClientRect(hWnd, &rc);
    float windowWidth  = float(rc.right - rc.left);
    float windowHeight = float(rc.bottom - rc.top);

    D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

    if (!preserveCenter)
    {
        // Always center in window
        g_targetOffsetX = (windowWidth  - imgSize.width  * newZoom) * 0.5f;
        g_targetOffsetY = (windowHeight - imgSize.height * newZoom) * 0.5f;

        UpdateTargetZoom(newZoom);

        if (instant)
        {
            g_zoom = newZoom;
            g_offsetX = g_targetOffsetX;
            g_offsetY = g_targetOffsetY;
        }

        return;
    }

    // ---- Existing behavior: preserve current on-screen center ----

    if (instant)
    {
        g_zoom = newZoom;
        UpdateTargetZoom(newZoom);

        g_offsetX = (windowWidth  - imgSize.width  * newZoom) * 0.5f;
        g_offsetY = (windowHeight - imgSize.height * newZoom) * 0.5f;

        g_targetOffsetX = g_offsetX;
        g_targetOffsetY = g_offsetY;
        return;
    }

    float centerX = g_offsetX + imgSize.width  * g_zoom * 0.5f;
    float centerY = g_offsetY + imgSize.height * g_zoom * 0.5f;

    g_targetOffsetX = centerX - imgSize.width  * newZoom * 0.5f;
    g_targetOffsetY = centerY - imgSize.height * newZoom * 0.5f;

    UpdateTargetZoom(newZoom);
}

void CaptureDesktop(HWND hWnd)
{
    g_wicBackground.Reset();

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(
        MonitorFromWindow(hWnd ? hWnd : g_mainWindow, MONITOR_DEFAULTTONEAREST),
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

    g_wicFactory->CreateBitmapFromHBITMAP(
        hBitmap,
        nullptr,
        WICBitmapIgnoreAlpha,
        g_wicBackground.GetAddressOf());

    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

void FitToWindowRelative(HWND hWnd, float zoom, bool preserveCenter = true)
{
    if (!g_d2dBitmap) return;

    RECT rc;
    GetClientRect(hWnd, &rc);

    float windowWidth  = (float)(rc.right - rc.left);
    float windowHeight = (float)(rc.bottom - rc.top);

    D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

    float scaleX = windowWidth  / imgSize.width;
    float scaleY = windowHeight / imgSize.height;

    float scale = min(scaleX, scaleY)*zoom;

    SetZoomCentered(scale, hWnd, false, preserveCenter);
}

bool CreateBackgroundBitmap()
{
    if (!g_renderTarget || !g_wicBackground)
        return false;

    g_backgroundBitmap.Reset();

    HRESULT hr = g_renderTarget->CreateBitmapFromWicBitmap(
        g_wicBackground.Get(),
        nullptr,
        g_backgroundBitmap.GetAddressOf()
    );

    if (FAILED(hr))
    {
        OutputDebugString(L"CreateBackgroundBitmap failed\n");
        return false;
    }

    return true;
}

void UpdateTargetZoom(float newZoom)
{
    if (!g_d2dBitmap) return;
    g_targetZoom = newZoom;
}

// -----------------------------------------------------------------------
// Builds a small (25 %) D2D bitmap that has been blurred through the
// Gaussian-blur effect and stored off-screen, ready for DrawBitmap.
// Must be called outside BeginDraw/EndDraw (or before BeginDraw in Render).
// -----------------------------------------------------------------------
void CreateSlideshowBgBitmap()
{
    g_slideshowBgBitmap.Reset();

    if (!g_renderTarget || !g_wicBitmapSource || !g_blurEffect || !g_wicFactory)
        return;

    UINT fullW = 0, fullH = 0;
    if (FAILED(g_wicBitmapSource->GetSize(&fullW, &fullH)) || fullW == 0 || fullH == 0)
        return;

    // ----- 1. Downscale to a fixed max size so blur always produces
    //          an indistinct color wash regardless of source resolution -----
    const UINT TARGET_MAX = 400u;
    float scaleRatio = min((float)TARGET_MAX / fullW, (float)TARGET_MAX / fullH);
    const UINT smallW = max(1u, (UINT)(fullW * scaleRatio));
    const UINT smallH = max(1u, (UINT)(fullH * scaleRatio));

    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(g_wicFactory->CreateBitmapScaler(&scaler))) return;
    if (FAILED(scaler->Initialize(g_wicBitmapSource.Get(), smallW, smallH,
                                   WICBitmapInterpolationModeHighQualityCubic))) return;

    ComPtr<IWICFormatConverter> conv;
    if (FAILED(g_wicFactory->CreateFormatConverter(&conv))) return;
    if (FAILED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                 WICBitmapDitherTypeNone, nullptr, 0.f,
                                 WICBitmapPaletteTypeCustom))) return;

    // ----- 2. Create small D2D source bitmap -----
    ComPtr<ID2D1Bitmap> smallBmp;
    if (FAILED(g_renderTarget->CreateBitmapFromWicBitmap(
            conv.Get(), nullptr, &smallBmp))) return;

    // ----- 3. Create off-screen render target at the same small size -----
    D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap1> offscreenBmp;
    if (FAILED(g_renderTarget->CreateBitmap(
            D2D1::SizeU(smallW, smallH), nullptr, 0, bmpProps, &offscreenBmp))) return;

    // ----- 4. Render blur to the off-screen bitmap -----
    g_blurEffect->SetInput(0, smallBmp.Get());

    ComPtr<ID2D1Image> prevTarget;
    g_renderTarget->GetTarget(&prevTarget);
    g_renderTarget->SetTarget(offscreenBmp.Get());

    g_renderTarget->BeginDraw();
    g_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));
    g_renderTarget->DrawImage(g_blurEffect.Get(), D2D1::Point2F(0.f, 0.f));
    g_renderTarget->EndDraw();

    g_renderTarget->SetTarget(prevTarget.Get());

    // ----- 5. Store — ID2D1Bitmap1 is a ID2D1Bitmap -----
    g_slideshowBgBitmap = offscreenBmp;
}

void EnterSlideshowMode()
{
    if (g_isSlideshowMode || g_slideshowPreFade) return;
    // Only start the fade-to-black. UpdateEngine handles everything else once
    // the screen is fully black, so no window transitions happen visibly.
    g_slideshowPreFade      = true;
    g_slideshowPreFadeAlpha = 0.0f;
}

void ExitSlideshowMode()
{
    DestroyBlackCoverWindow();

    if (g_slideshowPreFade)
    {
        // Cancelled mid-fade before the slideshow actually started.
        g_slideshowPreFade      = false;
        g_slideshowPreFadeAlpha = 0.0f;
        return;
    }

    if (!g_isSlideshowMode) return;

    KillTimer(g_mainWindow, SLIDESHOW_TIMER_ID);

    // Clear transition state first so Render doesn't try to use
    // bitmaps that are about to be discarded.
    g_prevD2DBitmap.Reset();
    g_prevSlideshowBgBitmap.Reset();
    g_slideshowTransitionAlpha = 1.0f;

    g_isSlideshowMode = false;
    ExitFullscreen();
}

void EnterFullscreen(bool preserveView = false, bool needsDelay = true)
{
    // Prevent duplicate first-stage calls
    if (g_isFullscreen && needsDelay) return;

    // ----------------------------------------
    // STAGE 1 — make main window fully transparent and wait for DWM
    // ----------------------------------------
    if (needsDelay)
    {
        g_isFullscreen = true;

        SetWindowLong(g_mainWindow, GWL_EXSTYLE,
            GetWindowLong(g_mainWindow, GWL_EXSTYLE) | WS_EX_LAYERED);

        SetLayeredWindowAttributes(g_mainWindow, 0, 0, LWA_ALPHA);
        UpdateWindow(g_mainWindow);

        SetTimer(g_mainWindow, 8888, 150, nullptr);
        g_pendingPreserveView = preserveView;
        return;
    }

    // If we are preserving, compute whether the current view is "centered"
    // using pan (imageCenter - windowCenter). pan==0 means centered for any window size.
    float panX = 0.f, panY = 0.f;
    float targetPanX = 0.f, targetPanY = 0.f;
    bool havePan = false;
    bool viewIsCentered = false;

    if (preserveView && g_d2dBitmap)
    {
        D2D1_SIZE_F imgSizeMain = g_d2dBitmap->GetSize();

        havePan =
            PanFromOffsets(g_mainWindow, g_zoom, g_offsetX, g_offsetY,
                           imgSizeMain.width, imgSizeMain.height, panX, panY) &&
            PanFromOffsets(g_mainWindow, g_targetZoom, g_targetOffsetX, g_targetOffsetY,
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
    // STAGE 2 — actual fullscreen overlay creation
    // ----------------------------------------
    CaptureDesktop(g_mainWindow);

    // Switching render targets -> discard device resources
    DiscardDeviceResources();

    g_overlayWindow = CreateOverlayWindow(g_mainWindow);

    // Create device resources for overlay now
    CreateRenderTarget(g_overlayWindow);

    if (g_renderTarget)
    {
        D2D1_SIZE_F size = g_renderTarget->GetSize();
        g_uiPixelScale = min(size.width, size.height);
    }

    CreateBackgroundBitmap();
    RecreateImageBitmap();
    InitializeButtons();
    InitializeImageInfoLabel();
        

    g_overlayAlpha = 0.0f;

    if (!preserveView)
    {
        g_needsFullscreenInit = true;
    }
    else
    {
        g_needsFullscreenInit = false;

        if (viewIsCentered && havePan && g_d2dBitmap)
        {
            // Center-relative preservation:
            // recompute absolute offsets for the new render target from pan.
            D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

            OffsetsFromPan(g_overlayWindow, g_zoom,
                           imgSize.width, imgSize.height,
                           panX, panY, g_offsetX, g_offsetY);

            OffsetsFromPan(g_overlayWindow, g_targetZoom,
                           imgSize.width, imgSize.height,
                           targetPanX, targetPanY, g_targetOffsetX, g_targetOffsetY);
        }
        else
        {
            // Screen-anchored preservation (your original behavior):
            // Convert image position from main window → screen → overlay client
            POINT pt = { (LONG)g_offsetX, (LONG)g_offsetY };
            ClientToScreen(g_mainWindow, &pt);
            ScreenToClient(g_overlayWindow, &pt);

            UpdateTargetZoom(g_zoom);
            g_offsetX = (float)pt.x;
            g_offsetY = (float)pt.y;
            g_targetOffsetX = g_offsetX;
            g_targetOffsetY = g_offsetY;
        }
    }

    ShowWindow(g_overlayWindow, SW_SHOW);
    UpdateWindow(g_overlayWindow);

    g_fullScreenInitDone = true;
    g_renderTargetWindow = g_overlayWindow;
}

void ExitFullscreen()
{
    if (!g_isFullscreen)
        return;

    // Kill the zoom-display timer and hide the textbox in case it was visible
    KillTimer(g_mainWindow, 666);
    if (!g_textBoxes.empty())
        g_textBoxes[TEXTBOX_ZOOM_INPUT].SetForcedVisibility(false);

    if (!g_d2dBitmap)
    {
        // No bitmap? Just bail out safely.
        g_isFullscreen = false;
        if (g_overlayWindow)
        {
            DestroyWindow(g_overlayWindow);
            g_overlayWindow = nullptr;
        }
        return;
    }

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

    // Account for the window expanding around the image
    offsetCorrectionX += extraW * 0.5f;
    offsetCorrectionY += extraH * 0.5f;

    // Adjust new position using visible region
    int newX = overlayRect.left + (int)visibleLeft;
    int newY = overlayRect.top  + (int)visibleTop;

    // Tear down the overlay first
    g_isFullscreen = false;
    g_needsFullscreenInit = false;
    g_overlayAlpha = 1.0f;

    if (g_overlayWindow)
    {
        DestroyWindow(g_overlayWindow);
        g_overlayWindow = nullptr;
    }

    // Discard device-dependent resources from the overlay render target.
    // This is crucial to avoid drawing one frame using bitmaps created on the old render target.
    DiscardDeviceResources();

    // Calculate window rect BEFORE showing.
    // FALSE = no menu bar (we removed it with SetMenu(nullptr)).
    RECT adj = { 0, 0, (LONG)clientWidth, (LONG)clientHeight };
    AdjustWindowRect(&adj, WS_OVERLAPPEDWINDOW, FALSE);

    int nonClientOffsetY = -adj.top;
    int nonClientOffsetX = -adj.left;

    int winX = newX - nonClientOffsetX;
    int winY = newY - nonClientOffsetY;
    int winW = adj.right  - adj.left;
    int winH = adj.bottom - adj.top;

    // Clamp so the window stays fully within the monitor's work area.
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromPoint({ newX, newY }, MONITOR_DEFAULTTONEAREST), &mi);
    RECT& wa = mi.rcWork;

    if (winX + winW > wa.right)  winX = wa.right  - winW;
    if (winY + winH > wa.bottom) winY = wa.bottom - winH;
    if (winX < wa.left)          winX = wa.left;
    if (winY < wa.top)           winY = wa.top;

    // Resize/move window WITHOUT triggering redraw yet (prevents a "wrong size" first frame)
    SetWindowPos(
        g_mainWindow,
        nullptr,
        winX, winY, winW, winH,
        SWP_NOZORDER | SWP_NOACTIVATE
    );

    // Snap offsets/targets to final state for the new (cropped) client area
    int preClampWinX = winX;
    int preClampWinY = winY;

    if (winX + winW > wa.right)  winX = wa.right  - winW;
    if (winY + winH > wa.bottom) winY = wa.bottom - winH;
    if (winX < wa.left)          winX = wa.left;
    if (winY < wa.top)           winY = wa.top;

    int clampShiftX = winX - preClampWinX;
    int clampShiftY = winY - preClampWinY;

    g_offsetX = offsetCorrectionX - (float)clampShiftX;
    g_offsetY = offsetCorrectionY - (float)clampShiftY;

    g_targetOffsetX = g_offsetX;
    g_targetOffsetY = g_offsetY;

    UpdateTargetZoom(g_zoom);

    // Recreate device resources for the main window BEFORE making it visible again.
    CreateRenderTarget(g_mainWindow);
    RecreateImageBitmap();

    // Restore main window visibility (remove layered transparency last)
    LONG ex = GetWindowLong(g_mainWindow, GWL_EXSTYLE);
    if (ex & WS_EX_LAYERED)
    {
        SetLayeredWindowAttributes(g_mainWindow, 0, 255, LWA_ALPHA);
        SetWindowLong(g_mainWindow, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    }

    ShowWindow(g_mainWindow, SW_SHOW);

    // Now redraw with correct size/zoom on the very first visible frame
    RedrawWindow(g_mainWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

    // Update button and text layouts for the new render target
    for (auto& [id, btn] : g_buttons)
        btn.UpdateLayout(g_renderTarget.Get());

    for (auto& [key, textbox] : g_textBoxes)
        textbox.UpdateLayout(g_renderTarget.Get());

    g_fullScreenInitDone = false;
    g_renderTargetWindow = g_mainWindow;
    if (g_isSlideshowMode)
    {
        ExitSlideshowMode();
    }
}

void CreateRenderTarget(HWND hWnd)
{
    if (g_renderTarget)
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
        &g_d3dDevice,
        &featureLevel,
        &g_d3dContext);

    if (FAILED(hr))
        return;

    // 2. Get DXGI device
    hr = g_d3dDevice.As(&g_dxgiDevice);
    if (FAILED(hr))
        return;

    // 3. Create D2D device + context
    hr = g_d2dFactory->CreateDevice(g_dxgiDevice.Get(), &g_d2dDevice);
    if (FAILED(hr))
        return;

    hr = g_d2dDevice->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
        &g_renderTarget);   // your DeviceContext
    if (FAILED(hr))
        return;

    // 4. Create DXGI factory
    ComPtr<IDXGIAdapter> adapter;
    hr = g_dxgiDevice->GetAdapter(&adapter);
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
        g_d3dDevice.Get(),
        hWnd,
        &desc,
        nullptr,
        nullptr,
        &g_swapChain);

    if (FAILED(hr))
        return;

    // 6. Create target bitmap
    ComPtr<IDXGISurface> surface;
    hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr))
        return;

    D2D1_BITMAP_PROPERTIES1 props =
        D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET |
            D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED));

    hr = g_renderTarget->CreateBitmapFromDxgiSurface(
        surface.Get(),
        &props,
        &g_d2dTargetBitmap);

    if (FAILED(hr))
        return;
    g_renderTarget->SetTarget(g_d2dTargetBitmap.Get());

    if (!g_shadowEffect)
    {
        HRESULT hr = g_renderTarget->CreateEffect(
            CLSID_D2D1Shadow,
            &g_shadowEffect);

        if (SUCCEEDED(hr))
        {
            g_shadowEffect->SetValue(
                D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION,
                25.0f);   // softness
        }
    }

    if (!g_blurEffect)
    {
        HRESULT hr = g_renderTarget->CreateEffect(
            CLSID_D2D1GaussianBlur,
            &g_blurEffect);

        if (SUCCEEDED(hr))
        {
            // 20 px on a 25 %-size image ≈ 80 px of blur on the original
            g_blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 20.0f);
            // Mirror edges so the fill has no dark border
            g_blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
        }
    }
}

void RecreateImageBitmap()
{
    if (!g_wicBitmapSource || !g_renderTarget)
        return;

    g_d2dBitmap.Reset();

    // D3D11 maximum texture dimension is 16384. If the source exceeds this,
    // downscale via WIC before creating the D2D bitmap.
    const UINT D3D_MAX = 16384u;
    UINT srcW = 0, srcH = 0;
    g_wicBitmapSource->GetSize(&srcW, &srcH);

    ComPtr<IWICBitmapSource> bitmapSourceForD2D = g_wicBitmapSource;

    if (srcW > D3D_MAX || srcH > D3D_MAX)
    {
        float ratio = min((float)D3D_MAX / srcW, (float)D3D_MAX / srcH);
        UINT scaledW = max(1u, (UINT)(srcW * ratio));
        UINT scaledH = max(1u, (UINT)(srcH * ratio));

        ComPtr<IWICBitmapScaler> scaler;
        ComPtr<IWICFormatConverter> conv;
        if (SUCCEEDED(g_wicFactory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(g_wicBitmapSource.Get(), scaledW, scaledH,
                                         WICBitmapInterpolationModeHighQualityCubic)) &&
            SUCCEEDED(g_wicFactory->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                        WICBitmapDitherTypeNone, nullptr, 0.f,
                                        WICBitmapPaletteTypeCustom)))
        {
            bitmapSourceForD2D = conv;
        }
    }

    HRESULT hr = g_renderTarget->CreateBitmapFromWicBitmap(
        bitmapSourceForD2D.Get(),
        nullptr,
        g_d2dBitmap.GetAddressOf()
    );

    if (FAILED(hr))
    {           
        OutputDebugString(L"RecreateImageBitmap failed\n");
        g_d2dBitmap.Reset();
    }
}

void InitializeImageLayout(HWND hWnd, bool hard = false)
{
    if (!g_d2dBitmap) return;
    if (g_restoredStateThisLoad && !g_isSlideshowMode)
    return;
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
    if (g_isSlideshowMode || imgSize.width > windowWidth || imgSize.height > windowHeight)
    {
        float scaleX = (windowWidth  * 0.95f) / imgSize.width;
        float scaleY = (windowHeight * 0.95f) / imgSize.height;

        scale = min(scaleX, scaleY);
    }

    float windowCenterX = windowWidth / 2.0f;
    float windowCenterY = windowHeight / 2.0f;

    if (hard)
    {
        g_zoom = scale;          // Hard clamp to final scale

        UpdateTargetZoom(scale);

        // Target offsets for final scale
        g_targetOffsetX = windowCenterX - (imgSize.width  * g_targetZoom) / 2.0f;
        g_targetOffsetY = windowCenterY - (imgSize.height * g_targetZoom) / 2.0f;

        // Start offsets at final position since we're hard centering
        g_offsetX = g_targetOffsetX;
        g_offsetY = g_targetOffsetY;
    }
    else
    {
        g_zoom = 0.05f;             // Start tiny
        UpdateTargetZoom(scale);    // Animate to final scale

        // Start offsets so that image center is centered at window center at tiny zoom
        g_offsetX = windowCenterX - (imgSize.width  * g_zoom) / 2.0f;
        g_offsetY = windowCenterY - (imgSize.height * g_zoom) / 2.0f;

        // Target offsets for final scale
        g_targetOffsetX = windowCenterX - (imgSize.width  * g_targetZoom) / 2.0f;
        g_targetOffsetY = windowCenterY - (imgSize.height * g_targetZoom) / 2.0f;
    }
    // Reset rotation for new images
    g_imageRotationAngle = 0.0f;
    g_targetRotationAngle = 0.0f;
}

void InitializeMenuButtons()
{
    // -------------------------
    // Open File Button
    // -------------------------
    AnimatedButton::ActivationZone topLeftActivationZone;
    topLeftActivationZone.left   = 0.0f;
    topLeftActivationZone.top    = 0.0f;
    topLeftActivationZone.right  = 0.2f;
    topLeftActivationZone.bottom = 0.2f;

    AnimatedButton::Config openConfig;
    D2D1_SIZE_F size = g_renderTarget->GetSize();
    float width = 0.07f;
    float height = 0.07f;
    openConfig.layout.x.value = 0.05f*float(size.height)/float(size.width);
    openConfig.layout.y.value = 0.05f;
    openConfig.layout.x.anchor = AnimatedButton::Anchor::OffsetFromStart;
    openConfig.layout.y.anchor = AnimatedButton::Anchor::OffsetFromStart;
    openConfig.layout.x.mode = AnimatedButton::PosMode::Normalized;
    openConfig.layout.y.mode = AnimatedButton::PosMode::Normalized;
    openConfig.layout.width = width;
    openConfig.layout.height = height;
    openConfig.fontSize = 0.04f;
    openConfig.text = L"\U0001F4C2";
    openConfig.layout.activationZone = topLeftActivationZone;
    openConfig.layout.referenceWidth  = size.width;
    openConfig.layout.referenceHeight = size.height;
    openConfig.layout.uiPixelScale = min(size.width, size.height);
    openConfig.holdEnabled = false;
    openConfig.pressAnimation = false;
    g_buttons[BUTTON_OPEN].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        openConfig,
        []()
        {
            OpenImageFile(g_mainWindow);
        });
    g_buttons[BUTTON_OPEN].UpdateLayout(g_renderTarget.Get());
    g_buttons[BUTTON_OPEN].SetForcedVisibility(true);
    

    // -------------------------
    // Help Button
    // -------------------------

    openConfig.layout.x.value += 0.056;
    openConfig.text = L"\U0000FF1F";
    g_buttons[BUTTON_HELP].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        openConfig,
        []()
        {
            { DialogBox(hInst, MAKEINTRESOURCE(IDD_COMMANDBOX),  g_mainWindow, About); }
        });
    g_buttons[BUTTON_HELP].UpdateLayout(g_renderTarget.Get());
    g_buttons[BUTTON_HELP].SetForcedVisibility(true);

}

void InitializeButtons()
{
    //g_buttons.clear();
    g_buttons[BUTTON_OPEN].SetForcedVisibility(false);
    g_buttons[BUTTON_HELP].SetForcedVisibility(false);
    float width = 0.025f;
    float height = 0.025f;
    float fontSize = 0.011f;

    // -------------------------
    // Bottom activation zone
    // -------------------------
    AnimatedButton::ActivationZone bottomActivationZone;
    bottomActivationZone.left   = 0.0f;
    bottomActivationZone.top    = 0.8f;
    bottomActivationZone.right  = 1.0f;
    bottomActivationZone.bottom = 1.0f;

    // -------------------------
    // Zoom 1:1 Button
    // -------------------------
    AnimatedButton::Config buttonConfig;

    buttonConfig.layout.x.value = 0.49f;
    buttonConfig.layout.x.anchor = AnimatedButton::Anchor::OffsetFromCenter;
    buttonConfig.layout.x.mode = AnimatedButton::PosMode::Normalized;
    buttonConfig.layout.y.value = 0.96f;
    buttonConfig.layout.y.anchor = AnimatedButton::Anchor::OffsetFromEnd;
    buttonConfig.layout.y.mode = AnimatedButton::PosMode::Normalized;
    buttonConfig.layout.width = width;
    buttonConfig.layout.height = height;
    buttonConfig.fontSize = fontSize * 1.1f;
    buttonConfig.text = L"1:1";
    buttonConfig.layout.activationZone = bottomActivationZone;
    buttonConfig.layout.uiPixelScale = g_uiPixelScale;
    D2D1_SIZE_F size = g_renderTarget->GetSize();

    buttonConfig.layout.referenceWidth  = size.width;
    buttonConfig.layout.referenceHeight = size.height;
    buttonConfig.layout.uiPixelScale    = min(size.width, size.height);

    g_buttons[BUTTON_ZOOM_11].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            MakeZoomVisible(g_renderTargetWindow);
            SetZoomCentered(1.0f, g_renderTargetWindow, false, false);
        });
    g_buttons[BUTTON_ZOOM_11].UpdateLayout(g_renderTarget.Get());

 
    // -------------------------
    // Slide-show Button
    // -------------------------
    buttonConfig.layout.x.value = 0.51f;
    buttonConfig.fontSize = fontSize;
    buttonConfig.text = L"\u25B6";

    g_buttons[BUTTON_SLIDESHOW].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            EnterSlideshowMode();
        });
    g_buttons[BUTTON_SLIDESHOW].UpdateLayout(g_renderTarget.Get());


    // -------------------------
    // Zoom In Button
    // -------------------------
    buttonConfig.layout.x.value = 0.53f;
    buttonConfig.fontSize = fontSize;
    buttonConfig.text = L"\u2795";

    g_buttons[BUTTON_ZOOM_IN].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            ZoomIntoImage(g_renderTargetWindow, 250, nullptr);
            MakeZoomVisible(g_renderTargetWindow);

        });
    g_buttons[BUTTON_ZOOM_IN].UpdateLayout(g_renderTarget.Get());

    // -------------------------
    // Zoom Out Button
    // -------------------------

    buttonConfig.layout.x.value = 0.47f;
    buttonConfig.text = L"\u2796";

    g_buttons[BUTTON_ZOOM_OUT].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            ZoomIntoImage(g_renderTargetWindow, -250, nullptr);
            MakeZoomVisible(g_renderTargetWindow);
        });
    g_buttons[BUTTON_ZOOM_OUT].UpdateLayout(g_renderTarget.Get());

    // -------------------------
    // Rotate Left Button
    // -------------------------

    buttonConfig.layout.x.value = 0.45f;
    buttonConfig.fontSize = fontSize * 1.2f;
    buttonConfig.text = L"\u2B6F";

    g_buttons[BUTTON_ROTATE_LEFT].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            g_targetRotationAngle -= 90.0f;
        });
    g_buttons[BUTTON_ROTATE_LEFT].UpdateLayout(g_renderTarget.Get());

    // -------------------------
    // Rotate Right Button
    // -------------------------

    buttonConfig.layout.x.value = 0.55f;
    buttonConfig.text = L"\u2B6E";

    g_buttons[BUTTON_ROTATE_RIGHT].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            g_targetRotationAngle += 90.0f;
        });
    g_buttons[BUTTON_ROTATE_RIGHT].UpdateLayout(g_renderTarget.Get());

    // -------------------------
    // Previous Image Button
    // -------------------------
    buttonConfig.layout.x.value = 0.43f;
    buttonConfig.fontSize = fontSize * 1.3f;
    buttonConfig.text = L"\u2B9C";

    g_buttons[BUTTON_PREVIOUS].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            OpenPrevImage(g_renderTargetWindow);
            if (g_isSlideshowMode)
                SetTimer(g_mainWindow, SLIDESHOW_TIMER_ID, SLIDESHOW_INTERVAL_MS, nullptr);
        });
    g_buttons[BUTTON_PREVIOUS].UpdateLayout(g_renderTarget.Get());

    // -------------------------
    // Next Image Button
    // -------------------------

    buttonConfig.layout.x.value = 0.57f;
    buttonConfig.text = L"\u2B9E";
    
    g_buttons[BUTTON_NEXT].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            OpenNextImage(g_renderTargetWindow);
            if (g_isSlideshowMode)
                SetTimer(g_mainWindow, SLIDESHOW_TIMER_ID, SLIDESHOW_INTERVAL_MS, nullptr);
        });
    g_buttons[BUTTON_NEXT].UpdateLayout(g_renderTarget.Get());

    // -------------------------
    // Exit Button
    // -------------------------
    AnimatedButton::ActivationZone topRightActivationZone;
    topRightActivationZone.left   = 0.9f;
    topRightActivationZone.top    = 0.0f;
    topRightActivationZone.right  = 1.0f;
    topRightActivationZone.bottom = 0.1f;

    float top = 0.012f;
    float right = 1.0f - top * 9.0f / 16.0f;

    AnimatedButton::Config exitConfig;
    exitConfig.layout.x.value = right;
    exitConfig.layout.y.value = top;
    exitConfig.layout.x.anchor = AnimatedButton::Anchor::OffsetFromEnd;
    exitConfig.layout.y.anchor = AnimatedButton::Anchor::OffsetFromStart;
    exitConfig.layout.x.mode = AnimatedButton::PosMode::Normalized;
    exitConfig.layout.y.mode = AnimatedButton::PosMode::Normalized;
    exitConfig.layout.width = 0.036f;
    exitConfig.layout.height = 0.036f;
    exitConfig.fontSize = 0.016f;
    exitConfig.text = L"\u274C";
    exitConfig.layout.activationZone = topRightActivationZone;
    exitConfig.layout.referenceWidth  = size.width;
    exitConfig.layout.referenceHeight = size.height;
    exitConfig.layout.uiPixelScale = min(size.width, size.height);

    g_buttons[BUTTON_EXIT].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        exitConfig,
        []()
        {
            if (!g_d2dBitmap)
            {
                PostQuitMessage(0);
            }

            D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();
            float centerX = g_offsetX + imgSize.width  * g_zoom / 2.0f;
            float centerY = g_offsetY + imgSize.height * g_zoom / 2.0f;

            g_targetOffsetX = centerX - (imgSize.width * 0.05f) / 2.0f;
            g_targetOffsetY = centerY - (imgSize.height * 0.05f) / 2.0f;

            int refreshRate = GetMonitorRefreshRate(GetDesktopWindow());
            g_smooth = 2 * 0.18f / ((float)(refreshRate) / 60.0f);

            g_targetZoom = 0.0005f;
            g_isExiting = true;
        });
    g_buttons[BUTTON_EXIT].UpdateLayout(g_renderTarget.Get());
}

void InitializeImageInfoLabel()
{
    // Filename display.
    UITextBox::ActivationZone zone;
    zone.left   = 0.0f;
    zone.right  = 1.0f;
    zone.top    = 0.8f;
    zone.bottom = 1.0f;

    UITextBox::Config config;
    D2D1_SIZE_F size = g_renderTarget->GetSize();

    config.layout.referenceWidth  = size.width;
    config.layout.referenceHeight = size.height;
    config.layout.x.value = 0.5f;
    config.layout.x.anchor = UITextBox::Anchor::OffsetFromCenter;
    config.layout.x.mode = UITextBox::PosMode::Normalized;
    config.layout.y.value = 0.92f;
    config.layout.y.anchor = UITextBox::Anchor::OffsetFromEnd;
    config.layout.x.mode = UITextBox::PosMode::Normalized;

    config.relativeFontSize = 0.012f;
    config.layout.width  = 0.6f;      // wide enough for filenames
    config.layout.height = 0.05f;

    config.backgroundAlpha = 0.0f;   // no box
    config.isEditable = false;      // display only

    config.layout.activationZone = zone;

    config.layout.uiPixelScale = g_uiPixelScale;

    g_textBoxes[TEXTBOX_FILE_NAME].Initialize(
        g_dwriteFactory.Get(),
        g_renderTarget.Get(),
        config,
        nullptr);
    g_textBoxes[TEXTBOX_FILE_NAME].UpdateLayout(g_renderTarget.Get());

    // Zoom level display/input.
    zone.left   = 0.4f;
    zone.right  = 0.6f;
    zone.top    = 0.4f;
    zone.bottom = 0.6f;
    config.layout.activationZone = zone;
    config.layout.x.value = 0.5;
    config.layout.y.value = 0.5f;
    config.layout.y.anchor = UITextBox::Anchor::OffsetFromCenter;
    config.layout.width = 0.05f;
    config.layout.height = 0.025f;
    config.backgroundAlpha = 0.5f; // semi-transparent box for zoom display
    config.isEditable = true;
    config.inputMode = UITextBox::InputMode::NumericFloat;
    g_textBoxes[TEXTBOX_ZOOM_INPUT].Initialize(
        g_dwriteFactory.Get(),
        g_renderTarget.Get(),
        config,
        [](const std::wstring& text)
            {
              if (text.empty())
                    return;

                try
                {
                    float value = std::stof(text);
                    if (value < 1.0f)
                    {
                        value = 1.0f;
                    } 
                    else if (value > 10000.0f)
                    {
                        value = 10000.0f;
                    } 
                    
                    UpdateTargetZoom(value/100.0);
                    ZoomIntoImage(g_overlayWindow, 0, nullptr);
                    
                }
                catch (...)
                {
                    // invalid number → ignore
                }
            }
        );
    g_textBoxes[TEXTBOX_ZOOM_INPUT].UpdateLayout(g_renderTarget.Get());
}

void Render(HWND hWnd)
{
    // Only render into the currently active window to avoid accidentally
    // nuking/recreating device resources if the inactive window gets a stray WM_PAINT.
    HWND activeWindow = (g_isFullscreen && g_overlayWindow) ? g_overlayWindow : g_mainWindow;
    if (hWnd != activeWindow)
        return;

    CreateRenderTarget(hWnd);

    if (!g_renderTarget || !g_swapChain)
        return;

    // Recreate any missing device-dependent resources (device-loss safe).
    if (g_wicBitmapSource && !g_d2dBitmap)
        RecreateImageBitmap();

    if (!g_d2dBitmap && g_wicDefaultBackground && !g_defaultBackgroundBitmap)
        CreateDefaultBackgroundBitmap();

    if (g_isFullscreen && g_wicBackground && !g_backgroundBitmap)
        CreateBackgroundBitmap();

    // Slideshow blurred-bg: build lazily (off-screen render, must be before BeginDraw)
    if (g_wicBitmapSource && !g_slideshowBgBitmap && g_blurEffect)
        CreateSlideshowBgBitmap();

    g_renderTarget->BeginDraw();

    // Clear background
    if (g_isFullscreen)
        g_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0.6f));
    else
        g_renderTarget->Clear(D2D1::ColorF(0.4f, 0.4f, 0.4f));

    if (g_needsFullscreenInit)
    {
        // Ensure resources exist
        if (g_wicBitmapSource && !g_d2dBitmap)
            RecreateImageBitmap();
        if (g_wicBackground && !g_backgroundBitmap)
            CreateBackgroundBitmap();

        // Initialize layout AFTER everything exists
        InitializeImageLayout(hWnd, g_isSlideshowMode);

        g_needsFullscreenInit = false;
    }

    // ---- Background ----
    if (g_isSlideshowMode)
    {
        // Helper: draw a small bitmap stretched to cover the full screen at given opacity
        auto DrawCoverBg = [&](ID2D1Bitmap* bmp, float alpha)
        {
            if (!bmp || alpha <= 0.01f) return;

            D2D1_SIZE_F sz = bmp->GetSize();
            D2D1_SIZE_F rt = g_renderTarget->GetSize();

            float scaleX     = rt.width  / sz.width;
            float scaleY     = rt.height / sz.height;
            float coverScale = max(scaleX, scaleY);

            float destW = sz.width  * coverScale;
            float destH = sz.height * coverScale;
            float destX = (rt.width  - destW) * 0.5f;
            float destY = (rt.height - destH) * 0.5f;

            g_renderTarget->DrawBitmap(
                bmp,
                D2D1::RectF(destX, destY, destX + destW, destY + destH),
                alpha,
                D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        };

        // Fade out old blurred bg, fade in new one
        DrawCoverBg(g_prevSlideshowBgBitmap.Get(), 1.0f - g_slideshowTransitionAlpha);
        DrawCoverBg(g_slideshowBgBitmap.Get(),             g_slideshowTransitionAlpha);

        // Uniform dim overlay on top of the blurred fill
        D2D1_SIZE_F rt = g_renderTarget->GetSize();
        ComPtr<ID2D1SolidColorBrush> dimBrush;
        g_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.f, 0.f, 0.f, 0.45f * g_overlayAlpha), &dimBrush);
        if (dimBrush)
            g_renderTarget->FillRectangle(
                D2D1::RectF(0, 0, rt.width, rt.height), dimBrush.Get());
    }
    else if (g_isFullscreen && g_backgroundBitmap)
    {
        D2D1_SIZE_F size = g_backgroundBitmap->GetSize();

        g_renderTarget->DrawBitmap(
            g_backgroundBitmap.Get(),
            D2D1::RectF(0, 0, size.width, size.height),
            1.0f
        );

        ComPtr<ID2D1SolidColorBrush> brush;
        g_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0, 0, 0, 0.67f * g_overlayAlpha),
            brush.GetAddressOf()
        );

        if (brush)
        {
            g_renderTarget->FillRectangle(
                D2D1::RectF(0, 0, size.width, size.height),
                brush.Get()
            );
        }
    }    
    else if (!g_isFullscreen && g_slideshowBgBitmap)
    {
        // Blurred image fills the windowed background
        D2D1_SIZE_F sz = g_slideshowBgBitmap->GetSize();
        D2D1_SIZE_F rt = g_renderTarget->GetSize();
        float scaleX     = rt.width  / sz.width;
        float scaleY     = rt.height / sz.height;
        float coverScale = max(scaleX, scaleY);
        float destW = sz.width  * coverScale;
        float destH = sz.height * coverScale;
        float destX = (rt.width  - destW) * 0.5f;
        float destY = (rt.height - destH) * 0.5f;
        g_renderTarget->DrawBitmap(
            g_slideshowBgBitmap.Get(),
            D2D1::RectF(destX, destY, destX + destW, destY + destH),
            1.0f,
            D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);

        // Slight dim so the image on top has contrast
        ComPtr<ID2D1SolidColorBrush> dimBrush;
        g_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.f, 0.f, 0.f, 0.35f), &dimBrush);
        if (dimBrush)
        {
            D2D1_SIZE_F r = g_renderTarget->GetSize();
            g_renderTarget->FillRectangle(D2D1::RectF(0, 0, r.width, r.height), dimBrush.Get());
        }
    }

    if (g_d2dBitmap)
    {        
        D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

        // ---- Draw previous image fading out (slideshow only) ----
        if (g_isSlideshowMode && g_prevD2DBitmap && g_slideshowTransitionAlpha < 0.99f)
        {
            D2D1_SIZE_F prevSize = g_prevD2DBitmap->GetSize();

            D2D1::Matrix3x2F prevScale =
                D2D1::Matrix3x2F::Scale(g_prevImageZoom, g_prevImageZoom);
            D2D1::Matrix3x2F prevTranslate =
                D2D1::Matrix3x2F::Translation(g_prevImageOffX, g_prevImageOffY);
            D2D1::Matrix3x2F prevBase = prevScale * prevTranslate;

            D2D1_POINT_2F prevCenter = prevBase.TransformPoint(
                D2D1::Point2F(prevSize.width * 0.5f, prevSize.height * 0.5f));

            D2D1::Matrix3x2F prevRot =
                D2D1::Matrix3x2F::Rotation(g_prevImageRotation, prevCenter);

            g_renderTarget->SetTransform(prevBase * prevRot);
            g_renderTarget->DrawBitmap(
                g_prevD2DBitmap.Get(),
                D2D1::RectF(0, 0, prevSize.width, prevSize.height),
                1.0f - g_slideshowTransitionAlpha,
                D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
            g_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        }

        // ---- Draw current image (with fade-in alpha in slideshow) ----
        float imgCenterX = imgSize.width * 0.5f;
        float imgCenterY = imgSize.height * 0.5f;

        D2D1::Matrix3x2F scale =
            D2D1::Matrix3x2F::Scale(g_zoom, g_zoom);

        D2D1::Matrix3x2F translate =
            D2D1::Matrix3x2F::Translation(g_offsetX, g_offsetY);

        D2D1::Matrix3x2F baseTransform = scale * translate;
     
        D2D1_POINT_2F screenCenter =
            baseTransform.TransformPoint(D2D1::Point2F(imgCenterX, imgCenterY));

        D2D1::Matrix3x2F rotation =
            D2D1::Matrix3x2F::Rotation(g_imageRotationAngle, screenCenter);

        g_renderTarget->SetTransform(baseTransform * rotation);

        const float imgOpacity = g_isSlideshowMode ? g_slideshowTransitionAlpha : 1.0f;
        
        if (g_shadowEffect && g_d2dBitmap)
        {
            g_shadowEffect->SetInput(0, g_d2dBitmap.Get());
            g_renderTarget->PushLayer(
                D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                    D2D1::IdentityMatrix(), imgOpacity),
                nullptr);
            g_renderTarget->DrawImage(g_shadowEffect.Get(), D2D1::Point2F(0.0f, 0.0f));
            g_renderTarget->PopLayer();
        }

        g_renderTarget->DrawBitmap(
            g_d2dBitmap.Get(),
            D2D1::RectF(0, 0, imgSize.width, imgSize.height),
            imgOpacity,
            D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC
        );

        g_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    else if (g_defaultBackgroundBitmap)
    {
        RECT rc;
        GetClientRect(g_mainWindow, &rc);

        D2D1_RECT_F destRect = D2D1::RectF(
            0.f,
            0.f,
            (float)(rc.right - rc.left),
            (float)(rc.bottom - rc.top)
        );

        g_renderTarget->DrawBitmap(
            g_defaultBackgroundBitmap.Get(),
            destRect,
            1.0f,
            D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC
        );
    }
    D2D1_SIZE_F rtSize = g_renderTarget->GetSize();

    // Pre-fade overlay: full-screen opaque black drawn on top of everything,
    // including UI, until the slideshow overlay has presented its first frame.
    // This hides both the fade-to-black transition AND the device recreation gap.
    if (g_slideshowPreFade && g_slideshowPreFadeAlpha > 0.01f)
    {
        ComPtr<ID2D1SolidColorBrush> blackBrush;
        g_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.f, 0.f, 0.f, g_slideshowPreFadeAlpha), &blackBrush);
        if (blackBrush)
            g_renderTarget->FillRectangle(
                D2D1::RectF(0, 0, rtSize.width, rtSize.height), blackBrush.Get());
    }

    // Draw UI buttons

    std::wstring text = std::to_wstring(int(std::round(g_zoom*100)));
    if (!g_textBoxes[TEXTBOX_ZOOM_INPUT].IsFocused())
        g_textBoxes[TEXTBOX_ZOOM_INPUT].SetText(text);

    for (auto& [id, btn] : g_buttons)
    {
        if (!g_isFullscreen && id == BUTTON_EXIT)
            continue;
        btn.Draw(g_renderTarget.Get());
    }

    for (auto& [id, txt] : g_textBoxes)
    {
        txt.Draw(g_renderTarget.Get());
    }
    

    HRESULT hr = g_renderTarget->EndDraw();

    if (SUCCEEDED(hr) && g_swapChain)
    {
        g_swapChain->Present(1, 0);

        // The slideshow overlay has now presented its first frame (which is solid black
        // due to the pre-fade overlay above). It is safe to remove the cover window and
        // release the pre-fade flag — the overlay owns the screen from this point on.
        if (g_slideshowPreFade && g_isSlideshowMode && g_overlayWindow)
        {
            DestroyBlackCoverWindow();
            g_slideshowPreFade      = false;
            g_slideshowPreFadeAlpha = 0.0f;
        }
    }

    if (hr == D2DERR_RECREATE_TARGET)
    {
        DiscardDeviceResources();
    }
    g_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
}

bool LoadImageD2D(HWND hWnd, const wchar_t* filename)
{
    if (!g_wicFactory || !filename || !*filename)
        return false;

    // ---- Slideshow: snapshot current bitmap + bg before we clobber them ----
    if (g_isSlideshowMode && g_d2dBitmap)
    {
        g_prevD2DBitmap         = g_d2dBitmap;
        g_prevSlideshowBgBitmap = g_slideshowBgBitmap;
        g_prevImageZoom         = g_zoom;
        g_prevImageOffX         = g_offsetX;
        g_prevImageOffY         = g_offsetY;
        g_prevImageRotation     = g_imageRotationAngle;

        // Reset so Render recreates it for the new image
        g_slideshowBgBitmap.Reset();

        // Kick off the fade-in
        g_slideshowTransitionAlpha = 0.0f;
        g_slideshowTargetAlpha     = 1.0f;
    }
        
    // ------------------------------------------------------------
    // Save previous image state
    // ------------------------------------------------------------
    if (!g_currentFilePath.empty() && g_d2dBitmap)
    {
    
        D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

        float panX = 0.f, panY = 0.f;
        float targetPanX = 0.f, targetPanY = 0.f;

        if (PanFromOffsets(hWnd, g_zoom, g_offsetX, g_offsetY,
                           imgSize.width, imgSize.height, panX, panY) &&
            PanFromOffsets(hWnd, g_targetZoom, g_targetOffsetX, g_targetOffsetY,
                           imgSize.width, imgSize.height, targetPanX, targetPanY))
        {
            g_imageStates[g_currentFilePath] = {
                g_zoom, panX, panY,
                g_targetZoom, targetPanX, targetPanY,
                g_imageRotationAngle, g_targetRotationAngle
            };
        }
    }
    
    // Clear previous state
    g_d2dBitmap.Reset();
    g_wicBitmapSource.Reset();
    g_gifFrames.clear();
    g_gifFrameDelays.clear();
    g_isAnimatedGif = false;
    g_currentGifFrame = 0;
    g_lastGifFrameTime = 0;
    g_slideshowBgBitmap.Reset();

    // --------------------------------------------
    // Decode container
    // --------------------------------------------

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = g_wicFactory->CreateDecoderFromFilename(
        filename,
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        decoder.GetAddressOf());

    if (FAILED(hr) || !decoder)
        return false;

    UINT frameCount = 0;
    hr = decoder->GetFrameCount(&frameCount);

    if (FAILED(hr) || frameCount == 0)
        return false;

    GUID container = {};
    decoder->GetContainerFormat(&container);
    const bool isGifContainer = IsEqualGUID(container, GUID_ContainerFormatGif);

    // --------------------------------------------
    // Small local helpers (avoid min/max macros)
    // --------------------------------------------
    

    auto imin = [](int a, int b) { return (a < b) ? a : b; };
    auto imax = [](int a, int b) { return (a > b) ? a : b; };
    auto umin = [](UINT a, UINT b) { return (a < b) ? a : b; };

    auto ReadUIntMeta = [&](IWICMetadataQueryReader* reader, const wchar_t* name, UINT& outVal) -> bool
    {
        if (!reader) return false;

        PROPVARIANT var;
        PropVariantInit(&var);
        HRESULT h = reader->GetMetadataByName(name, &var);

        if (FAILED(h))
        {
            PropVariantClear(&var);
            return false;
        }

        UINT v = 0;
        switch (var.vt)
        {
            case VT_UI1: v = var.bVal;  break;
            case VT_UI2: v = var.uiVal; break;
            case VT_UI4: v = var.ulVal; break;
            case VT_I2:  v = (var.iVal < 0) ? 0u : (UINT)var.iVal; break;
            case VT_I4:  v = (var.lVal < 0) ? 0u : (UINT)var.lVal; break;
        default:
            PropVariantClear(&var);
            return false;
        }

        PropVariantClear(&var);
        outVal = v;
        return true;
    };

    auto CloneBitmap = [&](IWICBitmapSource* src) -> Microsoft::WRL::ComPtr<IWICBitmap>
    {
        Microsoft::WRL::ComPtr<IWICBitmap> clone;
        if (src)
        {
            if (SUCCEEDED(g_wicFactory->CreateBitmapFromSource(
                src, WICBitmapCacheOnLoad, clone.GetAddressOf())))
            {
                return clone;
            }
        }
        return nullptr;
    };

    auto ClearRect = [&](IWICBitmap* bmp, const WICRect& rc) -> void
    {
        if (!bmp) return;
        Microsoft::WRL::ComPtr<IWICBitmapLock> lock;

        if (FAILED(bmp->Lock(&rc, WICBitmapLockWrite, lock.GetAddressOf())) || !lock) return;

        UINT cb = 0;
        BYTE* data = nullptr;
        UINT stride = 0;

        if (FAILED(lock->GetDataPointer(&cb, &data)) || !data) return;
        if (FAILED(lock->GetStride(&stride)) || stride == 0) return;

        const UINT w = (UINT)rc.Width;
        const UINT h = (UINT)rc.Height;
        const size_t rowBytes = (size_t)w * 4;

        for (UINT y = 0; y < h; ++y)
            memset(data + (size_t)y * stride, 0, rowBytes); // transparent premultiplied
    };

    auto DecodeToPBGRA = [&](IWICBitmapSource* src,
                            const WICRect* optionalCopyRect,
                            std::vector<BYTE>& outPixels,
                            UINT& outW, UINT& outH, UINT& outStride) -> bool
    {
        if (!src) return false;

        UINT w = 0, h = 0;
        if (FAILED(src->GetSize(&w, &h)) || w == 0 || h == 0)
            return false;

        // Convert to 32bppPBGRA
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        HRESULT hrc = g_wicFactory->CreateFormatConverter(converter.GetAddressOf());

        if (FAILED(hrc) || !converter)
            return false;

        hrc = converter->Initialize(
            src,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0f,
            WICBitmapPaletteTypeCustom);

        if (FAILED(hrc))
            return false;

        WICRect rc = { 0, 0, (INT)w, (INT)h };

        if (optionalCopyRect)
        {
            // Clamp copy rect to source bounds
            int cx = imax(0, optionalCopyRect->X);
            int cy = imax(0, optionalCopyRect->Y);
            int cw = imin(optionalCopyRect->Width,  (int)w - cx);
            int ch = imin(optionalCopyRect->Height, (int)h - cy);

            if (cw <= 0 || ch <= 0) return false;

            rc = { cx, cy, cw, ch };
        }

        const UINT copyW = (UINT)rc.Width;
        const UINT copyH = (UINT)rc.Height;
        const UINT stride = copyW * 4;
        const size_t bufSize = (size_t)stride * copyH;
        outPixels.assign(bufSize, 0);

        hrc = converter->CopyPixels(
            optionalCopyRect ? &rc : nullptr,
            stride,
            (UINT)bufSize,
            outPixels.data());

        if (FAILED(hrc))
            return false;

        // CRITICAL: enforce correct premultiplied invariant:
        // if A==0 then RGB MUST be 0, or you get dark speckles/noise.
        for (size_t p = 0; p + 3 < outPixels.size(); p += 4)
        {
            if (outPixels[p + 3] == 0)
            {
                outPixels[p + 0] = 0;
                outPixels[p + 1] = 0;
                outPixels[p + 2] = 0;
            }
        }

        outW = copyW;
        outH = copyH;
        outStride = stride;
        return true;
    };

    auto BlendSrcOverCanvas = [&](IWICBitmap* canvas,
                                  int dstX, int dstY,
                                  const BYTE* srcPixels,
                                  UINT srcW, UINT srcH, UINT srcStride) -> void
    {
        if (!canvas || !srcPixels || srcW == 0 || srcH == 0)
            return;

        UINT canvasW = 0, canvasH = 0;

        if (FAILED(canvas->GetSize(&canvasW, &canvasH)) || canvasW == 0 || canvasH == 0)
            return;

        int x0 = dstX;
        int y0 = dstY;

        int x1 = dstX + (int)srcW;
        int y1 = dstY + (int)srcH;

        if (x1 <= 0 || y1 <= 0 || x0 >= (int)canvasW || y0 >= (int)canvasH)
            return;

        int clipX0 = imax(0, x0);
        int clipY0 = imax(0, y0);
        int clipX1 = imin((int)canvasW, x1);
        int clipY1 = imin((int)canvasH, y1);

        const UINT drawW = (UINT)(clipX1 - clipX0);
        const UINT drawH = (UINT)(clipY1 - clipY0);
        const UINT srcOffX = (UINT)(clipX0 - x0);
        const UINT srcOffY = (UINT)(clipY0 - y0);

        WICRect lockRc{ clipX0, clipY0, (INT)drawW, (INT)drawH };

        Microsoft::WRL::ComPtr<IWICBitmapLock> lock;

        if (FAILED(canvas->Lock(&lockRc, WICBitmapLockWrite, lock.GetAddressOf())) || !lock)
            return;

        UINT cb = 0;
        BYTE* dst = nullptr;
        UINT dstStride = 0;

        if (FAILED(lock->GetDataPointer(&cb, &dst)) || !dst) return;
        if (FAILED(lock->GetStride(&dstStride)) || dstStride == 0) return;

        for (UINT y = 0; y < drawH; ++y)
        {
            BYTE* dRow = dst + (size_t)y * dstStride;
            const BYTE* sRow = srcPixels + (size_t)(y + srcOffY) * srcStride + (size_t)srcOffX * 4;
            for (UINT x = 0; x < drawW; ++x)
            {
                const BYTE sb = sRow[x * 4 + 0];
                const BYTE sg = sRow[x * 4 + 1];
                const BYTE sr = sRow[x * 4 + 2];
                const BYTE sa = sRow[x * 4 + 3];

                if (sa == 0)
                {
                    continue;
                }
                else if (sa == 255)
                {
                    dRow[x * 4 + 0] = sb;
                    dRow[x * 4 + 1] = sg;
                    dRow[x * 4 + 2] = sr;
                    dRow[x * 4 + 3] = 255;
                    continue;
                }

                // Premultiplied source-over
                const UINT inv = 255u - sa;
                const UINT db = dRow[x * 4 + 0];
                const UINT dg = dRow[x * 4 + 1];
                const UINT dr = dRow[x * 4 + 2];
                const UINT da = dRow[x * 4 + 3];

                dRow[x * 4 + 0] = (BYTE)(sb + (db * inv + 127) / 255);
                dRow[x * 4 + 1] = (BYTE)(sg + (dg * inv + 127) / 255);
                dRow[x * 4 + 2] = (BYTE)(sr + (dr * inv + 127) / 255);
                dRow[x * 4 + 3] = (BYTE)(sa + (da * inv + 127) / 255);
            }
        }
    };

    // ============================================================
    // Animated GIF (compose frames properly)
    // ============================================================

    if (isGifContainer && frameCount > 1)
    {
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
            // Fallback: first frame size
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> f0;

            if (SUCCEEDED(decoder->GetFrame(0, f0.GetAddressOf())) && f0)
                f0->GetSize(&canvasW, &canvasH);
        }

        if (canvasW == 0 || canvasH == 0)
            return false;

        // Composition canvas (full size)
        Microsoft::WRL::ComPtr<IWICBitmap> canvas;
        hr = g_wicFactory->CreateBitmap(
            canvasW, canvasH,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapCacheOnLoad,
            canvas.GetAddressOf());

        if (FAILED(hr) || !canvas)
            return false;

        // Clear to transparent
        ClearRect(canvas.Get(), WICRect{ 0, 0, (INT)canvasW, (INT)canvasH });
        UINT prevDisposal = 0;
        WICRect prevFrameRect{ 0,0,0,0 };
        Microsoft::WRL::ComPtr<IWICBitmap> savedCanvasForDisposal3;

        for (UINT i = 0; i < frameCount; ++i)
        {
            // Apply previous frame disposal BEFORE drawing this frame
            if (i > 0)
            {
                if (prevDisposal == 2)
                {
                    // Restore to background (use transparent)
                    if (prevFrameRect.Width > 0 && prevFrameRect.Height > 0)
                        ClearRect(canvas.Get(), prevFrameRect);
                }

                else if (prevDisposal == 3 && savedCanvasForDisposal3)
                {
                    // Restore to previous
                    canvas = savedCanvasForDisposal3;
                    savedCanvasForDisposal3.Reset();
                }
            }

            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;

            if (FAILED(decoder->GetFrame(i, frame.GetAddressOf())) || !frame)
                continue;

            // Frame defaults
            UINT left = 0, top = 0;
            UINT w = 0, h = 0;
            UINT disposal = 0;
            UINT delayMs = 100; // default 100ms
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

            // If this frame uses "restore to previous", save canvas BEFORE drawing it.
            if (disposal == 3)
                savedCanvasForDisposal3 = CloneBitmap(canvas.Get());
            else
                savedCanvasForDisposal3.Reset();

            // Decode pixels.
            // If the frame decode surface is larger than the described subimage rect,
            // copy only the described region to avoid undefined pixels (black noise).

            std::vector<BYTE> src;
            UINT srcW = 0, srcH = 0, srcStride = 0;
            WICRect copyRect{ 0,0,(INT)frameW,(INT)frameH };
            const WICRect* copyRectPtr = nullptr;
            if (frameW >= left + dstW && frameH >= top + dstH &&
                (frameW != dstW || frameH != dstH) && (left != 0 || top != 0))
            {
                // Pull only the subimage region out of a full-size frame surface.
                copyRect = WICRect{ (INT)left, (INT)top, (INT)dstW, (INT)dstH };
                copyRectPtr = &copyRect;
            }

            if (!DecodeToPBGRA(frame.Get(), copyRectPtr, src, srcW, srcH, srcStride))
                continue;
            const UINT drawW = umin(dstW, srcW);
            const UINT drawH = umin(dstH, srcH);

            // Blend onto the composition canvas at (left, top)
            BlendSrcOverCanvas(canvas.Get(), (int)left, (int)top,
                               src.data(), drawW, drawH, srcStride);

            // Snapshot the composed full canvas as the visible frame
            auto composedFrame = CloneBitmap(canvas.Get());

            if (!composedFrame)
                continue;

            g_gifFrames.push_back(composedFrame);
            g_gifFrameDelays.push_back(delayMs);
            prevDisposal = disposal;
            prevFrameRect = frameRect;

            g_imageWidth  = (int)canvasW;
            g_imageHeight = (int)canvasH;
        }

        if (g_gifFrames.empty())
            return false;

        g_isAnimatedGif = true;
        g_currentGifFrame = 0;
        g_wicBitmapSource = g_gifFrames[0];
        g_lastGifFrameTime = GetTickCount64();
    }
    // ============================================================
    // Static image (or non-GIF multi-frame): load frame 0
    // ============================================================
    else
    {
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.GetAddressOf());

        if (FAILED(hr) || !frame)
            return false;

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        hr = g_wicFactory->CreateFormatConverter(converter.GetAddressOf());

        if (FAILED(hr) || !converter)
            return false;

        hr = converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0f,
            WICBitmapPaletteTypeCustom);

        if (FAILED(hr))
            return false;

        Microsoft::WRL::ComPtr<IWICBitmap> cached;
        hr = g_wicFactory->CreateBitmapFromSource(
            converter.Get(),
            WICBitmapCacheOnLoad,
            cached.GetAddressOf());

        if (FAILED(hr) || !cached)
            return false;

        // Enforce premultiplied invariant for fully transparent pixels.
        // (Prevents speckle/noise if RGB is non-zero under A==0.)
        {
            UINT w = 0, h = 0;

            if (SUCCEEDED(cached->GetSize(&w, &h)) && w && h)
            {
                WICRect rc{ 0,0,(INT)w,(INT)h };
                Microsoft::WRL::ComPtr<IWICBitmapLock> lock;
                g_imageWidth  = (int)w;
                g_imageHeight = (int)h;
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

        g_wicBitmapSource = cached;
    }
    // Create Direct2D Bitmap for the *current device*
    RecreateImageBitmap();

    // ------------------------------------------------------------
    // Restore state for new image (if exists)
    // ------------------------------------------------------------
    std::wstring newPath = filename;

    auto it = g_imageStates.find(newPath);
            
    if (it != g_imageStates.end() && g_d2dBitmap)
    {
        

        const ImageViewState& s = it->second;

        g_zoom = s.zoom;
        UpdateTargetZoom(s.targetZoom);

        D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

        // Convert stored pan back into absolute offsets for THIS render target
        OffsetsFromPan(hWnd, g_zoom, imgSize.width, imgSize.height,
                       s.panX, s.panY, g_offsetX, g_offsetY);

        OffsetsFromPan(hWnd, g_targetZoom, imgSize.width, imgSize.height,
                       s.targetPanX, s.targetPanY, g_targetOffsetX, g_targetOffsetY);

        g_imageRotationAngle = s.rotation;
        g_targetRotationAngle = s.targetRotation;

        g_restoredStateThisLoad = true;
    }
    else
    {
        g_restoredStateThisLoad = false;
    }
    // Update current path
    g_currentFilePath = newPath;
    size_t slash = g_currentFilePath.find_last_of(L"\\/");

    if (slash != std::wstring::npos)
        g_currentFileName = g_currentFilePath.substr(slash + 1);
    else
        g_currentFileName = g_currentFilePath;

    uintmax_t bytes = std::filesystem::file_size(g_currentFileName);
    wchar_t buffer[64];
    if (bytes < 1024)
        swprintf(buffer, 64, L"%llu B", bytes);
    else if (bytes < 1024 * 1024)
        swprintf(buffer, 64, L"%.1f KB", bytes / 1024.0);
    else
        swprintf(buffer, 64, L"%.2f MB", bytes / (1024.0 * 1024.0));


    std::wstring text =
        g_currentFileName +
        L" (" +
        std::to_wstring(g_imageWidth) +
        L" x " +
        std::to_wstring(g_imageHeight) +
        L", " +
        buffer +
        L")";

    g_textBoxes[TEXTBOX_FILE_NAME].SetText(text);

    return (g_d2dBitmap != nullptr);
    }

void BuildImageList(const wchar_t* filename)
{
    g_imageFiles.clear();
    g_imageStates.clear();
    g_currentImageIndex = -1;

    std::filesystem::path p(filename);
    std::filesystem::path dir = p.parent_path();

    for (auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;

        std::wstring path = entry.path().wstring();
        if (IsSupportedImage(path))
            g_imageFiles.push_back(path);
    }

    std::sort(g_imageFiles.begin(), g_imageFiles.end(),
        [](const std::wstring& a, const std::wstring& b)
        {
            return _wcsicmp(a.c_str(), b.c_str()) < 0;
        });

    for (size_t i = 0; i < g_imageFiles.size(); ++i)
    {
        if (_wcsicmp(g_imageFiles[i].c_str(), filename) == 0)
        {
            g_currentImageIndex = (int)i;
            break;
        }
    }
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
    BuildImageList(fileName);
    EnterFullscreen();
    return true;
}

void OpenPrevImage(HWND hWnd)
{
    if (!g_imageFiles.empty())
    {
        g_currentImageIndex--;

        if (g_currentImageIndex < 0)
            g_currentImageIndex =
                (int)g_imageFiles.size() - 1;

        LoadImageD2D(hWnd, g_imageFiles[g_currentImageIndex].c_str());
        InitializeImageLayout(hWnd, true);
    }
}

void OpenNextImage(HWND hWnd)
{
    if (!g_imageFiles.empty())
    {
        g_currentImageIndex =
            (g_currentImageIndex + 1) % (int)g_imageFiles.size();

        LoadImageD2D(hWnd, g_imageFiles[g_currentImageIndex].c_str());
        InitializeImageLayout(hWnd, true);
    }
}

bool ZoomIntoImage(HWND hWnd, short delta, POINT* optionalPt)
{
    if (g_isExiting || !g_d2dBitmap)
        return false;

    bool useMouse = (optionalPt != nullptr);

    POINT pt{};
    if (useMouse)
    {
        pt = *optionalPt;
        ScreenToClient(hWnd, &pt);
    }

    float zoomStep = delta / 120.0f;
    float zoomAmount = powf(1.1f, zoomStep);
    float newTargetZoom = g_targetZoom * zoomAmount;

    if (newTargetZoom < 0.01f) newTargetZoom = 0.01f;
    if (newTargetZoom > 100.0f) newTargetZoom = 100.0f;

    ScreenToClient(hWnd, &pt);

    D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

    // Check if mouse is over the image
    bool overImage = false;

    if (useMouse)
    {
        overImage =
            pt.x >= g_offsetX &&
            pt.x <= g_offsetX + imgSize.width * g_zoom &&
            pt.y >= g_offsetY &&
            pt.y <= g_offsetY + imgSize.height * g_zoom;
    }

    if (useMouse && overImage)
    {
        // zoom relative to cursor
        float imageX = (pt.x - g_offsetX) / g_zoom;
        float imageY = (pt.y - g_offsetY) / g_zoom;

        g_targetOffsetX = pt.x - imageX * newTargetZoom;
        g_targetOffsetY = pt.y - imageY * newTargetZoom;
    }
    else
    {
        // ALWAYS zoom from center
        float centerX = g_offsetX + imgSize.width  * g_zoom / 2.0f;
        float centerY = g_offsetY + imgSize.height * g_zoom / 2.0f;

        g_targetOffsetX = centerX - (imgSize.width  * newTargetZoom) / 2.0f;
        g_targetOffsetY = centerY - (imgSize.height * newTargetZoom) / 2.0f;
    }
    UpdateTargetZoom(newTargetZoom);
    return true;
}

void MakeZoomVisible(HWND hWnd)
{
    g_textBoxes[TEXTBOX_ZOOM_INPUT].SetForcedVisibility(true);
    SetTimer(hWnd, 666, 1000, nullptr);
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
                if (LoadImageD2D(g_mainWindow, incomingPath))
                {
                    // Use existing logic to enter fullscreen and display
                    EnterFullscreen(true, true);
                    // Bring ourselves to front (safe way)
                    if (IsIconic(hWnd))
                        ShowWindow(hWnd, SW_RESTORE);

                    SetForegroundWindow(hWnd);
                    SetActiveWindow(hWnd);
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
            if (g_isFullscreen && g_fullScreenInitDone)
            {
                ExitFullscreen();
            }
        }
    }
    break;

    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;

        wchar_t filePath[MAX_PATH];
        DragQueryFile(hDrop, 0, filePath, MAX_PATH);

        DragFinish(hDrop);

        LoadImageD2D(hWnd, filePath);
        BuildImageList(filePath);
        EnterFullscreen();
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
        case ID_HELP_COMMANDS:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_COMMANDBOX), hWnd, About);
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
        if (!g_swapChain || !g_renderTarget)
            break;

        UINT width  = LOWORD(lParam);
        UINT height = HIWORD(lParam);

        if (width == 0 || height == 0)
            break;

        // Unbind current target
        g_renderTarget->SetTarget(nullptr);
        g_d2dTargetBitmap.Reset();

        HRESULT hr = g_swapChain->ResizeBuffers(
            0,
            width,
            height,
            DXGI_FORMAT_UNKNOWN,
            0);

        if (FAILED(hr))
            break;

        // Recreate target bitmap
        ComPtr<IDXGISurface> surface;
        if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface))))
        {
            D2D1_BITMAP_PROPERTIES1 props =
                D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_TARGET |
                    D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                    D2D1::PixelFormat(
                        DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED));

            if (SUCCEEDED(
                g_renderTarget->CreateBitmapFromDxgiSurface(
                    surface.Get(),
                    &props,
                    &g_d2dTargetBitmap)))
            {
                g_renderTarget->SetTarget(g_d2dTargetBitmap.Get());
                for (auto& [id, btn] : g_buttons)
                    btn.UpdateLayout(g_renderTarget.Get());

                for (auto& [key, textbox] : g_textBoxes)
                    textbox.UpdateLayout(g_renderTarget.Get());
            }
        }

 
        if (g_d2dBitmap)
        {
            if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED)
            {
                // Recenter image for new window size
                SetZoomCentered(g_zoom, hWnd, true, false);
            }
        }

        //return DefWindowProc(hWnd, message, wParam, lParam);
  
    }
    break;

    case WM_MOUSEWHEEL:
    {
        if (!g_d2dBitmap)
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
        float x = GET_X_LPARAM(lParam);
        float y = GET_Y_LPARAM(lParam);
        for (auto& [id, btn] : g_buttons)
        {
            if (btn.OnMouseDown(x, y))
                return 0;  // STOP — button handled it
        }

        for (auto& [id, box] : g_textBoxes)
        {
            if (box.OnMouseDown(x, y))
                return 0;  // STOP — message box handled it
        }
        g_isDragging = true;
        SetCapture(hWnd);
        g_lastMouse.x = x;
        g_lastMouse.y = y;
        g_mouseFromDown = g_lastMouse;
    }
    break;
    
    case WM_LBUTTONUP:
    {
        if (!g_d2dBitmap) break;

        g_isDragging = false;
        ReleaseCapture();
        
        float x = (float)GET_X_LPARAM(lParam);
        float y = (float)GET_Y_LPARAM(lParam);
        D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();
        bool overImage =
            x >= g_offsetX &&
            x <= g_offsetX + imgSize.width * g_zoom &&
            y >= g_offsetY &&
            y <= g_offsetY + imgSize.height * g_zoom;

        bool wasReallyDragging = (abs(x - g_mouseFromDown.x) > 5) || (abs(y - g_mouseFromDown.y) > 5);

        if (g_isFullscreen && !overImage && !wasReallyDragging)
        {
            ExitFullscreen();
        }

        for (auto& [id, btn] : g_buttons)
        {
            btn.OnMouseUp(x, y);
        }

        for (auto& [id, box] : g_textBoxes)
        {
            break;
        }
        // Handle incomplete rotations
        SetTimer(hWnd, 123, 120, nullptr);
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
            btn.UpdateProximity(pt.x, pt.y, windowWidth, windowHeight);

        for (auto& [id, txt] : g_textBoxes)
            txt.UpdateProximity(pt.x, pt.y, windowWidth, windowHeight);


        if (g_isDragging && (wParam & MK_LBUTTON))
        {
            float dx = (float)(pt.x - g_lastMouse.x);
            float dy = (float)(pt.y - g_lastMouse.y);

            g_offsetX += dx;
            g_offsetY += dy;

            // Keep animation targets aligned
            g_targetOffsetX = g_offsetX;
            g_targetOffsetY = g_offsetY;

            g_lastMouse = pt;

            HWND renderWindow = (g_isFullscreen && g_overlayWindow)
                ? g_overlayWindow
                : hWnd;
        }
    }
    break;

    case WM_LBUTTONDBLCLK:
    {
        if (!g_d2dBitmap){ 
            OpenImageFile(hWnd);
            break;
        }

        D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

        float x = GET_X_LPARAM(lParam);
        float y = GET_Y_LPARAM(lParam);

        for (auto& [id, btn] : g_buttons)
        {
            if (btn.OnMouseDown(x, y))
                return 0;  // STOP — button handled it
        }

        for (auto& [id, box] : g_textBoxes)
        {
            if (box.OnMouseDown(x, y))
                return 0;
        }

        bool overImage =
            x >= g_offsetX &&
            x <= g_offsetX + imgSize.width * g_zoom &&
            y >= g_offsetY &&
            y <= g_offsetY + imgSize.height * g_zoom;

        if (!g_isFullscreen && overImage)
        {
            EnterFullscreen(true, true); // preserve zoom & position
        }
        else if (g_isFullscreen)
        {
            if (overImage)
            {
                if (g_targetZoom == 1.0f)
                    FitToWindowRelative(g_overlayWindow, 0.96f, false);
                else
                    SetZoomCentered(1.0f, g_overlayWindow, false, false);
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
        if (wParam == 123)
        {
            // Handle incomplete rotations:
            KillTimer(hWnd, 123);
            if (g_imageRotationAngle < g_targetRotationAngle)
            {
                g_targetRotationAngle = std::ceil(g_imageRotationAngle / 90.0f) * 90.0f;
            }
            else
            {
                g_targetRotationAngle = std::floor(g_imageRotationAngle / 90.0f) * 90.0f;
            }
        }
        if (wParam == 666)
            {
                KillTimer(hWnd, 666);
                g_textBoxes[TEXTBOX_ZOOM_INPUT].SetForcedVisibility(false);
        }
        if (wParam == SLIDESHOW_TIMER_ID)
        {
            if (g_isSlideshowMode && !g_imageFiles.empty())
            {
                // Advance only once the current transition has settled (> 80 %)
                // so rapid fires don't pile up if the system is under load.
                if (g_slideshowTransitionAlpha > 0.8f)
                {
                    OpenNextImage(g_overlayWindow ? g_overlayWindow : g_mainWindow);
                    InitializeImageLayout(g_overlayWindow ? g_overlayWindow : g_mainWindow, true);
                }
                // Re-arm for next picture
                SetTimer(g_mainWindow, SLIDESHOW_TIMER_ID, SLIDESHOW_INTERVAL_MS, nullptr);
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
                if (g_currentImageIndex < 0 || g_currentImageIndex >= (int)g_imageFiles.size())
                    return 0;

                std::wstring fileToDelete = g_imageFiles[g_currentImageIndex];

                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool success = false;

                if (shiftHeld)
                {
                    // Permanent delete
                    success = std::filesystem::remove(fileToDelete);
                }
                else
                {
                    // Send to recycle bin
                    SHFILEOPSTRUCTW op = {};
                    std::wstring from = fileToDelete + L'\0';

                    op.wFunc = FO_DELETE;
                    op.pFrom = from.c_str();
                    op.fFlags =
                        FOF_ALLOWUNDO |       // recycle bin
                        FOF_NOCONFIRMATION |
                        FOF_SILENT;

                    success = (SHFileOperationW(&op) == 0);
                }

                if (success)
                {
                    g_imageFiles.erase(g_imageFiles.begin() + g_currentImageIndex);

                    if (g_imageFiles.empty())
                    {
                        g_currentImageIndex = -1;
                        ExitFullscreen();
                        return 0;
                    }

                    if (g_currentImageIndex >= (int)g_imageFiles.size())
                        g_currentImageIndex = (int)g_imageFiles.size() - 1;

                    LoadImageD2D(hWnd, g_imageFiles[g_currentImageIndex].c_str());
                }
                return 0;
            } 
            break;
            case VK_ESCAPE:
            {
                if (!g_d2dBitmap)
                {
                    PostQuitMessage(0);
                    return 0;
                }
                if (g_isSlideshowMode)
                {
                    ExitSlideshowMode();
                    return 0;
                }
                if (g_isFullscreen)
                {
                    D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();
                    float centerX = g_offsetX + imgSize.width  * g_zoom / 2.0f;
                    float centerY = g_offsetY + imgSize.height * g_zoom / 2.0f;

                    g_targetOffsetX = centerX - (imgSize.width * 0.05f) / 2.0f;
                    g_targetOffsetY = centerY - (imgSize.height * 0.05f) / 2.0f;

                    int refreshRate = GetMonitorRefreshRate(GetDesktopWindow());
                    g_smooth = 2*0.18f/((float)(refreshRate) / 60.0f);
                    g_targetZoom = 0.0005f;
                }

                g_isExiting = true;

                return 0;
            }

            case 0x46: // 'F' key
            {
                if (!g_d2dBitmap)
                {
                    break;
                }
                else if (g_isFullscreen)
                {
                    ExitFullscreen();
                }
                else
                {
                    EnterFullscreen(true, true);
                }
                return 0;
            }
            break;

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
                OpenPrevImage(hWnd);
                if (g_isSlideshowMode)
                    SetTimer(g_mainWindow, SLIDESHOW_TIMER_ID, SLIDESHOW_INTERVAL_MS, nullptr);
                return 0;
            }

            case 0x44: // 'D' key
            case VK_SPACE:
            case VK_RIGHT:
            {
                OpenNextImage(hWnd);
                if (g_isSlideshowMode)
                    SetTimer(g_mainWindow, SLIDESHOW_TIMER_ID, SLIDESHOW_INTERVAL_MS, nullptr);
                return 0;
            }

            case 0x51: // 'Q' key
            {
                if (g_d2dBitmap)
                {
                    g_targetRotationAngle -= 90.0f;
                }
                return 0;
            }

            case 0x45: // 'E' key
            {
                if (g_d2dBitmap)
                {
                    g_targetRotationAngle += 90.0f;
                }
                return 0;
            }

            case VK_F5: // F5 — toggle slideshow mode
            {
                if (!g_d2dBitmap) break;
                if (g_isSlideshowMode)
                    ExitSlideshowMode();
                else
                    EnterSlideshowMode();
                return 0;
            }
        }
    }
    break;

    case WM_SYSCOMMAND:
    {
        switch (wParam & 0xFFF0)
        {
            case SC_MAXIMIZE:
                if (!g_d2dBitmap)
                {
                    return DefWindowProc(hWnd, message, wParam, lParam);
                }
                else
                {
                    EnterFullscreen();
                    return 0;
                }

            default:
                // Let Windows handle all other system commands (menus, icon clicks, etc.)
                return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;

    case WM_KEYUP:
    {
        // Handle incomplete rotations
        SetTimer(hWnd, 123, 120, nullptr);
    }
    break;

    case WM_DESTROY:
    {
        if (hWnd == g_mainWindow)
        {
            // Ensure overlay is cleaned up too.
            if (g_overlayWindow)
            {
                DestroyWindow(g_overlayWindow);
                g_overlayWindow = nullptr;
            }

            DiscardDeviceResources();
            g_wicDefaultBackground.Reset();
            g_wicBackground.Reset();
            g_wicBitmapSource.Reset();
            g_wicFactory.Reset();
            g_d2dFactory.Reset();
            g_textFormat.Reset();
            g_dwriteFactory.Reset();
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