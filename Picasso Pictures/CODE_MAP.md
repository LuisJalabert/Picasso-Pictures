# CODE_MAP — `Picasso Pictures.cpp`

**Split complete (steps 0-8) and nothing further is being split.** `Picasso Pictures.cpp`
is now **~1,892 lines / ~67 kB / 12 top-level definitions** (it started at 9,216 lines /
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

The open-item list, working rules and local tool list live in a local-only `TODO.md` that is not in
the repo; section 14 is the public summary of what is known.

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
| `g_view.smooth = g_view.smooth * 60.0f / (float)refreshRate` | `wWinMain` | frame-rate-independent smoothing |
| `DWRITE_FONT_WEIGHT_SEMI_BOLD` + `0.023f` | `wWinMain` | text format sized from 2.3% of screen height |
| `while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))` | main loop | then `UpdateEngine(dt)`, `dt` clamped to 0.03s; `goto shutdown` on `WM_QUIT` |
| `MyRegisterClass` / `RegisterOverlayClass` / `RegisterBlackCoverClass` | 3 window classes | main = `g_app.windowClass`, overlay = `L"OverlayWindowClass"`, cover = `L"BlackCoverClass"` (plain `DefWindowProcW` + `BLACK_BRUSH`) |
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
| `TimerId::ENTER_FULL_SCREEN` | `EnterFullscreen(preserveView, needsDelay)` | **2-stage**: stage 1 makes main window `WS_EX_LAYERED` alpha 0 + 150 ms timer; stage 2 captures desktop, `DiscardDeviceResources`, creates overlay, re-uploads everything, and re-captures the vignette + thumbnail-strip anchors from the new overlay's render target (clearing `g_thumb.clipGeos` so the clip geometry is rebuilt at the new size) - stage 2 is the only place those anchors are set |
| `g_fullscreen.pendingExit` | `ExitFullscreen(bool immediate)` | starts fade; `UpdateEngine` drives `g_fullscreen.exitBgFadeAlpha` 0→1 |
| `CompleteExitFullscreen` | inner teardown | recomputes main-window rect from the *visible* image rect; min 800×600; clamps to work area; **correctly swaps W/H when rotated** — the reference for a rotation-aware hit test (see `IsPointOverImage`) |
| `InitFullScreenExit` | shrink-to-0.0005 + `g_app.isExiting = true` | used by Esc and by "no files left" |
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
| `DiscardDeviceResources` | resets every GPU object | **does not** touch `g_wic*` sources — that's how the desktop capture survives fullscreen↔slideshow |
| `Render` | the big one | see below |
| `DrawVignetteBar` / `DrawCoverBg` | gradient bar / cover-stretch blit | |

### `Render` control flow
1. Bail if `hWnd != activeWindow` (protects the hidden window from stray `WM_PAINT`).
2. Lazy re-creates: image bitmap, GIF frame 0 after device loss, default bg, desktop bg, slideshow bg, **one** thumbnail per frame spiralling out from current.
3. `BeginDraw` → clear (fullscreen `0,0,0,0.6`; windowed `0.4` grey) → `g_fullscreen.needsInit` → `InitializeImageLayout`.
4. Background: slideshow blurred pair / desktop capture + dim / leftover blurred bg. During the slideshow stop fade's black-in phase the blurred wash is held (`holdBlurred`), so the swap to the desktop capture lands at full black together with the overlay resize.
5. Image: prev-frame cross-fade, then transform `scale*translate` then `rotation` about the transformed centre; shadow via `CLSID_D2D1Shadow` on the **small** bitmap with an extra scale; `useD3D11Image = g_pipeline.ready && g_pipeline.srv && !g_settings.useHQFilter`.
6. Load-error screen ("This file is corrupted…") using `g_image.lastLoadError` + `g_image.loadAttemptName`.
7. **Three passes** when D3D11 is used: D2D(bg+shadow+prefade)→`EndDraw` → D3D11 quad → D2D(vignettes, zoom text, thumbs, buttons, textboxes)→`EndDraw`→`Present(1,0)`. **Single pass** otherwise, deliberately, to avoid splitting a frame with no D3D11 work between them.
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
| `g_slideshow.preFadeAlpha += SLIDESHOW_FADE_STEP` | phase 1 fade-to-black → phase 2 cover window + immediate exit + new overlay + slideshow timer |
| `g_slideshow.stopFadeAlpha +=/-= SLIDESHOW_FADE_STEP` | slideshow → fullscreen stop fade: up to 1, `ResizeOverlayToWorkArea()` at full black, back to 0. If `g_app.isExiting` or `g_fullscreen.pendingExit` is already set the pending resize is dropped and the black just fades out |
| `g_fullscreen.exitBgFadeAlpha += 0.04f` | fullscreen-exit fade → `CompleteExitFullscreen()` |
| `g_slideshow.transitionAlpha` | image↔image cross-fade, linear + exponential tail |
| `g_thumb.readyQueue` | drains **one** thumbnail upload per frame under `g_thumb.readyMutex` |
| `g_thumb.targetOffset = -(g_image.index * (g_thumb.width + g_thumb.gap))` | auto-centring, skipped while dragging/free-scroll |
| `g_view.zoom += (g_view.targetZoom - g_view.zoom) * g_view.smooth` | the spring for zoom/pan |
| `VIGN_FALLOFF = 0.08f` | vignette fade |
| `if (g_app.isExiting && ... g_fullscreen.overlayAlpha < 0.01f)` | destroys both windows + `PostQuitMessage` |

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
| `StrCmpLogicalW` | `BuildImageList` — Explorer-identical natural sort; snapshots thumbs **by path** before index shift. **Now in `Thumbnails.cpp`**, which therefore needs `<shlwapi.h>` + `Shlwapi.lib` |

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
| `StartThumbnailLoader` | spiral order from current index; fast path = embedded JPEG thumbnail if short side ≥ 180; slow path = full decode + HighQualityCubic to `TARGET_H = 180`; EXIF applied via `IWICBitmapFlipRotator` on the small bitmap |
| `StopThumbnailLoader` | sets `g_thumb.loaderStop`, joins |
| `DrawThumbnailStrip` | one cached origin-centred rounded-rect geometry reused via RT transform; centre-crop to square slot; non-current darkened 50%; placeholder brush while loading. Its only caller is `Render` in `Rendering.cpp`, which includes `Thumbnails.h`. |
| `StartDirectoryWatcher` / `ReadDirectoryChangesW` | non-recursive, `FILE_NOTIFY_CHANGE_FILE_NAME`. The handle, the thread and the stop event are `static` in `Thumbnails.cpp`. Shutdown = set the stop event, join, then close both handles; the thread waits on that event next to the read's own event and, on the stop path, cancels its outstanding read and waits for the cancellation, so the handle is never closed under a live request. Posts `WM_APP_DIRCHANGE` |
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
| `InitializeMenuButtons` | Open (📂) + hamburger (☰) — top-left zone, always visible, `SetForcedVisibility(true)` |
| hamburger: `HWND ownerWnd = (g_view.isFullscreen && g_app.overlayWindow...` | the long comment explains why hardcoding `g_app.mainWindow` used to break fullscreen; wrapped in `FullscreenExitSuppressor` because `TrackPopupMenu`'s menu window isn't ours |
| `InitializeButtons` | 9 more: 1:1, ▶ slideshow, ➕/➖ zoom, ⭯/⭮ rotate, ⮜/⮞ prev/next, ❌ exit (top-right zone, hidden when windowed). The slideshow button's glyph is the play triangle or a pair of heavy vertical bars (`kSlideshowStopGlyph` - `U+23F8` is unusable, it renders as a color emoji), re-set by `UpdateSlideshowButton` at every slideshow transition and at the end of this function. All use `ActivationZone` + normalized anchors + `uiPixelScale = min(w,h)` |
| `ButtonId::PREVIOUS` / `ButtonId::NEXT` lambdas | both call `RestartSlideshowTimer()` after navigating (the guard is inside the helper) |
| `InitializeImageInfoLabel` | `TextBoxId::FILE_NAME` (bottom, display-only) + `TextBoxId::ZOOM_INPUT` (centre, `InputMode::NumericFloat`, clamped 1–10000 then `/100`) |
| `InitializeImageLayout` | opens at 100% unless oversized or `g_settings.openFitToScreen`, then 95% fit; `hard=false` starts at `g_view.zoom = 0.05f` and springs in; early-outs on `g_view.restoredStateThisLoad`. Called from `Render` (`Rendering.cpp`), `WM_SIZE` and `DeleteCurrentImage` — `Rendering.cpp` includes `UI.h`. |
| `SetZoomCentered` / `FitToWindowRelative` / `ZoomIntoImage` / `MakeZoomVisible` | `ZoomIntoImage` = `pow(1.1, delta/120)`, clamped 0.01–100, cursor-anchored when over the image; `optionalPt` arrives in screen coords (`GetCursorPos`) and is converted to client coords exactly once |
| `g_textBoxes[TextBoxId::ZOOM_INPUT].IsFocused()` guard | in `Render`, so typing isn't overwritten |

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
| `AssociateFileTypes(HWND)` | `MenuCommandId::ASSOCIATE` in `WndProc`'s `WM_COMMAND`. Asks first with a `TASKDIALOGCONFIG` / `TaskDialogIndirect` (owned by the main window, so `IsOwnedByOurWindow()` recognizes it) and returns **without touching anything** unless the answer is `IDOK`. Then `GetModuleFileNameW` -> ProgID `PicassoPictures.ImageFile` under `HKCU\Software\Classes`, its `shell\open\command` value (`"<exe>" "%1"`, built with `swprintf_s`), the friendly name, `Software\PicassoPictures\Capabilities` + `Software\RegisteredApplications`, one `Software\Classes\<ext>` -> ProgID per extension (.jpg .jpeg .png .bmp .gif .tif .tiff .webp .avif .jxl) plus a `FileAssociations` value per extension, then `SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, ...)`. A second `TaskDialog` reports whether every extension took. The `RegSetStr` lambda is local to the function. **The only place in the project that writes those keys.** |
| `About` | **ONE `DLGPROC` for TWO templates**: `IDD_ABOUTBOX` (`IDM_ABOUT`, `MenuCommandId::ABOUT`) and `IDD_COMMANDBOX` = "Keyboard shortcuts" (`ID_HELP_COMMANDS`, `MenuCommandId::SHORTCUTS`). Four `DialogBox` sites in `WndProc`; there is no other shortcuts dialog anywhere in the project. `WM_INITDIALOG` returns TRUE; `WM_COMMAND` maps `IDOK`/`IDCANCEL` to `EndDialog`; `WM_NOTIFY` matches `IDC_SYSLINK_GITHUB` + `NM_CLICK` and `ShellExecuteW`s `pLink->item.szUrl`, so the GitHub URL lives in the dialog template (`Picasso Pictures.rc`), not in code. Modal, so the main window is insensitive while it is up. |
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
| `WM_COMMAND` | `IDM_OPEN`, `IDM_ABOUT`, `IDM_EXIT`, `ID_HELP_COMMANDS`/`MenuCommandId::SHORTCUTS`, `MenuCommandId::HQ_FILTER`, `MenuCommandId::FIT_TO_SCREEN`, `MenuCommandId::ASSOCIATE`, and 6 context-menu IDs (copy→`CF_DIB`, delete, open folder, properties, metadata, wallpaper) |
| `WM_PAINT` | `BeginPaint` → `Render` → `EndPaint` |
| `WM_SIZE` | unbind target + `g_devices.swapRTV`, `ResizeBuffers`, re-create both, `UpdateLayout` on all UI, re-centre |
| `WM_RBUTTONUP` | 6-item popup, wrapped in `FullscreenExitSuppressor` |
| `WM_MOUSEWHEEL` | `ZoomIntoImage(delta, &pt)` + `MakeZoomVisible` |
| `WM_LBUTTONDOWN` | buttons → textboxes → **thumbnail strip row claimed whole** → else image pan + `SetCapture`. A button claims the press on the down - its callback runs in `OnMouseDown`, not on the release - and sets `g_buttonClaimedClick` (worth knowing: `g_view.mouseFromDown` is written only in that pan branch, so after a button press it still holds the previous drag's point, which is why the explicit claim - not `wasReallyDragging` - is what keeps a button click from also leaving fullscreen) |
| `WM_LBUTTONUP` | resolve strip drag (click <5 px vs momentum; a click that opens another image calls `RestartSlideshowTimer`), then `!overImage && !wasReallyDragging && !buttonClaimed` → `ExitFullscreen` ⚠️ **(item 7 - closed)**; `TimerId::KILL_ROTATION` 120 ms snaps partial rotations to the nearest 90° |
| `WM_MOUSEMOVE` | button/textbox proximity, vignette activation zones, `stripCoasting` keeps the strip visible mid-fling, strip drag + EMA velocity, image pan |
| `WM_LBUTTONDBLCLK` | no image → open; windowed+over image → fullscreen preserving view; fullscreen → toggle 1:1 ↔ 96% fit |
| `WM_TIMER` | 4 IDs: enter-fullscreen stage 2, kill-rotation, zoom display hide, slideshow advance (only if `g_slideshow.transitionAlpha > 0.8f`) |
| `WM_CHAR` | forward to textboxes |
| `WM_KEYDOWN` | focused textbox swallows everything; else `Del`/`Shift+Del`, `Esc`, `F` toggle fullscreen (auto-repeat ignored via `lParam & (1<<30)`), `O`, `W`/`↑`, `S`/`↓`, `A`/`←`, `D`/`→`/`Space` (both gated by `g_fullscreen.navPendingRender`), `Q`/`E` rotate, `F5` slideshow |
| `WM_APP_DIRCHANGE` | `BuildImageList` again; if the current file vanished, clamp to index 0 |
| `WM_DESTROY` | main window only: destroy overlay, stop all 3 threads, discard resources, reset all `g_wic*`/factories, `CoUninitialize`, `PostQuitMessage` |

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
(`g_background.bitmap`) are structs with one instance each; everything
`FullscreenState`: it runs for any fullscreen exit (the ramp is gated on
`g_view.isFullscreen`, not on slideshow state), so it is fullscreen state rather
than slideshow state. The ID enums and the `constexpr` slideshow timings are not
state either, and neither are the watcher's `static` handle / thread / stop
event in `Thumbnails.cpp`.

- **Identity/lifetime (`AppIdentity`, one instance `g_app`):** `instance`, `title`, `windowClass`, `mainWindow`, `overlayWindow`, `blackCoverWindow`, `metadataViewerWnd`, `isExiting`, `launchedWithFile`
- **Factories/devices (`DeviceResources`, one instance `g_devices`):** `d2dFactory`, `wicFactory`, `dwriteFactory`, `textFormat`, `d3dDevice`/`d3dContext`, `d2dDevice`, `renderTarget`, `swapChain`, `d2dTargetBitmap`, `swapRTV`, `renderTargetWindow`
- **View state (`ViewState`, one instance `g_view`):** `zoom`/`targetZoom`, `offsetX/Y`/`targetOffsetX/Y`, `rotationAngle`/`targetRotationAngle`/`rotationSpeed`, `exifRotation`, `smooth`, `stateByPath` (path → `ImageViewState`), `restoredStateThisLoad`, `isDragging`, `lastMouse`, `mouseFromDown`, `bitmap`, `isFullscreen`, `uiPixelScale`
- **Image (`ImageState`, one instance `g_image`):** `source` (the WIC source), `files`, `index`, `path`, `fileName`, `lastLoadError`, `loadAttemptName`, `showLoadError`, `width`, `height`. Its GPU upload `g_view.bitmap` stays in ViewState
- **Animation (`AnimState`, one instance `g_anim`):** `frames` (WIC), `gpuFrames` (GPU), `frameDelays`, `currentFrame`, `isAnimated`, `lastFrameTime`, `decodeThread`, `decodeStop`, `framesReadyUpTo`
- **Image pipeline (`RenderPipeline`, one instance `g_pipeline`):** `dimBrush`, `blackBrush`, `mipTex`, `srv`, `sampler`, `vs`/`ps`/`il`, `vb`/`cb`, `blend`/`rast`/`ds`, `ready`
- **Settings (`AppSettings`, one instance `g_settings`):** `useHQFilter`, `openFitToScreen` - the two values persisted under `Software\PicassoPictures`, read in `wWinMain` and toggled from the hamburger menu
- **Fullscreen (`FullscreenState`, one instance `g_fullscreen`):** `needsInit`, `initDone`, `overlayAlpha`, `navPendingRender`, `pendingPreserveView`, `suppressExitDepth` (a refcount, touched only through `FullscreenExitSuppressor`), plus the exit-fade trio `pendingExit`, `wasInSlideshowOnExit`, `exitBgFadeAlpha`. `g_view.isFullscreen` is the flag itself, in ViewState
- **Slideshow (`SlideshowState`, one instance `g_slideshow`):** `active`, `fromFullscreen`, `preFade`/`preFadeAlpha`, `stopFade`/`stopFadePendingResize`/`stopFadeAlpha`, `transitionAlpha`/`targetAlpha`, `prevBitmap`, `prevBgBitmap`, `bgBitmap`, `prevZoom`/`prevOffsetX`/`prevOffsetY`/`prevRotation`, `blurEffect`. `SLIDESHOW_INTERVAL_MS = 6000` and `SLIDESHOW_FADE_STEP = 0.05f` are `constexpr`, not state
- **Vignettes (`VignetteState`, one instance `g_vignette`):** `topVisibility`/`topTarget`, `bottomVisibility`/`bottomTarget`, `topBarHeightPx`/`bottomBarHeightPx`, `topActivationPx`/`bottomActivationPx`, `heightsCaptured`
- **Thumbnails (`ThumbState`, one instance `g_thumb`):** `entries` (`ThumbnailEntry{wic,d2d}`), `readyQueue` + `readyMutex`, `loaderThread`/`loaderStop`, `scrollOffset`/`targetOffset`, `width`/`height`/`gap`, `stripInsetFromBottomPx`, `sizeCaptured`, `clipGeos`, and the drag set: `dragging`, `dragStartX`, `dragLastX`, `velocity`, `freeScroll`, `dragHitIndex`
- **Backgrounds (`BackgroundArt`, one instance `g_background`):** `bitmap`/`wicBitmap` (desktop capture), `defaultBitmap`/`wicDefaultBitmap` (embedded JPG resource `IDR_DEFAULT_BKG_JPG`), `shadowEffect`, `shadowSourceBitmap` (the two solid brushes are in `RenderPipeline`)
- **Timers:** `TimerId::SLIDESHOW 0xcafe`, `TimerId::ZOOM_DISPLAY 0xbebe`, `TimerId::KILL_ROTATION 0xbeca`, `TimerId::ENTER_FULL_SCREEN 0xcaca`
- **Messages:** `WM_APP_DIRCHANGE (WM_APP+1)`, `WM_APP_EXITFULLSCREEN (WM_APP+2)`

## 13. Third-party / build notes

- `vcpkg install libavif dav1d libjxl`, static triplets (`x64-windows-static`), so no extra DLLs ship.
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
  change while the app runs is deliberately not implemented.
- **Cancelling a slideshow inside the 150 ms fullscreen stage-1 gap.** `Esc` or `F5` in that window
  clears the pre-fade but leaves the mode on, so the app fades to black and comes back up in slideshow
  mode. A fix was attempted and reverted: the flags available in `ExitSlideshowMode` cannot tell a
  stage-1 cancel from the late call that `CompleteExitFullscreen` makes, and the attempt regressed the
  ordinary stop fade.

Behaviour that looks wrong but is deliberate: Shift+Delete deletes permanently with no confirmation
(fast bulk culling); the slideshow blur keeps `D2D1_BORDER_MODE_HARD` (`SOFT` was tried and darkens two
screen edges); the slideshow `SetTimer` in `UpdateEngine`'s phase-2 block is unguarded on purpose, since
every image-change path re-arms through `RestartSlideshowTimer`, which carries the guard itself; the
thumbnail rubber-band drag floats at the ends of the strip; and `F` / fullscreen does nothing when no
image is loaded, so a fresh launch with no file stays windowed.
