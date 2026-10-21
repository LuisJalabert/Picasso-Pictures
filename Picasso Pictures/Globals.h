#pragma once
// Globals.h : shared global state + shared types for Picasso Pictures.
//
// Every module (ViewMath / Fullscreen / Rendering / Decoding / Thumbnails / UI /
// Metadata / Dialogs) and the main file touch these variables, so they are
// DECLARED here (extern) and DEFINED once, in the same order, in "Globals.cpp"
// - defining a global in a header would be an ODR violation, and per-module
// accessors would be pure noise.
//
// The section banners below mark the groups of state that belong together. All
// but the two control maps are structs with one instance each: AppIdentity
// (g_app.mainWindow), DeviceResources (g_devices.renderTarget), ViewState
// (g_view.zoom), BackgroundArt (g_background.bitmap), FullscreenState
// (g_fullscreen.initDone), ImageState (g_image.path), AnimState (g_anim.frames),
// RenderPipeline (g_pipeline.mipTex), AppSettings (g_settings.useHQFilter),
// VignetteState (g_vignette.topVisibility), ThumbState (g_thumb.entries) and
// SlideshowState (g_slideshow.active). The only flat globals are the two control
// maps, g_textBoxes and g_buttons; the ID enums and the slideshow timings are
// not state at all.
//
// Two rules for anyone adding to this file:
//   1. Declare here (extern), define in Globals.cpp - never define a
//      global in a header. (The `constexpr` values and the ID enums below are
//      the exception: a namespace-scope constexpr and an enumerator both have
//      internal linkage, so each TU gets its own copy and that is fine and
//      ODR-safe. Do not "also" define the constexpr ones in Globals.cpp - that
//      would be a redefinition.)
//   2. g_fullscreen.suppressExitDepth is only ever touched through
//      FullscreenExitSuppressor, declared at the bottom of this file.

#ifndef MAX_LOADSTRING
#define MAX_LOADSTRING 100
#endif

#include "framework.h"
#include "resource.h"
#include "AnimatedButton.h"
#include "UITextBox.h"

#include <windows.h>
#include <objidl.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

// ============================================================================
// Types shared between modules
// ============================================================================

// Per-file view state (pan/zoom/rotation), remembered per image path so
// returning to a file restores how you left it. Held in ViewState's
// stateByPath map, which only image navigation touches: LoadImageD2D saves the
// outgoing file's state, FinishImageLoad restores the incoming file's, and
// DeleteCurrentImage erases the entry for a file that was deleted.
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

// One thumbnail in the film strip. Split CPU-side vs GPU-side on purpose:
// `wic` survives a device loss, `d2d` does not (cleared by
// DiscardDeviceResources and re-uploaded lazily by Render).
struct ThumbnailEntry
{
    Microsoft::WRL::ComPtr<IWICBitmap>  wic;  // CPU-side; survives device loss
    Microsoft::WRL::ComPtr<ID2D1Bitmap> d2d;  // GPU-side; cleared on device loss
};

// ============================================================================
// Control / command IDs
// ============================================================================

// One scoped enum per group. The groups stay separate types because they
// collide in value: TextBoxId::FILE_NAME and ButtonId::ZOOM_11 are both 0,
// TextBoxId::ZOOM_INPUT and ButtonId::ZOOM_IN are both 1. Scoped is what makes
// every use name its group (ButtonId::ROTATE_LEFT); the cost is an explicit
// conversion wherever an ID crosses to Win32 - AppendMenuW, SetTimer/KillTimer,
// GetDlgItem, the accelerator table, and the WM_COMMAND switch, whose control
// value is the int LOWORD(wParam) and which also handles the IDM_OPEN /
// IDM_ABOUT / IDM_EXIT ids that come from resource.h.
// Like a namespace-scope constexpr, an enumerator has internal linkage, so each
// TU gets its own copy and nothing is defined in Globals.cpp.

// Text-box control IDs; also the key type of g_textBoxes.
enum class TextBoxId
{
    FILE_NAME  = 0,
    ZOOM_INPUT = 1
};

// Button control IDs; also the key type of g_buttons.
enum class ButtonId
{
    ZOOM_11      = 0,
    ZOOM_IN      = 1,
    ZOOM_OUT     = 2,
    ROTATE_LEFT  = 3,
    ROTATE_RIGHT = 4,
    PREVIOUS     = 5,
    NEXT         = 6,
    EXIT         = 7,
    SLIDESHOW    = 8,
    OPEN         = 9,
    HELP         = 10
};

// Right-click context menu command IDs. DELETE_FILE rather than DELETE:
// winsvc.h #defines DELETE as the 0x00010000 access right, so a bare DELETE
// enumerator expands into a number and the enum does not parse.
enum class ContextMenuId
{
    COPY         = 2001,
    DELETE_FILE  = 2002,
    OPEN_FOLDER  = 2003,
    PROPERTIES   = 2004,
    WALLPAPER    = 2005,
    METADATA     = 2006
};

// Metadata viewer window control IDs.
enum class MetadataControlId
{
    EDIT      = 4001,
    SELECTALL = 4002
};

// Hamburger menu command IDs.
enum class MenuCommandId
{
    SHORTCUTS     = 3001,
    ABOUT         = 3002,
    ASSOCIATE     = 3003,
    HQ_FILTER     = 3004,
    FIT_TO_SCREEN = 3005
};

// Timer IDs (SetTimer/KillTimer/WM_TIMER). The underlying type is spelled out
// because these go to SetTimer/KillTimer as UINT_PTR and are compared against
// WPARAM in WM_TIMER.
enum class TimerId : UINT_PTR
{
    SLIDESHOW         = 0xcafe,
    ZOOM_DISPLAY      = 0xbebe,
    KILL_ROTATION     = 0xbeca,
    ENTER_FULL_SCREEN = 0xcaca
};

// ms between pictures
constexpr UINT SLIDESHOW_INTERVAL_MS = 6000;

// Per-frame step for both slideshow fades through black: the pre-fade that covers
// the window transition into a slideshow, and the stop fade that covers the
// overlay's rect change back out of one. UpdateEngine multiplies it by dt * 60, so
// it is a per-60Hz-frame fraction: 0.05 is about a third of a second each way.
// One value for both directions so entering and leaving cannot drift apart.
constexpr float SLIDESHOW_FADE_STEP = 0.05f;

// Custom window messages.
// WM_APP_DIRCHANGE   posted by the directory watcher thread
// WM_APP_EXITFULLSCREEN  deferred exit to avoid mid-focus-change destruction
#define WM_APP_DIRCHANGE  (WM_APP + 1)
#define WM_APP_EXITFULLSCREEN (WM_APP + 2)

// ============================================================================
// Globals - declared here, defined in Globals.cpp. The order is deliberate: it
// fixes construction/destruction order for the whole process, so keep
// Globals.cpp in the same order. Each section banner below marks one group of
// state (see the note at the top of this file).
// ============================================================================

// ---- Identity / lifetime ----
// The module handle, the strings LoadString fills in, the four windows this app
// creates (main, fullscreen overlay, black cover, metadata viewer), and the two
// flags that say whether it is going away or started from a file.
// One instance: g_app.
struct AppIdentity
{
    HINSTANCE                                          instance;            // current instance
    WCHAR                                              title[MAX_LOADSTRING];         // The title bar text
    WCHAR                                              windowClass[MAX_LOADSTRING];   // the main window class name
    HWND                                               mainWindow;
    HWND                                               overlayWindow;
    HWND                                               blackCoverWindow;
    HWND                                               metadataViewerWnd;
    bool                                               isExiting = false;
    bool                                               launchedWithFile = false;
};

extern AppIdentity                                      g_app;

// ---- Factories / devices ----
// The D2D / WIC / DWrite / D3D11 factories and devices, the device contexts, the
// swap chain and its views, plus the window the render target was created for.
// DiscardDeviceResources releases the device-dependent ones; the factories and
// the device-independent DWrite text format outlive them. One instance:
// g_devices.
struct DeviceResources
{
    ComPtr<ID2D1Factory1>                              d2dFactory;
    HWND                                               renderTargetWindow = nullptr;
    ComPtr<IWICImagingFactory>                         wicFactory;
    ComPtr<IDWriteFactory>                             dwriteFactory;
    ComPtr<IDWriteTextFormat>                          textFormat;
    ComPtr<ID3D11Device>                               d3dDevice;
    ComPtr<ID3D11DeviceContext>                        d3dContext;
    ComPtr<ID2D1Device>                                d2dDevice;
    ComPtr<ID2D1DeviceContext>                         renderTarget;
    ComPtr<IDXGISwapChain1>                            swapChain;
    ComPtr<ID2D1Bitmap1>                               d2dTargetBitmap;
    ComPtr<ID3D11RenderTargetView>                     swapRTV;           // RTV wrapping swap-chain buffer 0
};

extern DeviceResources                                  g_devices;

// ---- View state ----
// The current image's zoom / pan / rotation, each with the target the engine
// eases toward, plus the drag and fullscreen flags that travel with them and the
// per-path map that lets a file reopen the way it was left. One instance: g_view.
struct ViewState
{
    ComPtr<ID2D1Bitmap>                              bitmap;
    float                                            zoom = 1.0f;              // Current zoom level
    float                                            targetZoom = 1.0f;
    float                                            offsetX = 0.0f;           // Pan offset X
    float                                            offsetY = 0.0f;           // Pan offset Y
    float                                            targetOffsetX = 0.0f;
    float                                            targetOffsetY = 0.0f;
    bool                                             isDragging = false;
    POINT                                            lastMouse = {};
    POINT                                            mouseFromDown = {};
    bool                                             isFullscreen = false;
    // Process-wide UI scale in pixels: the smaller dimension of the monitor the app
    // is on. Set in InitInstance from the primary monitor and overwritten by
    // fullscreen stage 2 with the monitor actually being fullscreened. Read by
    // UIElement's tooltip sizing; the widgets themselves use their own frozen
    // Layout::uiPixelScale instead (see the anchor-capture row in CODE_MAP.md).
    float                                            uiPixelScale = 1.0f;
    float                                            rotationAngle = 0.0f;
    float                                            targetRotationAngle = 0.0f; // New rotation target
    float                                            rotationSpeed = 300.0f;     // degrees per second
    float                                            exifRotation = 0.0f;        // EXIF orientation for current image (degrees CW)
    std::unordered_map<std::wstring, ImageViewState> stateByPath;
    bool                                             restoredStateThisLoad = false;
    // Spring rate for the zoom / pan / film-strip easing in UpdateEngine: the
    // fraction of the remaining distance one 60 Hz frame consumes. It is
    // re-derived from the monitor's refresh rate at three sites - wWinMain
    // scales this default once per process, UI.cpp and Fullscreen.cpp assign
    // their own base for the current monitor - so changing 0.13f on its own
    // only moves the windowed start.
    float                                            smooth = 0.13f;
};

extern ViewState                                     g_view;

// ---- Backgrounds ----
// What is drawn behind the image, and the shadow effect that is drawn on top of
// it. The desktop capture and the embedded default stay device-independent (their
// WIC copies) so they survive DiscardDeviceResources; the D2D copies are rebuilt
// from those. One instance: g_background.
struct BackgroundArt
{
    ComPtr<ID2D1Bitmap>                                bitmap;            // Device-dependent background bitmap
    ComPtr<IWICBitmap>                                 wicBitmap;         // Device-independent captured background (WIC)
    ComPtr<IWICBitmapSource>                           wicDefaultBitmap;
    ComPtr<ID2D1Bitmap>                                defaultBitmap;
    ComPtr<ID2D1Effect>                                shadowEffect;
    ComPtr<ID2D1Bitmap>                                shadowSourceBitmap;  // small mip-extracted D2D bitmap used as shadow input
};

extern BackgroundArt                                    g_background;

// ---- Fullscreen lifecycle ----
// The overlay window's own entry/exit animation and the flags that guard it.
// One instance: g_fullscreen.
struct FullscreenState
{
    bool                                            needsInit = false;
    float                                           overlayAlpha = 0.0f;
    // True from the moment a keyboard navigation fires until the next frame is
    // fully rendered. Prevents auto-repeat from queuing up loads faster than
    // the render loop can display them.
    bool                                            navPendingRender = false;
    bool                                            initDone = false;
    bool                                            pendingPreserveView = false;
    // Reference count of active "don't exit fullscreen" scopes. A plain bool
    // here is fragile: any early return/break between setting it true and
    // setting it false again leaves fullscreen-exit permanently disabled.
    // Use FullscreenExitSuppressor (RAII, defined at the bottom of this file)
    // instead of touching this directly - it can't be forgotten because the
    // destructor always runs.
    // This should only be needed for UI that steals OS foreground activation
    // without going through a real owned window (see FullscreenExitSuppressor
    // comment below) - anything created as an owned window of g_app.mainWindow /
    // g_app.overlayWindow (DialogBox, MessageBox, our own CreateWindowExW popups,
    // common dialogs with hwndOwner set) is recognized automatically by
    // IsOwnedByOurWindow() in WM_ACTIVATE and needs no manual suppression at
    // all.
    int                                             suppressExitDepth = 0;

    // While pendingExit is true the overlay stays alive while exitBgFadeAlpha
    // animates 0->1, then CompleteExitFullscreen() tears it down.
    // wasInSlideshowOnExit lets Render cross-fade the blurred bg -> desktop bg.
    bool                                            pendingExit = false;
    bool                                            wasInSlideshowOnExit = false;
    float                                           exitBgFadeAlpha = 0.0f;
};

extern FullscreenState                              g_fullscreen;

// ---- Current image / file list ----
// The image on screen and the folder it came from. source is the
// device-independent source of the current image; it pairs with g_view.bitmap
// (its GPU upload) the same way ThumbnailEntry's wic/d2d pair does, and
// survives DiscardDeviceResources. files is the current file's directory
// listing and index the position within it, so prev/next, the film strip and
// the slideshow are all the same navigation. Distinct from ImageViewState
// above, which is the saved zoom/pan of a file you have already left.
// One instance: g_image.
struct ImageState
{
    ComPtr<IWICBitmapSource>                           source;            // Cached, device-independent WIC source for current image
    std::vector<std::wstring>                          files;             // every supported image in the current file's directory, sorted
    int                                                index = -1;        // position in files; -1 when no image is current
    std::wstring                                       path;              // full path of the current image
    std::wstring                                       fileName;         // bare file name of the current image
    std::wstring                                       lastLoadError;     // human-readable reason the most recent LoadImageD2D failed
    std::wstring                                       loadAttemptName;   // bare file name of the most recent LoadImageD2D attempt
    bool                                               showLoadError = false;  // true while the last load failed -> Render shows a "corrupted file" message
    int                                                width = 0;
    int                                                height = 0;
};

extern ImageState                                      g_image;

// ---- Animated-image playback state ----
// Every animated format this app supports (GIF, WebP, APNG and AVIF) is decoded
// into this same set of frame vectors and played back through the same code in
// UpdateEngine/RecreateImageBitmap - only the per-frame *decode* routine differs
// (DecodeGifCompositeFrames / DecodeWebpFrames / DecodeApngFrames /
// DecodeAvifFrames), because GIF and APNG frames must be composited against
// prior frames' disposal state, while WebP and AVIF frames each decode (or seek)
// independently. See LoadImageD2D for where the four formats' loaders meet back
// up and populate this shared state.
// One instance: g_anim.
struct AnimState
{
    // Frame 0 is decoded synchronously in LoadImageD2D so the window can open
    // immediately; every later frame is decoded/composited on decodeThread. For
    // GIF the frames cannot be parallelized at all (disposal makes them
    // inherently sequential - frame N's canvas state depends on frame N-1's
    // disposal); the only freedom is to run the whole sequential decode somewhere
    // other than the UI thread. WebP and AVIF frames are independent but are
    // still decoded on this same background thread for a uniform loading path.
    // framesReadyUpTo is the count of frames, starting from 0, that are safe to
    // display right now; it only ever grows, and only that thread writes past
    // index 0.
    std::vector<ComPtr<IWICBitmapSource>>  frames;       // CPU-side; survives device loss
    std::vector<ComPtr<ID2D1Bitmap>>       gpuFrames;    // pre-uploaded GPU bitmaps for each frame
    ULONGLONG                              lastFrameTime = 0;
    std::vector<UINT>                      frameDelays;
    UINT                                   currentFrame = 0;
    bool                                   isAnimated = false;  // GIF, WebP, APNG and AVIF alike
    std::thread                            decodeThread;
    std::atomic<bool>                      decodeStop{ false };
    std::atomic<int>                       framesReadyUpTo{ 0 };
};

extern AnimState                                         g_anim;

// ---- UI controls ----
// The widgets themselves, keyed by the control IDs above. The two menu buttons
// are created by InitializeMenuButtons at startup; the 9 in-image buttons and
// the two text boxes only exist after the first fullscreen stage 2 (see
// CODE_MAP.md section 8). Every COM object they hold comes from the DWrite
// factory rather than the render target, so DiscardDeviceResources leaves them
// alone and they survive a device loss.
extern std::unordered_map<TextBoxId, UITextBox>          g_textBoxes;
extern std::unordered_map<ButtonId, AnimatedButton>      g_buttons;

// ---- D3D11 mip-mapped image rendering pipeline ----
// The objects the image pass reuses every frame: the shaders and fixed-function
// state that draw it (built once per device), the mip chain for the current
// image (rebuilt per image), and the D2D brushes the surrounding passes reuse.
// Every D2D resource here belongs to the device context that made it, so
// DiscardDeviceResources (Rendering.cpp, the same file that creates them) resets
// all of them - the D3D11 objects along with them. One instance: g_pipeline.
struct RenderPipeline
{
    ComPtr<ID2D1SolidColorBrush>                       dimBrush;
    ComPtr<ID2D1SolidColorBrush>                       blackBrush;

    // The load-error screen's text brushes. Their colours are constants, so they
    // are created once per device context instead of once per frame.
    ComPtr<ID2D1SolidColorBrush>                       errorTitleBrush;
    ComPtr<ID2D1SolidColorBrush>                       errorDetailBrush;

    // Cached vignette gradient brushes, one per bar: slot 0 is the top bar (dark
    // at the top), slot 1 the bottom bar (dark at the bottom). A gradient brush's
    // stops are fixed when it is created, so the brush can only be reused while
    // the bar geometry and the stop alpha are unchanged - true for every idle
    // frame, not during a fade. See DrawVignetteBar.
    struct VignetteBrushCache
    {
        ComPtr<ID2D1LinearGradientBrush>               brush;
        float y0 = -1.f, y1 = -1.f, alpha = -1.f;      // -1 = nothing cached
    };
    VignetteBrushCache                                 vignetteBrush[2];

    ComPtr<ID3D11Texture2D>                            mipTex;        // Full mip chain for the current image
    ComPtr<ID3D11ShaderResourceView>                   srv;           // SRV: all mip levels
    ComPtr<ID3D11SamplerState>                         sampler;       // Trilinear / aniso sampler
    ComPtr<ID3D11VertexShader>                         vs;
    ComPtr<ID3D11PixelShader>                          ps;
    ComPtr<ID3D11InputLayout>                          il;
    ComPtr<ID3D11Buffer>                               vb;            // Dynamic quad verts (4 x 16 bytes)
    ComPtr<ID3D11Buffer>                               cb;            // Dynamic constant buffer
    ComPtr<ID3D11BlendState>                           blend;         // Pre-multiplied alpha blend
    ComPtr<ID3D11RasterizerState>                      rast;
    ComPtr<ID3D11DepthStencilState>                    ds;
    bool                                               ready = false;
};

extern RenderPipeline                                  g_pipeline;

// Rendering-path options persisted under Software\PicassoPictures and read in
// wWinMain, then toggled from the hamburger menu. Settings rather than device
// state, so they sit outside RenderPipeline, which only holds the objects that
// a device loss throws away. One instance: g_settings.
struct AppSettings
{
    bool                                               useHQFilter = false;      // when true, forces D2D HIGH_QUALITY_CUBIC instead of D3D11 trilinear
    bool                                               openFitToScreen = false;  // when true, images open scaled to fit the screen instead of 100%
};

extern AppSettings                                      g_settings;

// ---- Vignette animated visibility ----
// The two gradient bars at the top and bottom of a fullscreen overlay. Each has a
// visibility that springs toward a target, and the targets come from where the
// mouse is. One instance: g_vignette.
struct VignetteState
{
    float                                           topVisibility = 0.f;
    float                                           topTarget = 0.f;
    float                                           bottomVisibility = 0.f;
    float                                           bottomTarget = 0.f;
    // Pixel heights captured when a fullscreen overlay is created
    // (EnterFullscreen stage 2 - never on a windowed resize). They follow the
    // monitor the overlay was created on; draw code back-converts them to
    // normalized fractions for the current RT height so the bars never
    // grow/shrink with the window.
    float                                           topBarHeightPx = 0.f;
    float                                           bottomBarHeightPx = 0.f;
    // Activation zone thresholds in pixels, captured at the same fullscreen
    // stage 2 as the bar heights: how far in from the top / bottom edge of the
    // overlay the zone reaches.
    // Top zone:    show when mouseY <= topActivationPx                  (inset from top)
    // Bottom zone: show when mouseY >= windowHeight - bottomActivationPx (inset from bottom)
    float                                           topActivationPx = 0.f;
    float                                           bottomActivationPx = 0.f;
    bool                                            heightsCaptured = false;
};

extern VignetteState                                     g_vignette;

// ---- Thumbnail film strip ----
// The strip along the bottom of the window: the thumbnails themselves, the
// background loader that fills them in, and the scroll / drag state that moves
// them. One instance: g_thumb.
struct ThumbState
{
    std::vector<ThumbnailEntry>                                       entries;
    std::mutex                                                        readyMutex;
    std::queue<std::pair<int, Microsoft::WRL::ComPtr<IWICBitmap>>>    readyQueue;
    std::thread                                                       loaderThread;
    std::atomic<bool>                                                 loaderStop{ false };
    float                                                             scrollOffset           = 0.f;
    float                                                             targetOffset           = 0.f;
    float                                                             width                  = 0.f;
    float                                                             height                 = 0.f;
    float                                                             gap                    = 0.f;
    float                                                             stripInsetFromBottomPx = 0.f;  // pixels from RT bottom to strip centre (anchored like vignettes)
    bool                                                              sizeCaptured           = false;
    // Holds exactly ONE element, and DrawThumbnailStrip always uses clipGeos[0]:
    // every slot has the same shape, so each one is placed by transforming the
    // render target instead of building a geometry per slot. Created at
    // fullscreen stage 2 once the strip size has been re-captured, and cleared
    // there (and in WM_DESTROY) so it is rebuilt against the new size. It comes
    // from the D2D factory, not the device context, so it survives a device loss.
    std::vector<Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry>> clipGeos; // one shared slot shape, placed by the RT transform

    // Drag / momentum, while LMB is held over the strip.
    bool  dragging     = false;  // LMB held over strip, drag in progress
    float dragStartX   = 0.f;    // cursor X at mouse-down (click vs drag test)
    float dragLastX    = 0.f;    // cursor X at last WM_MOUSEMOVE
    float velocity     = 0.f;    // smoothed px/event velocity (EMA) for momentum
    bool  freeScroll   = false;  // true = UpdateEngine skips auto-centering
    int   dragHitIndex = -1;     // thumbnail slot under cursor at mouse-down (-1 = gap)
};

extern ThumbState                                                g_thumb;

// ---- Slideshow ----
// The slideshow as a playing/paused mode: the cross-fade from one image to the
// next, the blurred background behind each one, and the two fades through black
// that cover the window transitions into and out of it. One instance:
// g_slideshow.
struct SlideshowState
{
    bool                                             active = false;
    // Recorded at slideshow start: true when the app was already fullscreen, so
    // stopping the slideshow returns to fullscreen instead of the windowed view.
    bool                                             fromFullscreen = false;
    ComPtr<ID2D1Bitmap>                              bgBitmap;        // 25 %-size blurred bg for current image
    ComPtr<ID2D1Bitmap>                              prevBgBitmap;    // blurred bg fading out
    ComPtr<ID2D1Bitmap>                              prevBitmap;      // image bitmap fading out
    ComPtr<ID2D1Effect>                              blurEffect;      // reusable Gaussian blur effect
    float                                            transitionAlpha = 1.0f; // 0 = show prev, 1 = show new
    float                                            targetAlpha = 1.0f;
    float                                            prevZoom = 1.0f;
    float                                            prevOffsetX = 0.0f;
    float                                            prevOffsetY = 0.0f;
    float                                            prevRotation = 0.0f;
    bool                                             preFade = false;     // true from button-press until first overlay Present()
    float                                            preFadeAlpha = 0.0f; // 0=current view visible, 1=fully black

    // Stopping a slideshow that started in fullscreen fades through black while the
    // live overlay changes from the monitor rect a slideshow covers to the work area
    // plain fullscreen uses. Alpha ramps 0->1, the resize happens at full black, then
    // it ramps back to 0.
    bool                                             stopFade = false;
    bool                                             stopFadePendingResize = false;
    float                                            stopFadeAlpha = 0.0f;
};

extern SlideshowState                                          g_slideshow;

// ============================================================================
// Fullscreen exit suppression - shared contract, used by WndProc (WM_ACTIVATE,
// WM_RBUTTONUP), the hamburger menu (UI) and the dialogs.
// ============================================================================
//
// WM_ACTIVATE(WA_INACTIVE) fires on g_app.mainWindow/g_app.overlayWindow whenever
// OS foreground activation moves away from them - both for a genuine
// focus loss (Alt+Tab, clicking another app) and for the harmless case of
// one of our *own* windows (About box, metadata viewer, a common dialog)
// taking focus. We only want to exit fullscreen for the former.
//
// WM_ACTIVATE's lParam gives us the HWND that is becoming active, so we
// can tell the two cases apart structurally: walk that window's owner
// chain and see if it leads back to one of our own top-level windows.
// Any window we create as an *owned* window (DialogBox's parent hWnd,
// MessageBox's hWnd, CreateWindowExW's hWndParent, a common dialog's
// hwndOwner) is caught by this automatically - no per-call-site flag to
// remember. That means new dialogs added later are safe by default,
// instead of silently missing suppression until someone notices.
//
// NOTE: ContextMenuId::PROPERTIES opens ShowImageProperties(), an in-app DialogBox
// (IDD_PROPERTIES, the same style of bespoke dialog template as "Keyboard
// shortcuts") rather than invoking the shell's external "Properties" verb.
// A shell window is not one of our owned windows and would need a manual
// carve-out here; the in-app dialog is covered automatically by
// IsOwnedByOurWindow() like everything else - no special case needed.
//
// A manual suppression scope (FullscreenExitSuppressor) is only needed
// for UI that steals foreground activation *without* being an owned
// window of ours - e.g. temporarily forcing foreground onto our own
// window before TrackPopupMenu. Use it as an RAII guard, never as a
// raw true/false pair: the destructor always runs, so it can't be left
// stuck on by an early return/break the way a raw true/false pair can.
inline bool IsOwnedByOurWindow(HWND hwnd)
{
    HWND w = hwnd;
    for (int i = 0; w != nullptr && i < 8; ++i)   // bounded: owner chains are shallow
    {
        if (w == g_app.mainWindow || w == g_app.overlayWindow)
            return true;
        w = GetWindow(w, GW_OWNER);
    }
    return false;
}

struct FullscreenExitSuppressor
{
    FullscreenExitSuppressor()  { ++g_fullscreen.suppressExitDepth; }
    ~FullscreenExitSuppressor() { --g_fullscreen.suppressExitDepth; }
    FullscreenExitSuppressor(const FullscreenExitSuppressor&) = delete;
    FullscreenExitSuppressor& operator=(const FullscreenExitSuppressor&) = delete;
};
