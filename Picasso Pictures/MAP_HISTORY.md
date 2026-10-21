# CODE_MAP — `Picasso Pictures.cpp`

**ARCHIVE - do not read at session start.** This is CODE_MAP.md exactly as it stood before the
2026-10-23 restructure, kept so the provenance of the module split (which file moved when, what
linkage / default-argument fixes each move needed, what `verify_move` reported, and the one-off
scripts from that work) stays searchable. None of it is needed to change the code; CODE_MAP.md is
the working map. The same text is also in git history. Everything below is verbatim.

---

**Split complete (steps 0-8) and nothing further is being split.** `Picasso Pictures.cpp`
is now **2,032 lines / ~78 kB / 13 top-level definitions** (it started at 9,216 lines /
355,563 B), and everything still in it is app plumbing:
`GetInitialFileFromCommandLine`, `wWinMain`, `MyRegisterClass`, `EnableDarkTitleBar`, `InitInstance`,
`UpdateEngine`, `IsSupportedImage`, the four navigation functions (`OpenImageFile`, `OpenNextImage`,
`OpenPrevImage`, `DeleteCurrentImage`) and `WndProc`. The eight modules are `Globals.h`+`Globals.cpp`,
`ViewMath.h/.cpp`, `Fullscreen.h/.cpp`, `Rendering.h/.cpp`, `Decoding.h/.cpp`, `Thumbnails.h/.cpp`,
`UI.h/.cpp`, `Metadata.h/.cpp` and `Dialogs.h/.cpp`. What is left is the two accepted bugs in
section 14: the primary-monitor sizing on a multi-monitor desktop, and the slideshow stopped inside the
stage-1 gap. A full read of the
main file costs ~50k tokens. **Read this file instead**, then read only the function you need — and
for anything mid-file use `split_helper.ps1 cat`/`catfn` through the shell, never `read_text_file`
(it has no offset).

Section 14 is the summary of what is known to be wrong, and section 15 holds the
working rules and the local-tool list a new session needs before touching anything.

> **Comment rule (standing):** a comment describes **what the code does, how it works,
> or what it should do** - never *which refactor step moved which file*. The sources are
> clean of that now and pure ASCII, so keep it
> that way: no split banners, no "moved verbatim", no step numbers, no non-ASCII byte in
> a `.cpp`/`.h`. The history belongs here and in the git log, not in the code.

> Line numbers are omitted on purpose — they drift on the first edit. Every entry has a
> **unique grep anchor**. `_grep.ps1 -Pat "anchor"` (or `findstr /n`) gives you the real line
> right now, and works across all the module files. (`split_helper.ps1`, `verify_move.ps1` and the
> `_*.ps1` helpers are **local-only** - `*.ps1` is gitignored, so a fresh clone will not have them.)

---

## Layout at a glance

| Region | What's there |
|---|---|
| `Globals.h` / `Globals.cpp` | **moved out** — the ~100 globals + `ImageViewState` + `ThumbnailEntry` + `FullscreenExitSuppressor` + `IsOwnedByOurWindow` + `MAX_LOADSTRING` + control/command/timer IDs + `WM_APP_*`. `extern` decls in the header, one definition site in the `.cpp`. Include `Globals.h` and you get the whole set. |
| `ViewMath.h/.cpp` | **moved out** — `GetClientSizeF` (private), `PanFromOffsets`, `OffsetsFromPan`, `GetMonitorRefreshRate`, `IsPointOverImage`. No calls into any other module; the hit test
is the one function here that reads the view-state globals. |
| `Fullscreen.h/.cpp` | **moved out** — overlay + black-cover classes/windows, `CreateOverlayWindow`, `CaptureDesktop`, `Enter/ExitSlideshowMode`, `EnterFullscreen`, `CompleteExitFullscreen`, `ExitFullscreen`, `InitFullScreenExit`. |
| `Rendering.h/.cpp` | **moved out** — all device-resource creation/destruction, the D3D11 mip pipeline incl. the embedded HLSL, `RecreateImageBitmap`, `CreateSlideshowBgBitmap`, `DrawVignetteBar`/`DrawCoverBg`, and `Render`. |
| `Decoding.h/.cpp` | **moved out** — `LoadImageD2D`, `FinishImageLoad`, `LoadFail`, the AVIF / JXL / GIF / WebP / APNG decoders, the shared compositing primitives (in an anonymous namespace), the animated-playback glue and the directory-agnostic frame plumbing. |
| `Thumbnails.h/.cpp` | **moved out** — `BuildImageList` (the Explorer-identical file list), `Start/StopThumbnailLoader`, `DrawThumbnailStrip`, `Start/StopDirectoryWatcher`. `IsSupportedImage` stayed in the main file. |
| `UI.h/.cpp` | **moved out** — the three widget initialisers (menu buttons, the 9 in-image buttons, the two text boxes), `InitializeImageLayout`, the zoom/fit commands (`SetZoomCentered`, `FitToWindowRelative`, `UpdateTargetZoom`, `ZoomIntoImage`, `MakeZoomVisible`). The Open / prev / next / delete navigation functions stayed in the main file. |
| `Metadata.h/.cpp` | **moved out (step 7)** - the EXIF / PNG-tEXt / WebP-XMP / JXL-box / AVIF-XMP+Exif readers, the mini-XML helpers, the size/date/format formatters, `ShowImageMetadata`, the metadata viewer window (`MetadataViewerWndProc` + `RegisterMetadataViewerClass` + `ShowMetadataViewerWindow`) and the in-app Properties dialog (`ImagePropertiesFields` + `PropertiesDlgProc` + `ShowImageProperties`). Most of its functions are `static` and stay out of the header, as do both POD structs. |
| `Dialogs.h/.cpp` | **moved out (step 8 - the last module)** - `AssociateFileTypes` (HKCU file-association writer: `TaskDialogIndirect` confirmation, ProgID + `shell\open\command` + `Capabilities` + per-extension values + `SHChangeNotify`) and `About`, the single dialog proc behind BOTH `IDD_ABOUTBOX` and `IDD_COMMANDBOX` ("Keyboard shortcuts"), including `IDC_SYSLINK_GITHUB` -> `ShellExecuteW`. Neither was ever `static`; the header is exactly these two declarations. |
| main `head` | includes (now also `Rendering.h` + `Decoding.h` + `Thumbnails.h` + `UI.h` + `Metadata.h` + `Dialogs.h`), `#pragma comment` libs, vcpkg AVIF/JXL headers (now only needed by `Decoding.cpp`) |
| main fwd decls | 13 prototypes left (the Globals / ViewMath / Fullscreen / Rendering / Decoding / Thumbnails / UI / Metadata / Dialogs ones are gone; two of the 13 are the pre-existing duplicate `GetInitialFileFromCommandLine`) |
| `wWinMain` | single-instance, DPI, registry, factories, main loop |
| engine | `UpdateEngine` (all animation/interpolation) — `Render` moved to `Rendering.cpp` |
| loaders | **moved out** — see `Decoding.h/.cpp` below; only `IsSupportedImage` stayed behind (`ImageFormatNameFromExtension`, `Latin1BytesToWide` and `Utf8BytesToWide` went to `Metadata.cpp` with step 7) (`BuildImageList` went to `Thumbnails.cpp` in step 5) |
| metadata | **moved out** - see `Metadata.h/.cpp` (section 9) |
| thumbnails | **moved out** — see `Thumbnails.h/.cpp` (`StartThumbnailLoader`, `DrawThumbnailStrip`, `BuildImageList`, directory watcher) |
| UI | **moved out** — see `UI.h/.cpp` (buttons, text boxes, `InitializeImageLayout`, zoom commands) |
| `WndProc` | ~20 message handlers, ~15 commands |
| tail | `WndProc` is the last function in the file now - `About` + `AssociateFileTypes` left in step 8 (see section 10) |

### Module include graph (as of step 8; `Metadata.h` and `Dialogs.h` are leaves and are not drawn - they include only `Globals.h`, plus `Decoding.h` for Metadata)

```
Globals.h  ──┬── ViewMath.h
             ├── Fullscreen.h ──┐
             ├── Rendering.h ──┼── each .cpp
             ├── Decoding.h ───┤
             ├── Thumbnails.h ─┤
             └── UI.h ─────────┘
```
Headers are declarations-only, so they may be included in any order and cycles are impossible.
`Fullscreen.cpp` calls into Rendering (`DiscardDeviceResources`, `CreateRenderTarget`,
`CreateBackgroundBitmap`, `RecreateImageBitmap`) and `Rendering.cpp` calls back into Fullscreen
(`DestroyBlackCoverWindow`) — the edges are genuinely bidirectional at module level, which is only
safe because the headers declare rather than define. `Decoding.cpp` calls Rendering
(`RecreateImageBitmap`) and ViewMath (`PanFromOffsets` / `OffsetsFromPan`); `Metadata.cpp`'s AVIF
readers call back into Decoding (`AvifOpenAndParse` only), which is why the three decode internals
`AvifOpenAndParse`, `DecodeAvifToWicBitmap` and `DecodeJxlToWicBitmap` stayed exported (the other two
serve `Thumbnails.cpp`'s AVIF/JXL thumbnails).
`Thumbnails.cpp` also calls back into Decoding (`DecodeAvifToWicBitmap` / `DecodeJxlToWicBitmap` —
AVIF/JXL thumbnails have no WIC path) and is called by `Render` (`DrawThumbnailStrip`), which is why
`Rendering.cpp` includes `Thumbnails.h`. `UI.cpp` is the module with the **most edges out** (it sits
at the top of the graph): it calls `GetMonitorRefreshRate` (ViewMath), `EnterSlideshowMode`
(Fullscreen) and the three navigation functions still in the main file; and it is called back by
`Render` (`InitializeImageLayout`), by `Fullscreen.cpp` (`InitializeButtons`,
`InitializeImageInfoLabel`, `UpdateTargetZoom`) and by `Decoding.cpp` (`UpdateTargetZoom`) — all three
of those files now `#include "UI.h"`.
Four `.cpp` files carry a small **permanent bridge block** of declarations for functions that stay in
the main file forever: `Fullscreen.cpp` -> `WndProc` (the one declaration no module will ever own);
`Thumbnails.cpp` -> `IsSupportedImage`; `UI.cpp` -> `OpenImageFile` / `OpenNextImage` /
`OpenPrevImage`; `Metadata.cpp` -> `EnableDarkTitleBar`. Those are the **only** bridge lines left in
the tree now that the split is over: `Dialogs.cpp` needed none at all (analyze_deps reported no
imports and the build agreed), and `ViewMath.cpp` / `Rendering.cpp` / `Decoding.cpp` hold only their
own file-local declarations. Any bridge line beyond those four is a bug - it means a module moved and
its declaration was left behind, which is how duplicate declarations creep in.

---

## 1. Bootstrap

| Anchor | Function | Notes |
|---|---|---|
| `GetInitialFileFromCommandLine` | static | `CommandLineToArgvW`, returns `argv[1]` |
| `Local\\PicassoPictures_SingleInstance_v1` | `wWinMain` | named mutex → `FindWindowW` → `WM_COPYDATA` (`dwData==1`, wide path) → `AttachThreadInput` + `SetForegroundWindow` → `return 0` |
| `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` | `wWinMain` | set before any window creation |
| `Software\\PicassoPictures` + `HQFilter` / `FitToScreen` | `wWinMain` | restores `g_settings.useHQFilter`, `g_settings.openFitToScreen` |
| `g_view.smooth = g_view.smooth * 60.0f / (float)refreshRate` | `wWinMain` | frame-rate-independent smoothing. **Note:** also re-set absolutely in `Fullscreen.cpp` (`2*0.18f * 60/rate`) and `UI.cpp` (`0.18f * 60/rate`) — three sites, different bases, and this one multiplies rather than assigns, so it must stay a once-per-process line. `SmoothAlpha` (below) is the newer, dt-based way to express the same idea |
| **Timing convention: `SmoothAlpha(perFrameAt60Hz, dt)`** (`ViewMath.h/.cpp`) | used by `UIElement::UpdateVisibility`, `AnimatedButton::Update`, and the vignette fade in `UpdateEngine` | Every smoothing factor in the UI is written as "the fraction one 60 Hz frame consumes" and rescaled by the real frame time, so fades and the tooltip dwell take the same wall-clock time at any refresh rate (same convention `SLIDESHOW_FADE_STEP` already used). `UpdateVisibility(dt, falloff)` deliberately has **no default argument** - a caller that forgets `dt` must fail to compile. The old code added a hardcoded `0.016f` per call, which assumed 60 Hz: measured, the fade to 95% took 0.600 / 0.300 / 0.250 s at 60 / 120 / 144 Hz and the tooltip dwell 1.04 / 0.52 / 0.43 s; now 0.600 / 0.617 / 0.618 s and 1.0 s everywhere. Matters more than it looks: the main loop has no sleep or timer, so `UpdateEngine`'s call rate is only paced by `Present(1,0)` blocking on vsync inside `WM_PAINT` - with no paint pending the loop free-runs and per-frame timing drifts arbitrarily |
| `DWRITE_FONT_WEIGHT_SEMI_BOLD` + `0.023f` | `wWinMain` | text format sized from 2.3% of screen height |
| `while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))` | main loop | then `UpdateEngine(dt)`, `dt` clamped to 0.03s; `goto shutdown` on `WM_QUIT` |
| `MyRegisterClass` / `RegisterOverlayClass` / `RegisterBlackCoverClass` | 3 window classes | main = `g_app.windowClass`, overlay = `L"OverlayWindowClass"`, cover = `L"BlackCoverClass"` (plain `DefWindowProcW` + `BLACK_BRUSH`). **The overlay class's `lpfnWndProc` is the same `WndProc` as the main window's** (`Fullscreen.cpp`), which is why `hWnd` is the overlay while fullscreen and why a timer set from a message handler (`KILL_ROTATION`) still reaches the `WM_TIMER` branch. `MyRegisterClass` sets `lpszMenuName` but `wWinMain` immediately calls `SetMenu(g_app.mainWindow, nullptr)`, so the `.rc` menu is never shown |
| `InitInstance` | creates main window | height = 50% of screen, width from 1536/1024 aspect; centred; `DragAcceptFiles`; `SW_HIDE` if launched with a file |
| `EnableDarkTitleBar` | dark mode | `DWMWA_USE_IMMERSIVE_DARK_MODE` + **undocumented uxtheme ordinals 135/133/136** loaded via `MAKEINTRESOURCEA` |

## 2. View-state math

| Anchor | Function |
|---|---|
| `GetClientSizeF` | client rect → floats |
| `PanFromOffsets` | offsets → pan (image centre minus window centre); `pan==0` ⇒ centred at any window size |
| `OffsetsFromPan` | inverse; used when the render target changes size |
| `IsPointOverImage` | the image hit test: undoes Render's rotate-about-centre, then tests the unrotated rect. Called by `WM_LBUTTONUP`, `WM_LBUTTONDBLCLK` and `ZoomIntoImage` |
| `GetMonitorRefreshRate` | `EnumDisplaySettings`, falls back to 60 |

## 3. Fullscreen & slideshow lifecycle

| Anchor | Function | Notes |
|---|---|---|
| `IsOwnedByOurWindow` | walks `GW_OWNER` chain, depth ≤ 8 | now `inline` in **`Globals.h`**; distinguishes *our* dialogs from real focus loss in `WM_ACTIVATE` |
| `struct FullscreenExitSuppressor` | RAII, `++/--g_fullscreen.suppressExitDepth` | now in **`Globals.h`**; **never** touch the counter directly |
| `TimerId::ENTER_FULL_SCREEN` | `EnterFullscreen(preserveView, needsDelay)` | **2-stage**: stage 1 makes main window `WS_EX_LAYERED` alpha 0 + 150 ms timer; stage 2 captures desktop, `DiscardDeviceResources`, creates overlay, re-uploads everything, and re-captures the vignette + thumbnail-strip anchors from the new overlay's render target (clearing `g_thumb.clipGeos` so the clip geometry is rebuilt at the new size) - stage 2 is the only place those anchors are set. **What can reach the 150 ms gap:** `F` (a second press sees `isFullscreen` already true and starts the exit) and a click outside the image - the main window is transparent but still takes input. `WM_ACTIVATE` cannot, because it requires `g_fullscreen.initDone`, which `CompleteExitFullscreen` clears. Either way `CompleteExitFullscreen` now kills the timer and the `WM_TIMER` branch re-checks the flags, so stage 2 no longer runs after an exit has begun |
| `g_fullscreen.pendingExit` | `ExitFullscreen(bool immediate)` | starts fade; `UpdateEngine` drives `g_fullscreen.exitBgFadeAlpha` 0→1 |
| `CompleteExitFullscreen` | inner teardown | **cancels a pending fullscreen stage-1 timer first** (nothing else did - an exit inside the 150 ms gap was followed by stage 2 doing a full device teardown + rebuild and flashing an overlay that this function then destroyed; measured with `_fullscreen_probe.ps1`: stray overlay present mid-sequence before the fix, absent after). Then recomputes the main-window rect from the *visible* image rect; min 800×600; clamps to work area; **correctly swaps W/H when rotated** - the reference for a rotation-aware hit test (see `IsPointOverImage`). Runs in full even with no bitmap (failed load) or no overlay (stage 1): both fall back to the **size and centring `InitInstance` opens with** (half the screen height at 1536:1024, centred on the monitor), because the main window is `WS_EX_LAYERED` at alpha 0 by then and an early return leaves the app running but invisible (this one was the "the window banishes" symptom; the minimum-size fallback that replaced it first landed the window in the top-left corner). The work-area clamp happens **once**, with `preClampWinX/Y` captured before it, so `clampShiftX/Y` are the real distance the window was pushed and the image offsets are corrected by that amount - the second, post-`SetWindowPos` clamp that used to follow it could only ever measure 0, which dropped the correction and left the image a frame-width off its fullscreen screen position (measured: `shiftX=-15` vs `0` on a right-panned image at 4K) |
| `InitFullScreenExit` | shrink-to-0.0005 + `g_app.isExiting = true` | quits the **whole program**, not just fullscreen. Callers: `Esc` while fullscreen, the exit button (which calls this instead of repeating the shrink), and deleting the last file in the folder. With no bitmap it only sets the flag |
| `CaptureDesktop` | `BitBlt` of `rcWork` → `g_background.wicBitmap` | skipped when entering slideshow (cover window is up) |
| `CreateBlackCoverWindow` + `DwmFlush()` | hides D3D device recreation | `DwmFlush` is load-bearing: without it the cover isn't on screen before the target dies |
| `g_slideshow.preFade` | `EnterSlideshowMode` | only sets the flag; the state machine in `UpdateEngine` does the rest. The ▶ button's lambda and `F5` both toggle between `EnterSlideshowMode` and `ExitSlideshowMode`; `g_slideshow.fromFullscreen` (captured on entry) decides whether stopping starts the stop fade or runs the fullscreen exit fade |
| `g_slideshow.stopFade`/`stopFadeAlpha` + `ResizeOverlayToWorkArea` | stop fade | mirror of the pre-fade: black in, the live overlay moves from the monitor rect to the work area at full black, black out. `ExitSlideshowMode` only starts it; `UpdateEngine` drives it and calls the resize |
| `CreateSlideshowBgBitmap` | mip-extract (~200 px) → `CLSID_D2D1GaussianBlur` | swaps dims for 90/270 EXIF rotation; CPU WIC fallback if no mip chain. **Lives in `Rendering.cpp` now** (it is a mip-extract + effect, not lifecycle) — declared in `Rendering.h`, called only by `Render`. |
| `RestartSlideshowTimer` | `Fullscreen.cpp` | re-arms `TimerId::SLIDESHOW` for a full interval; no-op unless `g_slideshow.active`. Called by every path that changes image during a slideshow: prev/next lambdas, `WM_KEYDOWN`, and the thumbnail-click branch of `WM_LBUTTONUP`. Not used by slideshow entry - the comment above the unguarded `SetTimer` in `UpdateEngine`'s phase-2 block says why |
| `g_fullscreen.wasInSlideshowOnExit` | exit cross-fade | blurred bg → desktop bg |

## 4. D3D11 / D2D rendering

**All of section 4 now lives in `Rendering.h` (declarations) + `Rendering.cpp` (definitions).**
Only `CreateRenderTarget`'s caller `WM_SIZE`, and the `Render` caller `WM_PAINT`, remain in
`WndProc`. `SwapChainBitmapProps` lost `static` because `WM_SIZE` calls it; `Mat4Mul`,
`BuildImageToClip`, `ExtractMipAsD2DBitmap`, `RenderImageD3D11`,
`CreateSmallShadowSourceViaWic`, `DrawVignetteBar` and `DrawCoverBg` are all `static` in
`Rendering.cpp` and are **not** declared in the header. The embedded HLSL and the
`ImageVertex` / `CBImageTransform` PODs are `Rendering.cpp`-private too.

| Anchor | Function | Notes |
|---|---|---|
| `D3D11_CREATE_DEVICE_BGRA_SUPPORT` | `CreateRenderTarget` | device → DXGI → `ID2D1Device` → device context → flip-model swap chain (`FLIP_SEQUENTIAL`, 2 buffers) → target bitmap → shadow/blur effects → cached brushes → swap-chain RTV |
| `SwapChainBitmapProps` | `TARGET \| CANNOT_DRAW`, `B8G8R8A8`, premultiplied | shared by create + resize |
| `g_imageVSHLSL` / `g_imagePSHLSL` | embedded HLSL, `vs_4_0`/`ps_4_0` | compiled at runtime with `D3DCompile` |
| `struct alignas(16) CBImageTransform` | row-major 4×4 + opacity + pad | |
| `Mat4Mul` / `BuildImageToClip` | image-space → NDC | S(scale/translate) → R(rotation about pivot) → N(screen→NDC) |
| `CreateD3DImagePipeline` | device-once objects | dynamic VB (4 verts) + CB, trilinear sampler, premultiplied blend, no cull, no depth |
| `D3D11_RESOURCE_MISC_GENERATE_MIPS` | `CreateMipTextureFromSource` | clamps to 16384, WIC HighQualityCubic downscale, upload mip 0, `GenerateMips` on GPU |
| `ExtractMipAsD2DBitmap` | staging readback of one mip | used for slideshow bg (200 px) and shadow source (256 px) |
| `RenderImageD3D11` | `TRIANGLESTRIP`, `Draw(4,0)` | must run **between** D2D `EndDraw`/`BeginDraw`; unbinds SRV+RTV afterwards |
| `CreateSmallShadowSourceViaWic` | CPU downscale for animated formats | animated images skip the mip chain on purpose |
| `RecreateImageBitmap` | WIC → `ID2D1Bitmap` (+ mip chain if not animated) | 16384 clamp; sets `g_background.shadowSourceBitmap` |
| `DiscardDeviceResources` | resets every GPU object | **does not** touch `g_wic*` sources — that's how the desktop capture survives fullscreen↔slideshow. Also resets every cached brush in `g_pipeline` (`dimBrush`, `blackBrush`, the two error-text brushes, and both vignette gradient brushes **with their cache keys**) so nothing can outlive the context that made it |
| **Cached D2D objects in `g_pipeline`** (`RenderPipeline`) | `dimBrush`, `blackBrush`, `errorTitleBrush`/`errorDetailBrush`, `vignetteBrush[2]` | All created lazily by the current device context and reset by `DiscardDeviceResources` — the same file creates and destroys them, so there is no cross-module lifetime rule. A gradient brush's stops are fixed at creation, so `DrawVignetteBar` can't just call `SetColor`: it keys the cache on `(y0, y1, alpha)` and rebuilds when any of them changes — every frame during a fade, no frames while a vignette sits idle. The error screen's two `IDWriteTextFormat`s are cached as file-statics in `Rendering.cpp` instead, because DWrite objects are device-independent. **Their font size is derived from `g_view.uiPixelScale`, never from the live render-target size** — that scale is set in `InitInstance` from the primary monitor before anything draws and overwritten at fullscreen stage 2, so a small windowed window still gets readable text and a windowed `WM_SIZE` can never change it (same source the tooltip text uses) |
| **`DrawUiOverlay(rtSize)`** (`Rendering.cpp`) | vignette bars, zoom readout, film strip, buttons, text boxes | One function, called from both the three-pass path (pass 3) and the single-pass path. Those two blocks used to be duplicated verbatim and had already drifted (a commented-out `if (g_view.isFullscreen)` in one copy), and each carried its own `s_lastDisplayedZoom` static — there is now a single static, so switching render paths (e.g. toggling the HQ filter) no longer forces a redundant `SetText` |
| `Render` | the big one | see below |
| `DrawVignetteBar` / `DrawCoverBg` | gradient bar / cover-stretch blit | `DrawVignetteBar` caches its gradient brush per bar in `g_pipeline.vignetteBrush[]`, keyed on `(y0, y1, alpha)`; `DrawCoverBg` allocates nothing |

### `Render` control flow
1. Bail if `hWnd != activeWindow` (protects the hidden window from stray `WM_PAINT`).
2. Lazy re-creates: image bitmap, GIF frame 0 after device loss, default bg, desktop bg, slideshow bg, **one** thumbnail per frame spiralling out from current.
3. `BeginDraw` → clear (fullscreen `0,0,0,0.6`; windowed `0.4` grey) → `g_fullscreen.needsInit` → `InitializeImageLayout`.
4. Background: slideshow blurred pair / desktop capture + dim / leftover blurred bg. During the slideshow stop fade's black-in phase the blurred wash is held (`holdBlurred`), so the swap to the desktop capture lands at full black together with the overlay resize.
5. Image: prev-frame cross-fade, then transform `scale*translate` then `rotation` about the transformed centre; shadow via `CLSID_D2D1Shadow` on the **small** bitmap with an extra scale; `useD3D11Image = g_pipeline.ready && g_pipeline.srv && !g_settings.useHQFilter`.
6. Load-error screen ("This file is corrupted…") using `g_image.lastLoadError` + `g_image.loadAttemptName`.
7. **Three passes** when D3D11 is used: D2D(bg+shadow+prefade)→`EndDraw` → D3D11 quad → D2D(`DrawUiOverlay`: vignettes, zoom text, thumbs, buttons, textboxes)→`EndDraw`→`Present(1,0)`. **Single pass** otherwise, deliberately, to avoid splitting a frame with no D3D11 work between them. Both paths call the same `DrawUiOverlay`.
8. `g_fullscreen.navPendingRender = false` after a successful `Present` — this is the keyboard-auto-repeat throttle.
9. `D2DERR_RECREATE_TARGET` → `DiscardDeviceResources()`. The `g_devices.renderTarget->SetTransform(...)` at the
   tail of `Render` is guarded by `if (g_devices.renderTarget)`: that discard is what releases the target, and
   the next `WM_PAINT` recreates it before anything draws.

## 5. Update engine (per-frame)

| Anchor | What it drives |
|---|---|
| `g_anim.framesReadyUpTo.load()` | animated GIF/WebP/APNG/AVIF frame advance; won't pass the last decoded frame; swaps pre-uploaded `g_anim.gpuFrames[i]` or uploads lazily on first reach |
| `g_view.rotationSpeed * dt` | constant-rate rotation (300°/s), not spring |
| `g_fullscreen.overlayAlpha` | enter/exit dim, `* 0.15f * dt * 60` |
| `g_slideshow.preFadeAlpha += SLIDESHOW_FADE_STEP * dt * 60.0f` | phase 1 fade-to-black → phase 2 cover window + immediate exit + new overlay + slideshow timer |
| `g_slideshow.stopFadeAlpha +=/-= SLIDESHOW_FADE_STEP * dt * 60.0f` | slideshow → fullscreen stop fade: up to 1, `ResizeOverlayToWorkArea()` at full black, back to 0. If `g_app.isExiting` or `g_fullscreen.pendingExit` is already set the pending resize is dropped and the black just fades out |
| `g_fullscreen.exitBgFadeAlpha += 0.04f * dt * 60.0f` | fullscreen-exit fade → `CompleteExitFullscreen()` |
| `g_slideshow.transitionAlpha += 0.02f * dt * 60.0f` then `+= (1 - a) * (1 - powf(0.995f, dt * 60.0f))` | image↔image cross-fade, linear + exponential tail |
| `g_thumb.readyQueue` | drains **one** thumbnail upload per frame under `g_thumb.readyMutex` |
| `for (auto& [id, btn] : g_buttons) btn.Update(dt)` | press / hold-repeat animation, then `txt.UpdateVisibility(dt, 0.08f)` for the text boxes |
| `g_thumb.targetOffset = -(g_image.index * (g_thumb.width + g_thumb.gap))` | auto-centring, skipped while dragging/free-scroll |
| `g_view.zoom += (g_view.targetZoom - g_view.zoom) * g_view.smooth` | the spring for zoom/pan |
| `SmoothAlpha(0.08f, dt)` | vignette visibility spring - the same 0.08-per-60Hz-frame falloff the text boxes get from `UpdateVisibility(dt, 0.08f)` |
| `if (g_app.isExiting && ... g_fullscreen.overlayAlpha < 0.01f)` | **destroys the main window** so `WM_DESTROY` runs the teardown (it also destroys the overlay and posts `WM_QUIT`); falls back to destroying the overlay, or a bare `PostQuitMessage`, only when the main window is already gone. Quitting without destroying the window leaves the background threads joinable at static destruction, which is the `0xc0000409` exit crash |

## 6. Image loading & decoding

**All of section 6 now lives in `Decoding.h` + `Decoding.cpp`** (step 4), with one exception: the
helpers the *metadata* code uses (`ImageFormatNameFromExtension`, `Latin1BytesToWide`,
`Utf8BytesToWide`) moved to `Metadata.cpp` in step 7 (nothing in the loader region calls them), and
`IsSupportedImage` stayed too. `Decoding.h` exports only `LoadImageD2D`, `StopAnimDecodeThread`,
`AvifOpenAndParse`, `DecodeAvifToWicBitmap` and `DecodeJxlToWicBitmap`; every other entry below is
file-local to `Decoding.cpp` (`static`, or inside the anonymous namespace the compositing
primitives live in).

| Anchor | Function / format |
|---|---|
| `LoadFail` | every failure funnels here → `g_image.lastLoadError` (+ HRESULT suffix) + `g_image.showLoadError` |
| `SniffIsobmffBrands` | reads `ftyp`, names the major brand; flags `avis` (sequence) vs `avif` |
| `AvifOpenAndParse` | shared AVIF entry: path→UTF-8→`avifDecoderSetIOFile`→`avifDecoderParse` |
| `ConvertAvifImageToWicBitmap` | `avifImageYUVToRGB` → BGRA8 → WIC PBGRA → cached |
| `DecodeAvifToWicBitmap` / `AvifPeekFrameCount` / `DecodeAvifFrames` | still / frame count / animated (`avifDecoderNthImage` can **seek**, so continuation starts at frame 1) |
| `JxlThreadParallelRunnerCreate` | `DecodeJxlToWicBitmap` — whole file in memory, BASIC_INFO→FULL_IMAGE loop, **first frame only** |
| `ReadUIntMeta` / `CloneBitmap` / `ClearRect` / `DecodeToPBGRA` / `BlendOntoCanvas` | **shared** compositing primitives (hoisted out of per-format lambdas on purpose — see the long comment; `BlendOntoCanvas(blendOver=false)` = APNG `BLEND_OP_SOURCE`) |
| `DecodeGifCompositeFrames` | manual disposal replay (2 = clear rect, 3 = restore clone); **inherently sequential** |
| `DecodeWebpFrames` | WIC composites internally → frames independent; tries 6 metadata paths for ANMF duration |
| `ApngCrc32` / `PeekApngFrameCount` / `DecodeApngFrames` | walks `acTL`/`fcTL`/`IDAT`/`fdAT`, repackages each frame into a **synthetic single-frame PNG** so WIC does the inflate/unfilter; sequence number deliberately *not* used for indexing (comment explains why). `PeekApngFrameCount` seeks over chunk payloads instead of reading them, and `LoadImageD2D` calls it once per load (the count doubles as the branch condition) |
| `BeginAnimatedPlayback` / `OpenThreadLocalWicFactory` / `OpenThreadLocalWicDecoder` / `SpawnAnimDecodeThread` | shared glue; each background thread makes its **own** WIC factory (apartment safety) and `CoInitializeEx(COINIT_MULTITHREADED)` |
| `LoadImageD2D` | dispatch order: **AVIF → JXL → WebP → GIF → APNG → static+EXIF**; saves prior view state into `g_view.stateByPath[path]` first |
| `EXIF Orientation tag (tag 274)` | 3→180°, 6→90°, 8→270°; 5–8 rotation-only (no flip support in the pipeline) |
| `Enforce premultiplied invariant` | zeroes RGB where A==0 (kills speckle) |
| `FinishImageLoad` | upload, restore saved pan/zoom/rotation, filename+size label (uses **full path** for `file_size` — Explorer search results break on bare names) |
| `StrCmpLogicalW` | `BuildImageList` — Explorer-identical natural sort; snapshots thumbs **by path** before index shift. **Now in `Thumbnails.cpp`**, which therefore needs `<shlwapi.h>` + `Shlwapi.lib`. The folder walk uses the `error_code` overloads of `directory_iterator`/`is_regular_file`, so a folder that is gone or unreadable yields an **empty list** instead of throwing a `filesystem_error` into an unhandled stack. `g_image.files`/`g_thumb.entries` are rebuilt in **two** places: `BuildImageList` and `DeleteCurrentImage`, which stops the loader, re-aligns the entries and restarts it on its own |

## 7. Thumbnails & directory watching

**Section 7 now lives in `Thumbnails.h` (declarations) + `Thumbnails.cpp` (definitions)** — step 5.
All six functions kept the external linkage they already had (none was `static`, none carried a
default argument), so this is the one module that needed **zero** post-move edits — `verify_move`
reports 6/6 `IDENTICAL` with no diffs. `IsSupportedImage` stayed in the main file with the
navigation code; `Thumbnails.cpp` declares it in its bridge block. The `g_thumb` state is still
in `Globals.h` because `UpdateEngine` (main file) drains `g_thumb.readyQueue` — shared state, not a
function edge. The watcher's handle, thread and stop event are the opposite case: no TU outside
`Thumbnails.cpp` reads them, so they are `static` there and the shutdown order lives with them.

| Anchor | Notes |
|---|---|
| `StartThumbnailLoader` | spiral order from current index; fast path = embedded JPEG thumbnail if short side ≥ 180; slow path = full decode + HighQualityCubic to `TARGET_H = 180`; EXIF applied via `IWICBitmapFlipRotator` on the small bitmap. The thread's own WIC factory is `Reset()` **before** `CoUninitialize`, not at scope exit - releasing a WIC factory into an apartment that has already been shut down faults (0xc0000005), today masked only because `g_devices.wicFactory` keeps `windowscodecs.dll` loaded |
| `StopThumbnailLoader` | sets `g_thumb.loaderStop`, joins |
| `DrawThumbnailStrip` | one cached origin-centred rounded-rect geometry reused via RT transform; centre-crop to square slot; non-current darkened 50%; placeholder brush when a slot has no bitmap **or when `clipGeos` is empty** (a decoded thumbnail still cannot be drawn without its clip shape). The clip layer is placed by the transform current at `PushLayer`; `PopLayer` leaves the transform at identity, which the absolute-coordinate dim/placeholder fills depend on. Its only caller is `Render` in `Rendering.cpp`, which includes `Thumbnails.h`. |
| `StartDirectoryWatcher` / `ReadDirectoryChangesW` | non-recursive, `FILE_NOTIFY_CHANGE_FILE_NAME`. The handle, the thread and the stop event are `static` in `Thumbnails.cpp`. Shutdown = set the stop event, join, then close both handles; the thread waits on that event next to the read's own event and, on the stop path, cancels its outstanding read and waits for the cancellation, so the handle is never closed under a live request. Posts `WM_APP_DIRCHANGE` to **`g_app.mainWindow`** - never the overlay handle, whose value changes while this thread runs - and also when the read returns 0 bytes, which means the notification buffer overflowed and the changed names are unknown |
| `g_thumb.dragging` / `g_thumb.velocity` / `g_thumb.freeScroll` / `g_thumb.dragHitIndex` | drag vs click decided in `WM_LBUTTONUP` (<5 px = click); EMA velocity ×25 for momentum; hit half-width expanded by half the gap so gaps aren't dead zones |

## 8. UI

**Section 8 now lives in `UI.h` (declarations) + `UI.cpp` (definitions)** — step 6. Nothing in the
module was `static`, so there were no linkage fixes; the only non-verbatim edits were the **two
default arguments** (`FitToWindowRelative(..., bool preserveCenter = true)` and
`InitializeImageLayout(HWND, bool hard = false)`), which were on the definitions in the main file and
now appear **only** on the `UI.h` declarations (C2522 otherwise). `OpenImageFile` /
`OpenNextImage` / `OpenPrevImage` / `DeleteCurrentImage` stayed in the main file with the navigation
code; `UI.cpp` declares the first three in its bridge block.

| Anchor | Notes |
|---|---|
| `InitializeMenuButtons` | Open (📂) + hamburger (☰) — top-left zone, `SetForcedVisibility(true)` here, which `InitializeButtons` clears at the first fullscreen, so afterwards they are zone-driven rather than permanently shown. Position/scale captured against the **windowed** client, so they keep that size in fullscreen |
| **Aspect-ratio assumptions in the widget positions** (`InitializeMenuButtons`, `InitializeButtons`) | `Layout::x.value` is a fraction of the render-target **width** and `y.value` of **height**, so pixel-even insets need different fractions on each axis. Three places encode that: the Open button's margin is `0.05 * height / width`; the hamburger's step is a flat `0.056f` of width (not height-normalised, so the gap between the two buttons is not equal to that margin in pixels); and the ❌ inset uses `top * 9/16`, which is only square in pixels on a **16:9** surface. All captured once per surface and frozen, so they are stable — they just drift on a display of another aspect. Untested on anything but 16:9 |
| hamburger: `HWND ownerWnd = (g_view.isFullscreen && g_app.overlayWindow...` | the long comment explains why hardcoding `g_app.mainWindow` used to break fullscreen; wrapped in `FullscreenExitSuppressor` because `TrackPopupMenu`'s menu window isn't ours |
| `InitializeButtons` | 9 more: 1:1, ▶ slideshow, ➕/➖ zoom, ⭯/⭮ rotate, ⮜/⮞ prev/next, ❌ exit (top-right zone, hidden when windowed). The slideshow button's glyph is the play triangle or `kSlideshowStopGlyph` (`U+25A0` BLACK SQUARE — a plain geometric shape with no emoji form, so it stays monochrome; `U+23F8`/`U+23F9` were tried and arrive as colour emoji tiles, and `U+FE0E` text-presentation did not change that), re-set by `UpdateSlideshowButton` at every slideshow transition and at the end of this function. All use `ActivationZone` + normalized `Layout` positions + `uiPixelScale = min(w,h)` — but the positions stop being proportional after the first capture, see the anchor row below |
| **Anchor capture is once per process** (`UIElement::CaptureAnchorsOnce`, `ResetAnchors`, `SetLayout`) | Both axis caches and the activation-zone cache are captured the **first** time an element lays out, against `referenceWidth/Height` = the render-target size at that element's `Initialize()`: the **windowed client** for 📂/☰ (created at startup) and the **monitor** for the 9 in-image buttons + both text boxes (created at fullscreen stage 2). `CaptureAnchorsOnce` then early-returns forever, `SetLayout` deliberately keeps the caches, and **nothing calls `ResetAnchors()`**. So positions are fixed pixel offsets from the anchor edge/centre for the life of the process. While fullscreen on the capture monitor this is indistinguishable from the normalized spec; it shows up when the render target later differs from the capture size — e.g. after a fullscreen round trip the bottom row keeps its monitor-derived ~2% spacing (≈77 px at 4K) inside a ≈1600 px window instead of the ≈32 px the `0.49/0.51/0.53` fractions would give. Sizes are frozen the same way (`uiPixelScale` snapshot), so nothing rescales on resize. `//g_buttons.clear();` inside `InitializeButtons` is commented out — enabling it would give fresh anchors. Left as-is deliberately: the user wants sizes frozen. See the `ResetAnchors` note in `UIElement.h` |
| **The 9 in-image buttons and both text boxes exist only after the first fullscreen** (`InitializeButtons`, `InitializeImageInfoLabel`) | Their only call sites are fullscreen stage 2 (`Fullscreen.cpp`), while `InitializeMenuButtons` runs from `wWinMain`. So the windowed UI at startup is just 📂 + ☰, and the two groups carry different frozen `uiPixelScale` (windowed client vs monitor) for the whole process — see the row above |
| **Tooltip text sizing is process-wide, deliberately** (`UIElement::TooltipFontPx`, `g_view.uiPixelScale`, `Layout::tooltipFontSize`) | Tooltip px = `g_view.uiPixelScale * tooltipFontSize`, NOT the element's own frozen `uiPixelScale` — sizing it from that made the 📂/☰ tooltip ~11% larger than the bottom row's on every display, permanently. `g_view.uiPixelScale` is set in `InitInstance` from the primary monitor (so it is never the `1.0f` default) and overwritten by fullscreen stage 2 with the monitor actually used. Default factor `0.0144f` keeps the size 📂/☰ always had; **no per-element tooltip overrides** — the old `openConfig.layout.tooltipFontSize = 0.03f` was one and is gone. Button *glyph* sizes (`Config::fontSize`) stay per-element and hand-tuned per symbol; do not "unify" them |
| `ButtonId::PREVIOUS` / `ButtonId::NEXT` lambdas | both call `RestartSlideshowTimer()` after navigating (the guard is inside the helper) |
| hold-repeat policy (`AnimatedButton::Config::holdEnabled` / `repeatGate`) | **Off** for `OPEN`, `HELP` (hamburger) and `SLIDESHOW`: the first two open modal loops (`GetOpenFileNameW` / `TrackPopupMenu`) and the third *toggles* the mode, so auto-repeat would stack dialogs or flip-flop the slideshow. **On** for the rest, because held `W`/`S`/`Q`/`E` repeat on the keyboard too. `PREVIOUS`/`NEXT` additionally carry a `repeatGate` on `g_fullscreen.navPendingRender`, so a held nav button gets the same one-nav-per-presented-frame backpressure as the keyboard (the widget's own `m_repeatInterval` stays as the rate cap). Repeat also stops when the cursor leaves the button (`m_mouseOverSelf`). Because activation is on the press, `WM_LBUTTONDBLCLK` re-running `OnMouseDown` means a double-click on a button fires the action **twice - by design**, not a bug |
| **`UITextBox` typing rules** (`UITextBox::OnChar`) | `OnChar` is the **only** filter — there used to be a separate `AcceptChar` describing the rules, but nothing called it and its rules disagreed with what was enforced (it allowed `-`, which was never inserted). `NumericFloat` = digits + at most one `.`; **no minus sign anywhere**, because the only editable box is the zoom readout and a negative zoom is meaningless. Control characters (`Enter`/`Esc`/`Backspace` arrive on the `WM_CHAR` path as 13/27/8) never insert. There is **no caret and no selection range**: `OnMouseDown` sets `m_selectAllOnFocus`, and the first typed character — or `Backspace` — replaces the whole string. `VK_RETURN` fires the callback and drops focus; `VK_ESCAPE` drops focus and keeps the text |
| **`UITextBox` focus border is independent of `backgroundAlpha`** (`UITextBox::Draw`) | The border is drawn on `m_focused` alone, deliberately *not* inside the `backgroundAlpha > 0` block — an editable box with no background would otherwise have no focus feedback. Only an editable box can ever take focus (`OnMouseDown` refuses otherwise), so `m_focused` is sufficient. The pulsing "whole string selected" fill is separate again |
| `InitializeImageInfoLabel` | `TextBoxId::FILE_NAME` (bottom, display-only) + `TextBoxId::ZOOM_INPUT` (centre, `InputMode::NumericFloat`, clamped 1–10000 then `/100`). The typed-zoom callback anchors on the **viewport centre** (same convention as cursor-anchored wheel zoom) rather than on the image's own centre — preserving an off-screen image centre after a panned wheel zoom would keep the image off screen. It guards on `g_view.bitmap` because the readout stays clickable over the load-error screen. History: it used to be `UpdateTargetZoom` plus a **zero-delta** `ZoomIntoImage`, then `SetZoomCentered(preserveCenter=true)`, which is what pushed the image off screen |
| `InitializeImageLayout` | opens at 100% unless oversized or `g_settings.openFitToScreen`, then 95% fit; `hard=false` starts at `g_view.zoom = 0.05f` and springs in; early-outs on `g_view.restoredStateThisLoad && !g_slideshow.active`, and sets `g_fullscreen.overlayAlpha` absolutely (`1.0f` when `hard`, `0.0f` when not), so it is also what decides whether the enter/exit dim starts from black or from clear. Every caller passes `hard = true` except `Render`, which passes `hard = g_slideshow.active`. Callers: `Render` (`Rendering.cpp`), `ResizeOverlayToWorkArea` (`Fullscreen.cpp`), `OpenImageFile`, `OpenNextImage`, `OpenPrevImage`, `DeleteCurrentImage`, the strip-click branch of `WM_LBUTTONUP`, the slideshow branch of `WM_TIMER`, and `WM_APP_DIRCHANGE`. **`WM_SIZE` does not call it** - it recentres with `SetZoomCentered` instead (`Rendering.cpp` includes `UI.h` for the `Render` call). |
| `SetZoomCentered` / `FitToWindowRelative` / `ZoomIntoImage` / `MakeZoomVisible` | `ZoomIntoImage` = `pow(1.1, delta/120)`, clamped 0.01–100, cursor-anchored when over the image; `optionalPt` arrives in screen coords (`GetCursorPos`) and is converted to client coords exactly once |
| `g_textBoxes[TextBoxId::ZOOM_INPUT].IsFocused()` guard | in `Render`, so typing isn't overwritten |
| **UI visibility ownership** (`UIElement::UpdateProximity` / `SetForcedVisibility` / `SetTargetVisibility`) | `m_targetVisibility` has three owners in precedence order: explicit override → forced visibility → activation zone. `UpdateProximity` always records the zone verdict in `m_proximityTarget` (even while outranked) so `SetForcedVisibility(false)` and `ClearTargetVisibility()` hand control **back to the zone** instead of forcing 0. Forcing 0 was a real bug: the zone verdict is only recomputed on `WM_MOUSEMOVE`, so a still cursor left the element stuck invisible — the zoom readout vanished when Enter committed a typed value, because its 1 s `TimerId::ZOOM_DISPLAY` had already zeroed the target mid-typing and only `m_focused` was holding the box up. **Residual staleness:** the verdict still only refreshes on `WM_MOUSEMOVE`, so it can be out of date if a widget's rect moves under a still cursor (resize, fullscreen transition) — fixing that means driving `UpdateProximity` from `UpdateEngine` with the real cursor position |

## 9. Metadata

**All of section 9 now lives in `Metadata.h` (declarations) + `Metadata.cpp` (definitions)** - step 7.
It was **one contiguous range of 1,809 lines** and it is the cleanest move of the split: **zero**
linkage fixes and **zero** default-argument relocations. The `static` reader helpers were called
only from inside the region, so they keep internal linkage and are absent from the header; only the
six functions that were already non-static are declared there (`ShowImageMetadata`,
`ShowImageProperties`, `ShowMetadataViewerWindow`, `MetadataViewerWndProc`,
`RegisterMetadataViewerClass`, `PropertiesDlgProc`) and only the first two actually have outside
callers (both `WndProc` `WM_COMMAND`). `struct PngTextField` and `struct ImagePropertiesFields` are
`Metadata.cpp`-private. Its only module edge is `AvifOpenAndParse` -> `#include "Decoding.h"`; it also
had to pick up `<avif/avif.h>`, `<jxl/decode.h>`, `<algorithm>`, `<cstring>`, `<cstdio>` and
`<commctrl.h>` + `comctl32.lib` (for `TaskDialog`) that the main file's include block used to supply.

| Anchor | Notes |
|---|---|
| `RationalFromPackedUI8` | WIC packs EXIF RATIONAL into `VT_UI8`: num<<32 \| den |
| `PropVariantToDisplayString` | `VT_LPWSTR/LPSTR/UI1/UI2/UI4/I4/UI8` + single-elem vectors (ISO) |
| `AppendSimpleField` / `AppendRationalField` | `"label: value\n"`; shutter speeds as `1/N s` |
| `AppendGpsCoordinate` | 3 rationals + N/S/E/W hemisphere → decimal degrees |
| `OrientationName` | 1–8 human-readable |
| `ReadDateTakenValue` / `ReadColorSpaceValue` / `ReadOrientationValue` | bare-value variants for the Properties dialog (em dash `—` for N/A, layout stays static) |
| `FormatFileSizeW` / `FormatFileTimeW` | Explorer-style `1.23 MB (1,290,000 bytes)`; locale short date+time |
| `ReadPngTextChunks` | raw chunk walk for **tEXt/iTXt** — WIC needs the exact keyword and SD/ComfyUI use arbitrary ones; 8 MB cap; skips compressed iTXt and zTXt |
| `ReadWebpXmpFields` | WIC doesn't expose the `"XMP "` RIFF chunk at all; walks chunks (word-aligned), then every child of every `rdf:Description` |
| `DecodeXmlEntities` / `StripXmlTags` / `FindTagEnd` / `TrimWhitespace` | the shared mini-XML helpers (`FindTagEnd` respects quoted attribute values) |
| `ReadJxlMetadataBoxes` | `JXL_DEC_BOX` + `JxlDecoderSetDecompressBoxes`; `"Exif"`/`"xml "` boxes; **sanity-checks the TIFF header** before trusting the 4-byte offset prefix (hand-edited JXL omits it) |
| `OpenAvifExifMetadataReader` / `OpenJxlExifMetadataReader` | feed the raw TIFF blob to WIC's **TIFF** decoder → reuse all the above helpers |
| `ReadAvifXmpFields` / `ReadJxlXmpFields` | same XML strategy, three near-identical copies (extraction candidate) |
| `ShowImageMetadata` | tries `/app1/ifd/...` **and** `/ifd/...` for every tag (PNG/WebP have no APP1) |
| `MetadataViewerWndProc` | `L"MetadataViewerClass"`, dark owner-draw, `EM_SETLIMITTEXT 0` to lift the ~30k edit cap, **its own modal message pump** + accelerators (Esc, Ctrl+A) |
| `ShowImageProperties` + `PropertiesDlgProc` + `IDD_PROPERTIES` | in-app dialog (replaced the shell Properties verb, which is why `ContextMenuId::PROPERTIES` no longer needs manual suppression) |
| `AssociateFileTypes` + `About` | **moved out in step 8 - they were never metadata**; see section 10. Original note: `TaskDialogIndirect` confirm → HKCU ProgID + `shell\open\command` + `Capabilities` + per-ext → `SHChangeNotify(SHCNE_ASSOCCHANGED)` |

## 10. Dialogs - file association, About, Keyboard shortcuts

**All of section 10 now lives in `Dialogs.h` (declarations) + `Dialogs.cpp` (definitions)** - step 8,
the last module. Two functions, neither ever `static`, and the header is exactly those two
declarations. `g_app.instance` is the only global either one touches.

| Function | Notes |
|---|---|
| `AssociateFileTypes(HWND)` | `MenuCommandId::ASSOCIATE` in `WndProc`'s `WM_COMMAND`. Asks first with a `TASKDIALOGCONFIG` / `TaskDialogIndirect` (owned by the main window, so `IsOwnedByOurWindow()` recognizes it) and returns **without touching anything** unless the answer is `IDOK`. Then `GetModuleFileNameW` -> ProgID `PicassoPictures.ImageFile` under `HKCU\Software\Classes`, its `shell\open\command` value (`"<exe>" "%1"`, built with `swprintf_s`), the friendly name, `Software\PicassoPictures\Capabilities` + `Software\RegisteredApplications`, one `Software\Classes\<ext>` -> ProgID per extension (.jpg .jpeg .png .bmp .gif .tif .tiff .webp .avif .jxl) plus a `FileAssociations` value per extension, then `SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, ...)`. A second `TaskDialog` reports whether **every write** succeeded - the ProgID, its open command and the Capabilities keys count, not just the per-extension ones (they used to be ignored, so "Done!" could print with the ProgID missing). A `GetModuleFileNameW` that fails or hits `MAX_PATH` aborts with its own dialog before anything is written, rather than registering an empty or truncated open command. Everything is `HKCU`, so the failure text must not suggest elevation. The `RegSetStr` lambda is local to the function. **The only place in the project that writes those keys.** |
| `About` | **ONE `DLGPROC` for TWO templates**: `IDD_ABOUTBOX` (`IDM_ABOUT`, `MenuCommandId::ABOUT`) and `IDD_COMMANDBOX` = "Keyboard shortcuts" (`ID_HELP_COMMANDS`, `MenuCommandId::SHORTCUTS`). Four `DialogBox` sites in `WndProc`; there is no other shortcuts dialog anywhere in the project. `WM_INITDIALOG` returns TRUE; `WM_COMMAND` maps `IDOK`/`IDCANCEL` to `EndDialog`; `WM_NOTIFY` matches `IDC_SYSLINK_GITHUB` + `NM_CLICK` and `ShellExecuteW`s `pLink->item.szUrl`, so the GitHub URL lives in the dialog template (`Picasso Pictures.rc`), not in code. Modal, so the main window is insensitive while it is up. **The About box's version string (`"Picasso Pictures, Version 1.8"`) is a literal in the `.rc`** - bump it by hand at each release; there is no `VERSIONINFO` resource anywhere in the script, so nothing else carries a version. |
| `Dialogs.cpp` includes | What the main file used to supply: `<commctrl.h>` + `comctl32.lib` (task dialog + `NMHDR`/`NMLINK`/`NM_CLICK`), `<shellapi.h>` (`ShellExecuteW`), `<shlobj.h>` (`SHChangeNotify` - **not** in `shellapi.h` on this SDK), `<cstdio>` (`swprintf_s`), `<cwchar>` (`wcslen`), `shell32.lib`. `advapi32.lib` needs no pragma (linker default). **No bridge block.** |
| Grep anchors | `AssociateFileTypes(HWND hWnd)`, `PROG_ID      = L"PicassoPictures.ImageFile"`, `INT_PTR CALLBACK About(`, `IDC_SYSLINK_GITHUB` |

---

## 11. `WndProc` — message → handler

| Message | Behaviour |
|---|---|
| `WM_ERASEBKGND` | `return 1` (D2D owns the surface) |
| `WM_COPYDATA` | second instance's path → `BuildImageList` + `LoadImageD2D` + `EnterFullscreen(true,true)` |
| `WM_ACTIVATE` | `WA_INACTIVE` + `g_fullscreen.initDone` + depth 0 + `!IsOwnedByOurWindow(lParam)` → **post** `WM_APP_EXITFULLSCREEN` (never call directly — destroying the overlay mid-focus-change loses keyboard focus) |
| `WM_APP_EXITFULLSCREEN` | re-checks `g_fullscreen.suppressExitDepth == 0` at *dispatch* time, not post time |
| `WM_DROPFILES` | load → `BuildImageList` → `EnterFullscreen` |
| `WM_COMMAND` | `IDM_OPEN`, `IDM_ABOUT`, `IDM_EXIT`, `ID_HELP_COMMANDS`/`MenuCommandId::SHORTCUTS`, `MenuCommandId::HQ_FILTER`, `MenuCommandId::FIT_TO_SCREEN`, `MenuCommandId::ASSOCIATE`, and 6 context-menu IDs (copy→`CF_DIB`, delete, open folder, properties, metadata, wallpaper). `IDM_OPEN` / `IDM_EXIT` / `ID_HELP_COMMANDS` are dead in practice - the main window's menu is detached in `wWinMain`, so nothing can post them; only `IDM_ABOUT` can still arrive, from the `Alt+?` / `Alt+/` accelerators in the `.rc`. `HQ_FILTER` / `FIT_TO_SCREEN` are the only writers of the two registry values `wWinMain` reads back at launch |
| `WM_PAINT` | `BeginPaint` → `Render` → `EndPaint` |
| `WM_SIZE` | unbind target + `g_devices.swapRTV`, `ResizeBuffers`, re-create both, `UpdateLayout` on all UI, re-centre |
| `WM_RBUTTONUP` | 6-item popup, wrapped in `FullscreenExitSuppressor` |
| `WM_MOUSEWHEEL` | `ZoomIntoImage(delta, &pt)` + `MakeZoomVisible` |
| `WM_LBUTTONDOWN` | buttons → text boxes → **thumbnail strip row claimed whole** (`PointInStripRow`, shared with `WM_LBUTTONDBLCLK`) → else image pan + `SetCapture`. A button or an **editable text box** claims the press on the down and sets `g_buttonClaimedClick` (buttons act on the press — their callback runs in `OnMouseDown`, not on the release; text boxes claim so the release isn't read as "clicked outside the image", which would exit fullscreen when the zoom readout at screen centre isn't over the image). Worth knowing: `g_view.mouseFromDown` is written only in the pan branch, so after a widget press it still holds the previous drag's point, which is why the explicit claim — not `wasReallyDragging` — is what keeps a widget click from also leaving fullscreen |
| `WM_LBUTTONUP` | **the strip-drag resolution runs first and returns early; the button `OnMouseUp` loop runs next, still before the `if (!g_view.bitmap) break;` guard** — a release must always end the press, or `m_holdActive` survives and the button auto-repeats on hover alone (a failed load clears `g_view.bitmap`, which is exactly how that was hit: navigate onto a corrupt file, release swallowed, repeat runs free). `AnimatedButton::Update` also requires `m_pressed`, so a lost release can at most freeze the press animation, never fire actions. The strip branch is what decides click (<5 px) vs momentum (a click that opens another image calls `RestartSlideshowTimer`). After the button loop, `!overImage && !wasReallyDragging && !buttonClaimed` → `ExitFullscreen` ⚠️ **(item 7 - closed)**; `TimerId::KILL_ROTATION` 120 ms snaps partial rotations to the nearest 90° |
| `WM_MOUSEMOVE` | button/textbox proximity, vignette activation zones, `stripCoasting` keeps the strip visible mid-fling, strip drag + EMA velocity, image pan |
| `WM_LBUTTONDBLCLK` | buttons → text boxes → **the film-strip row**, then `!g_view.bitmap` → `OpenImageFile` (double-click on empty space of the load-error screen), then windowed+over image → fullscreen preserving view; fullscreen + overlay exists + over image → toggle 1:1 ↔ 96% fit. The order matters: the second click of a rapid pair arrives as DBLCLK, not DOWN, so a branch ahead of the widget loops would steal double-clicks aimed at a button while the error screen is up (it used to, and opened the file dialog). All three widget/strip branches set `g_buttonClaimedClick` so the release that follows the DBLCLK is not read as "clicked outside the image"; the strip branch exists because a double-click on a thumbnail otherwise fell through to the image branches and also toggled the image's fit/1:1 zoom. The `g_app.overlayWindow` test in the last branch is the guard for fullscreen stage 1, where `isFullscreen` is true but the overlay does not exist yet (`FitToWindowRelative`/`SetZoomCentered` would get a null HWND) |
| `WM_TIMER` | 4 IDs: enter-fullscreen stage 2 (**only if `g_view.isFullscreen && !pendingExit && !isExiting`** - `CompleteExitFullscreen` kills the timer, but a `WM_TIMER` already queued survives `KillTimer`), kill-rotation, zoom display hide, slideshow advance (only if `g_slideshow.transitionAlpha > 0.8f`) |
| `WM_CHAR` | forward to textboxes |
| `WM_KEYDOWN` | `RestartSlideshowTimer()` runs first, before anything else, so any key re-arms the 6 s slideshow interval - including keys typed into the zoom box (deliberate, see the end of this section); a focused textbox then swallows everything; else `Del`/`Shift+Del`, `Esc`, `F` toggle fullscreen (auto-repeat ignored via `lParam & (1<<30)`), `O`, `W`/`↑`, `S`/`↓`, `A`/`←`, `D`/`→`/`Space` (gated by `g_fullscreen.navPendingRender`; `W`/`S` are zoom in/out, not navigation, and are not gated), `Q`/`E` rotate, `F5` slideshow. **Every quit path goes through `DestroyWindow(g_app.mainWindow)`**, never a bare `PostQuitMessage`: `Esc` windowed, `Esc` with a failed load, and the ❌ button with no image all destroy the main window so `WM_DESTROY` runs the teardown first |
| `WM_APP_DIRCHANGE` | `BuildImageList` again; if the list comes back **empty** (every image deleted or moved out in Explorer, or the folder itself gone) `InitFullScreenExit()` - same treatment as deleting the last picture in the folder; otherwise, if the current file vanished, clamp to index 0 |
| `WM_DESTROY` | main window only: destroy overlay, stop all 3 threads, discard resources, **empty the containers that still hold COM objects the discard missed** (`g_thumb.entries` / `clipGeos` / `readyQueue`, `g_anim.frames` / `gpuFrames`), reset all `g_wic*`/factories, `PostQuitMessage`. **It does not call `CoUninitialize`** - `CoInitialize` runs once in `wWinMain` and its single matching call is on the `shutdown:` label after the message loop; `WM_DESTROY` runs while that loop is still alive, so a call here would make the loop's call a second one (that double uninitialize was the paired bug behind the exit crashes). **The container clearing is load-bearing:** these are globals, so anything left in them is destroyed *after* `main` returns — releasing COM objects into a runtime that has already gone, i.e. a crash on exit (`0xc0000005` in `~vector` → `~ComPtr::InternalRelease`, always at a nearby offset across builds). Thumbnails and clip geometries only exist after fullscreen stage 2, which is why a windowed close never crashed and a fullscreen close always did. **Not emptied, deliberately:** `g_textBoxes` / `g_buttons` - the widgets' only COM members are DWrite objects, never a WIC bitmap or a device-context resource, so nothing device-dependent is left behind there |

## 12. Globals worth knowing (grouped)

**All of these are declared `extern` in `Globals.h` and defined once in
`Globals.cpp`.** They are no longer in `Picasso Pictures.cpp`. The one exception is the
directory watcher's handle / thread / stop event: those are `static` in `Thumbnails.cpp`,
because nothing outside that file reads them. `Globals.h`
also owns `MAX_LOADSTRING`, `ImageViewState`, `ThumbnailEntry`, the control /
command / timer ID enums, `WM_APP_*`, `IsOwnedByOurWindow` and
`FullscreenExitSuppressor` — include it and you get the whole set.
`ViewState` (`g_view.zoom`), `FullscreenState` (`g_fullscreen.overlayAlpha`), `ImageState`
(`g_image.path`), `AnimState` (`g_anim.frames`), `RenderPipeline` (`g_pipeline.srv`),
`AppSettings` (`g_settings.useHQFilter`), `VignetteState` (`g_vignette.topVisibility`),
`ThumbState` (`g_thumb.entries`), `SlideshowState` (`g_slideshow.active`), `AppIdentity`
(`g_app.mainWindow`), `DeviceResources` (`g_devices.renderTarget`) and `BackgroundArt`
(`g_background.bitmap`) are structs with one instance each; the only flat globals
left are the two control maps, `g_textBoxes` and `g_buttons`. The
fullscreen-exit-fade trio lives in `FullscreenState`: it runs for any fullscreen
exit (the ramp is gated on
`g_view.isFullscreen`, not on slideshow state), so it is fullscreen state rather
than slideshow state. The ID enums and the `constexpr` slideshow timings are not
state either, and neither are the watcher's `static` handle / thread / stop
event in `Thumbnails.cpp`.

- **Identity/lifetime (`AppIdentity`, one instance `g_app`):** `instance`, `title`, `windowClass`, `mainWindow`, `overlayWindow`, `blackCoverWindow`, `metadataViewerWnd`, `isExiting`, `launchedWithFile`
- **Factories/devices (`DeviceResources`, one instance `g_devices`):** `d2dFactory`, `wicFactory`, `dwriteFactory`, `textFormat`, `d3dDevice`/`d3dContext`, `d2dDevice`, `renderTarget`, `swapChain`, `d2dTargetBitmap`, `swapRTV`, `renderTargetWindow`
- **View state (`ViewState`, one instance `g_view`):** `zoom`/`targetZoom`, `offsetX/Y`/`targetOffsetX/Y`, `rotationAngle`/`targetRotationAngle`/`rotationSpeed`, `exifRotation`, `smooth`, `stateByPath` (path → `ImageViewState`), `restoredStateThisLoad`, `isDragging`, `lastMouse`, `mouseFromDown`, `bitmap`, `isFullscreen`, `uiPixelScale`
- **Image (`ImageState`, one instance `g_image`):** `source` (the WIC source), `files`, `index`, `path`, `fileName`, `lastLoadError`, `loadAttemptName`, `showLoadError`, `width`, `height`. Its GPU upload `g_view.bitmap` stays in ViewState
- **Animation (`AnimState`, one instance `g_anim`):** `frames` (WIC), `gpuFrames` (GPU), `frameDelays`, `currentFrame`, `isAnimated`, `lastFrameTime`, `decodeThread`, `decodeStop`, `framesReadyUpTo`
- **Image pipeline (`RenderPipeline`, one instance `g_pipeline`):** the cached brushes `dimBrush`, `blackBrush`, `errorTitleBrush`/`errorDetailBrush` and `vignetteBrush[2]` (each with its `(y0, y1, alpha)` cache key), then `mipTex`, `srv`, `sampler`, `vs`/`ps`/`il`, `vb`/`cb`, `blend`/`rast`/`ds`, `ready`. Shaders and fixed-function state are built once per device, `mipTex` once per image
- **Settings (`AppSettings`, one instance `g_settings`):** `useHQFilter`, `openFitToScreen` - the two values persisted under `Software\PicassoPictures`, read in `wWinMain` and toggled from the hamburger menu
- **Fullscreen (`FullscreenState`, one instance `g_fullscreen`):** `needsInit`, `initDone`, `overlayAlpha`, `navPendingRender`, `pendingPreserveView`, `suppressExitDepth` (a refcount, touched only through `FullscreenExitSuppressor`), plus the exit-fade trio `pendingExit`, `wasInSlideshowOnExit`, `exitBgFadeAlpha`. `g_view.isFullscreen` is the flag itself, in ViewState
- **Slideshow (`SlideshowState`, one instance `g_slideshow`):** `active`, `fromFullscreen`, `preFade`/`preFadeAlpha`, `stopFade`/`stopFadePendingResize`/`stopFadeAlpha`, `transitionAlpha`/`targetAlpha`, `prevBitmap`, `prevBgBitmap`, `bgBitmap`, `prevZoom`/`prevOffsetX`/`prevOffsetY`/`prevRotation`, `blurEffect`. `SLIDESHOW_INTERVAL_MS = 6000` and `SLIDESHOW_FADE_STEP = 0.05f` are `constexpr`, not state
- **Vignettes (`VignetteState`, one instance `g_vignette`):** `topVisibility`/`topTarget`, `bottomVisibility`/`bottomTarget`, `topBarHeightPx`/`bottomBarHeightPx`, `topActivationPx`/`bottomActivationPx`, `heightsCaptured`
- **Thumbnails (`ThumbState`, one instance `g_thumb`):** `entries` (`ThumbnailEntry{wic,d2d}`), `readyQueue` + `readyMutex`, `loaderThread`/`loaderStop`, `scrollOffset`/`targetOffset`, `width`/`height`/`gap`, `stripInsetFromBottomPx`, `sizeCaptured`, `clipGeos`, and the drag set: `dragging`, `dragStartX`, `dragLastX`, `velocity`, `freeScroll`, `dragHitIndex`
- **Backgrounds (`BackgroundArt`, one instance `g_background`):** `bitmap`/`wicBitmap` (desktop capture), `defaultBitmap`/`wicDefaultBitmap` (embedded JPG resource `IDR_DEFAULT_BKG_JPG`), `shadowEffect`, `shadowSourceBitmap` (the two solid brushes are in `RenderPipeline`)
- **UI controls (the two flat globals):** `g_textBoxes` (keyed by `TextBoxId`) and `g_buttons` (by `ButtonId`). The widgets' only COM members are the DWrite factory, text format and tooltip text layout - device-independent, so `DiscardDeviceResources` has nothing to release and a device loss cannot leave them dangling. `g_buttons`/`g_textBoxes` are filled by `InitializeMenuButtons` at startup and by fullscreen stage 2, and are never cleared; the `WM_DESTROY` row records why those two maps are deliberately left out of the container clearing
- **Timers:** `TimerId::SLIDESHOW 0xcafe`, `TimerId::ZOOM_DISPLAY 0xbebe`, `TimerId::KILL_ROTATION 0xbeca`, `TimerId::ENTER_FULL_SCREEN 0xcaca`
- **Messages:** `WM_APP_DIRCHANGE (WM_APP+1)`, `WM_APP_EXITFULLSCREEN (WM_APP+2)`

## 13. Third-party / build notes

- `vcpkg install libavif dav1d libjxl`, static triplets (`x64-windows-static`). **The claim "so no extra DLLs ship" is not true today:** `jxl.lib` is a mixed-CRT archive - it carries `/DEFAULTLIB:LIBCMT,libcpmt` but its objects reference the *import* forms of the STL thread primitives (`__imp__Thrd_*`, `__imp__Mtx_*`, `__imp__Cnd_*`, `__imp__Syserror_map@std`) - so the released exe imports **MSVCP140.dll** and needs the VC++ runtime present. `avif.lib` and `dav1d.lib` are clean, and none of our own objects reference those stubs. Making the exe self-contained means rebuilding libjxl with a consistent `/MT`.
- The `#pragma comment(lib, ...)` names have shifted across libavif/libjxl releases — check vcpkg's install output.
- `GUID_ContainerFormatWebp` is redefined locally as `kContainerFormatWebp` for older SDKs. Only
  `Decoding.cpp` uses it now (`Metadata.cpp`'s WebP reader walks the RIFF chunks raw, so it never
  touches the WIC WebP container GUID).
- `TaskDialog` / `TaskDialogIndirect` come from `<commctrl.h>` + `comctl32.lib`. `Metadata.cpp` and
  `Dialogs.cpp` each include both explicitly; the main file has **no** explicit `commctrl.h` include
  and has always picked the declaration up implicitly from its own include block (which one of those
  headers provides it was never pinned down). Keep the explicit include in any module that calls a
  `TaskDialog*`.
- `SHChangeNotify` and `SHCNE_*` / `SHCNF_*` are declared in **`ShlObj.h`**, NOT in `shellapi.h` -
  checked against the 10.0.19041.0 and 10.0.26100.0 SDKs, where `shellapi.h` has no `SHChangeNotify`
  at all. `Dialogs.cpp` includes both; missing that cost step 8 one build cycle.
- `Resource.h` IDs referenced here: `IDR_DEFAULT_BKG_JPG`, `IDD_ABOUTBOX`, `IDD_COMMANDBOX`, `IDD_PROPERTIES`, `IDC_SYSLINK_GITHUB`, `IDC_PROP_*` (13 controls), `MetadataControlId::EDIT`, `MetadataControlId::SELECTALL`.
- Files: `AnimatedButton.h/.cpp`, `UITextBox.h/.cpp` (both use `Config` + `Layout{Anchor,PosMode,ActivationZone,uiPixelScale,referenceWidth/Height}` + `Initialize`/`UpdateLayout`/`Update`/`UpdateProximity`/`Draw`/`OnMouseDown`/`OnMouseUp`/`SetForcedVisibility`/`IsFocused`/`SetText`/`SetTooltip`), `framework.h`, `Resource.h`, `targetver.h`.

## 14. Known issues

Two bugs are known and **accepted as-is** rather than fixed:

- **Multi-monitor sizing.** The initial window size and centring come from
  `GetSystemMetrics(SM_CXSCREEN)`, which is the primary monitor only, so the window always opens on the
  primary monitor at primary-monitor proportions, and the shared text format is sized from it too. The
  fullscreen overlay path is monitor-aware (`MonitorFromWindow` + `GetMonitorInfo`). Reacting to a DPI
  change while the app runs is deliberately not implemented - there is no `WM_DPICHANGED` handler even
  though the process is `PER_MONITOR_AWARE_V2`, and one should not be added.
  **Already checked, do not re-check:** `CreateBlackCoverWindow`, `CreateOverlayWindow` and
  `CaptureDesktop` each take the monitor from `MonitorFromWindow(..., MONITOR_DEFAULTTONEAREST)`, and
  `CreateRenderTarget` sizes the swap chain from `GetClientRect` of the window it is handed. On a
  multi-monitor machine the diagnostic is at `EnterFullscreen` stage 2: compare
  `MonitorFromWindow(g_app.mainWindow, ...)` with the overlay's `GetWindowRect` and see which is wrong.
  Also latent: `Render`'s default-background branch calls `GetClientRect(g_app.mainWindow, ...)` while
  drawing into the current render target, which in fullscreen is the overlay - unreachable today because
  fullscreen is never entered with no image loaded. Fix it as part of this item, not standalone, and not
  by making no-image fullscreen possible.
- **Cancelling a slideshow inside the 150 ms fullscreen stage-1 gap.** `Esc` or `F5` in that window
  clears the pre-fade but leaves the mode on, so the app fades to black and comes back up in slideshow
  mode. A fix was attempted and reverted: the flags available in `ExitSlideshowMode` cannot tell a
  stage-1 cancel from the late call that `CompleteExitFullscreen` makes, and the attempt regressed the
  ordinary stop fade. **Do not retry that shape** - `ExitSlideshowMode` is also re-entered from
  `CompleteExitFullscreen` (`if (g_slideshow.active) ExitSlideshowMode();`) with the overlay already gone
  and `preFade` still set, so an `active && !overlayWindow` guard fires during a normal exit too. A real
  fix needs state that can tell the two apart, or has to defer phase 2's `active = true` until the
  overlay exists. The slideshow button cannot reach the gap because the main window is alpha-0 through
  all of stage 1.

Behaviour that looks wrong but is deliberate: Shift+Delete deletes permanently with no confirmation
(fast bulk culling); the slideshow blur keeps `D2D1_BORDER_MODE_HARD` (`SOFT` was tried and darkens two
screen edges); the slideshow `SetTimer` in `UpdateEngine`'s phase-2 block is unguarded on purpose, since
every image-change path re-arms through `RestartSlideshowTimer`, which carries the guard itself; the
thumbnail rubber-band drag floats at the ends of the strip; and `F` / fullscreen does nothing when no
image is loaded, so a fresh launch with no file stays windowed. Typing in the zoom box during a
slideshow re-arms the 6 s interval, because `RestartSlideshowTimer()` is the first thing `WM_KEYDOWN`
does, ahead of the focused-textbox swallow - deliberate, so a slide cannot advance out from under an
edit. **`Esc` in fullscreen quits the program**, not just fullscreen (`InitFullScreenExit`), which is
what the "Mouse and keyboard commands" dialog promises (`"Exit program", "ESC"`). **Deleting the last
file in the folder also quits the program** - `DeleteCurrentImage` calls `InitFullScreenExit` when
`g_image.files` comes back empty.

Two decode limitations are documented in `Decoding.cpp` rather than fixed: only the **first frame of an
animated JXL** is decoded, and **AVIF `irot`/`imir` orientation is not applied**. EXIF orientations 5-8
apply the rotation only, with no mirror, because the render pipeline has no flip support.

**A slideshow never shows the desktop capture as its background** - it always draws the blurred version
of the current image (`CreateSlideshowBgBitmap`, section 3). So the fact that `CaptureDesktop` grabs the
monitor's *work area* while a slideshow overlay covers the whole *monitor* has no visible effect; do not
"fix" it by capturing `rcMonitor`.

---

### Smoke-test areas

These paths have been exercised on the real app, so a change that touches one of them needs a **fresh**
smoke test from the user - ask, don't assume. Re-ask for the specific interaction, not a general "does it
still work":

- Directory-watcher shutdown and folder switching; `Del` / `Shift+Del` / `Esc`-windowed / `F`; right-click
  metadata, Properties, and Copy image -> paste into Paint.
- Animated playback in every supported format, plus a fullscreen toggle and a resize mid-playback, a large
  file still decoding, and the slideshow prev-image cross-fade.
- Film strip: fill-in as a folder loads, click-to-jump (which also re-arms the slideshow timer), drag and
  coast, auto-centring, the strip after a fullscreen round trip, and the strip fading in over the
  corrupted-file error screen.
- Slideshow: start/stop by button and `F5`, prev/next while running, the pre-fade and the stop fade
  (fullscreen-to-fullscreen and windowed-to-windowed, taskbar from a slideshow, image re-fitted, no
  garbage flash at the resize), the cross-fade, the blurred background, and `Esc` mid-slideshow.
- Fullscreen lifecycle: entering and leaving by every path including double-click and `Esc`, the enter/exit
  dim, the exit fade, and opening a dialog or the right-click menu in fullscreen without dropping out.
- Vignettes: bars sliding in on zone entry, their height across a fullscreen entry, and the strip riding on
  the bottom bar's visibility.
- Widget behaviour: zoom in/out and the 1:1 button, pan-drag, the rotate buttons and the 90-degree snap,
  per-file view restore (open A, zoom, go to B, back to A), the typed-zoom readout (Enter commits, Esc
  cancels, clicking away), tooltips and their dwell delay, and every button's hold-repeat (including that
  Open / hamburger / slideshow do not auto-repeat).
- Launch paths: from the command line, by drag-drop, from a second instance (`WM_COPYDATA`), and the two
  persisted settings (`HQFilter`, `FitToScreen`) surviving a restart.
- Clean exit from both windowed and fullscreen (the container clearing in `WM_DESTROY`).

Nothing multi-monitor has been verified at all - the development machine has one screen.

## 15. Working rules and local tools

### Rules

- **Report before changing.** Findings (a comment that disagrees with the code, or a bug) get reported
  to the user for a keep/change decision **before** any edit is made. Comment fixes and code fixes are
  separate decisions.
- **Encoding.** UTF-8 **without BOM**, and **no non-ASCII byte in any `.cpp`/`.h`**. MSVC is not given
  `/utf-8`, so it reads sources in the system ANSI code page - which is why every typographic character
  in a *string literal* is a `\uXXXX` escape (see `Metadata.cpp`) and why a glyph such as the slideshow
  stop button is written `L"\u25A0"`. `_bytes.ps1` printing nothing is the check.
- **Comments** follow the standing rule at the top of this file. They also state what the code *does*:
  a banner that names the wrong function, the wrong window count, or a format the code no longer
  supports is a bug in the comment.
- **Verify a comment-only edit** with `_diffcheck.ps1` ("code parts identical") before committing it, so
  a comment sweep cannot quietly delete a line of code.
- **Build baseline.** `_build.ps1` (full `/t:Rebuild`, Release x64) must report **0 errors** and **0
  warnings**. It used to report four `LNK4286` warnings, all from `msvcprt.lib`; that library was
  dropped from the Release x64 `AdditionalDependencies` (it is the import lib for the DLL STL, and with
  `/MT` the static `libcpmt.lib` is what should be used). If `LNK4286` comes back, something has put
  `msvcprt.lib` back in the link. Note this did **not** remove the `MSVCP140.dll` import - see section
  13, that comes from `jxl.lib`. (The `Debug` configuration is
  broken by the prebuilt Release-CRT `jxl.lib`; Release x64 only.)
- **Keep tooling light** - the model runs on the user's machine and most of its RAM/VRAM is in use, and
  a full symbol enumeration (`SymEnumSymbols("*")`) is off the table. Never launch the app or drive the
  GUI without asking. Logic is verified with throwaway headless harnesses compiled against the real
  `.cpp` (confirm they fail before the fix, delete them afterwards); visual judgement is the user's.
- **Git.** All of this cycle's work is squashed into one commit on `master` (2 ahead of
  `origin/master`, unpushed) with safety branches `backup-before-squash-20261019` and
  `pre-squash-backup`; new fixes are amended into it rather than added as separate commits. Run `git`
  from the repo root (one directory up) - `git -C ..\.git` fails. `*.ps1`, `TODO.md`, `.continue/`,
  `.localpilot/` and `.vscode/` are gitignored; `*.pre` snapshots are **not**, so delete them and
  `git add` the files you touched by name rather than `-A`.

### Local tools (gitignored - a fresh clone will not have them)

| Tool | What it's for |
|---|---|
| `_status.ps1` | **warm start**: per-file lines/bytes/non-ASCII/BOM, the main file's fwd-decl block, a per-TU definition count, blank-line seams. Run it first in a session. |
| `_build.ps1` | MSBuild `/t:Rebuild` -> `build.log`, then `EXIT=`, the error/warning counts and every error/warning line. |
| `_grep.ps1` | content search in ONE file: `-Pat <regex>` or `-Names "a,b"`; `-Src <file>` (defaults to the main cpp, so pass `-Src CODE_MAP.md` for the docs). |
| `xgrep.ps1` | `_grep` across the whole directory: `-Pat <regex>`, `-Filter "*.cpp,*.h"` (default) or `"*.md"`. Prints `file line: text`. Also good for non-ASCII: `-Pat "[^\x00-\x7F]"`. |
| `delrange.ps1` | delete an inclusive 1-based line range: `-File F -Start N -End M` with `-MustStart`/`-MustEnd` regex guards so the range cannot drift onto the wrong lines. Preserves the file's line endings (some docs are LF, the sources CRLF). |
| `verify_code.ps1` | `-Module X -Ref Y` -> compares every function body **with comments stripped**, so a comment-only edit still reports `ALL BODIES CODE-IDENTICAL`. Copy `X` to `X.pre`, edit, compare, delete the `.pre`. |
| `_diffcheck.ps1` | `-Files "a,b"` -> runs `git diff -w -U0` and says whether the diff is comment-only. The cheap check for a small edit inside a big function. |
| `verify_move.ps1` | `-Module X -Ref Y` -> `IDENTICAL`/`DIFFERENT`/`MISSING` per body, comments included: the blast radius of a code edit. Caveat: an insertion near the top of a long function prints its whole shifted tail - prefer `_diffcheck.ps1`. |
| `_bytes.ps1` | every distinct non-ASCII run in the tree with its code points and files. **No output = the tree is pure ASCII.** |
| `_nonascii.ps1` | per-file bytes / non-ASCII / BOM. **Needs `-Files "a,b,c"`** - with no arguments it scans nothing and prints `TOTAL = 0`, which looks like a pass and proves nothing. |
| `_peek.ps1` | `-File F -Start N -End M` - a line range with every line's non-ASCII code points tagged, so you see the file's **real** line wraps. Use it to copy a doc anchor exactly. |
| `_exit_harness.ps1` | **crash harness for the exit paths**: launches the built exe in a `%TEMP%` sandbox and drives ~11 exit scenarios (windowed / fullscreen / failed load / animated mid-decode / slideshow / directory-change / delete-the-last-file) with `PostMessage(WM_CLOSE / Esc / F / Del / F5)` only - no mouse, no keyboard injection - then reports each process exit code and any Application-log WER entries. Clean = `0x00000000`; a crash shows as the exception code. A deliberate null write in `WM_DESTROY` was used to prove it detects them. **Launches real windows on the user's desktop - ask first.** |
| `_fullscreen_probe.ps1` | **state probe for the fullscreen lifecycle**: launches the exe, drives it with posted `WM_KEYDOWN(F)` / `WM_ACTIVATE(WA_INACTIVE, GetShellWindow())` / a posted `WM_LBUTTONDOWN`+`WM_MOUSEMOVE`+`WM_LBUTTONUP` drag only, then reads back the process's own windows - is `OverlayWindowClass` present, is the main window visible, is it `WS_EX_LAYERED` and at what alpha. P-A = failed load + focus loss (the invisible-window case), P-B = two `F` presses inside the stage-1 gap, P-C = pan the image flush against the right edge and exit (the only way to make the work-area clamp actually move the window). Note `WM_ACTIVATE` cannot reach the stage-1 gap (it needs `g_fullscreen.initDone`, which `CompleteExitFullscreen` clears). **Launches real windows - ask first.** |
| `split_helper.ps1` | `<mode>` is positional (`cat`, `catfn`, `bounds`, `map`, `slice`); the file is **`-Src`** (default: the main cpp), with `-StartLine`/`-EndLine` or `-Anchor`. The convenient way to read a range or a whole function of a big file. (`-File` is *not* a parameter - it errors out.) |

`_ascii.ps1`, `_narr.ps1`, `_crlf.ps1`, `_blanks.ps1`, `_id*.ps1`, `_splice*.ps1`, `_show.ps1`,
`_sweep.ps1`, `_struct.ps1`, `_status`-adjacent one-offs and `verify_move`'s `.bak` workflow are left
over from the split and have no current use.

### PowerShell gotchas, already paid for

Single-dash params only (`--Flag` is a syntax error); pass a list as **one string** (`-Files "a,b"` -
array literals do not survive `-File` argv binding); **`$` is stripped** from
`powershell -Command "..."` sent through the shell tool, so anything containing a variable must live in
a `.ps1` and run with `-File`; parameter names are case-insensitive, so a `-Files` parameter and a local
`$files` are the same variable and the local silently wins; give a full rebuild 60-120 s; `&&` is not a
statement separator in this PowerShell version, use `;`. `files__edit_file` is atomic - one bad anchor
aborts every edit in the call without applying any of them, so grep the exact wrapped line first, and
re-read the file after a multi-edit call to confirm it landed. A `read_text_file` of a markdown file
**re-wraps** long paragraphs, so what it prints is not the file's line breaks - take doc anchors from
`_peek.ps1` or `_grep.ps1` output, never from a read of the whole file.
