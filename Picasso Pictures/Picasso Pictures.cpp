// Picasso Pictures.cpp : Defines the entry point for the application.
//

#define MAX_LOADSTRING 100

#include "framework.h"
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

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "d3dcompiler.lib")
#include <d3dcompiler.h>

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
constexpr int TEXTBOX_FILE_NAME  = 0;
constexpr int TEXTBOX_ZOOM_INPUT = 1;

// Control IDs for buttons
constexpr int BUTTON_ZOOM_11     = 0;
constexpr int BUTTON_ZOOM_IN     = 1;
constexpr int BUTTON_ZOOM_OUT    = 2;
constexpr int BUTTON_ROTATE_LEFT = 3;
constexpr int BUTTON_ROTATE_RIGHT= 4;
constexpr int BUTTON_PREVIOUS    = 5;
constexpr int BUTTON_NEXT        = 6;
constexpr int BUTTON_EXIT        = 7;
constexpr int BUTTON_SLIDESHOW   = 8;
constexpr int BUTTON_OPEN        = 9;
constexpr int BUTTON_HELP        = 10;

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
// True from the moment a keyboard navigation fires until the next frame is
// fully rendered. Prevents auto-repeat from queuing up loads faster than
// the render loop can display them.
bool                                                g_navPendingRender = false;
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
bool                                                g_launchedWithFile = false;
float                                               g_smooth = 0.13f;
bool                                                g_pendingPreserveView = false;
std::vector<ComPtr<IWICBitmapSource>>               g_gifFrames;
std::vector<ComPtr<ID2D1Bitmap>>                    g_gifD2DBitmaps;         // pre-uploaded GPU bitmaps for each GIF frame
std::vector<ComPtr<ID3D11ShaderResourceView>>       g_gifD3DSRVs;            // pre-uploaded D3D11 mip SRVs for each GIF frame
DWORD                                               g_lastGifFrameTime = 0;
std::vector<UINT>                                   g_gifFrameDelays;
UINT                                                g_currentGifFrame = 0;
bool                                                g_isAnimatedGif = false;
ComPtr<ID3D11Device>                                g_d3dDevice;
ComPtr<ID3D11DeviceContext>                         g_d3dContext;
ComPtr<ID2D1Device>                                 g_d2dDevice;
ComPtr<ID2D1DeviceContext>                          g_renderTarget;
ComPtr<IDXGISwapChain1>                             g_swapChain;
ComPtr<ID2D1Bitmap1>                                g_d2dTargetBitmap;
ComPtr<ID2D1Effect>                                 g_shadowEffect;
std::unordered_map<int, UITextBox>                  g_textBoxes;
std::unordered_map<int, AnimatedButton>             g_buttons;

// Cached solid-color brushes (created once, reused every frame)
ComPtr<ID2D1SolidColorBrush>                        g_dimBrush;
ComPtr<ID2D1SolidColorBrush>                        g_blackBrush;

// ---- D3D11 mip-mapped image rendering pipeline ----
ComPtr<ID3D11Texture2D>                             g_imageMipTex;          // Full mip chain for the current image
ComPtr<ID3D11ShaderResourceView>                    g_imageSRV;             // SRV: all mip levels
ComPtr<ID3D11SamplerState>                          g_trilinearSampler;     // Trilinear / aniso sampler
ComPtr<ID3D11VertexShader>                          g_imageVS;
ComPtr<ID3D11PixelShader>                           g_imagePS;
ComPtr<ID3D11InputLayout>                           g_imageIL;
ComPtr<ID3D11Buffer>                                g_imageVB;              // Dynamic quad verts (4 × 16 bytes)
ComPtr<ID3D11Buffer>                                g_imageCB;              // Dynamic constant buffer
ComPtr<ID3D11BlendState>                            g_imageBlend;           // Pre-multiplied alpha blend
ComPtr<ID3D11RasterizerState>                       g_imageRast;
ComPtr<ID3D11DepthStencilState>                     g_imageDS;
ComPtr<ID3D11RenderTargetView>                      g_swapRTV;              // RTV wrapping swap-chain buffer 0
bool                                                g_mipPipelineReady = false;

// Custom window message posted by the directory watcher thread
#define WM_APP_DIRCHANGE  (WM_APP + 1)
#define WM_APP_EXITFULLSCREEN (WM_APP + 2)  // deferred exit to avoid mid-focus-change destruction

// ---- Vignette animated visibility ----
float                                               g_topVignetteVisibility    = 0.f;
float                                               g_topVignetteTarget        = 0.f;
float                                               g_bottomVignetteVisibility = 0.f;
float                                               g_bottomVignetteTarget     = 0.f;
// Pixel heights captured once at fullscreen init (mirrors CaptureAnchorsOnce).
// After capture these stay fixed; draw code back-converts to normalized fractions
// for the current RT height so the bars never grow/shrink with the window.
float                                               g_vignetteTopBarHeightPx    = 0.f;
float                                               g_vignetteBottomBarHeightPx = 0.f;
// Activation zone thresholds captured at fullscreen init (OffsetFromStart / OffsetFromEnd).
// Top zone:    show when mouseY <= g_vignetteTopActivationPx           (inset from top)
// Bottom zone: show when mouseY >= windowHeight - g_vignetteBottomActivationPx (inset from bottom)
float                                               g_vignetteTopActivationPx   = 0.f;
float                                               g_vignetteBottomActivationPx= 0.f;
bool                                                g_vignetteHeightsCaptured   = false;

// ---- Directory watcher ----
HANDLE                                              g_watchHandle    = INVALID_HANDLE_VALUE;
std::thread                                         g_watchThread;
std::atomic<bool>                                   g_watchStop{ false };

// ---- Thumbnail film strip ----
struct ThumbnailEntry
{
    Microsoft::WRL::ComPtr<IWICBitmap>  wic;  // CPU-side; survives device loss
    Microsoft::WRL::ComPtr<ID2D1Bitmap> d2d;  // GPU-side; cleared on device loss
};
std::vector<ThumbnailEntry>                                      g_thumbs;
std::mutex                                                       g_thumbReadyMutex;
std::queue<std::pair<int, Microsoft::WRL::ComPtr<IWICBitmap>>>   g_thumbReadyQueue;
std::thread                                                      g_thumbLoaderThread;
std::atomic<bool>                                                g_thumbLoaderStop{ false };
float                                                            g_thumbScrollOffset = 0.f;
float                                                            g_thumbTargetOffset = 0.f;
float                                                            g_thumbW            = 0.f;
float                                                            g_thumbH            = 0.f;
float                                                            g_thumbGap          = 0.f;
float                                                            g_thumbStripInsetFromBottomPx = 0.f;  // pixels from RT bottom to strip centre (anchored like vignettes)
bool                                                             g_thumbSizeCaptured = false;
std::vector<Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry>> g_thumbClipGeos;     // one per slot, rebuilt on resize

// ---- Slideshow mode ----
bool                                                g_isSlideshowMode            = false;
ComPtr<ID2D1Bitmap>                                 g_slideshowBgBitmap;         // 25 %-size blurred bg for current image
ComPtr<ID2D1Bitmap>                                 g_prevSlideshowBgBitmap;     // blurred bg fading out
ComPtr<ID2D1Bitmap>                                 g_prevD2DBitmap;             // image bitmap fading out
ComPtr<ID2D1Effect>                                 g_blurEffect;                // reusable Gaussian blur effect
ComPtr<ID2D1Bitmap>                                 g_shadowSourceBitmap;        // small mip-extracted D2D bitmap used as shadow input
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
static D2D1_BITMAP_PROPERTIES1 SwapChainBitmapProps();
bool CreateMipTextureFromSource(IWICBitmapSource* wicSrc);
void CreateD3DImagePipeline();
static void RenderImageD3D11(float imgW, float imgH, float opacity, bool isPrev = false);
static ComPtr<ID2D1Bitmap> ExtractMipAsD2DBitmap(UINT targetMaxPx);
void InitializeImageLayout(HWND hWnd, bool hard);
void Render(HWND hWnd);
bool LoadImageD2D(HWND hWnd, const wchar_t* filename);
void BuildImageList(const wchar_t* filename);
bool OpenImageFile(HWND hWnd);
void OpenNextImage(HWND hWnd);
void OpenPrevImage(HWND hWnd);
bool ZoomIntoImage(HWND hWnd, short delta, POINT* optionalPt);
void MakeZoomVisible(HWND hWnd);
void StopThumbnailLoader();
void StartThumbnailLoader();
void DrawThumbnailStrip(float visibility);
void StartDirectoryWatcher(const std::wstring& dir);
void StopDirectoryWatcher();
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

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
    g_smooth = 1.0f - std::powf(1.0f - g_smooth, 60.0f / (float)refreshRate);

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
    g_gifD2DBitmaps.clear();
    g_gifD3DSRVs.clear();

    // Clear GPU-side thumbnail bitmaps (WIC source survives for re-upload)
    for (auto& t : g_thumbs)
        t.d2d.Reset();

    // Cached brushes
    g_dimBrush.Reset();
    g_blackBrush.Reset();

    // D2D context
    g_renderTarget.Reset();

    // DXGI / D3D
    g_swapChain.Reset();
    g_d3dContext.Reset();
    g_d3dDevice.Reset();
    g_d2dDevice.Reset();
    g_shadowEffect.Reset();
    g_blurEffect.Reset();
    g_shadowSourceBitmap.Reset();
    g_renderTargetWindow = nullptr;

    // D3D11 mip image pipeline
    g_imageMipTex.Reset();
    g_imageSRV.Reset();
    g_imageVS.Reset();
    g_imagePS.Reset();
    g_imageIL.Reset();
    g_imageVB.Reset();
    g_imageCB.Reset();
    g_trilinearSampler.Reset();
    g_imageBlend.Reset();
    g_imageRast.Reset();
    g_imageDS.Reset();
    g_swapRTV.Reset();
    g_mipPipelineReady = false;
}

void UpdateEngine(float dt)
{
    if (g_isAnimatedGif && !g_gifFrames.empty())
    {
        LONG now = GetTickCount64();
        if (now - g_lastGifFrameTime >= g_gifFrameDelays[g_currentGifFrame])
        {
            g_currentGifFrame =
                (g_currentGifFrame + 1) % g_gifFrames.size();

            // If all frames are pre-uploaded, just swap the D2D pointer (no CPU→GPU upload)
            if (g_currentGifFrame < g_gifD2DBitmaps.size() && g_gifD2DBitmaps[g_currentGifFrame])
            {
                g_d2dBitmap = g_gifD2DBitmaps[g_currentGifFrame];

                // Also swap the D3D11 mip SRV so the trilinear path shows the correct frame
                if (g_currentGifFrame < g_gifD3DSRVs.size() && g_gifD3DSRVs[g_currentGifFrame])
                    g_imageSRV = g_gifD3DSRVs[g_currentGifFrame];
            }
            else
            {
                g_wicBitmapSource = g_gifFrames[g_currentGifFrame];
                RecreateImageBitmap();  // also rebuilds g_imageSRV via CreateMipTextureFromSource
            }

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
    g_overlayAlpha += (overlayTarget - g_overlayAlpha) * (1.0f - std::powf(1.0f - 0.15f, dt * 60.0f));

    // ---- Slideshow pre-fade (current view → black, then window transition) ----
    // g_slideshowPreFade stays true until Render() confirms the overlay's first Present().
    if (g_slideshowPreFade && !g_isSlideshowMode)
    {
        // Phase 1: fade the current render target to black (slow ease-out)
        g_slideshowPreFadeAlpha += (1.0f - g_slideshowPreFadeAlpha) * (1.0f - std::powf(1.0f - 0.08f, dt * 60.0f));

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
            (g_slideshowTargetAlpha - g_slideshowTransitionAlpha) * (1.0f - std::powf(1.0f - 0.08f, dt * 60.0f));

        if (g_slideshowTransitionAlpha > 0.995f)
        {
            g_slideshowTransitionAlpha = 1.0f;
            g_prevD2DBitmap.Reset();
            g_prevSlideshowBgBitmap.Reset();
        }
    }

    // ---- Thumbnail strip: drain loader queue (one upload per frame) ----
    {
        std::lock_guard<std::mutex> lk(g_thumbReadyMutex);
        if (!g_thumbReadyQueue.empty())
        {
            auto& [idx, bmp] = g_thumbReadyQueue.front();
            if (idx >= 0 && idx < (int)g_thumbs.size())
                g_thumbs[idx].wic = std::move(bmp);
            g_thumbReadyQueue.pop();
        }
    }

    // Animate thumbnail scroll offset toward current image
    if (g_thumbSizeCaptured)
    {
        g_thumbTargetOffset = -(g_currentImageIndex * (g_thumbW + g_thumbGap));
        g_thumbScrollOffset += (g_thumbTargetOffset - g_thumbScrollOffset) * g_smooth;
    }

    g_zoom     += (g_targetZoom     - g_zoom)     * g_smooth;
    g_offsetX  += (g_targetOffsetX  - g_offsetX)  * g_smooth;
    g_offsetY  += (g_targetOffsetY  - g_offsetY)  * g_smooth;

    HWND renderWindow = (g_isFullscreen && g_overlayWindow)
        ? g_overlayWindow
        : g_mainWindow;

    // Update button animations
    for (auto& [id, btn] : g_buttons)
        btn.Update(dt);

    for (auto& [id, txt] : g_textBoxes)
        txt.UpdateVisibility();

    // Smooth-animate vignette visibility (same feel as buttons: ~0.08 falloff)
    const float VIGN_FALLOFF = 0.08f;
    g_topVignetteVisibility    += (g_topVignetteTarget    - g_topVignetteVisibility)    * VIGN_FALLOFF;
    g_bottomVignetteVisibility += (g_bottomVignetteTarget - g_bottomVignetteVisibility) * VIGN_FALLOFF;

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

    g_renderTargetWindow = nullptr; // will be set once render target is created
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
// Builds a small blurred D2D bitmap used as the slideshow background fill.
// Instead of doing an expensive CPU WIC downscale, we pull a small mip
// level directly from the already-built GPU mip chain via a staging readback
// — the GPU already downscaled the image for free as part of GenerateMips.
// Must be called outside BeginDraw/EndDraw (or before BeginDraw in Render).
// -----------------------------------------------------------------------
void CreateSlideshowBgBitmap()
{
    g_slideshowBgBitmap.Reset();

    if (!g_renderTarget || !g_blurEffect)
        return;

    // ----- 1. Get a small source bitmap -----
    // Prefer the GPU mip chain (fast, already computed).
    // Fall back to the WIC CPU path if the mip texture is not yet available
    // (e.g. during the very first frame before RecreateImageBitmap finishes).
    ComPtr<ID2D1Bitmap> smallBmp;

    if (g_imageMipTex)
    {
        // Pick a mip whose longest edge is ~200 px — enough colour detail for a blur wash.
        smallBmp = ExtractMipAsD2DBitmap(200u);
    }

    if (!smallBmp)
    {
        // CPU fallback (original path): WIC downscale to ≤400 px then upload.
        if (!g_wicBitmapSource || !g_wicFactory) return;

        UINT fullW = 0, fullH = 0;
        if (FAILED(g_wicBitmapSource->GetSize(&fullW, &fullH)) || fullW == 0 || fullH == 0)
            return;

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

        ComPtr<ID2D1Bitmap> tmp;
        if (FAILED(g_renderTarget->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &tmp)))
            return;
        smallBmp = tmp;
    }

    if (!smallBmp) return;

    D2D1_SIZE_U smallSz = smallBmp->GetPixelSize();

    // ----- 2. Create off-screen render target at the small bitmap's size -----
    D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap1> offscreenBmp;
    if (FAILED(g_renderTarget->CreateBitmap(
            smallSz, nullptr, 0, bmpProps, &offscreenBmp))) return;

    // ----- 3. Render the Gaussian blur into the off-screen bitmap -----
    g_blurEffect->SetInput(0, smallBmp.Get());

    ComPtr<ID2D1Image> prevTarget;
    g_renderTarget->GetTarget(&prevTarget);
    g_renderTarget->SetTarget(offscreenBmp.Get());

    g_renderTarget->BeginDraw();
    g_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));
    g_renderTarget->DrawImage(g_blurEffect.Get(), D2D1::Point2F(0.f, 0.f));
    g_renderTarget->EndDraw();

    g_renderTarget->SetTarget(prevTarget.Get());

    // ----- 4. Store -----
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

        // Capture vignette bar heights in pixels the first time we enter fullscreen.
        // Mirrors CaptureAnchorsOnce: once set, the physical thickness is fixed even
        // if the user resizes the window or exits/re-enters fullscreen.
        if (!g_vignetteHeightsCaptured && size.height > 0.f)
        {
            g_vignetteTopBarHeightPx     = size.height * 0.08f;  // topY=0.00→bottomY=0.08
            g_vignetteBottomBarHeightPx  = size.height * 0.14f;  // topY=0.86→bottomY=1.00
            // Activation zone: OffsetFromStart for top, OffsetFromEnd for bottom
            g_vignetteTopActivationPx    = size.height * 0.20f;  // show when mouseY <= this
            g_vignetteBottomActivationPx = size.height * 0.20f;  // show when mouseY >= windowH - this
            g_vignetteHeightsCaptured    = true;
        }

        // Capture thumbnail strip dimensions once at fullscreen init
        if (!g_thumbSizeCaptured && size.height > 0.f)
        {
            g_thumbH            = size.height * 0.08f;
            g_thumbW            = g_thumbH;            // square slots
            g_thumbGap          = g_thumbH    * 0.12f;
            g_thumbStripInsetFromBottomPx = size.height * (1.0f - 0.855f);  // mirrors vignette anchor pattern
            g_thumbSizeCaptured = true;

            // Snap scroll immediately so there is no slide-in on first open
            g_thumbTargetOffset = -(g_currentImageIndex * (g_thumbW + g_thumbGap));
            g_thumbScrollOffset = g_thumbTargetOffset;
        }

        // Rebuild per-slot clip geometries whenever thumb dimensions are freshly captured.
        // Slots share the same shape so we only need one; index them by position later.
        // We create exactly one geometry (reused for every slot via the same RR dimensions).
        if (g_thumbSizeCaptured && g_thumbClipGeos.empty() && g_d2dFactory)
        {
            const float radius = g_thumbH * 0.10f;
            // Placeholder rect centred at origin — position is irrelevant because
            // we transform the render target before pushing the layer.
            const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
                D2D1::RectF(-g_thumbW * 0.5f, -g_thumbH * 0.5f,
                             g_thumbW * 0.5f,  g_thumbH * 0.5f),
                radius, radius);
            Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> geo;
            if (SUCCEEDED(g_d2dFactory->CreateRoundedRectangleGeometry(rr, &geo)))
                g_thumbClipGeos.push_back(geo);
        }
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

    // 2. Get DXGI device (local — only needed here to create the swap chain)
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = g_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr))
        return;

    // 3. Create D2D device + context
    hr = g_d2dFactory->CreateDevice(dxgiDevice.Get(), &g_d2dDevice);
    if (FAILED(hr))
        return;

    hr = g_d2dDevice->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
        &g_renderTarget);   // your DeviceContext
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

    D2D1_BITMAP_PROPERTIES1 props = SwapChainBitmapProps();

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
                5.0f);   // softness
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

    // Create cached brushes (reused every frame — avoids per-frame COM allocations)
    g_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 1.f), &g_dimBrush);
    g_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 1.f), &g_blackBrush);

    // Create a D3D11 RTV wrapping the swap-chain back buffer.
    // This RTV is used later to bind the back buffer as a D3D11 render target
    // in-between two D2D BeginDraw/EndDraw pairs so the D3D11 mip quad draw
    // is composited on top of the D2D background / below the D2D UI.
    {
        ComPtr<ID3D11Texture2D> backBuf;
        if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuf))))
            g_d3dDevice->CreateRenderTargetView(backBuf.Get(), nullptr, &g_swapRTV);
    }

    // Build shaders, sampler, states for the D3D11 mip-mapped image path.
    CreateD3DImagePipeline();
}

// Returns the standard D2D1_BITMAP_PROPERTIES1 used for swap-chain target bitmaps.
static D2D1_BITMAP_PROPERTIES1 SwapChainBitmapProps()
{
    return D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
}

// ============================================================
//  D3D11 mip-mapped image rendering pipeline
// ============================================================

// Embedded HLSL — vertex shader
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

// Embedded HLSL — pixel shader (trilinear comes from the sampler state)
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
    float m[16];        // row-major 4×4  image-space → clip-space
    float opacity;
    float pad[3];
};

// Simple row-major 4×4 multiply: C = A * B
static void Mat4Mul(const float A[16], const float B[16], float C[16])
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            C[r*4+c] = 0.f;
            for (int k = 0; k < 4; ++k)
                C[r*4+c] += A[r*4+k] * B[k*4+c];
        }
}

// Build a row-major matrix that maps image-space points (x∈[0,imgW], y∈[0,imgH])
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

    // S: image-space → screen-space  (x_s = x_i*zoom + offsetX)
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

    // N: screen-space → NDC
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
    g_mipPipelineReady = false;
    if (!g_d3dDevice || !g_d3dContext) return;

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

    hr = g_d3dDevice->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_imageVS);
    if (FAILED(hr)) return;

    hr = g_d3dDevice->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_imagePS);
    if (FAILED(hr)) return;

    // ----- Input layout: POSITION float2, TEXCOORD0 float2 -----
    D3D11_INPUT_ELEMENT_DESC ilDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_d3dDevice->CreateInputLayout(
        ilDesc, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_imageIL);
    if (FAILED(hr)) return;

    // ----- Dynamic vertex buffer (4 vertices, updated every draw) -----
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth      = sizeof(ImageVertex) * 4;
    vbDesc.Usage          = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_d3dDevice->CreateBuffer(&vbDesc, nullptr, &g_imageVB);
    if (FAILED(hr)) return;

    // ----- Dynamic constant buffer -----
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth      = sizeof(CBImageTransform);
    cbDesc.Usage          = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_d3dDevice->CreateBuffer(&cbDesc, nullptr, &g_imageCB);
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
    hr = g_d3dDevice->CreateSamplerState(&sampDesc, &g_trilinearSampler);
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
    hr = g_d3dDevice->CreateBlendState(&blendDesc, &g_imageBlend);
    if (FAILED(hr)) return;

    // ----- Rasterizer: no culling (quad is always front-facing) -----
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    hr = g_d3dDevice->CreateRasterizerState(&rastDesc, &g_imageRast);
    if (FAILED(hr)) return;

    // ----- Depth-stencil: no depth test -----
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable   = FALSE;
    dsDesc.StencilEnable = FALSE;
    hr = g_d3dDevice->CreateDepthStencilState(&dsDesc, &g_imageDS);
    if (FAILED(hr)) return;

    g_mipPipelineReady = true;
}

// Upload a WIC image into a D3D11 Texture2D with a full mip chain,
// then call GenerateMips so the GPU can trilinearly filter during zoom-out.
// The full-resolution pixels are uploaded to mip 0; all lower mips are
// generated on the GPU (fast, high quality).
bool CreateMipTextureFromSource(IWICBitmapSource* wicSrc)
{
    g_imageMipTex.Reset();
    g_imageSRV.Reset();

    if (!wicSrc || !g_d3dDevice || !g_d3dContext) return false;

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
    if (FAILED(g_d3dDevice->CreateTexture2D(&texDesc, nullptr, &tex))) return false;

    // ----- Get the actual mip count that was allocated -----
    tex->GetDesc(&texDesc);   // mipLevels is now filled by the driver

    // ----- Decode (and optionally scale) the image to BGRA pixels -----
    ComPtr<IWICBitmapSource> src = wicSrc;

    // Downscale via WIC if the source exceeds our texture dimensions
    if (srcW != texW || srcH != texH)
    {
        ComPtr<IWICBitmapScaler> scaler;
        ComPtr<IWICFormatConverter> conv;
        if (SUCCEEDED(g_wicFactory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(wicSrc, texW, texH,
                                         WICBitmapInterpolationModeHighQualityCubic)) &&
            SUCCEEDED(g_wicFactory->CreateFormatConverter(&conv)) &&
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
            if (SUCCEEDED(g_wicFactory->CreateFormatConverter(&conv)) &&
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
    g_d3dContext->UpdateSubresource(tex.Get(), 0, nullptr, pixels.data(), stride, 0);

    // ----- Create SRV (all mip levels) so the sampler can access them -----
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_B8G8R8A8_UNORM;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = (UINT)-1;  // all levels

    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(g_d3dDevice->CreateShaderResourceView(tex.Get(), &srvDesc, &srv))) return false;

    // ----- GPU-generate all lower mip levels (very fast) -----
    g_d3dContext->GenerateMips(srv.Get());

    g_imageMipTex = tex;
    g_imageSRV    = srv;
    return true;
}

// Extract one mip level from g_imageMipTex as a small D2D bitmap.
// targetMaxPx is the desired maximum dimension — we pick the smallest mip
// level whose max dimension is still >= targetMaxPx (so we never upscale).
// The GPU already computed all mip levels, so this is just a cheap readback.
static ComPtr<ID2D1Bitmap> ExtractMipAsD2DBitmap(UINT targetMaxPx)
{
    if (!g_imageMipTex || !g_d3dDevice || !g_d3dContext || !g_renderTarget)
        return nullptr;

    D3D11_TEXTURE2D_DESC texDesc = {};
    g_imageMipTex->GetDesc(&texDesc);

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

    // Staging texture — CPU-readable copy of the chosen mip level.
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
    if (FAILED(g_d3dDevice->CreateTexture2D(&stagDesc, nullptr, &staging)))
        return nullptr;

    g_d3dContext->CopySubresourceRegion(
        staging.Get(), 0, 0, 0, 0,
        g_imageMipTex.Get(),
        D3D11CalcSubresource(mipLevel, 0, texDesc.MipLevels),
        nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(g_d3dContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return nullptr;

    D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap> bmp;
    g_renderTarget->CreateBitmap(
        D2D1::SizeU(mipW, mipH),
        mapped.pData,
        mapped.RowPitch,
        bmpProps,
        &bmp);

    g_d3dContext->Unmap(staging.Get(), 0);
    return bmp;
}

// Draw the image as a D3D11 textured quad with trilinear filtering.
// Must be called BETWEEN two D2D EndDraw/BeginDraw pairs (not inside BeginDraw).
// Uses the global g_zoom / g_offsetX / g_offsetY / g_imageRotationAngle to build
// the transform matrix each frame.
static void RenderImageD3D11(float imgW, float imgH, float opacity, bool /*isPrev*/)
{
    if (!g_mipPipelineReady || !g_imageSRV || !g_swapRTV) return;
    if (!g_d3dDevice || !g_d3dContext)                    return;
    if (opacity < 0.001f)                                  return;

    const D2D1_SIZE_F rtSize = g_renderTarget->GetSize();
    const float W = rtSize.width;
    const float H = rtSize.height;

    // ----- Update constant buffer -----
    CBImageTransform cb = {};
    BuildImageToClip(cb.m, imgW, imgH,
                     g_zoom, g_offsetX, g_offsetY,
                     g_imageRotationAngle, W, H);
    cb.opacity = opacity;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(g_d3dContext->Map(g_imageCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &cb, sizeof(cb));
        g_d3dContext->Unmap(g_imageCB.Get(), 0);
    }

    // ----- Update dynamic vertex buffer (quad in image space) -----
    ImageVertex verts[4] = {
        { 0.f,  0.f,  0.f, 0.f },
        { imgW, 0.f,  1.f, 0.f },
        { 0.f,  imgH, 0.f, 1.f },
        { imgW, imgH, 1.f, 1.f },
    };
    if (SUCCEEDED(g_d3dContext->Map(g_imageVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, verts, sizeof(verts));
        g_d3dContext->Unmap(g_imageVB.Get(), 0);
    }

    // ----- Set pipeline state -----
    D3D11_VIEWPORT vp = {};
    vp.Width    = W;
    vp.Height   = H;
    vp.MaxDepth = 1.0f;
    g_d3dContext->RSSetViewports(1, &vp);
    g_d3dContext->RSSetState(g_imageRast.Get());

    g_d3dContext->OMSetRenderTargets(1, g_swapRTV.GetAddressOf(), nullptr);
    g_d3dContext->OMSetBlendState(g_imageBlend.Get(), nullptr, 0xFFFFFFFF);
    g_d3dContext->OMSetDepthStencilState(g_imageDS.Get(), 0);

    g_d3dContext->IASetInputLayout(g_imageIL.Get());
    UINT stride = sizeof(ImageVertex), offset = 0;
    g_d3dContext->IASetVertexBuffers(0, 1, g_imageVB.GetAddressOf(), &stride, &offset);
    g_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    g_d3dContext->VSSetShader(g_imageVS.Get(), nullptr, 0);
    g_d3dContext->VSSetConstantBuffers(0, 1, g_imageCB.GetAddressOf());

    g_d3dContext->PSSetShader(g_imagePS.Get(), nullptr, 0);
    g_d3dContext->PSSetShaderResources(0, 1, g_imageSRV.GetAddressOf());
    g_d3dContext->PSSetSamplers(0, 1, g_trilinearSampler.GetAddressOf());
    g_d3dContext->PSSetConstantBuffers(0, 1, g_imageCB.GetAddressOf());

    g_d3dContext->Draw(4, 0);

    // ----- Unbind so D2D / the debug layer don't complain -----
    ID3D11ShaderResourceView* nullSRV = nullptr;
    g_d3dContext->PSSetShaderResources(0, 1, &nullSRV);
    ID3D11RenderTargetView*   nullRTV = nullptr;
    g_d3dContext->OMSetRenderTargets(0, &nullRTV, nullptr);
}

void RecreateImageBitmap()
{
    if (!g_wicBitmapSource || !g_renderTarget)
        return;

    g_d2dBitmap.Reset();
    g_shadowSourceBitmap.Reset();

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
        return;
    }

    // Also build the D3D11 mip texture for cache-friendly trilinear rendering.
    // We pass the (possibly downscaled) WIC source so the texture matches the D2D bitmap size.
    if (g_mipPipelineReady)
    {
        CreateMipTextureFromSource(bitmapSourceForD2D.Get());

        // Extract a ~256 px mip for use as the shadow effect input.
        // The shadow is a blurred silhouette — full resolution is wasted here.
        // This small bitmap is created once per image load and reused every frame.
        g_shadowSourceBitmap = ExtractMipAsD2DBitmap(256u);
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
    openConfig.tooltip = L"Open image  [O]";
    openConfig.layout.activationZone = topLeftActivationZone;
    openConfig.layout.referenceWidth  = size.width;
    openConfig.layout.referenceHeight = size.height;
    openConfig.layout.uiPixelScale = min(size.width, size.height);
    openConfig.layout.tooltipFontSize = 0.03f;
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
    openConfig.tooltip = L"Keyboard shortcuts";
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
    buttonConfig.layout.y.value = 0.93f;
    buttonConfig.layout.y.anchor = AnimatedButton::Anchor::OffsetFromEnd;
    buttonConfig.layout.y.mode = AnimatedButton::PosMode::Normalized;
    buttonConfig.layout.width = width;
    buttonConfig.layout.height = height;
    buttonConfig.fontSize = fontSize * 1.1f;
    buttonConfig.text = L"1:1";
    buttonConfig.tooltip = L"1:1 zoom";
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
    buttonConfig.tooltip = L"Slideshow\n[F5]";

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
    buttonConfig.tooltip = L"Zoom in\n[W / \u2191]";

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
    buttonConfig.tooltip = L"Zoom out [S / \u2193]";

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
    buttonConfig.tooltip = L"Rotate left\n[Q]";

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
    buttonConfig.tooltip = L"Rotate right\n[E]";

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
    buttonConfig.tooltip = L"Previous image\n[A / \u2190]";

    g_buttons[BUTTON_PREVIOUS].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            OpenPrevImage(g_renderTargetWindow);
            if (g_isSlideshowMode)
                KillTimer(g_mainWindow, SLIDESHOW_TIMER_ID);
                SetTimer(g_mainWindow, SLIDESHOW_TIMER_ID, SLIDESHOW_INTERVAL_MS, nullptr);
        });
    g_buttons[BUTTON_PREVIOUS].UpdateLayout(g_renderTarget.Get());

    // -------------------------
    // Next Image Button
    // -------------------------

    buttonConfig.layout.x.value = 0.57f;
    buttonConfig.text = L"\u2B9E";
    buttonConfig.tooltip = L"Next image\n[D / \u2192 / Space]";
    
    g_buttons[BUTTON_NEXT].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            OpenNextImage(g_renderTargetWindow);
            if (g_isSlideshowMode)
                KillTimer(g_mainWindow, SLIDESHOW_TIMER_ID);
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
    exitConfig.tooltip = L"Exit\n[Esc]";
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
            g_smooth = 1.0f - std::powf(1.0f - 2 * 0.18f, 60.0f / (float)refreshRate);

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
    config.layout.y.value = 0.97f;
    config.layout.y.anchor = UITextBox::Anchor::OffsetFromEnd;
    config.layout.x.mode = UITextBox::PosMode::Normalized;

    config.relativeFontSize = 0.016f;
    config.layout.width  = 1.0f;
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
    config.relativeFontSize = 0.012f;
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
    g_textBoxes[TEXTBOX_ZOOM_INPUT].SetTooltip(
        L"Zoom\nclick & type\n Enter to set");
}

// Draw a linear-gradient vignette bar across the full width of the render target.
// darkAtBottom=true  → transparent at topY,    dark at bottomY  (bottom bar)
// darkAtBottom=false → dark at topY, transparent at bottomY     (top bar)
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
    if (!bmp || !g_renderTarget || alpha <= 0.01f) return;

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

    // On device loss, GIF D2D bitmaps are cleared — re-upload all frames.
    if (g_isAnimatedGif && !g_gifFrames.empty() && g_gifD2DBitmaps.empty())
    {
        g_gifD2DBitmaps.reserve(g_gifFrames.size());
        g_gifD3DSRVs.clear();
        g_gifD3DSRVs.reserve(g_gifFrames.size());
        for (auto& wicFrame : g_gifFrames)
        {
            ComPtr<ID2D1Bitmap> bmp;
            g_renderTarget->CreateBitmapFromWicBitmap(wicFrame.Get(), nullptr, &bmp);
            g_gifD2DBitmaps.push_back(bmp);

            if (g_mipPipelineReady)
            {
                CreateMipTextureFromSource(wicFrame.Get());
                g_gifD3DSRVs.push_back(g_imageSRV);
            }
            else
            {
                g_gifD3DSRVs.push_back(nullptr);
            }
        }
        if (g_currentGifFrame < g_gifD2DBitmaps.size() && g_gifD2DBitmaps[g_currentGifFrame])
            g_d2dBitmap = g_gifD2DBitmaps[g_currentGifFrame];
        if (g_currentGifFrame < g_gifD3DSRVs.size() && g_gifD3DSRVs[g_currentGifFrame])
            g_imageSRV = g_gifD3DSRVs[g_currentGifFrame];
    }

    if (!g_d2dBitmap && g_wicDefaultBackground && !g_defaultBackgroundBitmap)
        CreateDefaultBackgroundBitmap();

    if (g_isFullscreen && g_wicBackground && !g_backgroundBitmap)
        CreateBackgroundBitmap();

    // Slideshow blurred-bg: build lazily (off-screen render, must be before BeginDraw)
    if (g_wicBitmapSource && !g_slideshowBgBitmap && g_blurEffect)
        CreateSlideshowBgBitmap();

    // Lazy D2D upload for one thumbnail per frame — spiral outward from the
    // current image so the visible window is always uploaded first.
    if (!g_thumbs.empty() && g_currentImageIndex >= 0)
    {
        const int tn  = (int)g_thumbs.size();
        const int cur = min(g_currentImageIndex, tn - 1);
        for (int d = 0; d < tn; ++d)
        {
            auto tryUpload = [&](int i) -> bool
            {
                if (i < 0 || i >= tn) return false;
                auto& t = g_thumbs[i];
                if (t.wic && !t.d2d)
                {
                    g_renderTarget->CreateBitmapFromWicBitmap(t.wic.Get(), nullptr, &t.d2d);
                    return true;  // one upload per frame
                }
                return false;
            };
            if (d == 0) { if (tryUpload(cur))           break; }
            else        { if (tryUpload(cur + d) ||
                             tryUpload(cur - d))         break; }
        }
    }

    g_renderTarget->BeginDraw();

    // Cache the render-target size once — used throughout this function.
    const D2D1_SIZE_F rtSize = g_renderTarget->GetSize();

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
        // Fade out old blurred bg, fade in new one
        DrawCoverBg(g_prevSlideshowBgBitmap.Get(), 1.0f - g_slideshowTransitionAlpha);
        DrawCoverBg(g_slideshowBgBitmap.Get(),             g_slideshowTransitionAlpha);

        // Uniform dim overlay on top of the blurred fill
        if (g_dimBrush)
        {
            g_dimBrush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, 0.45f * g_overlayAlpha));
            g_renderTarget->FillRectangle(D2D1::RectF(0, 0, rtSize.width, rtSize.height), g_dimBrush.Get());
        }
    }
    else if (g_isFullscreen && g_backgroundBitmap)
    {
        D2D1_SIZE_F size = g_backgroundBitmap->GetSize();

        g_renderTarget->DrawBitmap(
            g_backgroundBitmap.Get(),
            D2D1::RectF(0, 0, size.width, size.height),
            1.0f
        );

        if (g_dimBrush)
        {
            g_dimBrush->SetColor(D2D1::ColorF(0, 0, 0, 0.67f * g_overlayAlpha));
            g_renderTarget->FillRectangle(
                D2D1::RectF(0, 0, size.width, size.height),
                g_dimBrush.Get());
        }
    }    
    else if (!g_isFullscreen && g_slideshowBgBitmap)
    {
        DrawCoverBg(g_slideshowBgBitmap.Get(), 1.0f);

        // Slight dim so the image on top has contrast
        if (g_dimBrush)
        {
            g_dimBrush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, 0.35f));
            g_renderTarget->FillRectangle(D2D1::RectF(0, 0, rtSize.width, rtSize.height), g_dimBrush.Get());
        }
    }

    // Accumulate image info needed across the three-pass render split
    D2D1_SIZE_F imgSize  = {};
    float       imgOpacity   = 1.0f;
    bool        useD3D11Image = false;

    if (g_d2dBitmap)
    {        
        imgSize = g_d2dBitmap->GetSize();

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

        // ---- Compute transform for current image (shadow + mip quad share it) ----
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

        imgOpacity = g_isSlideshowMode ? g_slideshowTransitionAlpha : 1.0f;

        // During the pre-fade-to-black that precedes slideshow entry, the black
        // overlay is drawn in D2D Pass 1 and flushed before the D3D11 image quad
        // is drawn in Pass 2.  Without this modulation the image renders on top of
        // the overlay and never fades.  Multiplying here keeps them in sync.
        if (g_slideshowPreFade)
            imgOpacity *= (1.0f - g_slideshowPreFadeAlpha);

        // Shadow: use the pre-extracted small mip bitmap (set once per image load).
        // The small bitmap covers the same logical image rect as the full one, so we
        // push an extra scale that maps (shadowW x shadowH) → (imgW x imgH) before
        // applying the normal pan/zoom/rotation transform already on the context.
        // Result is visually identical to using the full bitmap — sigma=25 destroys
        // all detail — but the blur input is maybe 256×170 instead of 8000×5400.
        ID2D1Bitmap* shadowSrc = g_shadowSourceBitmap
                                 ? g_shadowSourceBitmap.Get()
                                 : (g_d2dBitmap ? g_d2dBitmap.Get() : nullptr);

        if (g_shadowEffect && shadowSrc)
        {
            D2D1_SIZE_F shSz = shadowSrc->GetSize();
            float scaleX = (shSz.width  > 0.f) ? imgSize.width  / shSz.width  : 1.f;
            float scaleY = (shSz.height > 0.f) ? imgSize.height / shSz.height : 1.f;

            // Temporarily extend the current transform with the small→full scale.
            D2D1::Matrix3x2F shadowScale =
                D2D1::Matrix3x2F::Scale(scaleX, scaleY, D2D1::Point2F(0.f, 0.f));

            g_renderTarget->SetTransform(shadowScale * baseTransform * rotation);

            g_shadowEffect->SetInput(0, shadowSrc);
            g_renderTarget->PushLayer(
                D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                    D2D1::IdentityMatrix(), imgOpacity),
                nullptr);
            g_renderTarget->DrawImage(g_shadowEffect.Get(), D2D1::Point2F(0.0f, 0.0f));
            g_renderTarget->PopLayer();

            // Restore the normal transform for anything drawn after.
            g_renderTarget->SetTransform(baseTransform * rotation);
        }

        // Decide which path draws the image bitmap itself:
        //   D3D11 path  — trilinear mip filtering, cache-friendly at all zoom levels
        //   D2D fallback — used only when the mip pipeline isn't available yet
        useD3D11Image = g_mipPipelineReady && (g_imageSRV != nullptr);

        if (!useD3D11Image)
        {
            // Fallback: D2D cubic (original behaviour, kept for safety)
            g_renderTarget->DrawBitmap(
                g_d2dBitmap.Get(),
                D2D1::RectF(0, 0, imgSize.width, imgSize.height),
                imgOpacity,
                D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC
            );
        }

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

    // Pre-fade overlay: full-screen opaque black drawn on top of everything,
    // including UI, until the slideshow overlay has presented its first frame.
    if (g_slideshowPreFade && g_slideshowPreFadeAlpha > 0.01f && g_blackBrush)
    {
        g_blackBrush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, g_slideshowPreFadeAlpha));
        g_renderTarget->FillRectangle(D2D1::RectF(0, 0, rtSize.width, rtSize.height), g_blackBrush.Get());
    }

    // =========================================================
    // When the D3D11 mip path is active we need three passes:
    //   Pass 1 (D2D)   — background + shadow + pre-fade  → EndDraw
    //   Pass 2 (D3D11) — image quad, trilinear mip filter
    //   Pass 3 (D2D)   — UI buttons + text boxes          → EndDraw → Present
    //
    // When D3D11 is NOT needed (no image, or mip pipeline not
    // ready yet) we stay in the original single-pass model so
    // that a second BeginDraw/EndDraw pair never splits a frame
    // that has no D3D11 work between them — avoiding any
    // potential back-buffer visibility issues in flip-model
    // swap chains.
    // =========================================================

    HRESULT hr = S_OK;

    if (useD3D11Image)
    {
        // ----- Pass 1 end: flush D2D background + shadow -----
        hr = g_renderTarget->EndDraw();

        if (SUCCEEDED(hr) && g_swapChain)
        {
            // ----- Pass 2: D3D11 draws the mip-mapped image quad -----
            RenderImageD3D11(imgSize.width, imgSize.height, imgOpacity);

            // ----- Pass 3: D2D draws UI on top -----
            g_renderTarget->BeginDraw();

            // Gradient vignette bars — only when an image is loaded.
            // Bar thickness is fixed in pixels (captured at fullscreen init); we
            // back-convert to normalized fractions for the current RT so the bars
            // never grow/shrink when the window is resized.
            if (g_d2dBitmap)
            {
                const float vigRTH        = rtSize.height > 0.f ? rtSize.height : 1.f;
                const float vigTopNorm    = g_vignetteHeightsCaptured ? (g_vignetteTopBarHeightPx    / vigRTH) : 0.08f;
                const float vigBottomNorm = g_vignetteHeightsCaptured ? (g_vignetteBottomBarHeightPx / vigRTH) : 0.14f;

                // Bottom bar: transparent at (1-vigBottomNorm) → dark at 1.0
                DrawVignetteBar(g_renderTarget.Get(),
                                1.f - vigBottomNorm, 1.f,
                                /*peakAlpha=*/0.65f,
                                g_bottomVignetteVisibility,
                                /*darkAtBottom=*/true);
                // Top bar: dark at 0.0 → transparent at vigTopNorm
                DrawVignetteBar(g_renderTarget.Get(),
                                0.f, vigTopNorm,
                                /*peakAlpha=*/0.65f,
                                g_topVignetteVisibility,
                                /*darkAtBottom=*/false);
            }

            // Update zoom text box
            {
                static float s_lastDisplayedZoom = -1.f;
                int zoomPct = int(std::round(g_zoom * 100));
                if (!g_textBoxes[TEXTBOX_ZOOM_INPUT].IsFocused())
                {
                    if (g_zoom != s_lastDisplayedZoom)
                    {
                        g_textBoxes[TEXTBOX_ZOOM_INPUT].SetText(std::to_wstring(zoomPct));
                        s_lastDisplayedZoom = g_zoom;
                    }
                }
            }

            // Thumbnail film strip (fades in/out with bottom vignette)
            DrawThumbnailStrip(g_bottomVignetteVisibility);

            for (auto& [id, btn] : g_buttons)
            {
                if (!g_isFullscreen && id == BUTTON_EXIT)
                    continue;
                btn.Draw(g_renderTarget.Get());
            }

            for (auto& [id, txt] : g_textBoxes)
                txt.Draw(g_renderTarget.Get());

            hr = g_renderTarget->EndDraw();

            g_swapChain->Present(1, 0);
            g_navPendingRender = false;  // frame delivered — next key repeat may fire

            if (g_slideshowPreFade && g_isSlideshowMode && g_overlayWindow)
            {
                DestroyBlackCoverWindow();
                g_slideshowPreFade      = false;
                g_slideshowPreFadeAlpha = 0.0f;
            }
        }
    }
    else
    {
        // ----- Single-pass: everything in one BeginDraw/EndDraw -----

        // Gradient vignette bars (same as three-pass path, pixel-fixed thickness)
        if (g_d2dBitmap)
        {
                const float vigRTH        = rtSize.height > 0.f ? rtSize.height : 1.f;
                const float vigTopNorm    = g_vignetteHeightsCaptured ? (g_vignetteTopBarHeightPx    / vigRTH) : 0.08f;
                const float vigBottomNorm = g_vignetteHeightsCaptured ? (g_vignetteBottomBarHeightPx / vigRTH) : 0.14f;

                DrawVignetteBar(g_renderTarget.Get(),
                                1.f - vigBottomNorm, 1.f,
                                /*peakAlpha=*/0.65f,
                                g_bottomVignetteVisibility,
                                /*darkAtBottom=*/true);
                DrawVignetteBar(g_renderTarget.Get(),
                                0.f, vigTopNorm,
                                /*peakAlpha=*/0.65f,
                                g_topVignetteVisibility,
                                /*darkAtBottom=*/false);
        }

        // Update zoom text box
        {
            static float s_lastDisplayedZoom = -1.f;
            int zoomPct = int(std::round(g_zoom * 100));
            if (!g_textBoxes[TEXTBOX_ZOOM_INPUT].IsFocused())
            {
                if (g_zoom != s_lastDisplayedZoom)
                {
                    g_textBoxes[TEXTBOX_ZOOM_INPUT].SetText(std::to_wstring(zoomPct));
                    s_lastDisplayedZoom = g_zoom;
                }
            }
        }

        // Thumbnail film strip (fades in/out with bottom vignette)
        //if (g_isFullscreen)
            DrawThumbnailStrip(g_bottomVignetteVisibility);

        for (auto& [id, btn] : g_buttons)
        {
            if (!g_isFullscreen && id == BUTTON_EXIT)
                continue;
            btn.Draw(g_renderTarget.Get());
        }

        for (auto& [id, txt] : g_textBoxes)
            txt.Draw(g_renderTarget.Get());

        hr = g_renderTarget->EndDraw();

        if (SUCCEEDED(hr) && g_swapChain)
        {
            g_swapChain->Present(1, 0);
            g_navPendingRender = false;  // frame delivered

            if (g_slideshowPreFade && g_isSlideshowMode && g_overlayWindow)
            {
                DestroyBlackCoverWindow();
                g_slideshowPreFade      = false;
                g_slideshowPreFadeAlpha = 0.0f;
            }
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
    g_gifD2DBitmaps.clear();
    g_gifD3DSRVs.clear();
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

    // Pre-upload all GIF frames to GPU so UpdateEngine can swap bitmaps
    // without a CPU→GPU upload every frame.
    if (g_isAnimatedGif && g_renderTarget)
    {
        g_gifD2DBitmaps.clear();
        g_gifD2DBitmaps.reserve(g_gifFrames.size());
        g_gifD3DSRVs.clear();
        g_gifD3DSRVs.reserve(g_gifFrames.size());
        for (auto& wicFrame : g_gifFrames)
        {
            // D2D bitmap
            ComPtr<ID2D1Bitmap> bmp;
            g_renderTarget->CreateBitmapFromWicBitmap(wicFrame.Get(), nullptr, &bmp);
            g_gifD2DBitmaps.push_back(bmp);

            // D3D11 mip SRV — reuse CreateMipTextureFromSource; grab the SRV it leaves in g_imageSRV
            if (g_mipPipelineReady)
            {
                CreateMipTextureFromSource(wicFrame.Get());
                g_gifD3DSRVs.push_back(g_imageSRV);
            }
            else
            {
                g_gifD3DSRVs.push_back(nullptr);
            }
        }
        // Point both pointers at frame 0
        if (!g_gifD2DBitmaps.empty() && g_gifD2DBitmaps[0])
            g_d2dBitmap = g_gifD2DBitmaps[0];
        if (!g_gifD3DSRVs.empty() && g_gifD3DSRVs[0])
            g_imageSRV = g_gifD3DSRVs[0];
    }

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

    // Start watching the directory for new/deleted/renamed image files
    StartDirectoryWatcher(std::filesystem::path(filename).parent_path().wstring());
    StartThumbnailLoader();
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
    if (!g_imageFiles.empty() && g_currentImageIndex > 0)
    {
        g_currentImageIndex--;
        LoadImageD2D(hWnd, g_imageFiles[g_currentImageIndex].c_str());
        InitializeImageLayout(hWnd, true);
    }
}

void OpenNextImage(HWND hWnd)
{
    if (!g_imageFiles.empty() &&
        g_currentImageIndex < (int)g_imageFiles.size() - 1)
    {
        g_currentImageIndex++;
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


// ============================================================
//  Thumbnail film strip
// ============================================================

// ============================================================
//  Directory watcher (ReadDirectoryChangesW)
// ============================================================

void StopDirectoryWatcher()
{
    g_watchStop = true;
    // Cancel any blocking ReadDirectoryChangesW by closing the handle
    if (g_watchHandle != INVALID_HANDLE_VALUE)
    {
        CancelIoEx(g_watchHandle, nullptr);
        CloseHandle(g_watchHandle);
        g_watchHandle = INVALID_HANDLE_VALUE;
    }
    if (g_watchThread.joinable())
        g_watchThread.join();
    g_watchStop = false;
}

void StartDirectoryWatcher(const std::wstring& dir)
{
    StopDirectoryWatcher();

    g_watchHandle = CreateFileW(
        dir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (g_watchHandle == INVALID_HANDLE_VALUE)
        return;

    g_watchThread = std::thread([dir]()
    {
        // Aligned buffer for FILE_NOTIFY_INFORMATION records
        alignas(DWORD) BYTE buf[4096];
        OVERLAPPED ov = {};
        ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return;

        while (!g_watchStop)
        {
            DWORD bytesReturned = 0;
            ResetEvent(ov.hEvent);

            BOOL ok = ReadDirectoryChangesW(
                g_watchHandle,
                buf, sizeof(buf),
                FALSE,  // non-recursive
                FILE_NOTIFY_CHANGE_FILE_NAME,  // created, deleted, renamed
                nullptr,
                &ov,
                nullptr);

            if (!ok) break;

            // Wait for the overlapped result or a stop signal
            DWORD wait = WaitForSingleObject(ov.hEvent, INFINITE);
            if (g_watchStop || wait != WAIT_OBJECT_0) break;

            if (!GetOverlappedResult(g_watchHandle, &ov, &bytesReturned, FALSE))
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
                // Post to main window — safe to call from any thread
                HWND target = g_overlayWindow ? g_overlayWindow : g_mainWindow;
                if (target)
                    PostMessage(target, WM_APP_DIRCHANGE, 0, 0);
            }
        }
        CloseHandle(ov.hEvent);
    });
}

void StopThumbnailLoader()
{
    g_thumbLoaderStop = true;
    if (g_thumbLoaderThread.joinable())
        g_thumbLoaderThread.join();
    g_thumbLoaderStop = false;
}

void StartThumbnailLoader()
{
    // Stop the thread first so there is no race when we read g_thumbs below.
    StopThumbnailLoader();

    const int n = (int)g_imageFiles.size();

    // Build a path→wic map from the CURRENT g_thumbs vector (thread is stopped
    // so this is safe). g_thumbs may be a different size than g_imageFiles if a
    // file was just deleted, so we zip by the PREVIOUS g_imageFiles snapshot
    // stored in g_thumbs. Since we cannot access the old list here, we key by
    // position only for entries that are still within range — this is safe
    // because the caller already erased the deleted entry from g_imageFiles
    // AND from g_thumbs (via the erase in the delete handler... wait, we removed
    // that). So instead, the caller must pass the old file list. We handle this
    // by keeping a persistent path→wic cache that callers update:
    // actually, simplest correct approach — the caller snapshots the map before
    // erasing. See the comment at the call site in the DEL handler.
    //
    // For non-delete calls (BuildImageList), g_thumbs is empty so nothing
    // is preserved and the loader decodes everything fresh.

    // Resize g_thumbs to match the new file list, preserving existing entries
    // by path lookup via a temporary snapshot taken before any index shift.
    // The snapshot is built by the caller into g_thumbSavedWic when needed.
    g_thumbs.resize(n);  // new slots default-construct to null wic+d2d

    {
        std::lock_guard<std::mutex> lk(g_thumbReadyMutex);
        while (!g_thumbReadyQueue.empty()) g_thumbReadyQueue.pop();
    }

    if (n == 0) return;

    // Build work list: only files whose slot has no WIC bitmap yet.
    // Spiral outward from the current image so the visible window fills first.
    const int cur = max(0, min(g_currentImageIndex, n - 1));
    std::vector<int> order;
    order.reserve(n);
    for (int d = 0; d < n; ++d)
    {
        const int a = cur + d, b = cur - d;
        if (d == 0) { if (!g_thumbs[cur].wic) order.push_back(cur); }
        else {
            if (a < n  && !g_thumbs[a].wic) order.push_back(a);
            if (b >= 0 && !g_thumbs[b].wic) order.push_back(b);
        }
    }

    if (order.empty()) return;  // everything already loaded

    g_thumbLoaderThread = std::thread(
        [files = g_imageFiles, indices = std::move(order)]()
        {
            if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;

            Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) || !wic)
            { CoUninitialize(); return; }

            for (int idx : indices)
            {
                if (g_thumbLoaderStop) break;
                if (idx < 0 || idx >= (int)files.size()) continue;

                Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
                if (FAILED(wic->CreateDecoderFromFilename(files[idx].c_str(), nullptr,
                        GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder)
                    continue;
                if (g_thumbLoaderStop) break;

                Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
                if (FAILED(decoder->GetFrame(0, &frame)) || !frame) continue;
                if (g_thumbLoaderStop) break;

                UINT sw = 0, sh = 0;
                frame->GetSize(&sw, &sh);
                if (sw == 0 || sh == 0) continue;

                const UINT TARGET_H = 180u;
                const UINT dw = max(1u, (UINT)((float)sw / (float)sh * TARGET_H));

                Microsoft::WRL::ComPtr<IWICBitmapScaler>    scaler;
                Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
                Microsoft::WRL::ComPtr<IWICBitmap>          thumb;

                if (FAILED(wic->CreateBitmapScaler(&scaler)) ||
                    FAILED(scaler->Initialize(frame.Get(), dw, TARGET_H,
                        WICBitmapInterpolationModeHighQualityCubic)) ||
                    FAILED(wic->CreateFormatConverter(&conv)) ||
                    FAILED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                        WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom)))
                    continue;
                if (g_thumbLoaderStop) break;

                if (FAILED(wic->CreateBitmapFromSource(conv.Get(), WICBitmapCacheOnLoad, &thumb)) || !thumb)
                    continue;

                {
                    std::lock_guard<std::mutex> lk(g_thumbReadyMutex);
                    g_thumbReadyQueue.push({ idx, thumb });
                }
            }
            CoUninitialize();
        });
}

void DrawThumbnailStrip(float visibility)
{
    if (!g_renderTarget || !g_d2dFactory)         return;
    if (visibility <= 0.01f)                       return;
    if (g_thumbs.empty() || !g_thumbSizeCaptured) return;
    if (g_currentImageIndex < 0)                   return;

    const D2D1_SIZE_F rtSz  = g_renderTarget->GetSize();
    const float stride = g_thumbW + g_thumbGap;
    const int   n      = (int)g_thumbs.size();
    const float baseX  = rtSz.width * 0.5f + g_thumbScrollOffset;
    const float cy     = rtSz.height - g_thumbStripInsetFromBottomPx;  // anchored: fixed distance from bottom
    const float hw     = g_thumbW * 0.5f;
    const float hh     = g_thumbH * 0.5f;
    const float radius = g_thumbH * 0.10f;

    // One cached geometry centred at the origin; we move the render target
    // transform to each thumbnail position so every slot reuses the same geometry.
    ID2D1RoundedRectangleGeometry* clipGeo =
        (!g_thumbClipGeos.empty()) ? g_thumbClipGeos[0].Get() : nullptr;

    // Use the base ID2D1RenderTarget pointer so PushLayer resolves to the
    // D2D1_LAYER_PARAMETERS (not D2D1_LAYER_PARAMETERS1) overload.
    ID2D1RenderTarget* rt = g_renderTarget.Get();

    for (int i = 0; i < n; ++i)
    {
        const float cx = baseX + i * stride;
        if (cx + hw < 0.f || cx - hw > rtSz.width) continue;

        const D2D1_RECT_F       r  = D2D1::RectF(cx - hw, cy - hh, cx + hw, cy + hh);
        const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, radius, radius);
        const bool isCurrent = (i == g_currentImageIndex);

        if (g_thumbs[i].d2d && clipGeo)
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
            const D2D1_SIZE_F bsz = g_thumbs[i].d2d->GetSize();
            const float bA = bsz.width  / max(1.f, bsz.height);
            const float tA = g_thumbW   / max(1.f, g_thumbH);
            float srcL = 0.f, srcT = 0.f, srcR = bsz.width, srcB = bsz.height;
            if (bA > tA) {
                const float nw = bsz.height * tA;
                srcL = (bsz.width  - nw) * 0.5f; srcR = srcL + nw;
            } else {
                const float nh = bsz.width / max(0.001f, tA);
                srcT = (bsz.height - nh) * 0.5f; srcB = srcT + nh;
            }

            const D2D1_RECT_F srcRect = D2D1::RectF(srcL, srcT, srcR, srcB);
            rt->DrawBitmap(g_thumbs[i].d2d.Get(), &r, 1.0f,
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

void InitFullScreenExit()
{
    D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();
    float centerX = g_offsetX + imgSize.width  * g_zoom / 2.0f;
    float centerY = g_offsetY + imgSize.height * g_zoom / 2.0f;
    
    g_targetOffsetX = centerX - (imgSize.width * 0.05f) / 2.0f;
    g_targetOffsetY = centerY - (imgSize.height * 0.05f) / 2.0f;
    
    int refreshRate = GetMonitorRefreshRate(GetDesktopWindow());
    g_smooth = 1.0f - std::powf(1.0f - 2*0.18f, 60.0f / (float)refreshRate);
    g_targetZoom = 0.0005f;
    g_isExiting = true;
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
                // Post rather than call directly — ExitFullscreen destroys the
                // overlay and calls SetFocus while Windows is mid-focus-change,
                // which leaves the main window visually active but without real
                // keyboard focus. Deferring lets the OS finish first.
                PostMessage(g_mainWindow, WM_APP_EXITFULLSCREEN, 0, 0);
            }
        }
        else
        {
            g_navPendingRender = false;
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

        // Unbind current D2D target and D3D11 RTV before ResizeBuffers —
        // both wrap the old back buffer which is about to be released.
        g_renderTarget->SetTarget(nullptr);
        g_d2dTargetBitmap.Reset();
        g_swapRTV.Reset();   // must release before ResizeBuffers

        HRESULT hr = g_swapChain->ResizeBuffers(
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
        if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface))) &&
            SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuf))))
        {
            D2D1_BITMAP_PROPERTIES1 props = SwapChainBitmapProps();

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

            // Recreate the D3D11 RTV that RenderImageD3D11 binds as render target
            if (g_d3dDevice)
                g_d3dDevice->CreateRenderTargetView(backBuf.Get(), nullptr, &g_swapRTV);
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
        // Thumbnail strip click: check before starting a drag
        if (g_thumbSizeCaptured && !g_thumbs.empty())
        {
            RECT rc2; GetClientRect(hWnd, &rc2);
            const float rtH    = (float)(rc2.bottom - rc2.top);
            const float stripCY = rtH - g_thumbStripInsetFromBottomPx;
            const float stripTop    = stripCY - g_thumbH * 0.5f;
            const float stripBottom = stripCY + g_thumbH * 0.5f;
            if (y >= stripTop && y <= stripBottom)
            {
                RECT rc2; GetClientRect(hWnd, &rc2);
                const float rtW    = (float)(rc2.right - rc2.left);
                const float baseX  = rtW * 0.5f + g_thumbScrollOffset;
                const float stride = g_thumbW + g_thumbGap;
                const int   n      = (int)g_thumbs.size();
                for (int i = 0; i < n; ++i)
                {
                    const float cx = baseX + i * stride;
                    if (x >= cx - g_thumbW * 0.5f && x <= cx + g_thumbW * 0.5f)
                    {
                        if (i != g_currentImageIndex && i < (int)g_imageFiles.size())
                        {
                            g_currentImageIndex = i;
                            LoadImageD2D(hWnd, g_imageFiles[i].c_str());
                        }
                        return 0;
                    }
                }
            }
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

        // Vignette activation zones: top 20 % for top bar, bottom 20 % for bottom bar.
        // Only show when an image is loaded (same rule as the buttons).
        if (g_d2dBitmap)
        {
            // Use pixel-fixed activation thresholds (captured at fullscreen init);
            // fall back to normalized 20% if not yet captured.
            const float topThresh    = g_vignetteHeightsCaptured
                                           ? g_vignetteTopActivationPx
                                           : windowHeight * 0.20f;
            const float bottomThresh = g_vignetteHeightsCaptured
                                           ? (windowHeight - g_vignetteBottomActivationPx)
                                           : windowHeight * 0.80f;
            g_topVignetteTarget    = ((float)pt.y <= topThresh)    ? 1.0f : 0.0f;
            g_bottomVignetteTarget = ((float)pt.y >= bottomThresh) ? 1.0f : 0.0f;
        }
        else
        {
            g_topVignetteTarget    = 0.0f;
            g_bottomVignetteTarget = 0.0f;
        }


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
                    // Remove the deleted file's saved view state so no other
                    // image can accidentally inherit it (e.g. if a new file
                    // with the same name is later added to the same folder).
                    g_imageStates.erase(fileToDelete);

                    // Stop the loader before touching g_thumbs (no race).
                    StopThumbnailLoader();

                    // Snapshot BOTH wic and d2d by path BEFORE erasing g_imageFiles
                    // so indices still match. D2D bitmaps are still valid — this is
                    // not a device loss, just a file list change.
                    struct ThumbSnap { Microsoft::WRL::ComPtr<IWICBitmap> wic;
                                       Microsoft::WRL::ComPtr<ID2D1Bitmap> d2d; };
                    std::unordered_map<std::wstring, ThumbSnap> snap;
                    for (int si = 0; si < (int)g_thumbs.size() &&
                                     si < (int)g_imageFiles.size(); ++si)
                        if (g_thumbs[si].wic || g_thumbs[si].d2d)
                            snap[g_imageFiles[si]] = { g_thumbs[si].wic, g_thumbs[si].d2d };

                    g_imageFiles.erase(g_imageFiles.begin() + g_currentImageIndex);

                    // Rebuild g_thumbs, restoring both wic and d2d so the D2D
                    // bitmaps don't go gray and the spiral skips already-loaded slots.
                    const int newN = (int)g_imageFiles.size();
                    g_thumbs.assign(newN, ThumbnailEntry{});
                    for (int si = 0; si < newN; ++si)
                    {
                        auto it = snap.find(g_imageFiles[si]);
                        if (it != snap.end())
                        {
                            g_thumbs[si].wic = it->second.wic;
                            g_thumbs[si].d2d = it->second.d2d;
                        }
                    }

                    if (g_imageFiles.empty())
                    {
                        InitFullScreenExit();
                        return 0;
                    }

                    if (g_currentImageIndex >= (int)g_imageFiles.size())
                        g_currentImageIndex = (int)g_imageFiles.size() - 1;

                    LoadImageD2D(hWnd, g_imageFiles[g_currentImageIndex].c_str());

                    // Reset zoom/pan for the incoming image when it has no
                    // saved state — mirrors what OpenNextImage/OpenPrevImage do.
                    // Without this call, the deleted image's view state bleeds
                    // into the next image because the global zoom/pan vars are
                    // never updated when g_restoredStateThisLoad is false.
                    InitializeImageLayout(hWnd, true);
                    StartThumbnailLoader();  // reload with updated file list
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
                    InitFullScreenExit();
                    return 0;
                }
                else
                {
                    PostQuitMessage(0);
                }
            }
            break;
            case 0x46: // 'F' key — ignore auto-repeat to prevent toggle glitches
            {
                if (lParam & (1 << 30)) break;  // auto-repeat, skip
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
                if (g_navPendingRender) break;
                g_navPendingRender = true;
                OpenPrevImage(hWnd);
                if (g_isSlideshowMode)
                    KillTimer(g_mainWindow, SLIDESHOW_TIMER_ID);
                    SetTimer(g_mainWindow, SLIDESHOW_TIMER_ID, SLIDESHOW_INTERVAL_MS, nullptr);
                return 0;
            }

            case 0x44: // 'D' key
            case VK_SPACE:
            case VK_RIGHT:
            {
                if (g_navPendingRender) break;
                g_navPendingRender = true;
                OpenNextImage(hWnd);
                if (g_isSlideshowMode)
                    KillTimer(g_mainWindow, SLIDESHOW_TIMER_ID);
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

    case WM_KEYUP:
    {
        // Handle incomplete rotations
        SetTimer(hWnd, 123, 120, nullptr);
    }
    break;
    
    case WM_APP_EXITFULLSCREEN:
    {
        if (g_isFullscreen)
            ExitFullscreen();
        return 0;
    }
    break;
 
    case WM_APP_DIRCHANGE:
    {
        // A supported image file was created, deleted, or renamed in the
        // current directory. Rebuild the file list, preserving the current
        // image if it still exists, or clamping to the nearest valid index.
        if (!g_currentFilePath.empty())
        {
            const std::wstring currentPath = g_currentFilePath;
            BuildImageList(currentPath.c_str());

            // BuildImageList sets g_currentImageIndex to -1 if the current
            // file no longer exists; clamp to a valid image in that case.
            if (g_currentImageIndex < 0 && !g_imageFiles.empty())
            {
                g_currentImageIndex = 0;
                LoadImageD2D(hWnd, g_imageFiles[0].c_str());
                InitializeImageLayout(hWnd, true);
            }
        }
        return 0;
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

            StopDirectoryWatcher();
            StopThumbnailLoader();
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