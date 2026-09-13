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
// writing — if your build reports different .lib names (this has shifted
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

// Local struct declarations:
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

// Right-click context menu command IDs
constexpr int IDM_CTX_COPY        = 2001;
constexpr int IDM_CTX_DELETE      = 2002;
constexpr int IDM_CTX_OPEN_FOLDER = 2003;
constexpr int IDM_CTX_PROPERTIES  = 2004;
constexpr int IDM_CTX_WALLPAPER   = 2005;
constexpr int IDM_CTX_METADATA    = 2006;

// Metadata viewer window control IDs
constexpr int IDC_METADATA_EDIT       = 4001;
constexpr int IDC_METADATA_SELECTALL  = 4002;

// Hamburger menu command IDs
constexpr int IDM_MENU_SHORTCUTS  = 3001;
constexpr int IDM_MENU_ABOUT      = 3002;
constexpr int IDM_MENU_ASSOCIATE  = 3003;
constexpr int IDM_MENU_HQ_FILTER  = 3004;
constexpr int IDM_MENU_FIT_TO_SCREEN = 3005;

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
float                                               g_exifRotation = 0.0f;              // EXIF orientation for current image (degrees CW)
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
// Reference count of active "don't exit fullscreen" scopes. A plain bool
// here is fragile: any early return/break between setting it true and
// setting it false again leaves fullscreen-exit permanently disabled.
// Use FullscreenExitSuppressor (RAII, defined below) instead of touching
// this directly — it can't be forgotten because the destructor always runs.
// This should only be needed for UI that steals OS foreground activation
// without going through a real owned window (see FullscreenExitSuppressor
// comment below) — anything created as an owned window of g_mainWindow /
// g_overlayWindow (DialogBox, MessageBox, our own CreateWindowExW popups,
// common dialogs with hwndOwner set) is recognized automatically by
// IsOwnedByOurWindow() in WM_ACTIVATE and needs no manual suppression at all.
int                                                 g_suppressFullscreenExitDepth = 0;
std::vector<std::wstring>                           g_imageFiles;
int                                                 g_currentImageIndex = -1;
std::wstring                                        g_currentFilePath;
std::wstring                                        g_currentFileName;
std::wstring                                        g_lastLoadError;                    // human-readable reason the most recent LoadImageD2D failed
int                                                 g_imageWidth  = 0;
int                                                 g_imageHeight = 0;
std::unordered_map<std::wstring, ImageViewState>    g_imageStates;
bool                                                g_restoredStateThisLoad = false;
ComPtr<IDWriteFactory>                              g_dwriteFactory;
ComPtr<IDWriteTextFormat>                           g_textFormat;
bool                                                g_launchedWithFile = false;
float                                               g_smooth = 0.13f;
bool                                                g_pendingPreserveView = false;
// ---- Animated-image playback state (shared by GIF, WebP, and AVIF) ----
// Every animated format this app supports (GIF, WebP, and AVIF) is decoded
// into this same set of frame vectors and played back through the same code
// in UpdateEngine/RecreateImageBitmap — only the per-frame *decode* routine
// differs (DecodeGifCompositeFrames / DecodeWebpFrames / DecodeAvifFrames),
// because GIF frames must be composited against prior frames' disposal
// state, while WebP and AVIF frames each decode (or seek) independently.
// See LoadImageD2D for where the three formats' loaders meet back up and
// populate this shared state.
std::vector<ComPtr<IWICBitmapSource>>               g_animFrames;
std::vector<ComPtr<ID2D1Bitmap>>                    g_animD2DBitmaps;         // pre-uploaded GPU bitmaps for each animation frame
std::vector<ComPtr<ID3D11ShaderResourceView>>       g_animD3DSRVs;            // pre-uploaded D3D11 mip SRVs for each animation frame (stays null; animated images skip the mip pipeline)
ULONGLONG                                           g_lastAnimFrameTime = 0;
std::vector<UINT>                                   g_animFrameDelays;
UINT                                                g_currentAnimFrame = 0;
bool                                                g_isAnimatedImage = false;   // true for animated GIF, animated WebP, and animated AVIF alike
// ---- Background animated-frame decoding ----
// Frame 0 is decoded synchronously in LoadImageD2D so the window can open
// immediately; every later frame is decoded/composited on this thread. For
// GIF specifically this can't be parallelized across frames (disposal makes
// frames inherently sequential - frame N's canvas state depends on frame
// N-1's disposal), only moved off the UI thread; WebP frames are independent
// but are still decoded on this same background thread for a uniform
// loading path. g_animFramesReadyUpTo is the count of frames, starting from
// 0, that are safe to display right now; it only ever grows, and only this
// thread writes past index 0.
std::thread                                         g_animDecodeThread;
std::atomic<bool>                                   g_animDecodeStop{ false };
std::atomic<int>                                    g_animFramesReadyUpTo{ 0 };
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
bool                                                g_useHQFilter      = false;  // when true, forces D2D HIGH_QUALITY_CUBIC instead of D3D11 trilinear
bool                                                g_openFitToScreen  = false;  // when true, images open scaled to fit the screen instead of 100%

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

// ---- Thumbnail strip drag / momentum ----
bool  g_thumbDragging     = false;  // LMB held over strip, drag in progress
float g_thumbDragStartX   = 0.f;    // cursor X at mouse-down (click vs drag test)
float g_thumbDragLastX    = 0.f;    // cursor X at last WM_MOUSEMOVE
float g_thumbVelocity     = 0.f;    // smoothed px/event velocity (EMA) for momentum
bool  g_thumbFreeScroll   = false;  // true = UpdateEngine skips auto-centering
int   g_thumbDragHitIndex = -1;     // thumbnail slot under cursor at mouse-down (-1 = gap)

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
constexpr UINT_PTR                                  SLIDESHOW_TIMER_ID           = 0xcafe;
constexpr UINT_PTR                                  ZOOM_DISPLAY_TIMER_ID        = 0xbebe;
constexpr UINT_PTR                                  KILL_ROTATION_TIMER_ID       = 0xbeca;
constexpr UINT_PTR                                  ENTER_FULL_SCREEN_TIMER_ID   = 0xcaca;
constexpr UINT                                      SLIDESHOW_INTERVAL_MS        = 6000;  // ms between pictures
bool                                                g_slideshowPreFade           = false; // true from button-press until first overlay Present()
float                                               g_slideshowPreFadeAlpha      = 0.0f;  // 0=current view visible, 1=fully black
HWND                                                g_blackCoverWindow           = nullptr;
HWND                                                g_metadataViewerWnd          = nullptr;

// ---- Fullscreen exit fade ----
// While g_pendingExitFullscreen is true the overlay stays alive while
// g_exitBgFadeAlpha animates 0->1, then CompleteExitFullscreen() tears it down.
// g_wasInSlideshowOnExit lets Render cross-fade the blurred bg -> desktop bg.
bool                                                g_pendingExitFullscreen      = false;
bool                                                g_wasInSlideshowOnExit       = false;
float                                               g_exitBgFadeAlpha            = 0.f;

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
void ExitFullscreen(bool immediate = false);
static void CompleteExitFullscreen();

// ---- Fullscreen exit suppression ---------------------------------------
//
// WM_ACTIVATE(WA_INACTIVE) fires on g_mainWindow/g_overlayWindow whenever
// OS foreground activation moves away from them — both for a genuine
// focus loss (Alt+Tab, clicking another app) and for the harmless case of
// one of our *own* windows (About box, metadata viewer, a common dialog)
// taking focus. We only want to exit fullscreen for the former.
//
// WM_ACTIVATE's lParam gives us the HWND that is becoming active, so we
// can tell the two cases apart structurally: walk that window's owner
// chain and see if it leads back to one of our own top-level windows.
// Any window we create as an *owned* window (DialogBox's parent hWnd,
// MessageBox's hWnd, CreateWindowExW's hWndParent, a common dialog's
// hwndOwner) is caught by this automatically — no per-call-site flag to
// remember. That means new dialogs added later are safe by default,
// instead of silently missing suppression until someone notices (as
// happened with one of the context-menu handlers under the old scheme,
// where the manual flag was simply never added at that call site).
//
// NOTE: IDM_CTX_PROPERTIES used to invoke the shell's external "Properties"
// verb, which wasn't one of our own owned windows and so needed manual
// carve-out here. It's since been replaced by ShowImageProperties(), an
// in-app DialogBox (IDD_PROPERTIES, same style of bespoke dialog template
// as "Keyboard shortcuts"), so it's now covered automatically by
// IsOwnedByOurWindow() like everything else — no special case needed.
//
// A manual suppression scope (FullscreenExitSuppressor) is only needed
// for UI that steals foreground activation *without* being an owned
// window of ours — e.g. temporarily forcing foreground onto our own
// window before TrackPopupMenu. Use it as an RAII guard, never as a
// raw true/false pair: the destructor always runs, so it can't be left
// stuck on by an early return/break the way the old bool could.
static bool IsOwnedByOurWindow(HWND hwnd)
{
    HWND w = hwnd;
    for (int i = 0; w != nullptr && i < 8; ++i)   // bounded: owner chains are shallow
    {
        if (w == g_mainWindow || w == g_overlayWindow)
            return true;
        w = GetWindow(w, GW_OWNER);
    }
    return false;
}

struct FullscreenExitSuppressor
{
    FullscreenExitSuppressor()  { ++g_suppressFullscreenExitDepth; }
    ~FullscreenExitSuppressor() { --g_suppressFullscreenExitDepth; }
    FullscreenExitSuppressor(const FullscreenExitSuppressor&) = delete;
    FullscreenExitSuppressor& operator=(const FullscreenExitSuppressor&) = delete;
};
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
static bool LoadFail(const wchar_t* msg, HRESULT hr = S_OK);
static bool DecodeAvifToWicBitmap(const wchar_t* path, IWICImagingFactory* wicFactory, ComPtr<IWICBitmap>& outBitmap, UINT& outW, UINT& outH, std::wstring* outReason = nullptr);
static bool DecodeJxlToWicBitmap(const wchar_t* path, IWICImagingFactory* wicFactory, ComPtr<IWICBitmap>& outBitmap, UINT& outW, UINT& outH, std::wstring* outReason = nullptr);
static bool FinishImageLoad(HWND hWnd, const wchar_t* filename);
void BuildImageList(const wchar_t* filename);
bool OpenImageFile(HWND hWnd);
void OpenNextImage(HWND hWnd);
void OpenPrevImage(HWND hWnd);
void DeleteCurrentImage(HWND hWnd, bool permanent);
void AssociateFileTypes(HWND hWnd);
void ShowImageMetadata(HWND hWnd);
void ShowImageProperties(HWND hWnd);
INT_PTR CALLBACK PropertiesDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
void ShowMetadataViewerWindow(HWND owner, const std::wstring& title, const std::wstring& body);
bool ZoomIntoImage(HWND hWnd, short delta, POINT* optionalPt);
void MakeZoomVisible(HWND hWnd);
void StopThumbnailLoader();
void StartThumbnailLoader();
static void StopAnimDecodeThread();
static UINT DecodeGifCompositeFrames(
    IWICImagingFactory* wicFactory,
    IWICBitmapDecoder* decoder,
    UINT frameCount,
    UINT maxFrames,
    const std::atomic<bool>* stopFlag,
    UINT* outCanvasW,
    UINT* outCanvasH,
    const std::function<void(UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)>& onFrame);
static UINT DecodeWebpFrames(
    IWICImagingFactory* wicFactory,
    IWICBitmapDecoder* decoder,
    UINT frameCount,
    UINT startFrame,
    UINT maxFrames,
    const std::atomic<bool>* stopFlag,
    UINT* outW,
    UINT* outH,
    const std::function<void(UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)>& onFrame);
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
static UINT AvifPeekFrameCount(const wchar_t* path);
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

    // Restore persisted settings from registry
    {
        DWORD hqVal = 0, cb = sizeof(hqVal);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\PicassoPictures",
                     L"HQFilter", RRF_RT_REG_DWORD, nullptr, &hqVal, &cb);
        g_useHQFilter = (hqVal != 0);

        DWORD fitVal = 0; cb = sizeof(fitVal);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\PicassoPictures",
                     L"FitToScreen", RRF_RT_REG_DWORD, nullptr, &fitVal, &cb);
        g_openFitToScreen = (fitVal != 0);
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
    g_smooth = g_smooth * 60.0f / (float)refreshRate;

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

    HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCEW(IDR_DEFAULT_BKG_JPG), L"JPG");
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
    g_animD2DBitmaps.clear();
    g_animD3DSRVs.clear();

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
    if (g_isAnimatedImage && !g_animFrames.empty())
    {
        ULONGLONG now = GetTickCount64();
        if (now - g_lastAnimFrameTime >= g_animFrameDelays[g_currentAnimFrame])
        {
            const UINT nextFrame = (g_currentAnimFrame + 1) % g_animFrames.size();
            const UINT readyCount = (UINT)max(0, g_animFramesReadyUpTo.load());

            // While the background decode thread is still working through a
            // large animated image (GIF, WebP, or AVIF), don't advance past
            // the last frame it's actually produced yet (this also blocks
            // wrapping around to frame 0 before the tail end of a long
            // animation has been decoded even once). Just hold on the
            // current frame and check again next tick — once fully caught
            // up, readyCount == frame count and this condition is always
            // true, i.e. normal playback resumes.
            if (nextFrame < readyCount || readyCount >= g_animFrames.size())
            {
                g_currentAnimFrame = nextFrame;

                // Already uploaded? Just swap the pointer — no CPU→GPU work.
                if (g_currentAnimFrame < g_animD2DBitmaps.size() && g_animD2DBitmaps[g_currentAnimFrame])
                {
                    g_d2dBitmap = g_animD2DBitmaps[g_currentAnimFrame];
                    g_imageSRV.Reset();  // animated formats never carry a mip SRV
                }
                else
                {
                    // First time reaching this frame: decode/upload it now, and
                    // cache the result so it's never re-uploaded on later loops.
                    g_wicBitmapSource = g_animFrames[g_currentAnimFrame];
                    RecreateImageBitmap();

                    if (g_currentAnimFrame < g_animD2DBitmaps.size())
                        g_animD2DBitmaps[g_currentAnimFrame] = g_d2dBitmap;
                    if (g_currentAnimFrame < g_animD3DSRVs.size())
                        g_animD3DSRVs[g_currentAnimFrame] = g_imageSRV;  // stays null (animated = no mips)
                }
            }

            g_lastAnimFrameTime = now;
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
    g_overlayAlpha += (overlayTarget - g_overlayAlpha) * 0.15f * dt * 60.0f;

    // ---- Slideshow pre-fade (current view → black, then window transition) ----
    // g_slideshowPreFade stays true until Render() confirms the overlay's first Present().
    if (g_slideshowPreFade && !g_isSlideshowMode)
    {
        // Phase 1: fade the current render target to black (slow ease-out)
        g_slideshowPreFadeAlpha += 0.02f * dt * 60.0f;

        if (g_slideshowPreFadeAlpha > 0.995f)
        {
            g_slideshowPreFadeAlpha = 1.0f;

            // Phase 2: screen is provably black in D2D — now raise the cover
            // window and flush DWM so it is physically on screen before we
            // destroy the render target.
            CreateBlackCoverWindow();

            if (g_isFullscreen) ExitFullscreen(/*immediate=*/true);
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

    // ---- Fullscreen exit fade (desktop bg fades in, dim lifts, then teardown) ----
    if (g_pendingExitFullscreen && g_isFullscreen)
    {
        g_exitBgFadeAlpha += 0.04f * dt * 60.0f;

        if (g_exitBgFadeAlpha > 0.995f)
        {
            g_exitBgFadeAlpha       = 1.0f;
            g_pendingExitFullscreen = false;
            CompleteExitFullscreen();
        }
    }

    // ---- Slideshow cross-fade (image-to-image transition) ----
    if (g_isSlideshowMode && g_slideshowTransitionAlpha < g_slideshowTargetAlpha)
    {
        g_slideshowTransitionAlpha += 0.02f * dt * 60.0f;
        g_slideshowTransitionAlpha += (1.0f - g_slideshowTransitionAlpha) * (1.0f - std::powf(1.0f - 0.005f, dt * 60.0f));

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

    // Animate thumbnail scroll offset
    if (g_thumbSizeCaptured)
    {
        if (!g_thumbFreeScroll && !g_thumbDragging)
        {
            // Normal mode: keep the strip centered on the current image.
            g_thumbTargetOffset = -(g_currentImageIndex * (g_thumbW + g_thumbGap));
        }
        // During an active drag g_thumbScrollOffset is updated directly in
        // WM_MOUSEMOVE (zero-lag direct manipulation); skip the spring step so
        // the strip tracks the finger exactly.  After release the momentum target
        // is already set and the spring provides natural deceleration.
        if (!g_thumbDragging)
        {
            g_thumbScrollOffset += (g_thumbTargetOffset - g_thumbScrollOffset) * g_smooth;
        }
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
           ext == L".webp" ||
           ext == L".avif" ||   // Decoded via bundled libavif/dav1d — no OS/Store extension required
           ext == L".jxl";      // Decoded via bundled libjxl — no OS/Store extension required
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

    // ----- 3. Render the Gaussian blur into the off-screen bitmap -----
    // If the image has an EXIF rotation we must rotate the small source bitmap
    // before blurring so the background matches the displayed orientation.
    g_blurEffect->SetInput(0, smallBmp.Get());

    ComPtr<ID2D1Image> prevTarget;
    g_renderTarget->GetTarget(&prevTarget);

    // For 90/270 rotations the blurred bg needs to be portrait, not landscape —
    // create the offscreen target with swapped dimensions in that case.
    const float rotMod = fmodf(fabsf(g_exifRotation), 180.f);
    const bool swapDims = (rotMod > 44.f && rotMod < 136.f);
    D2D1_SIZE_U bgSz = swapDims
        ? D2D1::SizeU(smallSz.height, smallSz.width)
        : smallSz;

    ComPtr<ID2D1Bitmap1> offscreenBmp;
    if (FAILED(g_renderTarget->CreateBitmap(
            bgSz, nullptr, 0, bmpProps, &offscreenBmp))) return;

    g_renderTarget->SetTarget(offscreenBmp.Get());
    g_renderTarget->BeginDraw();
    g_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    if (g_exifRotation != 0.f)
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
            D2D1::Matrix3x2F::Rotation(g_exifRotation) *
            D2D1::Matrix3x2F::Translation(cx, cy);
        g_renderTarget->SetTransform(xform);
    }

    g_renderTarget->DrawImage(g_blurEffect.Get(), D2D1::Point2F(0.f, 0.f));
    g_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
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

    g_wasInSlideshowOnExit = true;   // tell the exit fade to cross-fade blurred->desktop bg
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
        SetWindowLong  (g_mainWindow, 
                        GWL_EXSTYLE,
                        GetWindowLong(g_mainWindow, GWL_EXSTYLE) | WS_EX_LAYERED
                        );

        SetLayeredWindowAttributes(g_mainWindow, 0, 0, LWA_ALPHA);
        UpdateWindow(g_mainWindow);

        SetTimer(g_mainWindow, ENTER_FULL_SCREEN_TIMER_ID, 150, nullptr);
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
    // Capture the desktop behind the window — but only when transitioning from
    // windowed to fullscreen. When entering slideshow mode the screen is already
    // covered by the black cover window, so capturing now would overwrite
    // g_wicBackground with a black frame. The original desktop capture from the
    // initial fullscreen entry is still alive in g_wicBackground (DiscardDeviceResources
    // does not touch it) and will be re-uploaded below by CreateBackgroundBitmap().
    if (!g_isSlideshowMode)
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

    // Any drag that was in progress on the main window before the overlay took
    // over is now stale — clear all drag state so it cannot bleed through.
    g_isDragging    = false;
    g_thumbDragging = false;
    ReleaseCapture();

    g_fullScreenInitDone = true;
    g_renderTargetWindow = g_overlayWindow;
}

// Inner teardown — called either immediately (immediate=true path) or by
// UpdateEngine once the exit-fade animation has finished.
static void CompleteExitFullscreen()
{
    // Already cleaned up guard (e.g. double-call from slideshow timer race)
    if (!g_isFullscreen)
        return;

    if (!g_d2dBitmap)
    {
        // No bitmap — bail out safely.
        g_isFullscreen = false;
        if (g_overlayWindow)
        {
            DestroyWindow(g_overlayWindow);
            g_overlayWindow = nullptr;
        }
        g_wasInSlideshowOnExit = false;
        return;
    }

    D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

    RECT overlayClient;
    GetClientRect(g_overlayWindow, &overlayClient);

    RECT overlayRect;
    GetWindowRect(g_overlayWindow, &overlayRect);

    float overlayWidth  = (float)(overlayClient.right  - overlayClient.left);
    float overlayHeight = (float)(overlayClient.bottom - overlayClient.top);

    // The image is rendered rotated around its unrotated centre.
    // g_offsetX/Y is the top-left of the *unrotated* rectangle, so the
    // visual (post-rotation) bounding box must be derived from the centre
    // and the effective (possibly swapped) screen dimensions.
    float fitW = imgSize.width;
    float fitH = imgSize.height;
    {
        const float rotMod = fmodf(fabsf(g_imageRotationAngle), 180.f);
        if (rotMod > 44.f && rotMod < 136.f)
            std::swap(fitW, fitH);
    }
    // Rotation pivot (screen-space centre of the image, invariant to rotation)
    float cx = g_offsetX + imgSize.width  * g_zoom * 0.5f;
    float cy = g_offsetY + imgSize.height * g_zoom * 0.5f;

    float imgLeft   = cx - fitW * g_zoom * 0.5f;
    float imgTop    = cy - fitH * g_zoom * 0.5f;
    float imgRight  = cx + fitW * g_zoom * 0.5f;
    float imgBottom = cy + fitH * g_zoom * 0.5f;

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

    g_isFullscreen = false;
    g_needsFullscreenInit = false;
    g_overlayAlpha = 1.0f;
    g_wasInSlideshowOnExit = false;
    g_exitBgFadeAlpha      = 0.f;

    if (g_overlayWindow)
    {
        DestroyWindow(g_overlayWindow);
        g_overlayWindow = nullptr;
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
        g_mainWindow,
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
    // client rect.  g_offsetX/Y is the *unrotated* top-left, which is offset
    // from the visual edge by half the difference between unrotated and rotated
    // dimensions.  For non-rotated images fitW==imgW so the adjustment is zero.
    float cx_new = offsetCorrectionX - (float)clampShiftX + fitW * g_zoom * 0.5f;
    float cy_new = offsetCorrectionY - (float)clampShiftY + fitH * g_zoom * 0.5f;
    g_offsetX = cx_new - imgSize.width  * g_zoom * 0.5f;
    g_offsetY = cy_new - imgSize.height * g_zoom * 0.5f;

    g_targetOffsetX = g_offsetX;
    g_targetOffsetY = g_offsetY;

    UpdateTargetZoom(g_zoom);

    CreateRenderTarget(g_mainWindow);
    RecreateImageBitmap();

    LONG ex = GetWindowLong(g_mainWindow, GWL_EXSTYLE);
    if (ex & WS_EX_LAYERED)
    {
        SetLayeredWindowAttributes(g_mainWindow, 0, 255, LWA_ALPHA);
        SetWindowLong(g_mainWindow, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    }

    ShowWindow(g_mainWindow, SW_SHOW);
    RedrawWindow(g_mainWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

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

void ExitFullscreen(bool immediate)
{
    if (!g_isFullscreen)
        return;

    // Kill the zoom-display timer and hide the textbox in case it was visible.
    // Safe to do immediately regardless of the fade path.
    KillTimer(g_mainWindow, ZOOM_DISPLAY_TIMER_ID);
    if (!g_textBoxes.empty())
        g_textBoxes[TEXTBOX_ZOOM_INPUT].SetForcedVisibility(false);

    // If there is no image, or an immediate exit is requested (e.g. slideshow
    // entry which has already faded to black), skip the fade entirely.
    if (immediate || !g_d2dBitmap)
    {
        g_pendingExitFullscreen = false;
        g_exitBgFadeAlpha       = 0.f;
        CompleteExitFullscreen();
        return;
    }

    // Already fading — ignore re-entrant calls.
    if (g_pendingExitFullscreen)
        return;

    // Start the deferred exit fade: UpdateEngine animates g_exitBgFadeAlpha
    // 0->1 then calls CompleteExitFullscreen() when it reaches 1.
    g_pendingExitFullscreen = false; // will be set true below; clear first for safety
    g_exitBgFadeAlpha       = 0.f;
    g_pendingExitFullscreen = true;
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
            g_blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 4.0f);
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

// Cheap CPU-side downscale used as the shadow-effect input for animated
// formats, where we deliberately skip the D3D11 mip chain (see
// RecreateImageBitmap). Mirrors the WIC fallback path in CreateSlideshowBgBitmap.
static ComPtr<ID2D1Bitmap> CreateSmallShadowSourceViaWic(IWICBitmapSource* wicSrc, UINT targetMaxPx)
{
    if (!wicSrc || !g_wicFactory || !g_renderTarget) return nullptr;

    UINT fullW = 0, fullH = 0;
    if (FAILED(wicSrc->GetSize(&fullW, &fullH)) || fullW == 0 || fullH == 0) return nullptr;

    float scaleRatio = min(1.0f, min((float)targetMaxPx / fullW, (float)targetMaxPx / fullH));
    const UINT smallW = max(1u, (UINT)(fullW * scaleRatio));
    const UINT smallH = max(1u, (UINT)(fullH * scaleRatio));

    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(g_wicFactory->CreateBitmapScaler(&scaler))) return nullptr;
    if (FAILED(scaler->Initialize(wicSrc, smallW, smallH, WICBitmapInterpolationModeHighQualityCubic)))
        return nullptr;

    ComPtr<IWICFormatConverter> conv;
    if (FAILED(g_wicFactory->CreateFormatConverter(&conv))) return nullptr;
    if (FAILED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                 WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom)))
        return nullptr;

    ComPtr<ID2D1Bitmap> bmp;
    if (FAILED(g_renderTarget->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &bmp))) return nullptr;
    return bmp;
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
    if (g_mipPipelineReady && !g_isAnimatedImage)
    {
        CreateMipTextureFromSource(bitmapSourceForD2D.Get());

        // Extract a ~256 px mip for use as the shadow effect input.
        // The shadow is a blurred silhouette - full resolution is wasted here.
        // This small bitmap is created once per image load and reused every frame.
        g_shadowSourceBitmap = ExtractMipAsD2DBitmap(256u);
    }
    else if (g_isAnimatedImage)
    {
        // Animated formats: skip the D3D11 mip chain entirely (no benefit -
        // trilinear filtering isn't worth a per-frame upload+GenerateMips
        // cost for something shown near 1:1 for a few seconds). g_imageSRV
        // staying null makes the render path fall back to plain D2D draw
        // automatically. Still build a *small* shadow source via cheap WIC
        // downscale so the drop shadow doesn't blur the full-res frame
        // every single render call.
        g_imageMipTex.Reset();
        g_imageSRV.Reset();
        g_shadowSourceBitmap = CreateSmallShadowSourceViaWic(bitmapSourceForD2D.Get(), 256u);
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

    // If the image will be displayed rotated 90° or 270° (from EXIF), its
    // effective on-screen footprint has swapped width and height.
    float fitW = imgSize.width;
    float fitH = imgSize.height;
    const float rotMod = fmodf(fabsf(g_exifRotation), 180.f);
    if (rotMod > 44.f && rotMod < 136.f)
        std::swap(fitW, fitH);

    float scale = 1.0f;

    if (hard)
        g_overlayAlpha = 1.0f;
    else
        g_overlayAlpha = 0.0f;

    // If image is larger than screen, scale down to ~95% of screen.
    // g_openFitToScreen forces the same fit-to-screen scaling used by
    // slideshow mode even when the image would otherwise open at 100%.
    if (g_isSlideshowMode || g_openFitToScreen || fitW > windowWidth || fitH > windowHeight)
    {
        float scaleX = (windowWidth  * 0.95f) / fitW;
        float scaleY = (windowHeight * 0.95f) / fitH;

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
    // Reset rotation for new images, honouring any EXIF orientation tag.
    g_imageRotationAngle  = g_exifRotation;
    g_targetRotationAngle = g_exifRotation;
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
    // Hamburger Menu Button
    // -------------------------

    openConfig.layout.x.value += 0.056f;
    openConfig.text = L"\u2630";
    openConfig.tooltip = L"Menu";
    g_buttons[BUTTON_HELP].Initialize(
        g_renderTarget.Get(),
        g_dwriteFactory.Get(),
        openConfig,
        []()
        {
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING,             IDM_MENU_SHORTCUTS, L"Keyboard shortcuts");
            AppendMenuW(hMenu, MF_STRING,             IDM_MENU_ABOUT,     L"About");
            AppendMenuW(hMenu, MF_SEPARATOR, 0,                            nullptr);
            AppendMenuW(hMenu, MF_STRING | (g_useHQFilter ? MF_CHECKED : MF_UNCHECKED),
                                                      IDM_MENU_HQ_FILTER, L"High quality filter");
            AppendMenuW(hMenu, MF_STRING | (g_openFitToScreen ? MF_CHECKED : MF_UNCHECKED),
                                                      IDM_MENU_FIT_TO_SCREEN, L"Open images fit to screen");
            AppendMenuW(hMenu, MF_SEPARATOR, 0,                            nullptr);
            AppendMenuW(hMenu, MF_STRING,             IDM_MENU_ASSOCIATE, L"Associate file types");

            POINT pt;
            GetCursorPos(&pt);
            // Anchor the menu (and shift foreground) to whichever window is
            // actually visible/interactive right now — the overlay in
            // fullscreen, the main window otherwise. Hardcoding g_mainWindow
            // here was the real problem: in fullscreen it permanently dragged
            // OS foreground/activation onto the invisible main window and
            // never gave it back, so the very next real click on the image
            // (landing on the overlay) triggered a genuine main->overlay
            // activation switch that looked like focus loss and exited
            // fullscreen — even though the menu interaction itself was fine.
            HWND ownerWnd = (g_isFullscreen && g_overlayWindow && IsWindow(g_overlayWindow))
                                ? g_overlayWindow
                                : g_mainWindow;
            // TrackPopupMenu's internal menu window isn't a window we own
            // (IsOwnedByOurWindow won't recognize it), and SetForegroundWindow
            // itself is what fires WA_INACTIVE (it steals activation) — so this
            // needs an explicit suppression scope, opened BEFORE shifting
            // foreground focus, covering both calls. It closes automatically
            // when the guard goes out of scope after TrackPopupMenu returns,
            // so a genuine focus loss (Alt+Tab, etc.) still exits fullscreen
            // normally afterward.
            {
                FullscreenExitSuppressor guard;
                SetForegroundWindow(ownerWnd);
                TrackPopupMenu(hMenu, TPM_LEFTBUTTON, pt.x, pt.y, 0, ownerWnd, nullptr);
            }
            DestroyMenu(hMenu);
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

    float top = 0.01f;
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
    exitConfig.fontSize = 0.012f;
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
            g_smooth = 0.18f * 60.0f / (float)refreshRate;

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
                    
                    UpdateTargetZoom(value/100.0f);
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

    // On device loss, GIF D2D bitmaps are cleared — re-upload only the frame
    // currently on screen. The rest are re-created lazily by UpdateEngine as
    // playback reaches them, same as the initial-load path.
    if (g_isAnimatedImage && !g_animFrames.empty() && g_animD2DBitmaps.empty())
    {
        g_animD2DBitmaps.assign(g_animFrames.size(), nullptr);
        g_animD3DSRVs.assign(g_animFrames.size(), nullptr);

        if (g_currentAnimFrame < g_animFrames.size())
        {
            ComPtr<ID2D1Bitmap> bmp;
            g_renderTarget->CreateBitmapFromWicBitmap(g_animFrames[g_currentAnimFrame].Get(), nullptr, &bmp);
            g_animD2DBitmaps[g_currentAnimFrame] = bmp;
            g_d2dBitmap = bmp;
        }
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

        // When exiting from slideshow, cross-fade the blurred bg out on top of the
        // desktop bg that was just drawn (slides from fully visible down to invisible).
        if (g_pendingExitFullscreen && g_wasInSlideshowOnExit && g_slideshowBgBitmap)
            DrawCoverBg(g_slideshowBgBitmap.Get(), 1.0f - g_exitBgFadeAlpha);

        // Dim overlay: fades in on enter (driven by g_overlayAlpha), fades out on
        // exit (modulated by g_exitBgFadeAlpha so it lifts in sync with the bg fade).
        if (g_dimBrush)
        {
            const float dimAlpha = g_pendingExitFullscreen
                ? g_overlayAlpha * (1.0f - g_exitBgFadeAlpha)
                : g_overlayAlpha;
            g_dimBrush->SetColor(D2D1::ColorF(0, 0, 0, 0.67f * dimAlpha));
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
        //   D2D fallback — used only when the mip pipeline isn't available yet,
        //                  or when the user has enabled the HQ filter option
        useD3D11Image = g_mipPipelineReady && (g_imageSRV != nullptr) && !g_useHQFilter;

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

// ============================================================
//  Load-failure reporting
// ============================================================
// Every failure path in LoadImageD2D / FinishImageLoad funnels through here
// so the UI can show a specific reason instead of a generic "Failed to load
// image." g_lastLoadError is cleared at the top of LoadImageD2D and read by
// the caller (OpenImageFile, WM_DROPFILES, WM_COPYDATA) immediately after a
// failed call.
static bool LoadFail(const wchar_t* msg, HRESULT hr)
{
    g_lastLoadError = msg;
    if (hr != S_OK)
    {
        wchar_t buf[48];
        swprintf_s(buf, L" (HRESULT 0x%08X)", (unsigned)hr);
        g_lastLoadError += buf;
    }
    OutputDebugStringW((g_lastLoadError + L"\n").c_str());
    return false;
}

// ============================================================
//  AVIF decoding (via libavif + dav1d, statically linked)
// ============================================================
// Bundled so AVIF works with zero extra installs — no Store "AV1 Video
// Extension" required. Decodes straight into a premultiplied-alpha WIC
// bitmap so everything downstream (RecreateImageBitmap, the D3D11 mip
// path, the shadow effect) sees exactly what it already expects.
//
// NOTE: libavif's public API has shifted slightly across releases. This
// targets the mainstream 1.x API shape (avifDecoderSetIOFile /
// avifImageYUVToRGB). If your vcpkg version differs, <avif/avif.h> is the
// source of truth — the overall shape (parse -> decode frame -> convert to
// RGB -> wrap as WIC bitmap) has been stable for a long time.
//
// KNOWN LIMITATION: AVIF's irot/imir orientation transforms aren't applied
// yet (g_exifRotation is left at 0 for AVIF). Most AVIF exporters don't set
// them, but a file that does may appear in its untransformed orientation.
// Reads just enough of a file to report its ISOBMFF major/compatible brands
// (the 'ftyp' box). Used to diagnose "BMFF parsing failed" — the file might
// not actually be AVIF at all (wrong/misleading extension, an AVIF *image
// sequence* ('avis' brand, a different code path than the still-image
// 'avif' brand), or simply truncated/corrupted). This is a light manual
// read of the box header, not a full parser — just enough for a human to
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

// avifResultToString() returns a narrow (const char*) string — convert to
// wide so it can flow into g_lastLoadError / MessageBox alongside every
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
// any pixel data yet). Shared by every AVIF entry point below — the
// static-image decode, the animated per-frame decode, and the frame-count
// peek LoadImageD2D uses to choose between them all start with exactly
// this. Caller owns the returned decoder and must call avifDecoderDestroy()
// on it; returns nullptr on failure.
static avifDecoder* AvifOpenAndParse(const wchar_t* path, std::wstring* outReason = nullptr)
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
// (per-frame) AVIF decode paths below — the YUV→RGB conversion and WIC
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

static bool DecodeAvifToWicBitmap(const wchar_t* path, IWICImagingFactory* wicFactory, ComPtr<IWICBitmap>& outBitmap, UINT& outW, UINT& outH, std::wstring* outReason)
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
// perfectly ordinary still AVIF that just has one frame) — the static
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
//  Animated AVIF frame decode — analogous to DecodeWebpFrames above (and,
//  through it, to DecodeGifCompositeFrames), but AVIF's own thing: it's
//  decoded via libavif rather than WIC, so this opens its own avifDecoder
//  from the file path instead of taking a pre-opened IWICBitmapDecoder.
//  wicFactory is only used for the final YUV→RGB→WIC wrapping step (via
//  ConvertAvifImageToWicBitmap) and must belong to whichever thread is
//  calling this, same reasoning as DecodeGifCompositeFrames.
//
//  avifDecoderNthImage() can seek straight to any frame index — it
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
// libjxl version if this doesn't compile as-is — a few struct/field names
// have moved between releases, though the event loop shape has been stable.
//
// KNOWN LIMITATION: only the first frame of an animated JXL is decoded
// (mirrors how AVIF is handled here; static JXL — the overwhelmingly common
// case — is unaffected).
static bool DecodeJxlToWicBitmap(const wchar_t* path, IWICImagingFactory* wicFactory, ComPtr<IWICBitmap>& outBitmap, UINT& outW, UINT& outH, std::wstring* outReason)
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
                // First (or only, for a static image) frame decoded — stop here.
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
//  GIF frame decode/composite — extracted so it can run identically on the
//  main thread (just frame 0, for an immediate first paint) and on the
//  background continuation thread (the rest of the frames). GIF disposal
//  methods make this inherently sequential: each frame's starting canvas
//  state depends on how the previous frame was disposed, so frames cannot
//  be decoded out of order or in parallel — only moved off the UI thread.
//
//  wicFactory and decoder must both belong to whichever thread is calling
//  this (WIC objects aren't safely shared across the apartment boundary —
//  same reason the thumbnail loader creates its own per-thread factory).
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
            if (SUCCEEDED(wicFactory->CreateBitmapFromSource(
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

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        HRESULT hrc = wicFactory->CreateFormatConverter(converter.GetAddressOf());

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
            savedCanvasForDisposal3 = CloneBitmap(canvas.Get());
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

        if (!DecodeToPBGRA(frame.Get(), copyRectPtr, src, srcW, srcH, srcStride))
            continue;
        const UINT drawW = umin(dstW, srcW);
        const UINT drawH = umin(dstH, srcH);

        BlendSrcOverCanvas(canvas.Get(), (int)left, (int)top, src.data(), drawW, drawH, srcStride);

        auto composedFrame = CloneBitmap(canvas.Get());
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
//  WebP frame decode — analogous to DecodeGifCompositeFrames above, but
//  simpler: WIC composites WebP animation frames internally (unlike GIF,
//  where this app has to manually replay disposal methods), so each frame
//  is independent and decode can start at any index without needing to
//  replay anything before it. That's why the background continuation for
//  WebP (in LoadImageD2D below) starts at frame 1 directly, instead of
//  redoing frame 0 the way the GIF continuation has to.
//
//  wicFactory and decoder must both belong to whichever thread is calling
//  this, same reasoning as DecodeGifCompositeFrames. maxFrames=0 means
//  "decode through the end"; outW/outH report the first decoded frame's
//  dimensions (frame 0 unless startFrame skips it).
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

    auto imin = [](int a, int b) { return (a < b) ? a : b; };
    auto imax = [](int a, int b) { return (a > b) ? a : b; };

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

    auto DecodeToPBGRA = [&](IWICBitmapSource* src,
                            const WICRect* optionalCopyRect,
                            std::vector<BYTE>& outPixels,
                            UINT& outW2, UINT& outH2, UINT& outStride) -> bool
    {
        if (!src) return false;

        UINT w = 0, h = 0;
        if (FAILED(src->GetSize(&w, &h)) || w == 0 || h == 0)
            return false;

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        HRESULT hrc = wicFactory->CreateFormatConverter(converter.GetAddressOf());

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

        for (size_t p = 0; p + 3 < outPixels.size(); p += 4)
        {
            if (outPixels[p + 3] == 0)
            {
                outPixels[p + 0] = 0;
                outPixels[p + 1] = 0;
                outPixels[p + 2] = 0;
            }
        }

        outW2 = copyW;
        outH2 = copyH;
        outStride = stride;
        return true;
    };

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
        if (!DecodeToPBGRA(frame.Get(), nullptr, pixels, w, h, stride))
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

// Stops (and joins) the background GIF/WebP/AVIF-frame-decoding thread, if
// one is running. Every LoadImageD2D call starts with this - the thread it
// might stop belongs to whatever image was previously loaded, and must not
// be left writing into g_animFrames/g_animFrameDelays after those vectors
// get reassigned for the new image.
static void StopAnimDecodeThread()
{
    g_animDecodeStop = true;
    if (g_animDecodeThread.joinable())
        g_animDecodeThread.join();
    g_animDecodeStop = false;
}

// ============================================================
//  Shared animated-image loading glue (GIF + WebP + AVIF)
// ------------------------------------------------------------
//  All three formats follow the exact same shape once their frame-0 is
//  decoded: publish the shared g_anim* state, then hand the remaining
//  frames off to a background thread. Only the actual per-frame decode
//  call differs (DecodeGifCompositeFrames vs. DecodeWebpFrames vs.
//  DecodeAvifFrames, and where each one needs to start), so that's the one
//  thing callers still provide themselves, as a small lambda — including
//  opening whatever thread-local decode resources their format needs
//  (OpenThreadLocalWicDecoder for GIF/WebP; DecodeAvifFrames opens its own
//  avifDecoder internally, so AVIF only needs OpenThreadLocalWicFactory).
// ============================================================

// Publishes g_animFrames[0] (already decoded by the caller) as the current
// frame and marks the image as animated. Common tail of all three formats'
// synchronous frame-0 decode step.
static void BeginAnimatedPlayback(UINT firstFrameW, UINT firstFrameH)
{
    g_isAnimatedImage = true;
    g_currentAnimFrame = 0;
    g_wicBitmapSource = g_animFrames[0];
    g_lastAnimFrameTime = GetTickCount64();
    g_imageWidth = (int)firstFrameW;
    g_imageHeight = (int)firstFrameH;
    g_animFramesReadyUpTo.store(1);
}

// Opens a fresh WIC factory for use on a background thread. WIC objects
// aren't safely shared across the apartment boundary (see
// DecodeGifCompositeFrames's comment), so every background continuation
// below creates its own rather than reusing the main thread's g_wicFactory.
static bool OpenThreadLocalWicFactory(ComPtr<IWICImagingFactory>& outFactory)
{
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&outFactory))) && outFactory;
}

// Same, plus a WIC decoder for the given file — what the GIF and WebP
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
    g_animDecodeThread = std::thread([frameCount, decodeRestFn]()
    {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
            return;

        decodeRestFn(frameCount, &g_animDecodeStop,
            [](UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)
            {
                if (index < g_animFrames.size())
                {
                    g_animFrames[index] = bmp;
                    g_animFrameDelays[index] = delayMs;
                    // Frames are produced strictly in order by this single
                    // thread, so it's always safe to publish index+1 here.
                    g_animFramesReadyUpTo.store((int)(index + 1));
                }
            });

        CoUninitialize();
    });
}

bool LoadImageD2D(HWND hWnd, const wchar_t* filename)
{
    g_lastLoadError.clear();

    if (!g_wicFactory || !filename || !*filename)
        return LoadFail(L"Internal error: the image system hasn't finished initializing.");

    // Stop any background GIF-frame decode left over from the previously
    // loaded image before we touch g_animFrames/g_animFrameDelays below — see
    // StopAnimDecodeThread's comment.
    StopAnimDecodeThread();

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
    g_animFrames.clear();
    g_animFrameDelays.clear();
    g_animD2DBitmaps.clear();
    g_animD3DSRVs.clear();
    g_isAnimatedImage = false;
    g_currentAnimFrame = 0;
    g_lastAnimFrameTime = 0;
    g_slideshowBgBitmap.Reset();

    std::wstring extLower = std::filesystem::path(filename).extension().wstring();
    std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::towlower);

    // --------------------------------------------------------------
    // AVIF / JPEG XL — decoded ourselves (bundled libavif/dav1d and
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
            // Animated AVIF — same shape as the animated-WebP branch further
            // down (see the comment there): decode frame 0 synchronously so
            // the window opens immediately, then hand the rest to a
            // background thread via SpawnAnimDecodeThread. AVIF's
            // avifDecoderNthImage() can seek to any frame directly, the same
            // as WebP, so the continuation starts right at frame 1.
            g_animFrames.assign(avifFrameCount, nullptr);
            g_animFrameDelays.assign(avifFrameCount, 100);
            g_animFramesReadyUpTo.store(0);

            UINT frameW = 0, frameH = 0;
            std::wstring reason;
            UINT produced = DecodeAvifFrames(
                filename, g_wicFactory.Get(), /*startFrame=*/0, /*maxFrames=*/1, nullptr,
                &frameW, &frameH,
                [&](UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)
                {
                    if (index < g_animFrames.size())
                    {
                        g_animFrames[index] = bmp;
                        g_animFrameDelays[index] = delayMs;
                    }
                }, &reason);

            if (produced == 0 || !g_animFrames[0])
                return LoadFail((L"No animation frames could be decoded from this AVIF file: " + reason).c_str());

            BeginAnimatedPlayback(frameW, frameH);
            g_exifRotation = 0.f;  // AVIF irot/imir transforms aren't applied yet — see DecodeAvifToWicBitmap

            std::wstring filePathCopy = filename;
            SpawnAnimDecodeThread(avifFrameCount,
                [filePathCopy](UINT fc, const std::atomic<bool>* stopFlag,
                                const std::function<void(UINT, ComPtr<IWICBitmap>, UINT)>& onFrame) -> UINT
                {
                    ComPtr<IWICImagingFactory> wic;
                    if (!OpenThreadLocalWicFactory(wic))
                        return 0;
                    // Starts at frame 1 (not 0) — same reasoning as WebP.
                    return DecodeAvifFrames(filePathCopy.c_str(), wic.Get(), /*startFrame=*/1, /*maxFrames=*/0,
                                             stopFlag, nullptr, nullptr, onFrame);
                });

            return FinishImageLoad(hWnd, filename);
        }

        // Static AVIF (single image)
        ComPtr<IWICBitmap> bmp;
        UINT w = 0, h = 0;
        std::wstring reason;
        if (!DecodeAvifToWicBitmap(filename, g_wicFactory.Get(), bmp, w, h, &reason))
            return LoadFail((L"This AVIF file couldn't be decoded: " + reason).c_str());
        g_wicBitmapSource = bmp;
        g_imageWidth   = (int)w;
        g_imageHeight  = (int)h;
        g_exifRotation = 0.f;  // AVIF irot/imir transforms aren't applied yet — see DecodeAvifToWicBitmap
        return FinishImageLoad(hWnd, filename);
    }
    else if (extLower == L".jxl")
    {
        ComPtr<IWICBitmap> bmp;
        UINT w = 0, h = 0;
        std::wstring reason;
        if (!DecodeJxlToWicBitmap(filename, g_wicFactory.Get(), bmp, w, h, &reason))
            return LoadFail((L"This JPEG XL file couldn't be decoded: " + reason).c_str());
        g_wicBitmapSource = bmp;
        g_imageWidth   = (int)w;
        g_imageHeight  = (int)h;
        g_exifRotation = 0.f;
        return FinishImageLoad(hWnd, filename);
    }

    // --------------------------------------------
    // Decode container (everything else still goes through WIC, as before)
    // --------------------------------------------

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = g_wicFactory->CreateDecoderFromFilename(
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
    // Small local helpers (avoid min/max macros)
    // --------------------------------------------
    

    // --------------------------------------------
    // Small local helper
    // --------------------------------------------
    // Note: imin/imax/umin/DecodeToPBGRA/CloneBitmap/ClearRect/
    // BlendSrcOverCanvas used to live here too, but both the GIF-composite
    // and WebP-frame decode paths that needed them now run through the
    // standalone DecodeGifCompositeFrames()/DecodeWebpFrames() (above this
    // function in the file), which have their own copies so they can run on
    // a background thread. ReadUIntMeta is the only one still needed here -
    // by the static-image EXIF-orientation read further down.

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

    // ============================================================
    // Animated WebP / animated GIF
    // ------------------------------------------------------------
    // (Animated AVIF is handled earlier, in the .avif branch above — it
    // bypasses WIC's decoder registry entirely the same way static AVIF
    // does, so it can't share the WIC decoder this function opens below.
    // It still goes through the exact same BeginAnimatedPlayback /
    // SpawnAnimDecodeThread glue as these two, though — see the comment on
    // that section for why all three end up looking so similar.)
    //
    // Both branches below follow the same shape (assign the shared g_anim*
    // vectors, decode frame 0 synchronously so the window opens right away,
    // then call SpawnAnimDecodeThread for the rest) via BeginAnimatedPlayback
    // and SpawnAnimDecodeThread above. What's left here is genuinely
    // format-specific: which Decode*Frames function to call, and — because
    // WIC composites WebP frames internally while GIF frames must be
    // replayed through prior frames' disposal state — where each one needs
    // to start decoding from.
    // ============================================================
    if (isWebpContainer && frameCount > 1)
    {
        g_animFrames.assign(frameCount, nullptr);
        g_animFrameDelays.assign(frameCount, 100);
        g_animFramesReadyUpTo.store(0);

        // Decode just frame 0 synchronously, on the main thread, using the
        // decoder we already have open - this is what lets the window open
        // immediately.
        UINT frameW = 0, frameH = 0;
        UINT produced = DecodeWebpFrames(
            g_wicFactory.Get(), decoder.Get(), frameCount, /*startFrame=*/0, /*maxFrames=*/1, nullptr,
            &frameW, &frameH,
            [&](UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)
            {
                if (index < g_animFrames.size())
                {
                    g_animFrames[index] = bmp;
                    g_animFrameDelays[index] = delayMs;
                }
            });

        if (produced == 0 || !g_animFrames[0])
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
        g_animFrames.assign(frameCount, nullptr);
        g_animFrameDelays.assign(frameCount, 100);
        g_animFramesReadyUpTo.store(0);

        // Decode just frame 0 synchronously, on the main thread, using the
        // decoder we already have open — this is what lets the window open
        // immediately instead of stalling on the whole animation.
        UINT canvasW = 0, canvasH = 0;
        UINT produced = DecodeGifCompositeFrames(
            g_wicFactory.Get(), decoder.Get(), frameCount, /*maxFrames=*/1, nullptr,
            &canvasW, &canvasH,
            [&](UINT index, ComPtr<IWICBitmap> bmp, UINT delayMs)
            {
                if (index < g_animFrames.size())
                {
                    g_animFrames[index] = bmp;
                    g_animFrameDelays[index] = delayMs;
                }
            });

        if (produced == 0 || !g_animFrames[0])
            return LoadFail(L"No frames could be decoded from this GIF.");

        BeginAnimatedPlayback(canvasW, canvasH);

        // Unlike WebP, the background thread here has to redecode from frame
        // 0 to rebuild identical canvas/disposal state before it can
        // continue past where the synchronous pass left off (see
        // DecodeGifCompositeFrames's comment) — so its onFrame callback
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
        //   1 = normal          → 0°
        //   3 = upside-down     → 180°
        //   6 = 90° CW shot     → 90°   (phone held 90° CCW)
        //   8 = 90° CCW shot    → 270°  (phone held 90° CW)
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
                        case 3: g_exifRotation =  180.f; break;
                        case 6: g_exifRotation =   90.f; break;
                        case 8: g_exifRotation =  270.f; break;
                        default:g_exifRotation =    0.f; break;
                    }
                }
                else
                {
                    g_exifRotation = 0.f;
                }
            }
            else
            {
                g_exifRotation = 0.f;
            }
        }

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        hr = g_wicFactory->CreateFormatConverter(converter.GetAddressOf());

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
        hr = g_wicFactory->CreateBitmapFromSource(
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

    return FinishImageLoad(hWnd, filename);
}

// ============================================================
//  Shared "tail" of an image load — runs after g_wicBitmapSource (and, for
//  animated images, g_animFrames) has been populated by either the WIC path
//  or the AVIF/JXL paths above. Uploads to the GPU, restores any saved
//  view state, and updates the filename label.
// ============================================================
static bool FinishImageLoad(HWND hWnd, const wchar_t* filename)
{
    // Create Direct2D Bitmap for the *current device*
    RecreateImageBitmap();

    // Only frame 0 gets uploaded here (RecreateImageBitmap() just above already
    // built it, since g_wicBitmapSource points at g_animFrames[0] for animated
    // formats). Every other frame is uploaded lazily, one at a time, by
    // UpdateEngine as playback reaches it - this is what lets a large/long
    // animated GIF, WebP, or AVIF display its first frame immediately instead of
    // blocking on every frame hitting the GPU up front.
    if (g_isAnimatedImage && g_renderTarget)
    {
        g_animD2DBitmaps.assign(g_animFrames.size(), nullptr);
        g_animD3DSRVs.assign(g_animFrames.size(), nullptr);
        if (!g_animD2DBitmaps.empty())
            g_animD2DBitmaps[0] = g_d2dBitmap;   // already built above, no need to re-upload
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

        // Convert stored pan back into absolute offsets for THIS render target.
        // When in fullscreen the overlay is the render target; always use it
        // here so that callers passing g_mainWindow (e.g. the Open button
        // callback or WM_COPYDATA) still get offsets in overlay coordinates.
        HWND panWnd = (g_isFullscreen && g_overlayWindow && IsWindow(g_overlayWindow))
                      ? g_overlayWindow : hWnd;

        OffsetsFromPan(panWnd, g_zoom, imgSize.width, imgSize.height,
                       s.panX, s.panY, g_offsetX, g_offsetY);

        OffsetsFromPan(panWnd, g_targetZoom, imgSize.width, imgSize.height,
                       s.targetPanX, s.targetPanY, g_targetOffsetX, g_targetOffsetY);

        g_imageRotationAngle = s.rotation;
        g_targetRotationAngle = s.targetRotation;

        g_restoredStateThisLoad = true;
    }
    else
    {
        g_restoredStateThisLoad = false;
        // Apply EXIF orientation so the image appears upright immediately.
        // GIFs never carry EXIF so g_exifRotation will be 0 for them.
        g_imageRotationAngle  = g_exifRotation;
        g_targetRotationAngle = g_exifRotation;
    }
    // Update current path
    g_currentFilePath = newPath;
    size_t slash = g_currentFilePath.find_last_of(L"\\/");

    if (slash != std::wstring::npos)
        g_currentFileName = g_currentFilePath.substr(slash + 1);
    else
        g_currentFileName = g_currentFilePath;

    // Use the full path here, not g_currentFileName (the bare name).
    // When the app is launched from a Windows Explorer *search* results window the
    // working directory is NOT set to the file's folder, so file_size(bare_name)
    // would throw a filesystem_error and crash the process before anything is shown.
    std::error_code ec;
    uintmax_t bytes = std::filesystem::file_size(g_currentFilePath, ec);
    if (ec) bytes = 0;
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

    if (!g_d2dBitmap)
        return LoadFail(L"The image decoded, but couldn't be uploaded to the graphics device.");

    return true;
}

void BuildImageList(const wchar_t* filename)
{
    // Stop the loader before touching g_imageFiles or g_thumbs to avoid races.
    StopThumbnailLoader();

    // Snapshot existing thumbnails keyed by full path BEFORE we clear g_imageFiles.
    // After the sorted list is rebuilt, a new file may land in the middle, shifting
    // all subsequent indices.  A naive resize() would misalign every thumbnail that
    // follows the insertion point.  Keying by path lets us restore each entry to
    // its correct new index regardless of where the new file was inserted.
    std::unordered_map<std::wstring, ThumbnailEntry> thumbSnap;
    for (int i = 0; i < (int)g_thumbs.size() && i < (int)g_imageFiles.size(); ++i)
        if (g_thumbs[i].wic || g_thumbs[i].d2d)
            thumbSnap[g_imageFiles[i]] = g_thumbs[i];

    g_imageFiles.clear();
    // NOTE: do NOT clear g_imageStates here.  States are keyed by full path, so
    // they never collide when the folder changes, and they must survive directory-
    // change notifications (WM_APP_DIRCHANGE) that are fired after a DEL — clearing
    // them here would erase every other image's saved zoom/pan on every deletion.
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

    // Explorer's default "Name" sort is a natural/logical sort (e.g. "img2"
    // before "img10"), not a plain character-by-character comparison — a
    // naive _wcsicmp would put "img10" before "img2" since '1' < '2'.
    // StrCmpLogicalW is the actual API Explorer uses for this, so sorting
    // with it here matches Explorer's order exactly. Comparing full paths
    // (rather than just filenames) is fine since every entry shares the
    // same parent directory prefix.
    std::sort(g_imageFiles.begin(), g_imageFiles.end(),
        [](const std::wstring& a, const std::wstring& b)
        {
            return StrCmpLogicalW(a.c_str(), b.c_str()) < 0;
        });

    for (size_t i = 0; i < g_imageFiles.size(); ++i)
    {
        if (_wcsicmp(g_imageFiles[i].c_str(), filename) == 0)
        {
            g_currentImageIndex = (int)i;
            break;
        }
    }

    // Rebuild g_thumbs aligned to the new sorted g_imageFiles, restoring any
    // already-decoded thumbnails from the snapshot.  New slots (e.g. a freshly
    // added file) start empty and will be filled by the loader thread.
    const int newN = (int)g_imageFiles.size();
    g_thumbs.assign(newN, ThumbnailEntry{});
    for (int i = 0; i < newN; ++i)
    {
        auto it = thumbSnap.find(g_imageFiles[i]);
        if (it != thumbSnap.end())
            g_thumbs[i] = it->second;
    }

    // Start watching the directory for new/deleted/renamed image files
    StartDirectoryWatcher(std::filesystem::path(filename).parent_path().wstring());
    StartThumbnailLoader();
}

void AssociateFileTypes(HWND hWnd)
{
    // Extensions we will associate, shown to the user before anything is written
    static const wchar_t* exts[] = {
        L".jpg", L".jpeg", L".png", L".bmp",
        L".gif", L".tif",  L".tiff", L".webp",
        L".avif", L".jxl"
    };

    // ----------------------------------------------------------------
    // 1. Confirmation dialog via TaskDialog
    // ----------------------------------------------------------------
    std::wstring extList;
    for (auto ext : exts)
        extList += std::wstring(ext) + L"  ";

    std::wstring detail =
        L"The following file types will be associated with Picasso Pictures:\n\n"
        + extList +
        L"\n\nAfter this change, double-clicking any of these files in Explorer "
        L"will open them in Picasso Pictures.";

    TASKDIALOGCONFIG tdc       = {};
    tdc.cbSize                 = sizeof(tdc);
    tdc.hwndParent             = hWnd;
    tdc.hInstance              = hInst;
    tdc.dwFlags                = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    tdc.pszWindowTitle         = L"Associate file types";
    tdc.pszMainIcon            = TD_INFORMATION_ICON;
    tdc.pszMainInstruction     = L"Set Picasso Pictures as the default image viewer?";
    tdc.pszContent             = detail.c_str();
    tdc.dwCommonButtons        = TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON;

    int button = 0;
    if (FAILED(TaskDialogIndirect(&tdc, &button, nullptr, nullptr)) ||
        button != IDOK)
        return;  // user cancelled

    // ----------------------------------------------------------------
    // 2. Write registry entries
    // ----------------------------------------------------------------
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    static const wchar_t* PROG_ID      = L"PicassoPictures.ImageFile";
    static const wchar_t* FRIENDLY     = L"Picasso Pictures Image";
    static const wchar_t* OPEN_CMD_FMT = L"\"%s\" \"%%1\"";

    wchar_t openCmd[MAX_PATH + 8] = {};
    swprintf_s(openCmd, MAX_PATH + 8, OPEN_CMD_FMT, exePath);

    auto RegSetStr = [](const wchar_t* keyPath, const wchar_t* valueName, const wchar_t* data) -> bool
    {
        HKEY hKey = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                            &hKey, nullptr) != ERROR_SUCCESS)
            return false;
        DWORD cb = (DWORD)((wcslen(data) + 1) * sizeof(wchar_t));
        bool ok = RegSetValueExW(hKey, valueName, 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(data), cb) == ERROR_SUCCESS;
        RegCloseKey(hKey);
        return ok;
    };

    // ProgID + open command
    std::wstring progBase = std::wstring(L"Software\\Classes\\") + PROG_ID;
    RegSetStr(progBase.c_str(),                               nullptr, FRIENDLY);
    RegSetStr((progBase + L"\\shell\\open\\command").c_str(), nullptr, openCmd);

    // App capabilities (shows up in Windows Settings → Default Apps)
    static const wchar_t* CAP_KEY = L"Software\\PicassoPictures\\Capabilities";
    RegSetStr(L"Software\\RegisteredApplications", L"PicassoPictures", CAP_KEY);
    RegSetStr(CAP_KEY, L"ApplicationName",          L"Picasso Pictures");
    RegSetStr(CAP_KEY, L"ApplicationDescription",   L"Lightweight image viewer");

    // Per-extension association
    bool allOk = true;
    for (auto ext : exts)
    {
        std::wstring extKey = std::wstring(L"Software\\Classes\\") + ext;
        std::wstring capKey = std::wstring(CAP_KEY) + L"\\FileAssociations";

        if (!RegSetStr(extKey.c_str(), nullptr, PROG_ID))
            allOk = false;

        RegSetStr(capKey.c_str(), ext, PROG_ID);
    }

    // Flush the shell cache
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    // ----------------------------------------------------------------
    // 3. Result dialog
    // ----------------------------------------------------------------
    if (allOk)
    {
        TaskDialog(hWnd, hInst,
            L"File association",
            L"Done!",
            L"Picasso Pictures is now the default viewer for all supported image types.",
            TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
    }
    else
    {
        TaskDialog(hWnd, hInst,
            L"File association",
            L"Some types could not be associated",
            L"One or more file types could not be registered.\n"
            L"Try running Picasso Pictures as administrator and trying again.",
            TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
    }
}

// ---------------------------------------------------------------------
// Image metadata (EXIF / PNG text chunk) viewer
// ---------------------------------------------------------------------

// WIC stores EXIF RATIONAL values packed into a single UI8: the numerator
// in the high 32 bits, the denominator in the low 32 bits.
static bool RationalFromPackedUI8(ULONGLONG packed, double& outValue)
{
    UINT32 numerator   = static_cast<UINT32>(packed >> 32);
    UINT32 denominator = static_cast<UINT32>(packed & 0xFFFFFFFFull);
    if (denominator == 0)
        return false;
    outValue = static_cast<double>(numerator) / static_cast<double>(denominator);
    return true;
}

// Pulls a displayable string out of the PROPVARIANT types WIC's EXIF/PNG
// metadata readers actually return. Returns false if the variant is empty
// or of a type this viewer doesn't render.
static bool PropVariantToDisplayString(const PROPVARIANT& pv, std::wstring& out)
{
    switch (pv.vt)
    {
    case VT_LPWSTR:
        if (!pv.pwszVal) return false;
        out = pv.pwszVal;
        return !out.empty();

    case VT_LPSTR:  // EXIF ASCII fields (Make, Model, DateTime, ...) come back narrow
    {
        if (!pv.pszVal) return false;
        std::string s(pv.pszVal);
        while (!s.empty() && (s.back() == '\0' || s.back() == ' '))
            s.pop_back();
        out.assign(s.begin(), s.end());
        return !out.empty();
    }

    case VT_UI1: out = std::to_wstring(static_cast<unsigned>(pv.bVal));  return true;
    case VT_UI2: out = std::to_wstring(static_cast<unsigned>(pv.uiVal)); return true;
    case VT_UI4: out = std::to_wstring(static_cast<unsigned long>(pv.ulVal)); return true;
    case VT_I4:  out = std::to_wstring(static_cast<long>(pv.lVal));      return true;
    case VT_UI8: out = std::to_wstring(static_cast<unsigned long long>(pv.uhVal.QuadPart)); return true;

    // Some readers hand back a single-element vector instead of a scalar
    // (ISOSpeedRatings is the classic example).
    case (VT_VECTOR | VT_UI2):
        if (pv.caui.cElems == 0) return false;
        out = std::to_wstring(static_cast<unsigned>(pv.caui.pElems[0]));
        return true;
    case (VT_VECTOR | VT_UI4):
        if (pv.caul.cElems == 0) return false;
        out = std::to_wstring(static_cast<unsigned long>(pv.caul.pElems[0]));
        return true;

    default:
        return false;
    }
}

// Reads one tag by WIC metadata query path and appends "label: value\n" to
// 'out' if present. Returns true if the field was found.
static bool AppendSimpleField(IWICMetadataQueryReader* reader, const wchar_t* path,
                               const wchar_t* label, std::wstring& out)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(reader->GetMetadataByName(path, &pv)))
    {
        std::wstring val;
        if (PropVariantToDisplayString(pv, val))
        {
            out += std::wstring(label) + L": " + val + L"\n";
            found = true;
        }
    }
    PropVariantClear(&pv);
    return found;
}

// Same idea but reads a packed EXIF rational and applies custom formatting —
// exposure time as a fraction of a second, f-number/focal length as decimals.
static bool AppendRationalField(IWICMetadataQueryReader* reader, const wchar_t* path,
                                 const wchar_t* label, const wchar_t* fmt,
                                 std::wstring& out, bool asShutterFraction = false)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(reader->GetMetadataByName(path, &pv)) && pv.vt == VT_UI8)
    {
        double value;
        if (RationalFromPackedUI8(pv.uhVal.QuadPart, value) && value > 0.0)
        {
            wchar_t buf[64];
            if (asShutterFraction && value < 1.0)
                swprintf_s(buf, L"1/%.0f s", 1.0 / value);
            else
                swprintf_s(buf, fmt, value);
            out += std::wstring(label) + L": " + buf + L"\n";
            found = true;
        }
    }
    PropVariantClear(&pv);
    return found;
}

// GPS latitude/longitude are each three packed rationals (degrees, minutes,
// seconds) plus a hemisphere reference ("N"/"S", "E"/"W"). 'ifdGpsRoot' is
// the container-specific root path (e.g. "/app1/ifd/gps" for JPEG,
// "/ifd/gps" for PNG/WebP).
static bool AppendGpsCoordinate(IWICMetadataQueryReader* reader, const wchar_t* ifdGpsRoot, std::wstring& out)
{
    PROPVARIANT latRef, lat, lonRef, lon;
    PropVariantInit(&latRef); PropVariantInit(&lat);
    PropVariantInit(&lonRef); PropVariantInit(&lon);

    std::wstring root = ifdGpsRoot;
    bool ok =
        SUCCEEDED(reader->GetMetadataByName((root + L"/{ushort=1}").c_str(), &latRef)) &&
        SUCCEEDED(reader->GetMetadataByName((root + L"/{ushort=2}").c_str(), &lat))    &&
        SUCCEEDED(reader->GetMetadataByName((root + L"/{ushort=3}").c_str(), &lonRef)) &&
        SUCCEEDED(reader->GetMetadataByName((root + L"/{ushort=4}").c_str(), &lon))    &&
        lat.vt == (VT_VECTOR | VT_UI8) && lat.cauh.cElems == 3 &&
        lon.vt == (VT_VECTOR | VT_UI8) && lon.cauh.cElems == 3;

    if (ok)
    {
        auto toDecimalDegrees = [](const PROPVARIANT& dms) -> double
        {
            double deg = 0.0, min = 0.0, sec = 0.0;
            RationalFromPackedUI8(dms.cauh.pElems[0].QuadPart, deg);
            RationalFromPackedUI8(dms.cauh.pElems[1].QuadPart, min);
            RationalFromPackedUI8(dms.cauh.pElems[2].QuadPart, sec);
            return deg + min / 60.0 + sec / 3600.0;
        };

        double latDec = toDecimalDegrees(lat);
        double lonDec = toDecimalDegrees(lon);

        std::wstring latRefStr, lonRefStr;
        PropVariantToDisplayString(latRef, latRefStr);
        PropVariantToDisplayString(lonRef, lonRefStr);

        if (!latRefStr.empty() && towlower(latRefStr[0]) == L's')
            latDec = -latDec;
        if (!lonRefStr.empty() && towlower(lonRefStr[0]) == L'w')
            lonDec = -lonDec;

        wchar_t buf[128];
        swprintf_s(buf, L"GPS location: %.6f, %.6f\n", latDec, lonDec);
        out += buf;
    }

    PropVariantClear(&latRef); PropVariantClear(&lat);
    PropVariantClear(&lonRef); PropVariantClear(&lon);
    return ok;
}

// Human-readable EXIF Orientation (tag 274) values: 1-8, describing the
// rotation/mirroring needed to display the image upright.
static const wchar_t* OrientationName(unsigned value)
{
    switch (value)
    {
    case 1: return L"Normal";
    case 2: return L"Flipped horizontally";
    case 3: return L"Rotated 180\u00B0";
    case 4: return L"Flipped vertically";
    case 5: return L"Rotated 90\u00B0 CW, then flipped horizontally";
    case 6: return L"Rotated 90\u00B0 CW";
    case 7: return L"Rotated 90\u00B0 CCW, then flipped horizontally";
    case 8: return L"Rotated 90\u00B0 CCW";
    default: return L"Unknown";
    }
}

// EXIF ColorSpace (tag 40961): 1 = sRGB, 0xFFFF = "Uncalibrated" — the
// latter is what Adobe RGB and other non-sRGB workflows typically write,
// since EXIF has no tag that names the calibrated space directly.
static bool AppendColorSpaceField(IWICMetadataQueryReader* reader, const wchar_t* path, std::wstring& out)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(reader->GetMetadataByName(path, &pv)))
    {
        unsigned v = (pv.vt == VT_UI2) ? pv.uiVal : (pv.vt == VT_UI4 ? pv.ulVal : 0u);
        if (v == 1)
        {
            out += L"Color space: sRGB\n";
            found = true;
        }
        else if (v == 0xFFFF)
        {
            out += L"Color space: Uncalibrated (non-sRGB)\n";
            found = true;
        }
    }
    PropVariantClear(&pv);
    return found;
}

// EXIF Orientation (tag 274). See OrientationName() above for the mapping.
static bool AppendOrientationField(IWICMetadataQueryReader* reader, const wchar_t* path, std::wstring& out)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(reader->GetMetadataByName(path, &pv)))
    {
        unsigned v = (pv.vt == VT_UI2) ? pv.uiVal : (pv.vt == VT_UI4 ? pv.ulVal : 0u);
        if (v >= 1 && v <= 8)
        {
            out += std::wstring(L"Orientation: ") + OrientationName(v) + L"\n";
            found = true;
        }
    }
    PropVariantClear(&pv);
    return found;
}

// ---- Bare-value variants for the "Properties" dialog (IDD_PROPERTIES) ----
// The AppendXField() helpers above build "label: value\n" lines for the
// free-form "View metadata" text dump. The Properties dialog instead has a
// dedicated static control per field, so these return just the value (no
// label, no trailing newline) for SetDlgItemTextW to drop straight in.

static bool ReadDateTakenValue(IWICMetadataQueryReader* reader, const wchar_t* path, std::wstring& out)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(reader->GetMetadataByName(path, &pv)))
        found = PropVariantToDisplayString(pv, out);
    PropVariantClear(&pv);
    return found;
}

static bool ReadColorSpaceValue(IWICMetadataQueryReader* reader, const wchar_t* path, std::wstring& out)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(reader->GetMetadataByName(path, &pv)))
    {
        unsigned v = (pv.vt == VT_UI2) ? pv.uiVal : (pv.vt == VT_UI4 ? pv.ulVal : 0u);
        if (v == 1)          { out = L"sRGB";                        found = true; }
        else if (v == 0xFFFF) { out = L"Uncalibrated (non-sRGB)";     found = true; }
    }
    PropVariantClear(&pv);
    return found;
}

static bool ReadOrientationValue(IWICMetadataQueryReader* reader, const wchar_t* path, std::wstring& out)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(reader->GetMetadataByName(path, &pv)))
    {
        unsigned v = (pv.vt == VT_UI2) ? pv.uiVal : (pv.vt == VT_UI4 ? pv.ulVal : 0u);
        if (v >= 1 && v <= 8)
        {
            out = OrientationName(v);
            found = true;
        }
    }
    PropVariantClear(&pv);
    return found;
}

// Renders a byte count the way Explorer's Properties dialog does: a
// human-scaled size followed by the exact byte count in parentheses.
static std::wstring FormatFileSizeW(ULONGLONG bytes)
{
    wchar_t buf[96];
    if (bytes < 1024ULL)
        swprintf_s(buf, L"%llu bytes", bytes);
    else if (bytes < 1024ULL * 1024)
        swprintf_s(buf, L"%.1f KB (%llu bytes)", bytes / 1024.0, bytes);
    else if (bytes < 1024ULL * 1024 * 1024)
        swprintf_s(buf, L"%.2f MB (%llu bytes)", bytes / (1024.0 * 1024.0), bytes);
    else
        swprintf_s(buf, L"%.2f GB (%llu bytes)", bytes / (1024.0 * 1024.0 * 1024.0), bytes);
    return buf;
}

// Formats a FILETIME (as returned by GetFileAttributesEx, i.e. UTC) using
// the user's locale short-date and time formats.
static std::wstring FormatFileTimeW(const FILETIME& ftUtc)
{
    if (ftUtc.dwLowDateTime == 0 && ftUtc.dwHighDateTime == 0)
        return L"Unknown";

    FILETIME ftLocal;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&ftUtc, &ftLocal) || !FileTimeToSystemTime(&ftLocal, &st))
        return L"Unknown";

    wchar_t dateBuf[64] = {};
    wchar_t timeBuf[64] = {};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, dateBuf, ARRAYSIZE(dateBuf), nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, timeBuf, ARRAYSIZE(timeBuf));
    return std::wstring(dateBuf) + L"  " + timeBuf;
}

// Maps this app's supported file extensions to a display-friendly format
// name. Kept in sync with the extension list in IsSupportedImage.
static const wchar_t* ImageFormatNameFromExtension(const std::wstring& extLower)
{
    if (extLower == L".jpg" || extLower == L".jpeg") return L"JPEG";
    if (extLower == L".png")                          return L"PNG";
    if (extLower == L".bmp")                           return L"BMP";
    if (extLower == L".gif")                           return L"GIF";
    if (extLower == L".tif" || extLower == L".tiff")   return L"TIFF";
    if (extLower == L".webp")                          return L"WebP";
    if (extLower == L".avif")                          return L"AVIF";
    if (extLower == L".jxl")                            return L"JPEG XL";
    return L"Unknown";
}

// ---------------------------------------------------------------------
// Raw PNG text-chunk reader (tEXt / iTXt).
//
// WIC's PNG metadata query reader only surfaces a tEXt/iTXt chunk if you
// query it by its exact keyword, and there's no fixed list of keywords
// to try — AI generation tools (Stable Diffusion, ComfyUI, Automatic1111)
// embed their data under arbitrary keywords like "prompt", "workflow", or
// "parameters" that aren't part of any standard set. Parsing the PNG
// chunk stream directly finds every text chunk regardless of keyword.
// ---------------------------------------------------------------------
struct PngTextField
{
    std::wstring keyword;
    std::wstring value;
};

static std::wstring Latin1BytesToWide(const char* data, size_t len)
{
    // Each Latin-1 byte maps 1:1 to the Unicode code point of the same
    // value, so this widen is exact — no conversion table needed.
    std::wstring w;
    w.reserve(len);
    for (size_t i = 0; i < len; ++i)
        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(data[i])));
    return w;
}

static std::wstring Utf8BytesToWide(const char* data, size_t len)
{
    if (len == 0) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(len), nullptr, 0);
    if (wlen <= 0) return L"";
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(len), &w[0], wlen);
    return w;
}

static bool ReadPngTextChunks(const std::wstring& path, std::vector<PngTextField>& out)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER sizeLI = {};
    if (!GetFileSizeEx(hFile, &sizeLI) || sizeLI.QuadPart <= 0)
    {
        CloseHandle(hFile);
        return false;
    }

    // Metadata chunks are conventionally written right after IHDR, well
    // before the (often large) IDAT pixel data, so capping how much we
    // read keeps this cheap even on big files without missing anything
    // in practice. 8 MB comfortably covers even large embedded workflow
    // JSON blobs from tools like ComfyUI.
    constexpr DWORD kMaxRead = 8 * 1024 * 1024;
    DWORD toRead = static_cast<DWORD>(min(sizeLI.QuadPart, (LONGLONG)kMaxRead));

    std::vector<BYTE> buf(toRead);
    DWORD bytesRead = 0;
    bool ok = ReadFile(hFile, buf.data(), toRead, &bytesRead, nullptr) != 0;
    CloseHandle(hFile);
    if (!ok || bytesRead < 8)
        return false;

    static const BYTE kSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (memcmp(buf.data(), kSig, 8) != 0)
        return false;

    auto readBE32 = [&](size_t at) -> UINT32
    {
        return (UINT32(buf[at]) << 24) | (UINT32(buf[at + 1]) << 16) |
               (UINT32(buf[at + 2]) << 8) | UINT32(buf[at + 3]);
    };

    size_t pos = 8;
    while (pos + 8 <= bytesRead)
    {
        UINT32 len = readBE32(pos);
        char type[5] = {};
        memcpy(type, &buf[pos + 4], 4);
        size_t dataStart = pos + 8;

        if (dataStart + len > bytesRead)
            break;  // chunk runs past what we read — stop rather than misread

        if (strcmp(type, "IEND") == 0)
            break;

        const char* data = reinterpret_cast<const char*>(&buf[dataStart]);

        if (strcmp(type, "tEXt") == 0)
        {
            // keyword \0 text — both Latin-1
            size_t nul = 0;
            while (nul < len && data[nul] != '\0') ++nul;
            if (nul < len)
            {
                PngTextField f;
                f.keyword = Latin1BytesToWide(data, nul);
                f.value   = Latin1BytesToWide(data + nul + 1, len - nul - 1);
                out.push_back(std::move(f));
            }
        }
        else if (strcmp(type, "iTXt") == 0)
        {
            // keyword \0 compressionFlag(1) compressionMethod(1) languageTag \0 translatedKeyword \0 text
            size_t nul1 = 0;
            while (nul1 < len && data[nul1] != '\0') ++nul1;
            if (nul1 + 2 < len)
            {
                unsigned char compressionFlag = static_cast<unsigned char>(data[nul1 + 1]);
                size_t i = nul1 + 3;
                size_t nul2 = i;
                while (nul2 < len && data[nul2] != '\0') ++nul2;        // language tag
                size_t nul3 = (nul2 < len) ? nul2 + 1 : nul2;
                while (nul3 < len && data[nul3] != '\0') ++nul3;        // translated keyword
                size_t textStart = (nul3 < len) ? nul3 + 1 : len;

                // Compressed iTXt (compressionFlag == 1) is skipped — the
                // tools that embed generation metadata this way virtually
                // always leave it uncompressed.
                if (compressionFlag == 0 && textStart <= len)
                {
                    PngTextField f;
                    f.keyword = Latin1BytesToWide(data, nul1);
                    f.value   = Utf8BytesToWide(data + textStart, len - textStart);
                    out.push_back(std::move(f));
                }
            }
        }
        // zTXt (zlib-compressed tEXt) intentionally skipped — no zlib
        // dependency in this project to decompress it.

        pos = dataStart + len + 4;  // + 4-byte CRC
    }

    return !out.empty();
}

// ---------------------------------------------------------------------
// Raw WebP XMP-chunk reader.
//
// WIC's WebP decoder doesn't surface the "XMP " RIFF chunk through its
// metadata query reader at all, so any XMP payload — hand-edited, or
// written by a tool that stashes arbitrary fields there the same way
// Stable Diffusion / ComfyUI stash them in PNG tEXt chunks — is otherwise
// invisible. This walks the RIFF chunk list directly, grabs the "XMP "
// chunk's XML payload, and pulls out every child element of every
// rdf:Description block regardless of namespace prefix or tag name — the
// same "don't assume a fixed keyword list" approach as the PNG reader
// above.
// ---------------------------------------------------------------------
static std::wstring DecodeXmlEntities(const std::wstring& in)
{
    std::wstring out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); )
    {
        if (in[i] == L'&')
        {
            size_t semi = in.find(L';', i);
            if (semi != std::wstring::npos && semi - i <= 10)
            {
                std::wstring ent = in.substr(i + 1, semi - i - 1);
                if (ent == L"amp")  { out += L'&';  i = semi + 1; continue; }
                if (ent == L"lt")   { out += L'<';  i = semi + 1; continue; }
                if (ent == L"gt")   { out += L'>';  i = semi + 1; continue; }
                if (ent == L"quot") { out += L'"';  i = semi + 1; continue; }
                if (ent == L"apos") { out += L'\''; i = semi + 1; continue; }
                if (ent.size() > 1 && ent[0] == L'#')
                {
                    bool hex = ent.size() > 2 && (ent[1] == L'x' || ent[1] == L'X');
                    wchar_t* end = nullptr;
                    long code = wcstol(ent.c_str() + (hex ? 2 : 1), &end, hex ? 16 : 10);
                    if (code > 0)
                    {
                        out += static_cast<wchar_t>(code);
                        i = semi + 1;
                        continue;
                    }
                }
            }
        }
        out += in[i++];
    }
    return out;
}

// Strips nested tags (e.g. the rdf:Alt/rdf:li wrapper some tools put
// around simple text values) so only the visible text remains.
static std::wstring StripXmlTags(const std::wstring& in)
{
    std::wstring out;
    out.reserve(in.size());
    bool inTag = false;
    for (wchar_t c : in)
    {
        if (c == L'<')      { inTag = true;  continue; }
        else if (c == L'>') { inTag = false; continue; }
        else if (!inTag)    out += c;
    }
    return out;
}

static std::wstring TrimWhitespace(const std::wstring& in)
{
    size_t start = in.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    size_t end = in.find_last_not_of(L" \t\r\n");
    return in.substr(start, end - start + 1);
}

// Finds the end of an XML opening tag (the '>' that isn't inside a quoted
// attribute value). Returns the index of that '>', or npos.
static size_t FindTagEnd(const std::wstring& xml, size_t from, size_t limit)
{
    bool inQuote = false;
    wchar_t quoteChar = 0;
    for (size_t i = from; i < limit; ++i)
    {
        wchar_t c = xml[i];
        if (inQuote) { if (c == quoteChar) inQuote = false; }
        else if (c == L'"' || c == L'\'') { inQuote = true; quoteChar = c; }
        else if (c == L'>') return i;
    }
    return std::wstring::npos;
}

static bool ReadWebpXmpFields(const std::wstring& path, std::vector<PngTextField>& out)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER sizeLI = {};
    if (!GetFileSizeEx(hFile, &sizeLI) || sizeLI.QuadPart <= 0)
    {
        CloseHandle(hFile);
        return false;
    }

    // XMP payloads carrying large JSON blobs (ComfyUI workflows etc.) can be
    // sizeable; 16 MB comfortably covers them without reading huge amounts
    // of trailing pixel data unnecessarily.
    constexpr DWORD kMaxRead = 16 * 1024 * 1024;
    DWORD toRead = static_cast<DWORD>(min(sizeLI.QuadPart, (LONGLONG)kMaxRead));

    std::vector<BYTE> buf(toRead);
    DWORD bytesRead = 0;
    bool ok = ReadFile(hFile, buf.data(), toRead, &bytesRead, nullptr) != 0;
    CloseHandle(hFile);
    if (!ok || bytesRead < 12)
        return false;

    if (memcmp(&buf[0], "RIFF", 4) != 0 || memcmp(&buf[8], "WEBP", 4) != 0)
        return false;

    auto readLE32 = [&](size_t at) -> UINT32
    {
        return UINT32(buf[at]) | (UINT32(buf[at + 1]) << 8) |
               (UINT32(buf[at + 2]) << 16) | (UINT32(buf[at + 3]) << 24);
    };

    // Walk the RIFF chunk list looking for "XMP " (the standard WebP
    // metadata FourCC for an embedded XMP packet).
    std::string xmpUtf8;
    size_t pos = 12;
    while (pos + 8 <= bytesRead)
    {
        UINT32 chunkLen = readLE32(pos + 4);
        size_t dataStart = pos + 8;

        if (dataStart + chunkLen > bytesRead)
            break;  // chunk runs past what we read — stop rather than misread

        if (memcmp(&buf[pos], "XMP ", 4) == 0)
        {
            xmpUtf8.assign(reinterpret_cast<const char*>(&buf[dataStart]), chunkLen);
            break;
        }

        pos = dataStart + chunkLen + (chunkLen & 1);  // chunks are word-aligned
    }

    if (xmpUtf8.empty())
        return false;

    std::wstring xmp = Utf8BytesToWide(xmpUtf8.data(), xmpUtf8.size());

    // Pull out every child element of every rdf:Description block. (A
    // document can legally have more than one, so keep scanning instead of
    // stopping at the first.)
    size_t searchFrom = 0;
    while (true)
    {
        size_t descStart = xmp.find(L"<rdf:Description", searchFrom);
        if (descStart == std::wstring::npos)
            break;

        size_t openEnd = FindTagEnd(xmp, descStart, xmp.size());
        if (openEnd == std::wstring::npos)
            break;

        bool selfClosing = (openEnd > 0 && xmp[openEnd - 1] == L'/');
        size_t contentStart = openEnd + 1;

        size_t descEnd = xmp.find(L"</rdf:Description>", contentStart);
        if (descEnd == std::wstring::npos)
            descEnd = xmp.size();

        if (!selfClosing)
        {
            size_t p = contentStart;
            while (p < descEnd)
            {
                size_t tagOpen = xmp.find(L'<', p);
                if (tagOpen == std::wstring::npos || tagOpen >= descEnd)
                    break;

                if (xmp[tagOpen + 1] == L'/')  // stray/unexpected close tag
                {
                    p = tagOpen + 1;
                    continue;
                }

                size_t nameEnd = tagOpen + 1;
                while (nameEnd < descEnd && !iswspace(xmp[nameEnd]) &&
                       xmp[nameEnd] != L'>' && xmp[nameEnd] != L'/')
                    ++nameEnd;
                std::wstring tagName = xmp.substr(tagOpen + 1, nameEnd - tagOpen - 1);

                size_t childOpenEnd = FindTagEnd(xmp, nameEnd, descEnd);
                if (childOpenEnd == std::wstring::npos)
                    break;

                bool childSelfClosing = (childOpenEnd > 0 && xmp[childOpenEnd - 1] == L'/');
                size_t innerStart = childOpenEnd + 1;

                if (childSelfClosing || tagName.empty())
                {
                    p = innerStart;
                    continue;
                }

                std::wstring closeTag = L"</" + tagName + L">";
                size_t innerEnd = xmp.find(closeTag, innerStart);
                if (innerEnd == std::wstring::npos || innerEnd > descEnd)
                {
                    p = innerStart;
                    continue;
                }

                std::wstring rawValue = xmp.substr(innerStart, innerEnd - innerStart);
                std::wstring value = TrimWhitespace(DecodeXmlEntities(StripXmlTags(rawValue)));

                if (!value.empty())
                {
                    size_t colon = tagName.find(L':');
                    std::wstring keyword = (colon != std::wstring::npos) ? tagName.substr(colon + 1) : tagName;

                    PngTextField f;
                    f.keyword = keyword;
                    f.value   = value;
                    out.push_back(std::move(f));
                }

                p = innerEnd + closeTag.size();
            }
        }

        searchFrom = descEnd + 1;
    }

    return !out.empty();
}

void ShowImageMetadata(HWND hWnd)
{
    if (g_currentFilePath.empty() || !g_wicFactory)
        return;

    // AVIF/JXL go through this app's own custom decoders rather than a WIC
    // codec, so there's no WIC metadata query reader to pull tags from.
    std::wstring extLower = g_currentFilePath.substr(g_currentFilePath.rfind(L'.'));
    std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::towlower);
    if (extLower == L".avif" || extLower == L".jxl")
    {
        TaskDialog(hWnd, hInst, L"Image metadata", L"Not supported for this format",
            L"Reading embedded metadata isn't supported for AVIF/JXL files yet.",
            TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
        return;
    }

    std::wstring body;
    bool foundAny = false;

    // PNG text chunks — parsed directly from the file so arbitrary keywords
    // (Stable Diffusion / ComfyUI's "prompt"/"workflow", Automatic1111's
    // "parameters", etc.) are found regardless of what they're named.
    if (extLower == L".png")
    {
        std::vector<PngTextField> fields;
        if (ReadPngTextChunks(g_currentFilePath, fields))
        {
            // Shown in a scrollable, selectable edit control (see
            // ShowMetadataViewerWindow below), so there's no need to
            // truncate long fields like Stable Diffusion / ComfyUI
            // "prompt"/"workflow" text — show it in full.
            for (auto& f : fields)
            {
                body += f.keyword + L":\n" + f.value + L"\n\n";
                foundAny = true;
            }
        }
    }

    // WebP XMP chunk — same idea as the PNG text chunks above, but WIC's
    // WebP decoder doesn't expose "XMP " through its metadata query reader
    // at all, so this is parsed straight from the RIFF chunk list.
    if (extLower == L".webp")
    {
        std::vector<PngTextField> fields;
        if (ReadWebpXmpFields(g_currentFilePath, fields))
        {
            for (auto& f : fields)
            {
                body += f.keyword + L":\n" + f.value + L"\n\n";
                foundAny = true;
            }
        }
    }

    // EXIF / TIFF-style tags via WIC — present on JPEG and TIFF files from
    // cameras, and occasionally on PNG (eXIf chunk) or WebP (EXIF chunk)
    // files exported from editing tools. JPEG exposes these under
    // "/app1/ifd/..." (the APP1 marker), but PNG/WebP containers expose the
    // same tags under "/ifd/..." instead — no APP1 marker, since that's a
    // JPEG-specific concept — so both roots are tried for every tag.
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = g_wicFactory->CreateDecoderFromFilename(
        g_currentFilePath.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);

    ComPtr<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &frame);

    ComPtr<IWICMetadataQueryReader> reader;
    if (SUCCEEDED(hr))
        hr = frame->GetMetadataQueryReader(&reader);

    if (SUCCEEDED(hr) && reader)
    {
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/{ushort=271}",        L"Camera make",   body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/{ushort=271}",             L"Camera make",   body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/{ushort=272}",        L"Camera model",  body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/{ushort=272}",             L"Camera model",  body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/exif/{ushort=36867}", L"Date taken",    body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/exif/{ushort=36867}",      L"Date taken",    body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/{ushort=306}",        L"Date modified", body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/{ushort=306}",             L"Date modified", body);
        foundAny |= AppendRationalField(reader.Get(), L"/app1/ifd/exif/{ushort=33434}", L"Exposure time", L"%.3g s", body, /*asShutterFraction=*/true)
                 || AppendRationalField(reader.Get(), L"/ifd/exif/{ushort=33434}",      L"Exposure time", L"%.3g s", body, /*asShutterFraction=*/true);
        foundAny |= AppendRationalField(reader.Get(), L"/app1/ifd/exif/{ushort=33437}", L"F-number",      L"f/%.1f", body)
                 || AppendRationalField(reader.Get(), L"/ifd/exif/{ushort=33437}",      L"F-number",      L"f/%.1f", body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/exif/{ushort=34855}", L"ISO speed",     body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/exif/{ushort=34855}",      L"ISO speed",     body);
        foundAny |= AppendRationalField(reader.Get(), L"/app1/ifd/exif/{ushort=37386}", L"Focal length",  L"%.1f mm", body)
                 || AppendRationalField(reader.Get(), L"/ifd/exif/{ushort=37386}",      L"Focal length",  L"%.1f mm", body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/{ushort=305}",        L"Software",      body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/{ushort=305}",             L"Software",      body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/{ushort=315}",        L"Artist",        body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/{ushort=315}",             L"Artist",        body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/{ushort=33432}",      L"Copyright",     body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/{ushort=33432}",           L"Copyright",     body);
        foundAny |= AppendGpsCoordinate(reader.Get(), L"/app1/ifd/gps", body)
                 || AppendGpsCoordinate(reader.Get(), L"/ifd/gps",      body);
    }

    if (!foundAny)
    {
        TaskDialog(hWnd, hInst, L"Image metadata", L"No metadata found",
            L"This image doesn't contain any readable embedded metadata.",
            TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
        return;
    }

    std::wstring windowTitle = L"Image metadata \u2014 " + g_currentFileName;
    ShowMetadataViewerWindow(hWnd, windowTitle, body);
}

// A resizable window with a read-only multiline edit control showing the
// full metadata text. Unlike TaskDialog (used previously), this supports
// mouse-wheel scrolling, text selection/copying, and doesn't truncate long
// content.
LRESULT CALLBACK MetadataViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HFONT  s_font      = nullptr;
    static HBRUSH s_editBrush = nullptr;
    static HBRUSH s_bkBrush   = nullptr;

    switch (message)
    {
    case WM_CREATE:
    {
        HWND hEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_LEFT,
            0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_METADATA_EDIT)),
            hInst, nullptr);

        // Remove the ~30k-character default cap so long fields (e.g. full
        // Stable Diffusion / ComfyUI prompt+workflow text) display in full.
        SendMessageW(hEdit, EM_SETLIMITTEXT, 0, 0);

        NONCLIENTMETRICSW ncm = { sizeof(ncm) };
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        {
            if (s_font) DeleteObject(s_font);
            s_font = CreateFontIndirectW(&ncm.lfMessageFont);
            SendMessageW(hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(s_font), TRUE);
        }

        auto* cs   = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* body = reinterpret_cast<const std::wstring*>(cs->lpCreateParams);
        if (body)
            SetWindowTextW(hEdit, body->c_str());

        EnableDarkTitleBar(hWnd);
        break;
    }
    case WM_SIZE:
    {
        HWND hEdit = GetDlgItem(hWnd, IDC_METADATA_EDIT);
        RECT rc;
        GetClientRect(hWnd, &rc);
        constexpr int margin = 8;
        MoveWindow(hEdit, margin, margin,
            (rc.right - rc.left) - margin * 2,
            (rc.bottom - rc.top) - margin * 2, TRUE);
        break;
    }
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, RGB(225, 225, 225));
        SetBkColor(hdc, RGB(32, 32, 32));
        if (!s_editBrush)
            s_editBrush = CreateSolidBrush(RGB(32, 32, 32));
        return reinterpret_cast<LRESULT>(s_editBrush);
    }
    case WM_ERASEBKGND:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(hWnd, &rc);
        if (!s_bkBrush)
            s_bkBrush = CreateSolidBrush(RGB(24, 24, 24));
        FillRect(hdc, &rc, s_bkBrush);
        return 1;
    }
    case WM_SETFOCUS:
        SetFocus(GetDlgItem(hWnd, IDC_METADATA_EDIT));
        break;
    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 480;
        mmi->ptMinTrackSize.y = 360;
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL)
        {
            DestroyWindow(hWnd);
            return 0;
        }
        if (LOWORD(wParam) == IDC_METADATA_SELECTALL)
        {
            SendMessageW(GetDlgItem(hWnd, IDC_METADATA_EDIT), EM_SETSEL, 0, -1);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        if (s_font)      { DeleteObject(s_font);      s_font      = nullptr; }
        if (s_editBrush) { DeleteObject(s_editBrush);  s_editBrush = nullptr; }
        if (s_bkBrush)   { DeleteObject(s_bkBrush);    s_bkBrush   = nullptr; }
        g_metadataViewerWnd = nullptr;
        break;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

ATOM RegisterMetadataViewerClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex   = {};
    wcex.cbSize        = sizeof(WNDCLASSEX);
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = MetadataViewerWndProc;
    wcex.hInstance     = hInstance;
    wcex.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PICASSOPICTURES));
    wcex.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr; // painted manually in WM_ERASEBKGND for the dark theme
    wcex.lpszClassName = L"MetadataViewerClass";
    wcex.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));
    return RegisterClassExW(&wcex);
}

// Shows the metadata text in a resizable, ownerdrawn-dark window with a
// read-only multiline edit control (mouse-wheel scroll, text selection,
// and Ctrl+C copying all work natively; no truncation). Runs its own
// message loop so it behaves like a modal dialog, matching the previous
// TaskDialog behavior.
void ShowMetadataViewerWindow(HWND owner, const std::wstring& title, const std::wstring& body)
{
    static bool s_classRegistered = false;
    if (!s_classRegistered)
    {
        RegisterMetadataViewerClass(hInst);
        s_classRegistered = true;
    }

    if (g_metadataViewerWnd)
    {
        DestroyWindow(g_metadataViewerWnd);
        g_metadataViewerWnd = nullptr;
    }

    // Size relative to the monitor's work area so the window is roomy on
    // large displays instead of the small fixed size used previously.
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
    const int workW = mi.rcWork.right  - mi.rcWork.left;
    const int workH = mi.rcWork.bottom - mi.rcWork.top;

    int width  = static_cast<int>(workW * 0.75);
    int height = static_cast<int>(workH * 0.85);

    RECT ownerRc;
    GetWindowRect(owner, &ownerRc);
    const int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - width) / 2;
    const int y = ownerRc.top  + ((ownerRc.bottom - ownerRc.top) - height) / 2;

    g_metadataViewerWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        L"MetadataViewerClass", title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        x, y, width, height,
        owner, nullptr, hInst, const_cast<std::wstring*>(&body));

    if (!g_metadataViewerWnd)
        return;

    ShowWindow(g_metadataViewerWnd, SW_SHOW);
    UpdateWindow(g_metadataViewerWnd);

    // Disable the owner and pump our own messages while the viewer is open
    // so it behaves like a modal dialog (same UX as the TaskDialog it
    // replaces), and so Escape / Ctrl+A work no matter which child has
    // focus.
    EnableWindow(owner, FALSE);

    ACCEL accels[] = {
        { FVIRTKEY,            VK_ESCAPE, IDCANCEL },
        { FVIRTKEY | FCONTROL, L'A',      IDC_METADATA_SELECTALL },
    };
    HACCEL hAccel = CreateAcceleratorTable(accels, ARRAYSIZE(accels));

    MSG msg;
    while (IsWindow(g_metadataViewerWnd) && GetMessage(&msg, nullptr, 0, 0))
    {
        if (hAccel && (msg.hwnd == g_metadataViewerWnd || IsChild(g_metadataViewerWnd, msg.hwnd)) &&
            TranslateAccelerator(g_metadataViewerWnd, hAccel, &msg))
            continue;

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hAccel)
        DestroyAcceleratorTable(hAccel);

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

// Plain data carried into PropertiesDlgProc via DialogBoxParamW's lParam.
// Every field is pre-formatted display text; fields that don't apply to
// the current image (e.g. "Date taken" on a screenshot) are left as an
// em dash rather than the row being hidden, so the dialog's layout is
// completely static — same spirit as IDD_COMMANDBOX.
struct ImagePropertiesFields
{
    std::wstring windowTitle;
    std::wstring fileName;
    std::wstring location;
    std::wstring size;
    std::wstring format;
    std::wstring dimensions;
    std::wstring bitDepth;
    std::wstring transparency;
    std::wstring resolution;
    std::wstring created;
    std::wstring modified;
    std::wstring dateTaken;
    std::wstring colorSpace;
    std::wstring orientation;
};

// Dialog proc for IDD_PROPERTIES — a bespoke, fixed-layout dialog template
// (built the same way as IDD_COMMANDBOX / "Keyboard shortcuts": static
// labels baked into the .rc, values filled in at WM_INITDIALOG) rather than
// the resizable MetadataViewerClass window used by "View metadata". Modal,
// like About() and the shortcuts box, so no manual message pump is needed.
INT_PTR CALLBACK PropertiesDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        const auto* f = reinterpret_cast<const ImagePropertiesFields*>(lParam);
        if (f)
        {
            SetWindowTextW(hDlg, f->windowTitle.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_FILENAME,     f->fileName.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_LOCATION,     f->location.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_SIZE,         f->size.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_FORMAT,       f->format.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_DIMENSIONS,   f->dimensions.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_BITDEPTH,     f->bitDepth.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_TRANSPARENCY, f->transparency.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_RESOLUTION,   f->resolution.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_CREATED,      f->created.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_MODIFIED,     f->modified.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_DATETAKEN,    f->dateTaken.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_COLORSPACE,   f->colorSpace.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_ORIENTATION,  f->orientation.c_str());
        }
        return (INT_PTR)TRUE;
    }

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

// Shows an in-app "Properties" dialog for the current image, built as its
// own DIALOGEX template (IDD_PROPERTIES) — a bespoke, fixed-size window in
// the same style as "Keyboard shortcuts", rather than reusing the
// resizable free-text viewer behind "View metadata". Covers the
// file-system-level facts Explorer's own Properties dialog leads with
// (name, location, size, timestamps) plus a handful of quick image-level
// facts (format, dimensions, bit depth, transparency, resolution, color
// space, orientation, date taken). Full embedded EXIF (camera/lens/GPS/
// software/etc.) is intentionally left to "View metadata" rather than
// duplicated here.
void ShowImageProperties(HWND hWnd)
{
    if (g_currentFilePath.empty())
        return;

    std::wstring extLower = g_currentFilePath.substr(g_currentFilePath.rfind(L'.'));
    std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::towlower);

    const wchar_t* kNotAvailable = L"\u2014"; // em dash, shown for fields that don't apply
    ImagePropertiesFields f;

    // ---- File-system properties ----
    f.fileName = g_currentFileName;

    std::wstring location = g_currentFilePath;
    size_t slash = location.find_last_of(L"\\/");
    f.location = (slash != std::wstring::npos) ? location.substr(0, slash) : L".";

    f.size = f.created = f.modified = kNotAvailable;
    WIN32_FILE_ATTRIBUTE_DATA attr = {};
    if (GetFileAttributesExW(g_currentFilePath.c_str(), GetFileExInfoStandard, &attr))
    {
        ULARGE_INTEGER size;
        size.HighPart = attr.nFileSizeHigh;
        size.LowPart  = attr.nFileSizeLow;
        f.size     = FormatFileSizeW(size.QuadPart);
        f.created  = FormatFileTimeW(attr.ftCreationTime);
        f.modified = FormatFileTimeW(attr.ftLastWriteTime);
    }

    f.format = ImageFormatNameFromExtension(extLower);

    // ---- Image properties (from the already-decoded WIC source, so this
    // doesn't need to touch disk again) ----
    f.dimensions = f.bitDepth = f.transparency = f.resolution = kNotAvailable;

    if (g_imageWidth > 0 && g_imageHeight > 0)
    {
        wchar_t dimBuf[64];
        swprintf_s(dimBuf, L"%d \u00D7 %d pixels", g_imageWidth, g_imageHeight);
        f.dimensions = dimBuf;
    }

    if (g_wicBitmapSource && g_wicFactory)
    {
        WICPixelFormatGUID pf;
        if (SUCCEEDED(g_wicBitmapSource->GetPixelFormat(&pf)))
        {
            ComPtr<IWICComponentInfo> compInfo;
            if (SUCCEEDED(g_wicFactory->CreateComponentInfo(pf, &compInfo)))
            {
                ComPtr<IWICPixelFormatInfo2> pfInfo;
                if (SUCCEEDED(compInfo.As(&pfInfo)))
                {
                    UINT bpp = 0;
                    if (SUCCEEDED(pfInfo->GetBitsPerPixel(&bpp)) && bpp > 0)
                        f.bitDepth = std::to_wstring(bpp) + L" bits per pixel";

                    BOOL hasAlpha = FALSE;
                    if (SUCCEEDED(pfInfo->SupportsTransparency(&hasAlpha)))
                        f.transparency = hasAlpha ? L"Yes" : L"No";
                }
            }
        }

        double dpiX = 0.0, dpiY = 0.0;
        if (SUCCEEDED(g_wicBitmapSource->GetResolution(&dpiX, &dpiY)) && dpiX > 0.0 && dpiY > 0.0)
        {
            wchar_t dpiBuf[64];
            if (std::lround(dpiX) == std::lround(dpiY))
                swprintf_s(dpiBuf, L"%.0f DPI", dpiX);
            else
                swprintf_s(dpiBuf, L"%.0f x %.0f DPI", dpiX, dpiY);
            f.resolution = dpiBuf;
        }
    }

    // A few EXIF fields that belong on "Properties" as much as on the raw
    // metadata dump. AVIF/JXL go through this app's own decoders rather
    // than a WIC codec, so there's no metadata query reader to read these
    // from — same limitation as "View metadata".
    f.dateTaken = f.colorSpace = f.orientation = kNotAvailable;

    if (extLower != L".avif" && extLower != L".jxl" && g_wicFactory)
    {
        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr = g_wicFactory->CreateDecoderFromFilename(
            g_currentFilePath.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder);

        ComPtr<IWICBitmapFrameDecode> frame;
        if (SUCCEEDED(hr))
            hr = decoder->GetFrame(0, &frame);

        ComPtr<IWICMetadataQueryReader> reader;
        if (SUCCEEDED(hr))
            hr = frame->GetMetadataQueryReader(&reader);

        if (SUCCEEDED(hr) && reader)
        {
            std::wstring val;
            if (ReadDateTakenValue(reader.Get(), L"/app1/ifd/exif/{ushort=36867}", val)
                || ReadDateTakenValue(reader.Get(), L"/ifd/exif/{ushort=36867}", val))
                f.dateTaken = val;

            val.clear();
            if (ReadColorSpaceValue(reader.Get(), L"/app1/ifd/exif/{ushort=40961}", val)
                || ReadColorSpaceValue(reader.Get(), L"/ifd/exif/{ushort=40961}", val))
                f.colorSpace = val;

            val.clear();
            if (ReadOrientationValue(reader.Get(), L"/app1/ifd/{ushort=274}", val)
                || ReadOrientationValue(reader.Get(), L"/ifd/{ushort=274}", val))
                f.orientation = val;
        }
    }

    f.windowTitle = L"Properties \u2014 " + g_currentFileName;

    // Owned by hWnd, so IsOwnedByOurWindow() in WM_ACTIVATE recognizes it
    // automatically — no manual fullscreen-exit suppression needed.
    DialogBoxParamW(hInst, MAKEINTRESOURCE(IDD_PROPERTIES), hWnd, PropertiesDlgProc,
        reinterpret_cast<LPARAM>(&f));
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
    // also makes the dialog an owned window of g_overlayWindow, which
    // IsOwnedByOurWindow() recognizes in WM_ACTIVATE — no manual suppression
    // needed here anymore.
    if (g_isFullscreen && g_overlayWindow && IsWindow(g_overlayWindow))
        ofn.hwndOwner = g_overlayWindow;

    bool got = GetOpenFileName(&ofn) != FALSE;

    if (!got)
        return false;   // User cancelled

    // Load image using Direct2D/WIC
    if (!LoadImageD2D(hWnd, fileName))
    {
        std::wstring msg = g_lastLoadError.empty()
            ? L"Failed to load image."
            : g_lastLoadError;
        MessageBox(hWnd, msg.c_str(), L"Error", MB_ICONERROR);
        return false;
    }
    BuildImageList(fileName);

    if (g_isFullscreen)
    {
        // Already fullscreen — reset layout using the overlay window so that
        // g_offsetX/Y are computed against the full-screen client rect.
        // Using g_mainWindow here (e.g. when called from the button callback)
        // would give wrong coordinates and cause the overImage check in
        // WM_LBUTTONUP to fail, exiting fullscreen on the next click.
        HWND layoutWnd = (g_overlayWindow && IsWindow(g_overlayWindow))
                         ? g_overlayWindow : hWnd;
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
    if (!g_imageFiles.empty() && g_currentImageIndex > 0)
    {
        g_thumbFreeScroll = false;  // resume auto-centering on the new image
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
        g_thumbFreeScroll = false;  // resume auto-centering on the new image
        g_currentImageIndex++;
        LoadImageD2D(hWnd, g_imageFiles[g_currentImageIndex].c_str());
        InitializeImageLayout(hWnd, true);
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
    g_smooth = 2*0.18f * 60.0f / (float)refreshRate;
    g_targetZoom = 0.0005f;
    g_isExiting = true;
}

void DeleteCurrentImage(HWND hWnd, bool permanent)
{
    if (g_currentImageIndex < 0 || g_currentImageIndex >= (int)g_imageFiles.size())
        return;

    std::wstring fileToDelete = g_imageFiles[g_currentImageIndex];
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
        g_imageStates.erase(fileToDelete);

        StopThumbnailLoader();

        struct ThumbSnap { Microsoft::WRL::ComPtr<IWICBitmap> wic;
                           Microsoft::WRL::ComPtr<ID2D1Bitmap> d2d; };
        std::unordered_map<std::wstring, ThumbSnap> snap;
        for (int si = 0; si < (int)g_thumbs.size() &&
                         si < (int)g_imageFiles.size(); ++si)
            if (g_thumbs[si].wic || g_thumbs[si].d2d)
                snap[g_imageFiles[si]] = { g_thumbs[si].wic, g_thumbs[si].d2d };

        g_imageFiles.erase(g_imageFiles.begin() + g_currentImageIndex);

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
            return;
        }

        if (g_currentImageIndex >= (int)g_imageFiles.size())
            g_currentImageIndex = (int)g_imageFiles.size() - 1;

        LoadImageD2D(hWnd, g_imageFiles[g_currentImageIndex].c_str());
        InitializeImageLayout(hWnd, true);
        StartThumbnailLoader();
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
    SetTimer(hWnd, ZOOM_DISPLAY_TIMER_ID, 1000, nullptr);
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

                std::wstring thumbExt = std::filesystem::path(files[idx]).extension().wstring();
                std::transform(thumbExt.begin(), thumbExt.end(), thumbExt.begin(), ::towlower);
                const bool isAvifThumb = (thumbExt == L".avif");
                const bool isJxlThumb  = (thumbExt == L".jxl");

                // For AVIF/JXL there's no WIC decoder/frame object at all — decode
                // through the same bundled codecs LoadImageD2D uses, straight into
                // a full-res IWICBitmapSource, then fall into the same scale-down
                // path used for every other format below. No embedded-thumbnail
                // fast path exists for these two (libavif/libjxl don't expose one
                // through this API), so it's always the "slow path" for them —
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
                    // wicTransform stays Rotate0 — AVIF/JXL orientation isn't applied
                    // yet anywhere in this app (see DecodeAvifToWicBitmap's comment).
                }
                else
                {
                    if (FAILED(wic->CreateDecoderFromFilename(files[idx].c_str(), nullptr,
                            GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) || !decoder)
                        continue;
                    if (g_thumbLoaderStop) break;

                    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) continue;
                    if (g_thumbLoaderStop) break;
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
                            // Embedded thumbnail is large enough — just convert to PBGRA.
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
                if (g_thumbLoaderStop) break;

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
                    if (g_thumbLoaderStop) break;

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
                else
                {
                    std::wstring msg = g_lastLoadError.empty()
                        ? L"Failed to load image."
                        : g_lastLoadError;
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
            // etc.) this isn't a real focus loss — see IsOwnedByOurWindow.
            HWND becoming = reinterpret_cast<HWND>(lParam);
            bool ownWindowTookFocus = becoming && IsOwnedByOurWindow(becoming);

            if (g_isFullscreen && g_fullScreenInitDone &&
                g_suppressFullscreenExitDepth == 0 && !ownWindowTookFocus)
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

        if (LoadImageD2D(hWnd, filePath))
        {
            BuildImageList(filePath);
            EnterFullscreen();
        }
        else
        {
            std::wstring msg = g_lastLoadError.empty()
                ? L"Failed to load image."
                : g_lastLoadError;
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
            // WM_ACTIVATE recognizes it automatically — no manual
            // suppression needed.
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        case ID_HELP_COMMANDS:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_COMMANDBOX), hWnd, About);
            break;

        case IDM_MENU_SHORTCUTS:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_COMMANDBOX), hWnd, About);
            break;

        case IDM_MENU_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;

        case IDM_MENU_HQ_FILTER:
        {
            // Pure registry write, no window involved — never needed
            // suppression in the first place.
            g_useHQFilter = !g_useHQFilter;
            DWORD val = g_useHQFilter ? 1 : 0;
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

        case IDM_MENU_FIT_TO_SCREEN:
        {
            // Pure registry write, no window involved — never needed
            // suppression in the first place.
            g_openFitToScreen = !g_openFitToScreen;
            DWORD val = g_openFitToScreen ? 1 : 0;
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

        case IDM_MENU_ASSOCIATE:
            // AssociateFileTypes shows a TaskDialogIndirect owned by hWnd —
            // recognized automatically by IsOwnedByOurWindow().
            AssociateFileTypes(hWnd);
            break;

        case IDM_CTX_COPY:
        {
            if (!g_d2dBitmap || !g_wicBitmapSource || g_currentFilePath.empty()) break;

            UINT srcW = 0, srcH = 0;
            g_wicBitmapSource->GetSize(&srcW, &srcH);

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
            memcpy(pBuf, &bi, sizeof(bi));
            BYTE* pDst = pBuf + sizeof(BITMAPINFOHEADER);

            ComPtr<IWICFormatConverter> conv;
            if (SUCCEEDED(g_wicFactory->CreateFormatConverter(&conv)) &&
                SUCCEEDED(conv->Initialize(g_wicBitmapSource.Get(),
                    GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone,
                    nullptr, 0.f, WICBitmapPaletteTypeCustom)))
            {
                conv->CopyPixels(nullptr, srcW * 4, (UINT)pixelBytes, pDst);
            }
            GlobalUnlock(hMem);

            if (OpenClipboard(hWnd))
            {
                EmptyClipboard();
                SetClipboardData(CF_DIB, hMem);
                CloseClipboard();
            }
            break;
        }

        case IDM_CTX_DELETE:
        {
            bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            DeleteCurrentImage(hWnd, shiftHeld);
            break;
        }

        case IDM_CTX_OPEN_FOLDER:
        {
            if (g_currentFilePath.empty()) break;
            PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(g_currentFilePath.c_str());
            if (pidl)
            {
                SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
                ILFree(pidl);
            }
            break;
        }

        case IDM_CTX_PROPERTIES:
            // ShowImageProperties's dialog is owned by hWnd — recognized
            // automatically by IsOwnedByOurWindow(), same as "View
            // metadata" below.
            ShowImageProperties(hWnd);
            break;

        case IDM_CTX_METADATA:
            // ShowImageMetadata's viewer window is owned by hWnd —
            // recognized automatically by IsOwnedByOurWindow().
            ShowImageMetadata(hWnd);
            break;

        case IDM_CTX_WALLPAPER:
        {
            // Both MessageBoxW calls below are owned by hWnd, so they're
            // recognized automatically by IsOwnedByOurWindow() — no manual
            // suppression needed. (The old version set the suppress flag
            // true at the top of this block and had two early `break`s
            // before the reset at the bottom, which used to leave
            // fullscreen-exit permanently disabled after hitting either
            // one — that whole class of bug goes away once nothing here
            // has to set/reset a flag by hand.)
            if (g_currentFilePath.empty()) break;
            auto ext = g_currentFilePath.substr(g_currentFilePath.rfind(L'.'));
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
                (PVOID)g_currentFilePath.c_str(),
                SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            MessageBoxW(hWnd,
                (L"\"" + g_currentFileName + L"\" set as wallpaper.").c_str(),
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

    case WM_RBUTTONUP:
    {
        if (g_currentFilePath.empty()) break;

        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING,    IDM_CTX_COPY,        L"Copy image");
        AppendMenuW(hMenu, MF_STRING,    IDM_CTX_DELETE,      L"Delete");
        AppendMenuW(hMenu, MF_SEPARATOR, 0,                    nullptr);
        AppendMenuW(hMenu, MF_STRING,    IDM_CTX_OPEN_FOLDER, L"Open containing folder");
        AppendMenuW(hMenu, MF_STRING,    IDM_CTX_PROPERTIES,  L"Properties");
        AppendMenuW(hMenu, MF_STRING,    IDM_CTX_METADATA,    L"View metadata");
        AppendMenuW(hMenu, MF_SEPARATOR, 0,                    nullptr);
        AppendMenuW(hMenu, MF_STRING,    IDM_CTX_WALLPAPER,   L"Set as wallpaper");

        POINT pt;
        GetCursorPos(&pt);
        // Same reasoning as the hamburger menu: TrackPopupMenu's internal
        // menu window isn't one of our owned windows, and SetForegroundWindow
        // is what fires WA_INACTIVE — so this needs an explicit suppression
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
        float x = static_cast<float>(GET_X_LPARAM(lParam));
        float y = static_cast<float>(GET_Y_LPARAM(lParam));
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
        // Thumbnail strip interaction.
        // The entire strip row is claimed on mouse-down so the user can drag to
        // pan without accidentally starting an image-pan drag.  Whether the
        // gesture ends as a click (open image) or a drag (pan with momentum) is
        // decided in WM_LBUTTONUP.
        if (g_thumbSizeCaptured)
        {
            RECT rc2; GetClientRect(hWnd, &rc2);
            const float rtH      = (float)(rc2.bottom - rc2.top);
            const float rtW      = (float)(rc2.right  - rc2.left);
            const float stripCY  = rtH - g_thumbStripInsetFromBottomPx;
            const float stripTop = stripCY - g_thumbH * 0.5f;
            const float stripBot = stripCY + g_thumbH * 0.5f;

            if (y >= stripTop && y <= stripBot)
            {
                // Identify which slot (if any) the cursor is over — we'll need
                // this if the gesture turns out to be a click, not a drag.
                // The hit half-width is expanded by half the gap on each side so
                // clicks in the gaps between thumbnails register on the nearest
                // one, leaving no dead zone and no overlap.
                const float baseX    = rtW * 0.5f + g_thumbScrollOffset;
                const float stride   = g_thumbW + g_thumbGap;
                const float hitHalfW = g_thumbW * 0.5f + g_thumbGap * 0.5f;
                const int   n        = (int)g_thumbs.size();
                g_thumbDragHitIndex = -1;
                for (int i = 0; i < n; ++i)
                {
                    const float cx = baseX + i * stride;
                    if (x >= cx - hitHalfW && x <= cx + hitHalfW)
                    {
                        g_thumbDragHitIndex = i;
                        break;
                    }
                }

                g_thumbDragging   = true;
                g_thumbFreeScroll = true;   // suspend auto-centering immediately
                g_thumbDragStartX = x;
                g_thumbDragLastX  = x;
                g_thumbVelocity   = 0.f;
                SetCapture(hWnd);
                return 0;
            }
        }

        g_isDragging = true;
        SetCapture(hWnd);
        g_lastMouse.x = static_cast<LONG>(x);
        g_lastMouse.y = static_cast<LONG>(y);
        g_mouseFromDown = g_lastMouse;
    }
    break;
    
    case WM_LBUTTONUP:
    {
        float x = (float)GET_X_LPARAM(lParam);
        float y = (float)GET_Y_LPARAM(lParam);

        // ---- Resolve thumbnail strip drag ----------------------------------------
        if (g_thumbDragging)
        {
            g_thumbDragging = false;
            g_isDragging    = false;  // ensure image-drag state is never left stale
            ReleaseCapture();

            const float totalMove = fabsf(x - g_thumbDragStartX);
            if (totalMove < 5.f)
            {
                // Gesture was a click: open the thumbnail that was under the cursor.
                // Clicking the current image or a gap just snaps the strip back.
                if (g_thumbDragHitIndex >= 0 &&
                    g_thumbDragHitIndex < (int)g_imageFiles.size() &&
                    g_thumbDragHitIndex != g_currentImageIndex)
                {
                    g_currentImageIndex = g_thumbDragHitIndex;
                    LoadImageD2D(hWnd, g_imageFiles[g_thumbDragHitIndex].c_str());
                    InitializeImageLayout(hWnd, true);  // reset zoom/pan to fit, matching OpenNextImage/OpenPrevImage
                }
                g_thumbFreeScroll = false;  // snap strip back to current image
            }
            else
            {
                // Gesture was a drag: apply momentum.
                g_thumbTargetOffset = g_thumbScrollOffset + g_thumbVelocity * 25.f;
                // Clamp the momentum target so a fast fling cannot overshoot
                // the first or last thumbnail, and spring back any rubber-band overshoot.
                if (g_thumbSizeCaptured && !g_imageFiles.empty())
                {
                    const float step   = g_thumbW + g_thumbGap;
                    const float minOff = -(static_cast<float>(g_imageFiles.size() - 1) * step);
                    const float maxOff = 0.f;
                    if (g_thumbTargetOffset > maxOff) g_thumbTargetOffset = maxOff;
                    if (g_thumbTargetOffset < minOff) g_thumbTargetOffset = minOff;
                }
                // g_thumbFreeScroll stays true so UpdateEngine does not overwrite
                // the momentum target until the user navigates to another image.
            }
            return 0;
        }
        // --------------------------------------------------------------------------

        if (!g_d2dBitmap) break;

        g_isDragging = false;
        ReleaseCapture();
        
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
        SetTimer(hWnd, KILL_ROTATION_TIMER_ID, 120, nullptr);
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
            // Keep the bottom vignette (and the thumbnail strip it drives) visible
            // for as long as the strip is still moving under momentum so it never
            // disappears mid-coast.
            const bool stripCoasting = g_thumbFreeScroll &&
                                       !g_thumbDragging &&
                                       g_thumbSizeCaptured &&
                                       fabsf(g_thumbTargetOffset - g_thumbScrollOffset) > 1.0f;
            g_bottomVignetteTarget = (((float)pt.y >= bottomThresh) || stripCoasting) ? 1.0f : 0.0f;
        }
        else
        {
            g_topVignetteTarget    = 0.0f;
            g_bottomVignetteTarget = 0.0f;
        }


        // ---- Strip drag: pan the filmstrip directly, tracking velocity ----
        if (g_thumbDragging && (wParam & MK_LBUTTON))
        {
            const float rawDx = (float)pt.x - g_thumbDragLastX;

            // Apply rubber-band resistance when already past an edge:
            // movement in the overshoot zone counts at 30%.
            float dx = rawDx;
            if (g_thumbSizeCaptured && !g_imageFiles.empty())
            {
                const float step   = g_thumbW + g_thumbGap;
                const float minOff = -(static_cast<float>(g_imageFiles.size() - 1) * step);
                const float maxOff = 0.f;
                const bool pastMax = g_thumbScrollOffset > maxOff && rawDx > 0.f;
                const bool pastMin = g_thumbScrollOffset < minOff && rawDx < 0.f;
                if (pastMax || pastMin)
                    dx = rawDx * 1.0f;
            }

            g_thumbScrollOffset += dx;
            g_thumbTargetOffset  = g_thumbScrollOffset;
            g_thumbVelocity      = g_thumbVelocity * 0.65f + dx * 0.35f;
            g_thumbDragLastX     = (float)pt.x;

            // Hard cap: never let the strip go further than half the screen width
            // past the boundary — after that it simply stops stretching.
            if (g_thumbSizeCaptured && !g_imageFiles.empty())
            {
                const float step        = g_thumbW + g_thumbGap;
                const float minOff      = -(static_cast<float>(g_imageFiles.size() - 1) * step);
                const float maxOff      = 0.f;
                RECT rcRB; GetClientRect(hWnd, &rcRB);
                const float maxOvershoot = 100.0f*(float)(rcRB.right - rcRB.left);// * 0.5f;

                if (g_thumbScrollOffset > maxOff + maxOvershoot)
                {
                    g_thumbScrollOffset = maxOff + maxOvershoot;
                    g_thumbTargetOffset = g_thumbScrollOffset;
                }
                else if (g_thumbScrollOffset < minOff - maxOvershoot)
                {
                    g_thumbScrollOffset = minOff - maxOvershoot;
                    g_thumbTargetOffset = g_thumbScrollOffset;
                }
            }
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

        float x = static_cast<float>(GET_X_LPARAM(lParam));
        float y = static_cast<float>(GET_Y_LPARAM(lParam));

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
        if (wParam == ENTER_FULL_SCREEN_TIMER_ID)
        {
            KillTimer(hWnd, ENTER_FULL_SCREEN_TIMER_ID);
            EnterFullscreen(g_pendingPreserveView, false);
        }
        if (wParam == KILL_ROTATION_TIMER_ID)
        {
            // Handle incomplete rotations:
            KillTimer(hWnd, KILL_ROTATION_TIMER_ID);
            if (g_imageRotationAngle < g_targetRotationAngle)
            {
                g_targetRotationAngle = std::ceil(g_imageRotationAngle / 90.0f) * 90.0f;
            }
            else
            {
                g_targetRotationAngle = std::floor(g_imageRotationAngle / 90.0f) * 90.0f;
            }
        }
        if (wParam == ZOOM_DISPLAY_TIMER_ID)
            {
                KillTimer(hWnd, ZOOM_DISPLAY_TIMER_ID);
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
        if (g_isSlideshowMode)
            KillTimer(g_mainWindow, SLIDESHOW_TIMER_ID);
            SetTimer(g_mainWindow, SLIDESHOW_TIMER_ID, SLIDESHOW_INTERVAL_MS, nullptr);

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
                return 0;
            }

            case 0x44: // 'D' key
            case VK_SPACE:
            case VK_RIGHT:
            {
                if (g_navPendingRender) break;
                g_navPendingRender = true;
                OpenNextImage(hWnd);
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
        SetTimer(hWnd, KILL_ROTATION_TIMER_ID, 120, nullptr);
    }
    break;
    
    case WM_APP_EXITFULLSCREEN:
    {
        // Re-check the suppress depth here, not just at post-time. This
        // message is posted (queued), so by the time it's actually
        // dispatched — which can happen well after the WM_ACTIVATE
        // (WA_INACTIVE) that posted it, e.g. once a modal popup menu's
        // internal message pump gets around to it — an intervening guard
        // (hamburger menu, etc.) may have opened a new suppression scope.
        // Without this check the stale post fires anyway and exits
        // fullscreen out from under whatever UI is currently suppressing it.
        if (g_isFullscreen && g_suppressFullscreenExitDepth == 0)
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
            StopAnimDecodeThread();
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

    case WM_NOTIFY:
    {
        NMHDR* pHdr = reinterpret_cast<NMHDR*>(lParam);
        if (pHdr->idFrom == IDC_SYSLINK_GITHUB && pHdr->code == NM_CLICK)
        {
            NMLINK* pLink = reinterpret_cast<NMLINK*>(lParam);
            ShellExecuteW(hDlg, L"open", pLink->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
        }
        return (INT_PTR)TRUE;
    }


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