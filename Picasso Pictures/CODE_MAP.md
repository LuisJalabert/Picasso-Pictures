# CODE_MAP - `Picasso Pictures`

**Read this instead of reading a module end-to-end**, then read only the function you need. For
anything mid-file use `split_helper.ps1 cat`/`catfn` through the shell, never `read_text_file`
(it has no offset). `Picasso Pictures.cpp` is **2,032 lines / ~78 kB / 13 top-level definitions**
and everything in it is app plumbing: `GetInitialFileFromCommandLine`, `wWinMain`,
`MyRegisterClass`, `EnableDarkTitleBar`, `InitInstance`, `UpdateEngine`, `IsSupportedImage`, the
four navigation functions (`OpenImageFile`, `OpenNextImage`, `OpenPrevImage`, `DeleteCurrentImage`)
and `WndProc`. A full read of it costs ~50k tokens.

The *provenance* of this tree - how the modules were split out of the main file, which linkage
fixes and default-argument moves each move needed, what `verify_move` reported, and the one-off
scripts from that work - is in **MAP_HISTORY.md**. None of it is needed to change the code and it
should not be read at session start. Section numbering here matches the old map, so references
like "CODE_MAP section 7" still work.

> **Comment rule (standing):** a comment describes **what the code does, how it works, or what it
> should do** - never *which refactor step moved which file*. The sources are clean of that and
> pure ASCII, so keep it that way: no split banners, no "moved verbatim", no step numbers, no
> non-ASCII byte in a `.cpp`/`.h`. History belongs in this file, in MAP_HISTORY.md and in the git
> log, not in the code.

> Line numbers are omitted on purpose - they drift on the first edit. Every entry has a
> **unique grep anchor**. `_grep.ps1 -Pat "anchor"` (or `findstr /n`) gives you the real line
> right now, and works across all the module files. (`*.ps1` helpers are local-only and
> gitignored, so a fresh clone will not have them.)

---

## Layout at a glance

| Module | What's there |
|---|---|
| `Globals.h` / `Globals.cpp` | the ~100 globals + `ImageViewState` + `ThumbnailEntry` + `FullscreenExitSuppressor` + `IsOwnedByOurWindow` + `MAX_LOADSTRING` + control/command/timer IDs + `WM_APP_*`. `extern` decls in the header, one definition site in the `.cpp`. Include `Globals.h` and you get the whole set. |
| `ViewMath.h/.cpp` | `GetClientSizeF` (private), `PanFromOffsets`, `OffsetsFromPan`, `GetMonitorRefreshRate`, `IsPointOverImage`. No calls into any other module; the hit test is the one function here that reads the view-state globals. |
| `Fullscreen.h/.cpp` | overlay + black-cover classes/windows, `CreateOverlayWindow`, `CaptureDesktop`, `Enter/ExitSlideshowMode`, `EnterFullscreen`, `CompleteExitFullscreen`, `ExitFullscreen`, `InitFullScreenExit`. |
| `Rendering.h/.cpp` | all device-resource creation/destruction, the D3D11 mip pipeline incl. the embedded HLSL, `RecreateImageBitmap`, `CreateSlideshowBgBitmap`, `DrawVignetteBar`/`DrawCoverBg`, and `Render`. |
| `Decoding.h/.cpp` | `LoadImageD2D`, `FinishImageLoad`, `LoadFail`, the AVIF / JXL / GIF / WebP / APNG decoders, the shared compositing primitives (in an anonymous namespace), the animated-playback glue and the frame plumbing. |
| `Thumbnails.h/.cpp` | `BuildImageList` (the Explorer-identical file list), `Start/StopThumbnailLoader`, `DrawThumbnailStrip`, `Start/StopDirectoryWatcher`. `IsSupportedImage` lives in the main file. |
| `UI.h/.cpp` | the three widget initialisers (menu buttons, the 9 in-image buttons, the two text boxes), `InitializeImageLayout`, the zoom/fit commands (`SetZoomCentered`, `FitToWindowRelative`, `UpdateTargetZoom`, `ZoomIntoImage`, `MakeZoomVisible`). The navigation functions live in the main file. |
| `Metadata.h/.cpp` | the EXIF / PNG-tEXt / WebP-XMP / JXL-box / AVIF-XMP+Exif readers, the mini-XML helpers, the size/date/format formatters, `ShowImageMetadata`, the metadata viewer window and the in-app Properties dialog. Most functions are `static` and stay out of the header, as do both POD structs. |
| `Dialogs.h/.cpp` | `AssociateFileTypes` (HKCU file-association writer) and `About`, the single dialog proc behind BOTH `IDD_ABOUTBOX` and `IDD_COMMANDBOX`. The header is exactly those two declarations. |
| main file | includes, `#pragma comment` libs, 13 forward declarations (two of them a pre-existing duplicate `GetInitialFileFromCommandLine`), `wWinMain`, `UpdateEngine`, `IsSupportedImage`, the navigation functions, `WndProc` (the last function in the file). |

### Module include graph (`Metadata.h` and `Dialogs.h` are leaves - they include only `Globals.h`, plus `Decoding.h` for Metadata)

```
Globals.h  --+-- ViewMath.h
             |-- Fullscreen.h --+
             |-- Rendering.h --+-- each .cpp
             |-- Decoding.h ---+
             |-- Thumbnails.h -+
             +-- UI.h ---------+
```

Headers are declarations-only, so they may be included in any order and cycles are impossible.
`Fullscreen.cpp` calls into Rendering (`DiscardDeviceResources`, `CreateRenderTarget`,
`CreateBackgroundBitmap`, `RecreateImageBitmap`) and `Rendering.cpp` calls back into Fullscreen
(`DestroyBlackCoverWindow`) - genuinely bidirectional at module level, safe only because the
headers declare rather than define. `Decoding.cpp` calls Rendering and ViewMath; `Metadata.cpp`'s
AVIF readers call back into Decoding (`AvifOpenAndParse` only), which is why `AvifOpenAndParse`,
`DecodeAvifToWicBitmap` and `DecodeJxlToWicBitmap` are exported. `Thumbnails.cpp` also calls back
into Decoding (AVIF/JXL thumbnails have no WIC path) and is called by `Render` (`DrawThumbnailStrip`),
which is why `Rendering.cpp` includes `Thumbnails.h`. `UI.cpp` has the **most edges out**: it calls
`GetMonitorRefreshRate`, `EnterSlideshowMode` and the three navigation functions, and is called back
by `Render`, by `Fullscreen.cpp` (`InitializeButtons`, `InitializeImageInfoLabel`,
`UpdateTargetZoom`) and by `Decoding.cpp` (`UpdateTargetZoom`).

**Bridge blocks:** four `.cpp` files carry a small permanent block of declarations for functions
that stay in the main file forever - `Fullscreen.cpp` -> `WndProc`; `Thumbnails.cpp` ->
`IsSupportedImage`; `UI.cpp` -> `OpenImageFile` / `OpenNextImage` / `OpenPrevImage`;
`Metadata.cpp` -> `EnableDarkTitleBar`. Those are the **only** bridge lines in the tree
(`Dialogs.cpp`, `ViewMath.cpp`, `Rendering.cpp`, `Decoding.cpp` hold only their own file-local
declarations). Any bridge line beyond those four is a bug.

---

## 1. Bootstrap

| Anchor | Function | Notes |
|---|---|---|
| `GetInitialFileFromCommandLine` | static | `CommandLineToArgvW`, returns `argv[1]` |
| `Local\\PicassoPictures_SingleInstance_v1` | `wWinMain` | named mutex -> `FindWindowW` -> `WM_COPYDATA` (`dwData==1`, wide path) -> `AttachThreadInput` + `SetForegroundWindow` -> `return 0` |
| `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` | `wWinMain` | set before any window creation |
| `Software\\PicassoPictures` + `HQFilter` / `FitToScreen` | `wWinMain` | restores `g_settings.useHQFilter`, `g_settings.openFitToScreen` |
| `g_view.smooth = g_view.smooth * 60.0f / (float)refreshRate` | `wWinMain` | frame-rate-independent smoothing. **Note:** also re-set absolutely in `Fullscreen.cpp` (`2*0.18f * 60/rate`) and `UI.cpp` (`0.18f * 60/rate`) - three sites, different bases, and this one multiplies rather than assigns, so it must stay a once-per-process line. `SmoothAlpha` (below) is the newer, dt-based way to express the same idea |
| **Timing convention: `SmoothAlpha(perFrameAt60Hz, dt)`** (`ViewMath.h/.cpp`) | `UIElement::UpdateVisibility`, `AnimatedButton::Update`, the vignette fade in `UpdateEngine` | Every UI smoothing factor is written as "the fraction one 60 Hz frame consumes" and rescaled by the real frame time, so fades and the tooltip dwell take the same wall-clock time at any refresh rate. `UpdateVisibility(dt, falloff)` deliberately has **no default argument** - a caller that forgets `dt` must fail to compile. Matters more than it looks: the main loop has no sleep or timer, so `UpdateEngine`'s call rate is only paced by `Present(1,0)` blocking on vsync inside `WM_PAINT` - with no paint pending the loop free-runs and per-frame timing drifts arbitrarily |
| `DWRITE_FONT_WEIGHT_SEMI_BOLD` + `0.023f` | `wWinMain` | text format sized from 2.3% of screen height |
| `while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))` | main loop | then `UpdateEngine(dt)`, `dt` clamped to 0.03s; `goto shutdown` on `WM_QUIT` |
| `MyRegisterClass` / `RegisterOverlayClass` / `RegisterBlackCoverClass` | 3 window classes | main = `g_app.windowClass`, overlay = `L"OverlayWindowClass"`, cover = `L"BlackCoverClass"` (plain `DefWindowProcW` + `BLACK_BRUSH`). **The overlay class's `lpfnWndProc` is the same `WndProc` as the main window's**, which is why `hWnd` is the overlay while fullscreen and why a timer set from a message handler (`KILL_ROTATION`) still reaches the `WM_TIMER` branch. `MyRegisterClass` sets `lpszMenuName` but `wWinMain` immediately calls `SetMenu(g_app.mainWindow, nullptr)`, so the `.rc` menu is never shown |
| `InitInstance` | creates main window | height = 50% of screen, width from 1536/1024 aspect; centred; `DragAcceptFiles`; `SW_HIDE` if launched with a file. Sets `g_view.uiPixelScale` from the primary monitor before any widget exists |
| `EnableDarkTitleBar` | dark mode | `DWMWA_USE_IMMERSIVE_DARK_MODE` + **undocumented uxtheme ordinals 135/133/136** loaded via `MAKEINTRESOURCEA` |

## 2. View-state math

| Anchor | Function |
|---|---|
| `GetClientSizeF` | client rect -> floats |
| `PanFromOffsets` | offsets -> pan (image centre minus window centre); `pan==0` => centred at any window size |
| `OffsetsFromPan` | inverse; used when the render target changes size |
| `IsPointOverImage` | the image hit test: undoes Render's rotate-about-centre, then tests the unrotated rect. Called by `WM_LBUTTONUP`, `WM_LBUTTONDBLCLK` and `ZoomIntoImage` |
| `GetMonitorRefreshRate` | `EnumDisplaySettings`, falls back to 60 |

---

## 3. Fullscreen & slideshow lifecycle

| Anchor | Function / state | Notes |
|---|---|---|
| `IsOwnedByOurWindow` | walks the `GW_OWNER` chain, depth <= 8 | in **`Globals.h`**; distinguishes *our* dialogs from real focus loss in `WM_ACTIVATE` |
| `struct FullscreenExitSuppressor` | RAII, `++/--g_fullscreen.suppressExitDepth` | in **`Globals.h`**; **never** touch the counter directly |
| `TimerId::ENTER_FULL_SCREEN` | `EnterFullscreen(preserveView, needsDelay)` | **2-stage**: stage 1 makes the main window `WS_EX_LAYERED` alpha 0 + 150 ms timer; stage 2 captures the desktop, `DiscardDeviceResources`, creates the overlay, re-uploads everything and re-captures the vignette + thumbnail-strip anchors from the new overlay's render target (clearing `g_thumb.clipGeos` so the clip geometry is rebuilt at the new size) - stage 2 is the only place those anchors are set. **What can reach the 150 ms gap:** `F` (a second press sees `isFullscreen` already true and starts the exit) and a click outside the image - the main window is transparent but still takes input. `WM_ACTIVATE` cannot, because it requires `g_fullscreen.initDone`, which `CompleteExitFullscreen` clears. Either way `CompleteExitFullscreen` kills the timer and the `WM_TIMER` branch re-checks the flags, so stage 2 no longer runs after an exit has begun |
| `g_fullscreen.pendingExit` | `ExitFullscreen(bool immediate)` | starts the fade; `UpdateEngine` drives `g_fullscreen.exitBgFadeAlpha` 0->1 |
| `CompleteExitFullscreen` | inner teardown | **cancels a pending fullscreen stage-1 timer first** (an exit inside the 150 ms gap used to be followed by stage 2 doing a full device teardown + rebuild and flashing an overlay this function then destroyed). Then recomputes the main-window rect from the *visible* image rect; min 800x600; clamps to the work area; **correctly swaps W/H when rotated** - the reference for a rotation-aware hit test (compare `IsPointOverImage`). Runs in full even with no bitmap (failed load) or no overlay (stage 1): both fall back to the **size and centring `InitInstance` opens with**, because the main window is `WS_EX_LAYERED` at alpha 0 by then and an early return leaves the app running but invisible. The work-area clamp happens **once**, with `preClampWinX/Y` captured before it, so `clampShiftX/Y` are the real distance the window was pushed and the image offsets are corrected by that amount - a second, post-`SetWindowPos` clamp could only ever measure 0, which dropped the correction and left the image a frame-width off its fullscreen screen position (measured `shiftX=-15` vs `0` on a right-panned image at 4K) |
| `InitFullScreenExit` | shrink-to-0.0005 + `g_app.isExiting = true` | quits the **whole program**, not just fullscreen. Callers: `Esc` while fullscreen, the exit button, deleting the last file in the folder, and `WM_APP_DIRCHANGE` finding an empty file list. With no bitmap it only sets the flag |
| `CaptureDesktop` | `BitBlt` of `rcWork` -> `g_background.wicBitmap` | skipped when entering slideshow (the cover window is up) |
| `CreateBlackCoverWindow` + `DwmFlush()` | hides D3D device recreation | `DwmFlush` is load-bearing: without it the cover isn't on screen before the target dies |
| `g_slideshow.preFade` | `EnterSlideshowMode` | only sets the flag; the state machine in `UpdateEngine` does the rest. The play button's lambda and `F5` both toggle between `EnterSlideshowMode` and `ExitSlideshowMode`; `g_slideshow.fromFullscreen` (captured on entry) decides whether stopping starts the stop fade or the fullscreen exit fade |
| `g_slideshow.stopFade`/`stopFadeAlpha` + `ResizeOverlayToWorkArea` | stop fade | mirror of the pre-fade: black in, the live overlay moves from the monitor rect to the work area at full black, black out. `ExitSlideshowMode` only starts it; `UpdateEngine` drives it and calls the resize |
| `CreateSlideshowBgBitmap` | mip-extract (~200 px) -> `CLSID_D2D1GaussianBlur` | swaps dims for 90/270 EXIF rotation; CPU WIC fallback if no mip chain. Lives in `Rendering.cpp` (it is a mip-extract + effect, not lifecycle), declared in `Rendering.h`, called only by `Render` |
| `RestartSlideshowTimer` | `Fullscreen.cpp` | re-arms `TimerId::SLIDESHOW` for a full interval; no-op unless `g_slideshow.active`. Called by every path that changes image during a slideshow: prev/next lambdas, `WM_KEYDOWN`, and the thumbnail-click branch of `WM_LBUTTONUP`. Not used by slideshow entry - the comment above the unguarded `SetTimer` in `UpdateEngine`'s phase-2 block says why |
| `g_fullscreen.wasInSlideshowOnExit` | exit cross-fade | blurred bg -> desktop bg |

## 4. D3D11 / D2D rendering (`Rendering.h` declarations, `Rendering.cpp` definitions)

`SwapChainBitmapProps` is non-static because `WM_SIZE` calls it. `Mat4Mul`, `BuildImageToClip`,
`ExtractMipAsD2DBitmap`, `RenderImageD3D11`, `CreateSmallShadowSourceViaWic`, `DrawVignetteBar`,
`DrawCoverBg`, the embedded HLSL and the `ImageVertex` / `CBImageTransform` PODs are
`Rendering.cpp`-private and are **not** in the header. Only `WM_SIZE` and `WM_PAINT` in `WndProc`
call `CreateRenderTarget` and `Render`.

| Anchor | Notes |
|---|---|
| `D3D11_CREATE_DEVICE_BGRA_SUPPORT` | `CreateRenderTarget`: device -> DXGI -> `ID2D1Device` -> device context -> flip-model swap chain (`FLIP_SEQUENTIAL`, 2 buffers) -> target bitmap -> shadow/blur effects -> cached brushes -> swap-chain RTV |
| `SwapChainBitmapProps` | `TARGET \| CANNOT_DRAW`, `B8G8R8A8`, premultiplied; shared by create + resize |
| `g_imageVSHLSL` / `g_imagePSHLSL` | embedded HLSL, `vs_4_0`/`ps_4_0`, compiled at runtime with `D3DCompile` |
| `struct alignas(16) CBImageTransform` | row-major 4x4 + opacity + pad |
| `Mat4Mul` / `BuildImageToClip` | image-space -> NDC: S(scale/translate) -> R(rotation about pivot) -> N(screen->NDC) |
| `CreateD3DImagePipeline` | device-once objects: dynamic VB (4 verts) + CB, trilinear sampler, premultiplied blend, no cull, no depth |
| `D3D11_RESOURCE_MISC_GENERATE_MIPS` | `CreateMipTextureFromSource`: clamps to 16384, WIC HighQualityCubic downscale, upload mip 0, `GenerateMips` on GPU |
| `ExtractMipAsD2DBitmap` | staging readback of one mip; used for the slideshow bg (200 px) and the shadow source (256 px) |
| `RenderImageD3D11` | `TRIANGLESTRIP`, `Draw(4,0)`; must run **between** D2D `EndDraw`/`BeginDraw`; unbinds SRV+RTV afterwards |
| `CreateSmallShadowSourceViaWic` | CPU downscale for animated formats, which skip the mip chain on purpose |
| `RecreateImageBitmap` | WIC -> `ID2D1Bitmap` (+ mip chain if not animated); 16384 clamp; sets `g_background.shadowSourceBitmap` |
| `DiscardDeviceResources` | resets every GPU object; **does not** touch `g_wic*` sources - that is how the desktop capture survives fullscreen<->slideshow. Also resets every cached brush in `g_pipeline` (`dimBrush`, `blackBrush`, the two error-text brushes, both vignette gradient brushes **with their cache keys**) so nothing can outlive the context that made it |
| **Cached D2D objects in `g_pipeline`** | `dimBrush`, `blackBrush`, `errorTitleBrush`/`errorDetailBrush`, `vignetteBrush[2]` - created lazily by the current device context, destroyed by the same file. A gradient brush's stops are fixed at creation, so `DrawVignetteBar` keys its cache on `(y0, y1, alpha)` and rebuilds when any of them changes - every frame during a fade, no frames while a vignette sits idle. The error screen's two `IDWriteTextFormat`s are file-statics in `Rendering.cpp` instead, because DWrite objects are device-independent. **Their font size comes from `g_view.uiPixelScale`, never from the live render-target size** - that scale is set in `InitInstance` and overwritten at fullscreen stage 2, so a small windowed window still gets readable text and a windowed `WM_SIZE` can never change it |
| **`DrawUiOverlay(rtSize)`** | vignette bars, zoom readout, film strip, buttons, text boxes. One function called from both the three-pass path (pass 3) and the single-pass path; those two blocks used to be duplicated and had already drifted, and each had its own `s_lastDisplayedZoom` static - there is now one static, so switching render paths (e.g. toggling the HQ filter) no longer forces a redundant `SetText` |

### `Render` control flow
1. Bail if `hWnd != activeWindow` (protects the hidden window from stray `WM_PAINT`).
2. Lazy re-creates: image bitmap, GIF frame 0 after device loss, default bg, desktop bg, slideshow
   bg, **one** thumbnail per frame spiralling out from current.
3. `BeginDraw` -> clear (fullscreen `0,0,0,0.6`; windowed `0.4` grey) -> `g_fullscreen.needsInit` ->
   `InitializeImageLayout`.
4. Background: slideshow blurred pair / desktop capture + dim / leftover blurred bg. During the
   slideshow stop fade's black-in phase the blurred wash is held (`holdBlurred`), so the swap to the
   desktop capture lands at full black together with the overlay resize.
5. Image: prev-frame cross-fade, then transform `scale*translate` then `rotation` about the
   transformed centre; shadow via `CLSID_D2D1Shadow` on the **small** bitmap with an extra scale;
   `useD3D11Image = g_pipeline.ready && g_pipeline.srv && !g_settings.useHQFilter`.
6. Load-error screen ("This file is corrupted...") using `g_image.lastLoadError` + `g_image.loadAttemptName`.
7. **Three passes** when D3D11 is used: D2D (bg+shadow+prefade) -> `EndDraw` -> D3D11 quad ->
   D2D (`DrawUiOverlay`) -> `EndDraw` -> `Present(1,0)`. **Single pass** otherwise, deliberately, to
   avoid splitting a frame with no D3D11 work between them. Both paths call the same `DrawUiOverlay`.
8. `g_fullscreen.navPendingRender = false` after a successful `Present` - the keyboard-auto-repeat throttle.
9. `D2DERR_RECREATE_TARGET` -> `DiscardDeviceResources()`. The `g_devices.renderTarget->SetTransform(...)`
   at the tail of `Render` is guarded by `if (g_devices.renderTarget)`: that discard is what releases
   the target, and the next `WM_PAINT` recreates it before anything draws.

## 5. Update engine (per-frame)

| Anchor | What it drives |
|---|---|
| `g_anim.framesReadyUpTo.load()` | animated GIF/WebP/APNG/AVIF frame advance; won't pass the last decoded frame; swaps pre-uploaded `g_anim.gpuFrames[i]` or uploads lazily on first reach |
| `g_view.rotationSpeed * dt` | constant-rate rotation (300 deg/s), not spring |
| `g_fullscreen.overlayAlpha` | enter/exit dim, `* 0.15f * dt * 60` |
| `g_slideshow.preFadeAlpha += SLIDESHOW_FADE_STEP * dt * 60.0f` | phase 1 fade-to-black -> phase 2 cover window + immediate exit + new overlay + slideshow timer |
| `g_slideshow.stopFadeAlpha +=/-= SLIDESHOW_FADE_STEP * dt * 60.0f` | slideshow stop fade: up to 1, `ResizeOverlayToWorkArea()` at full black, back to 0. If `g_app.isExiting` or `g_fullscreen.pendingExit` is already set the pending resize is dropped and the black just fades out |
| `g_fullscreen.exitBgFadeAlpha += 0.04f * dt * 60.0f` | fullscreen-exit fade -> `CompleteExitFullscreen()` |
| `g_slideshow.transitionAlpha` | image<->image cross-fade: linear `+= 0.02f * dt * 60.0f` then `+= (1 - a) * (1 - powf(0.995f, dt * 60.0f))` exponential tail |
| `g_thumb.readyQueue` | drains **one** thumbnail upload per frame under `g_thumb.readyMutex` |
| `g_thumb.targetOffset = -(g_image.index * (g_thumb.width + g_thumb.gap))` | auto-centring, skipped while dragging/free-scroll |
| `g_view.zoom += (g_view.targetZoom - g_view.zoom) * g_view.smooth` | the spring for zoom/pan (same for offsetX/Y) |
| `SmoothAlpha(0.08f, dt)` | vignette visibility spring - the same falloff the text boxes get from `UpdateVisibility(dt, 0.08f)` |
| `if (g_app.isExiting && ... g_fullscreen.overlayAlpha < 0.01f)` | **destroys the main window** so `WM_DESTROY` runs the teardown (it also destroys the overlay and posts `WM_QUIT`); falls back to destroying the overlay, or a bare `PostQuitMessage`, only when the main window is already gone. Quitting without destroying the window leaves the background threads joinable at static destruction, which is the `0xc0000409` exit crash |

## 6. Image loading & decoding (`Decoding.h` + `Decoding.cpp`)

`Decoding.h` exports only `LoadImageD2D`, `StopAnimDecodeThread`, `AvifOpenAndParse`,
`DecodeAvifToWicBitmap` and `DecodeJxlToWicBitmap`; everything else is file-local (`static`, or in
the anonymous namespace the compositing primitives live in). The three shared AVIF/JXL entry points
carry their **default arguments in the header only** (C2522 if they also appear on the definitions).

| Anchor | Function / format |
|---|---|
| `LoadFail` | every failure funnels here -> `g_image.lastLoadError` (+ HRESULT suffix) + `g_image.showLoadError` |
| `SniffIsobmffBrands` | reads `ftyp`, names the major brand; flags `avis` (sequence) vs `avif` |
| `AvifOpenAndParse` | shared AVIF entry: path -> UTF-8 -> `avifDecoderSetIOFile` -> `avifDecoderParse` |
| `ConvertAvifImageToWicBitmap` | `avifImageYUVToRGB` -> BGRA8 -> WIC PBGRA -> cached |
| `DecodeAvifToWicBitmap` / `AvifPeekFrameCount` / `DecodeAvifFrames` | still / frame count / animated (`avifDecoderNthImage` can **seek**, so continuation starts at frame 1) |
| `JxlThreadParallelRunnerCreate` | `DecodeJxlToWicBitmap` - whole file in memory, BASIC_INFO -> FULL_IMAGE loop, **first frame only** |
| `ReadUIntMeta` / `CloneBitmap` / `ClearRect` / `DecodeToPBGRA` / `BlendOntoCanvas` | **shared** compositing primitives (hoisted out of per-format lambdas on purpose - see the long comment; `BlendOntoCanvas(blendOver=false)` = APNG `BLEND_OP_SOURCE`) |
| `DecodeGifCompositeFrames` | manual disposal replay (2 = clear rect, 3 = restore clone); **inherently sequential** |
| `DecodeWebpFrames` | WIC composites internally -> frames independent; tries 6 metadata paths for ANMF duration |
| `ApngCrc32` / `PeekApngFrameCount` / `DecodeApngFrames` | walks `acTL`/`fcTL`/`IDAT`/`fdAT`, repackages each frame into a **synthetic single-frame PNG** so WIC does the inflate/unfilter; the sequence number is deliberately *not* used for indexing (comment explains why). `PeekApngFrameCount` seeks over chunk payloads instead of reading them, and `LoadImageD2D` calls it once per load (the count doubles as the branch condition) |
| `BeginAnimatedPlayback` / `OpenThreadLocalWicFactory` / `OpenThreadLocalWicDecoder` / `SpawnAnimDecodeThread` | shared glue; each background thread makes its **own** WIC factory (apartment safety) and `CoInitializeEx(COINIT_MULTITHREADED)` - check the factory's release order against `CoUninitialize` there, as in `StartThumbnailLoader` |
| `LoadImageD2D` | dispatch order: **AVIF -> JXL -> WebP -> GIF -> APNG -> static+EXIF**; saves prior view state into `g_view.stateByPath[path]` first |
| `EXIF Orientation tag (tag 274)` | 3->180 deg, 6->90 deg, 8->270 deg; 5-8 rotation-only (no flip support in the pipeline) |
| `Enforce premultiplied invariant` | zeroes RGB where A==0 (kills speckle) |
| `FinishImageLoad` | upload, restore saved pan/zoom/rotation, filename+size label. Uses the **full path** for `file_size` and the `error_code` overload - Explorer search results break on bare names, and a throwing `file_size` would crash before anything is shown |

---

## 7. Thumbnails & directory watching (`Thumbnails.h` + `Thumbnails.cpp`)

`g_thumb` state stays in `Globals.h` because `UpdateEngine` drains `g_thumb.readyQueue` - shared
state, not a function edge. The watcher's handle, thread and stop event are the opposite case: no
TU outside `Thumbnails.cpp` reads them, so they are `static` there and the shutdown order lives
with them. `Thumbnails.cpp` declares `IsSupportedImage` in its bridge block.

| Anchor | Notes |
|---|---|
| `BuildImageList` | Rebuilds the list from the folder containing the given file, sorted with `StrCmpLogicalW`. **Two places rebuild `g_image.files`/`g_thumb.entries`: `BuildImageList` and `DeleteCurrentImage`** (main file), and both call `StopThumbnailLoader` first. Walks the folder with the `error_code` overloads of `directory_iterator` / `is_regular_file`, and keeps a failed walk distinct from an empty result: if the folder cannot be walked at all the function returns immediately, leaving the list, the entries, `g_image.index`, the watcher and the loader exactly as they were - so a sleeping drive or a dropped share cannot empty the film strip, and an empty list always means the folder really has no images. Snapshots the existing thumbnails **by path** before the natural sort shifts indices, then re-deposits them at their new indices |
| `StartThumbnailLoader` | spiral order from current index; fast path = embedded thumbnail when its **short side** is >= 180 px (the test never looks at orientation, and works because slots are square and get centre-cropped); slow path = full decode + HighQualityCubic to `TARGET_H = 180`; EXIF applied via `IWICBitmapFlipRotator` on the small bitmap; AVIF/JXL go through the bundled decoders because WIC cannot open them. The thread never touches `g_thumb.entries` - it publishes into `readyQueue` under `readyMutex`. Its own WIC factory is `Reset()` **before** `CoUninitialize`, not at scope exit: releasing a WIC factory into an apartment that has already shut down reads an unmapped `windowscodecs.dll` vtable (0xc0000005), masked today only because `g_devices.wicFactory` keeps that DLL loaded for the whole process |
| `StopThumbnailLoader` | sets `g_thumb.loaderStop`, joins, clears the flag |
| `DrawThumbnailStrip` | one cached origin-centred rounded-rect geometry reused via the RT transform; the clip is placed by the transform current at `PushLayer`, and `PopLayer` leaves the transform at identity - which the absolute-coordinate dim and placeholder fills depend on. Centre-crop to a square slot; non-current darkened 50%; placeholder brush when a slot has no bitmap **or when `clipGeos` is empty** (a decoded thumbnail still cannot be drawn without its clip shape). Only caller is `Render` in `Rendering.cpp` |
| `StartDirectoryWatcher` / `ReadDirectoryChangesW` | non-recursive, `FILE_NOTIFY_CHANGE_FILE_NAME`. Shutdown = set the stop event, join, then close both handles; the thread waits on that event next to the read's own event and, on the stop path, cancels its outstanding read and waits for the cancellation, so the handle is never closed under a live request. Posts `WM_APP_DIRCHANGE` to **`g_app.mainWindow`** - never the overlay handle, whose value changes while this thread runs - and also when the read returns 0 bytes, which means the notification buffer overflowed and the changed names are unknown |
| `g_thumb.dragging` / `velocity` / `freeScroll` / `dragHitIndex` | drag vs click decided in `WM_LBUTTONUP` (<5 px = click); EMA velocity x25 for momentum; hit half-width expanded by half the gap so gaps aren't dead zones |

## 8. UI (`UI.h` + `UI.cpp`)

The **two default arguments** (`FitToWindowRelative(..., bool preserveCenter = true)` and
`InitializeImageLayout(HWND, bool hard = false)`) appear **only** on the `UI.h` declarations -
putting them on the definitions too is C2522.

| Anchor | Notes |
|---|---|
| `InitializeMenuButtons` | Open + hamburger - top-left zone, `SetForcedVisibility(true)` here, which `InitializeButtons` clears at the first fullscreen, so afterwards they are zone-driven. Position/scale captured against the **windowed** client, so they keep that size in fullscreen |
| **Aspect-ratio assumptions in the widget positions** | `Layout::x.value` is a fraction of the render-target **width** and `y.value` of **height**, so pixel-even insets need different fractions per axis. Three places encode that: the Open button's margin is `0.05 * height / width`; the hamburger's step is a flat `0.056f` of width; the exit-button inset uses `top * 9/16`, square in pixels only on **16:9**. All captured once per surface and frozen, so they are stable but drift on a display of another aspect. Untested on anything but 16:9 |
| hamburger: `HWND ownerWnd = (g_view.isFullscreen && g_app.overlayWindow...` | hardcoding `g_app.mainWindow` used to break fullscreen; wrapped in `FullscreenExitSuppressor` because `TrackPopupMenu`'s menu window isn't ours |
| `InitializeButtons` | 9 more: 1:1, slideshow, +/- zoom, two rotate, prev/next, exit (top-right zone, hidden when windowed). The slideshow glyph is the play triangle or `kSlideshowStopGlyph` (`L"\u25A0"` - a plain geometric shape with no emoji form; `U+23F8`/`U+23F9` arrive as colour emoji tiles and `U+FE0E` did not change that), re-set by `UpdateSlideshowButton` at every slideshow transition and at the end of this function |
| **Anchor capture is once per process** (`UIElement::CaptureAnchorsOnce`, `ResetAnchors`, `SetLayout`) | Both axis caches and the activation-zone cache are captured the **first** time an element lays out, against the render-target size at that element's `Initialize()`: the **windowed client** for the two startup buttons and the **monitor** for the 9 in-image buttons + both text boxes. `CaptureAnchorsOnce` then early-returns forever, `SetLayout` deliberately keeps the caches, and **nothing calls `ResetAnchors()`**. So positions are fixed pixel offsets for the life of the process. While fullscreen on the capture monitor that is indistinguishable from the normalized spec; it shows when the render target later differs - e.g. after a fullscreen round trip the bottom row keeps its monitor-derived ~2% spacing (~77 px at 4K) inside a ~1600 px window instead of the ~32 px the fractions would give. Sizes are frozen the same way. `//g_buttons.clear();` inside `InitializeButtons` is commented out - enabling it would give fresh anchors. Left as-is deliberately: sizes are meant to be frozen |
| **The 9 in-image buttons and both text boxes exist only after the first fullscreen** | Their only call sites are fullscreen stage 2 (`Fullscreen.cpp`), while `InitializeMenuButtons` runs from `wWinMain`. So the windowed UI at startup is just the two menu buttons, and the two groups carry different frozen `uiPixelScale` for the whole process |
| **Tooltip text sizing is process-wide, deliberately** (`UIElement::TooltipFontPx`, `g_view.uiPixelScale`, `Layout::tooltipFontSize`) | Tooltip px = `g_view.uiPixelScale * tooltipFontSize`, NOT the element's own frozen `uiPixelScale` - sizing it from that made the menu buttons' tooltip ~11% larger than the bottom row's on every display, permanently. `g_view.uiPixelScale` is set in `InitInstance` from the primary monitor (never the `1.0f` default) and overwritten by fullscreen stage 2 with the monitor actually used. Default factor `0.0144f`; **no per-element tooltip overrides**. Button *glyph* sizes (`Config::fontSize`) stay per-element and hand-tuned per symbol - do not "unify" them |
| `ButtonId::PREVIOUS` / `ButtonId::NEXT` lambdas | both call `RestartSlideshowTimer()` after navigating (the guard is inside the helper) |
| hold-repeat policy (`Config::holdEnabled` / `repeatGate`) | **Off** for `OPEN`, `HELP` (hamburger) and `SLIDESHOW`: the first two open modal loops and the third *toggles* the mode, so auto-repeat would stack dialogs or flip-flop the slideshow. **On** for the rest, because held `W`/`S`/`Q`/`E` repeat on the keyboard too. `PREVIOUS`/`NEXT` carry a `repeatGate` on `g_fullscreen.navPendingRender` - one nav per presented frame, same backpressure as the keyboard. Repeat also stops when the cursor leaves the button. Because activation is on the press, `WM_LBUTTONDBLCLK` re-running `OnMouseDown` means a double-click on a button fires the action **twice - by design**, not a bug |
| **`UITextBox` typing rules** (`UITextBox::OnChar`) | `OnChar` is the **only** filter. `NumericFloat` = digits + at most one `.`; **no minus sign anywhere**, because the only editable box is the zoom readout and a negative zoom is meaningless. Control characters (`Enter`/`Esc`/`Backspace` arrive on the `WM_CHAR` path as 13/27/8) never insert. There is **no caret and no selection range**: `OnMouseDown` sets `m_selectAllOnFocus`, and the first typed character - or `Backspace` - replaces the whole string. `VK_RETURN` fires the callback and drops focus; `VK_ESCAPE` drops focus and keeps the text |
| **`UITextBox` focus border is independent of `backgroundAlpha`** | The border is drawn on `m_focused` alone, deliberately *not* inside the `backgroundAlpha > 0` block - an editable box with no background would otherwise have no focus feedback. Only an editable box can take focus, so `m_focused` is sufficient. The pulsing "whole string selected" fill is separate again |
| `InitializeImageInfoLabel` | `TextBoxId::FILE_NAME` (bottom, display-only) + `TextBoxId::ZOOM_INPUT` (centre, `InputMode::NumericFloat`, clamped 1-10000 then `/100`). The typed-zoom callback anchors on the **viewport centre** (same convention as cursor-anchored wheel zoom) rather than the image's own centre - preserving an off-screen image centre after a panned wheel zoom would keep the image off screen. Guards on `g_view.bitmap` because the readout stays clickable over the load-error screen |
| `InitializeImageLayout` | opens at 100% unless oversized or `g_settings.openFitToScreen`, then 95% fit; `hard=false` starts at `g_view.zoom = 0.05f` and springs in; early-outs on `g_view.restoredStateThisLoad && !g_slideshow.active`, and sets `g_fullscreen.overlayAlpha` absolutely (`1.0f` when `hard`, `0.0f` when not), so it also decides whether the enter/exit dim starts from black or from clear. Every caller passes `hard = true` except `Render`, which passes `hard = g_slideshow.active`. Callers: `Render`, `ResizeOverlayToWorkArea`, `OpenImageFile`, `OpenNextImage`, `OpenPrevImage`, `DeleteCurrentImage`, the strip-click branch of `WM_LBUTTONUP`, the slideshow branch of `WM_TIMER`, and `WM_APP_DIRCHANGE`. **`WM_SIZE` does not call it** - it recentres with `SetZoomCentered` instead |
| `SetZoomCentered` / `FitToWindowRelative` / `ZoomIntoImage` / `MakeZoomVisible` | `ZoomIntoImage` = `pow(1.1, delta/120)`, clamped 0.01-100, cursor-anchored when over the image; `optionalPt` arrives in screen coords (`GetCursorPos`) and is converted to client coords exactly once |
| `g_textBoxes[TextBoxId::ZOOM_INPUT].IsFocused()` guard | in `Render`, so typing isn't overwritten |
| **UI visibility ownership** (`UpdateProximity` / `SetForcedVisibility` / `SetTargetVisibility`) | `m_targetVisibility` has three owners in precedence order: explicit override -> forced visibility -> activation zone. `UpdateProximity` always records the zone verdict in `m_proximityTarget` (even while outranked) so `SetForcedVisibility(false)` and `ClearTargetVisibility()` hand control **back to the zone** instead of forcing 0. Forcing 0 was a real bug: the zone verdict is only recomputed on `WM_MOUSEMOVE`, so a still cursor left the element stuck invisible. **Residual staleness:** the verdict still only refreshes on `WM_MOUSEMOVE`, so it can be out of date if a widget's rect moves under a still cursor (resize, fullscreen transition) - fixing that means driving `UpdateProximity` from `UpdateEngine` with the real cursor position |

## 9. Metadata (`Metadata.h` + `Metadata.cpp`)

Only the six functions with outside callers are in the header (`ShowImageMetadata`,
`ShowImageProperties`, `ShowMetadataViewerWindow`, `MetadataViewerWndProc`,
`RegisterMetadataViewerClass`, `PropertiesDlgProc`) and only the first two actually have outside
callers (both `WndProc` `WM_COMMAND`). `struct PngTextField` and `struct ImagePropertiesFields` and
the `static` reader helpers are `Metadata.cpp`-private. Its only module edge is `AvifOpenAndParse`
-> `#include "Decoding.h"`; it also carries its own `<avif/avif.h>`, `<jxl/decode.h>`,
`<algorithm>`, `<cstring>`, `<cstdio>`, `<commctrl.h>` + `comctl32.lib` and a bridge declaration of
`EnableDarkTitleBar`.

| Anchor | Notes |
|---|---|
| `RationalFromPackedUI8` | WIC packs EXIF RATIONAL into `VT_UI8`: num<<32 \| den |
| `PropVariantToDisplayString` | `VT_LPWSTR/LPSTR/UI1/UI2/UI4/I4/UI8` + single-elem vectors (ISO) |
| `AppendSimpleField` / `AppendRationalField` | `"label: value\n"`; shutter speeds as `1/N s` |
| `AppendGpsCoordinate` | 3 rationals + N/S/E/W hemisphere -> decimal degrees |
| `OrientationName` | 1-8 human-readable |
| `ReadDateTakenValue` / `ReadColorSpaceValue` / `ReadOrientationValue` | bare-value variants for the Properties dialog (em dash escape for N/A, layout stays static) |
| `FormatFileSizeW` / `FormatFileTimeW` | Explorer-style `1.23 MB (1,290,000 bytes)`; locale short date+time |
| `ReadPngTextChunks` | raw chunk walk for **tEXt/iTXt** - WIC needs the exact keyword and SD/ComfyUI use arbitrary ones; 8 MB cap; skips compressed iTXt and zTXt |
| `ReadWebpXmpFields` | WIC doesn't expose the `"XMP "` RIFF chunk at all; walks chunks (word-aligned), then every child of every `rdf:Description` |
| `DecodeXmlEntities` / `StripXmlTags` / `FindTagEnd` / `TrimWhitespace` | the shared mini-XML helpers (`FindTagEnd` respects quoted attribute values) |
| `ReadJxlMetadataBoxes` | `JXL_DEC_BOX` + `JxlDecoderSetDecompressBoxes`; `"Exif"`/`"xml "` boxes; **sanity-checks the TIFF header** before trusting the 4-byte offset prefix (hand-edited JXL omits it) |
| `OpenAvifExifMetadataReader` / `OpenJxlExifMetadataReader` | feed the raw TIFF blob to WIC's **TIFF** decoder -> reuse all the above helpers |
| `ReadAvifXmpFields` / `ReadJxlXmpFields` | same XML strategy, three near-identical copies (extraction candidate) |
| `ShowImageMetadata` | tries `/app1/ifd/...` **and** `/ifd/...` for every tag (PNG/WebP have no APP1) |
| `MetadataViewerWndProc` | `L"MetadataViewerClass"`, dark owner-draw, `EM_SETLIMITTEXT 0` to lift the ~30k edit cap, **its own modal message pump** + accelerators (Esc, Ctrl+A) |
| `ShowImageProperties` + `PropertiesDlgProc` + `IDD_PROPERTIES` | in-app dialog (replaced the shell Properties verb, which is why `ContextMenuId::PROPERTIES` needs no manual suppression) |
| `AssociateFileTypes` + `About` | **not metadata** - see section 10 |

## 10. Dialogs - file association, About, Keyboard shortcuts (`Dialogs.h` + `Dialogs.cpp`)

Two functions, neither `static`; the header is exactly those two declarations and the file needs
**no** bridge block. `g_app.instance` is the only global either one touches.

| Function | Notes |
|---|---|
| `AssociateFileTypes(HWND)` | `MenuCommandId::ASSOCIATE` in `WM_COMMAND`. Asks first with a `TASKDIALOGCONFIG` / `TaskDialogIndirect` (owned by the main window, so `IsOwnedByOurWindow()` recognizes it) and returns **without touching anything** unless the answer is `IDOK`. Then `GetModuleFileNameW` -> ProgID `PicassoPictures.ImageFile` under `HKCU\Software\Classes`, its `shell\open\command` value (`"<exe>" "%1"`, built with `swprintf_s`), the friendly name, `Software\PicassoPictures\Capabilities` + `Software\RegisteredApplications`, one `Software\Classes\<ext>` -> ProgID per extension (.jpg .jpeg .png .bmp .gif .tif .tiff .webp .avif .jxl) plus a `FileAssociations` value per extension, then `SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, ...)`. A second `TaskDialog` reports whether **every write** succeeded - the ProgID, its open command and the Capabilities keys count, not just the per-extension ones. A `GetModuleFileNameW` that fails or hits `MAX_PATH` aborts with its own dialog before anything is written. Everything is `HKCU`, so the failure text must not suggest elevation. The `RegSetStr` lambda is local. **The only place in the project that writes those keys.** |
| `About` | **ONE `DLGPROC` for TWO templates**: `IDD_ABOUTBOX` (`IDM_ABOUT`, `MenuCommandId::ABOUT`) and `IDD_COMMANDBOX` = "Keyboard shortcuts" (`ID_HELP_COMMANDS`, `MenuCommandId::SHORTCUTS`). Four `DialogBox` sites in `WndProc`; no other shortcuts dialog exists. `WM_INITDIALOG` returns TRUE; `WM_COMMAND` maps `IDOK`/`IDCANCEL` to `EndDialog`; `WM_NOTIFY` matches `IDC_SYSLINK_GITHUB` + `NM_CLICK` and `ShellExecuteW`s `pLink->item.szUrl`, so the GitHub URL lives in the dialog template, not in code. Modal, so the main window is insensitive while it is up. **The About box's version string (`"Picasso Pictures, Version 1.8"`) is a literal in the UTF-16 `.rc`** - bump it by hand at each release; there is no `VERSIONINFO` resource anywhere, and git diffs that `.rc` as binary |
| `Dialogs.cpp` includes | `<commctrl.h>` + `comctl32.lib` (task dialog + `NMHDR`/`NMLINK`/`NM_CLICK`), `<shellapi.h>`, `<shlobj.h>` (`SHChangeNotify` - **not** in `shellapi.h` on this SDK), `<cstdio>`, `<cwchar>`, `shell32.lib`. `advapi32.lib` needs no pragma |
| Grep anchors | `AssociateFileTypes(HWND hWnd)`, `PROG_ID      = L"PicassoPictures.ImageFile"`, `INT_PTR CALLBACK About(`, `IDC_SYSLINK_GITHUB` |

---

## 11. `WndProc` - message -> handler

| Message | Behaviour |
|---|---|
| `WM_ERASEBKGND` | `return 1` (D2D owns the surface) |
| `WM_COPYDATA` | second instance's path -> `BuildImageList` + `LoadImageD2D` + `EnterFullscreen(true,true)` |
| `WM_ACTIVATE` | `WA_INACTIVE` + `g_fullscreen.initDone` + depth 0 + `!IsOwnedByOurWindow(lParam)` -> **post** `WM_APP_EXITFULLSCREEN` (never call directly - destroying the overlay mid-focus-change loses keyboard focus) |
| `WM_APP_EXITFULLSCREEN` | re-checks `g_fullscreen.suppressExitDepth == 0` at *dispatch* time, not post time |
| `WM_DROPFILES` | load -> `BuildImageList` -> `EnterFullscreen` |
| `WM_COMMAND` | `IDM_OPEN`, `IDM_ABOUT`, `IDM_EXIT`, `ID_HELP_COMMANDS`/`MenuCommandId::SHORTCUTS`, `MenuCommandId::HQ_FILTER`, `MenuCommandId::FIT_TO_SCREEN`, `MenuCommandId::ASSOCIATE`, and 6 context-menu IDs (copy -> `CF_DIB`, delete, open folder, properties, metadata, wallpaper). `IDM_OPEN` / `IDM_EXIT` / `ID_HELP_COMMANDS` are dead in practice - the menu is detached in `wWinMain`; only `IDM_ABOUT` can still arrive, from the `Alt+?` / `Alt+/` accelerators in the `.rc`. `HQ_FILTER` / `FIT_TO_SCREEN` are the only writers of the two registry values `wWinMain` reads back |
| `WM_PAINT` | `BeginPaint` -> `Render` -> `EndPaint` |
| `WM_SIZE` | unbind target + `g_devices.swapRTV`, `ResizeBuffers`, re-create both, `UpdateLayout` on all UI, re-centre |
| `WM_RBUTTONUP` | 6-item popup, wrapped in `FullscreenExitSuppressor` |
| `WM_MOUSEWHEEL` | `ZoomIntoImage(delta, &pt)` + `MakeZoomVisible` |
| `WM_LBUTTONDOWN` | buttons -> text boxes -> **thumbnail strip row claimed whole** (`PointInStripRow`, shared with `WM_LBUTTONDBLCLK`) -> else image pan + `SetCapture`. A button or an **editable text box** claims the press on the down and sets `g_buttonClaimedClick` (buttons act on the press; text boxes claim so the release isn't read as "clicked outside the image", which would exit fullscreen when the zoom readout at screen centre isn't over the image). Note `g_view.mouseFromDown` is written only in the pan branch, so after a widget press it still holds the previous drag's point - which is why the explicit claim, not `wasReallyDragging`, is what keeps a widget click from also leaving fullscreen |
| `WM_LBUTTONUP` | **the strip-drag resolution runs first and returns early; the button `OnMouseUp` loop runs next, still before the `if (!g_view.bitmap) break;` guard** - a release must always end the press, or `m_holdActive` survives and the button auto-repeats on hover alone (a failed load clears `g_view.bitmap`, which is exactly how that was hit). `AnimatedButton::Update` also requires `m_pressed`, so a lost release can at most freeze the press animation, never fire actions. The strip branch decides click (<5 px) vs momentum (a click that opens another image calls `RestartSlideshowTimer`). After the button loop, `!overImage && !wasReallyDragging && !buttonClaimed` -> `ExitFullscreen`; `TimerId::KILL_ROTATION` 120 ms snaps partial rotations to the nearest 90 deg |
| `WM_MOUSEMOVE` | button/textbox proximity, vignette activation zones, `stripCoasting` keeps the strip visible mid-fling, strip drag + EMA velocity, image pan |
| `WM_LBUTTONDBLCLK` | buttons -> text boxes -> **the film-strip row**, then `!g_view.bitmap` -> `OpenImageFile` (double-click on empty space of the load-error screen), then windowed+over image -> fullscreen preserving view; fullscreen + overlay exists + over image -> toggle 1:1 <-> 96% fit. The order matters: the second click of a rapid pair arrives as DBLCLK, not DOWN, so a branch ahead of the widget loops would steal double-clicks aimed at a button while the error screen is up. All three widget/strip branches set `g_buttonClaimedClick`; the strip branch exists because a double-click on a thumbnail otherwise also toggled the image's fit/1:1 zoom. The `g_app.overlayWindow` test in the last branch is the guard for fullscreen stage 1, where `isFullscreen` is true but the overlay does not exist yet |
| `WM_TIMER` | 4 IDs: enter-fullscreen stage 2 (**only if `g_view.isFullscreen && !pendingExit && !isExiting`** - `CompleteExitFullscreen` kills the timer, but a `WM_TIMER` already queued survives `KillTimer`), kill-rotation, zoom display hide, slideshow advance (only if `g_slideshow.transitionAlpha > 0.8f`) |
| `WM_CHAR` | forward to textboxes |
| `WM_KEYDOWN` | `RestartSlideshowTimer()` runs first, before anything else, so any key re-arms the 6 s slideshow interval - including keys typed into the zoom box (deliberate); a focused textbox then swallows everything; else `Del`/`Shift+Del`, `Esc`, `F` toggle fullscreen (auto-repeat ignored via `lParam & (1<<30)`), `O`, `W`/up, `S`/down, `A`/left, `D`/right/`Space` (nav gated by `g_fullscreen.navPendingRender`; `W`/`S` are zoom, not navigation, and are not gated), `Q`/`E` rotate, `F5` slideshow. **Every quit path goes through `DestroyWindow(g_app.mainWindow)`**, never a bare `PostQuitMessage`: `Esc` windowed, `Esc` with a failed load, and the exit button with no image all destroy the main window so `WM_DESTROY` runs the teardown first |
| `WM_APP_DIRCHANGE` | `BuildImageList` again; if the list comes back **empty** the folder really has no supported images (all deleted or moved out in Explorer - a folder that cannot be read keeps its previous list and never gets here), so `InitFullScreenExit()` - same treatment as deleting the last picture in the folder; otherwise, if the current file vanished, clamp to index 0 |
| `WM_DESTROY` | main window only: destroy overlay, stop all 3 threads, discard resources, **empty the containers that still hold COM objects the discard missed** (`g_thumb.entries` / `clipGeos` / `readyQueue`, `g_anim.frames` / `gpuFrames`), reset all `g_wic*`/factories, `PostQuitMessage`. **It does not call `CoUninitialize`** - `CoInitialize` runs once in `wWinMain` and its single matching call is on the `shutdown:` label after the message loop; `WM_DESTROY` runs while that loop is still alive, so a call here would make the loop's call a second one. **The container clearing is load-bearing:** these are globals, so anything left in them is destroyed *after* `main` returns - releasing COM objects into a runtime that has already gone, i.e. a crash on exit (`0xc0000005` in `~vector` -> `~ComPtr::InternalRelease`). Thumbnails and clip geometries only exist after fullscreen stage 2, which is why a windowed close never crashed and a fullscreen close always did. **Not emptied, deliberately:** `g_textBoxes` / `g_buttons` - the widgets' only COM members are DWrite objects, never a WIC bitmap or a device-context resource |

## 12. Globals worth knowing

All declared `extern` in `Globals.h` and defined once in `Globals.cpp`. The only exception is the
directory watcher's handle / thread / stop event: `static` in `Thumbnails.cpp`. `Globals.h` also
owns `MAX_LOADSTRING`, `ImageViewState`, `ThumbnailEntry`, the control / command / timer ID enums,
`WM_APP_*`, `IsOwnedByOurWindow` and `FullscreenExitSuppressor`. Nothing in `Globals.cpp` reads
another global, so dynamic-initialisation order cannot matter. The two `std::thread` members
(`g_anim.decodeThread`, `g_thumb.loaderThread`) and the watcher thread are all stopped and joined
during `WM_DESTROY` - a joinable `std::thread` destructing at exit calls `std::terminate`.

Structs with one instance each: **`g_app`** (`instance`, `title`, `windowClass`, `mainWindow`,
`overlayWindow`, `blackCoverWindow`, `metadataViewerWnd`, `isExiting`, `launchedWithFile`);
**`g_devices`** (factories, `d3dDevice`/`d3dContext`, `d2dDevice`, `renderTarget`, `swapChain`,
`d2dTargetBitmap`, `swapRTV`, `renderTargetWindow`); **`g_view`** (`zoom`/`targetZoom`,
`offsetX/Y`/`targetOffsetX/Y`, rotation trio, `exifRotation`, `smooth`, `stateByPath`,
`restoredStateThisLoad`, `isDragging`, `lastMouse`, `mouseFromDown`, `bitmap`, `isFullscreen`,
`uiPixelScale`); **`g_image`** (`source`, `files`, `index`, `path`, `fileName`, `lastLoadError`,
`loadAttemptName`, `showLoadError`, `width`, `height`); **`g_anim`** (`frames`, `gpuFrames`,
`frameDelays`, `currentFrame`, `isAnimated`, `lastFrameTime`, `decodeThread`, `decodeStop`,
`framesReadyUpTo`); **`g_pipeline`** (cached brushes + their cache keys, `mipTex`, `srv`, `sampler`,
`vs`/`ps`/`il`, `vb`/`cb`, `blend`/`rast`/`ds`, `ready`); **`g_settings`** (`useHQFilter`,
`openFitToScreen`); **`g_fullscreen`** (`needsInit`, `initDone`, `overlayAlpha`, `navPendingRender`,
`pendingPreserveView`, `suppressExitDepth` - a refcount touched only through
`FullscreenExitSuppressor` - plus the exit-fade trio `pendingExit`, `wasInSlideshowOnExit`,
`exitBgFadeAlpha`); **`g_slideshow`** (`active`, `fromFullscreen`, `preFade`/`preFadeAlpha`, `stopFade`/`stopFadePendingResize`/`stopFadeAlpha`,
`transitionAlpha`/`targetAlpha`, `prevBitmap`, `prevBgBitmap`, `bgBitmap`, `prevZoom`/`prevOffsetX`/
`prevOffsetY`/`prevRotation`, `blurEffect`); **`g_vignette`** (`topVisibility`/`topTarget`, `bottomVisibility`/`bottomTarget`,
`topBarHeightPx`/`bottomBarHeightPx`, `topActivationPx`/`bottomActivationPx`, `heightsCaptured`);
**`g_thumb`** (`entries`, `readyQueue` + `readyMutex`, `loaderThread`/`loaderStop`,
`scrollOffset`/`targetOffset`, `width`/`height`/`gap`, `stripInsetFromBottomPx` - the fixed distance
from the render-target bottom to the strip centre - `sizeCaptured`, `clipGeos`, and the drag set);
**`g_background`**
(desktop capture + embedded JPG + `shadowEffect` + `shadowSourceBitmap`). The only flat globals are
the two control maps `g_textBoxes` and `g_buttons`. The fullscreen-exit-fade trio lives in
`FullscreenState` because the ramp is gated on `g_view.isFullscreen`, not on slideshow state.
`SLIDESHOW_INTERVAL_MS = 6000` and `SLIDESHOW_FADE_STEP = 0.05f` are `constexpr`, not state.

**Timers:** `SLIDESHOW 0xcafe`, `ZOOM_DISPLAY 0xbebe`, `KILL_ROTATION 0xbeca`, `ENTER_FULL_SCREEN 0xcaca`.
**Messages:** `WM_APP_DIRCHANGE (WM_APP+1)`, `WM_APP_EXITFULLSCREEN (WM_APP+2)`.

## 13. Third-party / build notes

- `vcpkg install libavif dav1d libjxl`, static triplets (`x64-windows-static`). **The claim "so no
  extra DLLs ship" is not true today:** `jxl.lib` is a mixed-CRT archive - it carries
  `/DEFAULTLIB:LIBCMT,libcpmt` but its objects reference the *import* forms of the STL thread
  primitives (`__imp__Thrd_*`, `__imp__Mtx_*`, `__imp__Cnd_*`, `__imp__Syserror_map@std`) - so the
  released exe imports **MSVCP140.dll** and needs the VC++ runtime present. `avif.lib` and
  `dav1d.lib` are clean, and none of our own objects reference those stubs. Making the exe
  self-contained means rebuilding libjxl with a consistent `/MT`.
- The `#pragma comment(lib, ...)` names have shifted across libavif/libjxl releases - check vcpkg's install output.
- `GUID_ContainerFormatWebp` is redefined locally as `kContainerFormatWebp` for older SDKs. Only `Decoding.cpp` uses it now (`Metadata.cpp`'s WebP reader walks the RIFF chunks raw).
- `TaskDialog` / `TaskDialogIndirect` come from `<commctrl.h>` + `comctl32.lib`; `Metadata.cpp` and `Dialogs.cpp` each include both explicitly. The main file has **no** explicit `commctrl.h` include and has always picked the declaration up implicitly. Keep the explicit include in any module that calls a `TaskDialog*`.
- `SHChangeNotify` and `SHCNE_*` / `SHCNF_*` are declared in **`ShlObj.h`**, NOT in `shellapi.h` - checked against the 10.0.19041.0 and 10.0.26100.0 SDKs. `Dialogs.cpp` includes both.
- `<CharacterSet>Unicode</CharacterSet>` in the vcxproj is what defines `UNICODE`/`_UNICODE`, so the bare `CreateWindowEx` / `PostMessage` macro calls resolve to the **W** variants (confirmed from the exe's imports). Don't "fix" them to `ExW`.
- `Resource.h` IDs in use: `IDR_DEFAULT_BKG_JPG`, `IDD_ABOUTBOX`, `IDD_COMMANDBOX`, `IDD_PROPERTIES`, `IDC_SYSLINK_GITHUB`, `IDC_PROP_*` (13 controls), `MetadataControlId::EDIT`, `MetadataControlId::SELECTALL`.
- Widget files: `AnimatedButton.h/.cpp`, `UITextBox.h/.cpp`, `UIElement.h/.cpp` (both widgets use `Config` + `Layout{Anchor,PosMode,ActivationZone,uiPixelScale,referenceWidth/Height}` + `Initialize`/`UpdateLayout`/`Update`/`UpdateProximity`/`Draw`/`OnMouseDown`/`OnMouseUp`/`SetForcedVisibility`/`IsFocused`/`SetText`/`SetTooltip`), plus `framework.h`, `Resource.h`, `targetver.h`.

## 14. Known issues

Two bugs are known and **accepted as-is** rather than fixed:

- **Multi-monitor sizing.** The initial window size and centring come from
  `GetSystemMetrics(SM_CXSCREEN)` - primary monitor only - and the shared text format is sized from
  it too. The fullscreen overlay path *is* monitor-aware. Reacting to a DPI change while running is
  deliberately not implemented: there is no `WM_DPICHANGED` handler even though the process is
  `PER_MONITOR_AWARE_V2`, and one should not be added. **Already checked, do not re-check:**
  `CreateBlackCoverWindow`, `CreateOverlayWindow` and `CaptureDesktop` each take the monitor from
  `MonitorFromWindow(..., MONITOR_DEFAULTTONEAREST)`, and `CreateRenderTarget` sizes the swap chain
  from `GetClientRect` of the window it is handed. On a multi-monitor machine the diagnostic is at
  `EnterFullscreen` stage 2: compare `MonitorFromWindow(g_app.mainWindow, ...)` with the overlay's
  `GetWindowRect`. Also latent: `Render`'s default-background branch calls
  `GetClientRect(g_app.mainWindow, ...)` while drawing into the current render target, which in
  fullscreen is the overlay - unreachable today because fullscreen is never entered with no image
  loaded. Fix it as part of this item, not standalone, and not by making no-image fullscreen possible.
- **Cancelling a slideshow inside the 150 ms fullscreen stage-1 gap.** `Esc` or `F5` in that window
  clears the pre-fade but leaves the mode on, so the app fades to black and comes back up in
  slideshow mode. A fix was attempted and reverted: the flags available in `ExitSlideshowMode`
  cannot tell a stage-1 cancel from the late call that `CompleteExitFullscreen` makes, and the
  attempt regressed the ordinary stop fade. **Do not retry that shape** - `ExitSlideshowMode` is
  also re-entered from `CompleteExitFullscreen` (`if (g_slideshow.active) ExitSlideshowMode();`) with
  the overlay already gone and `preFade` still set, so an `active && !overlayWindow` guard fires
  during a normal exit too. A real fix needs state that can tell the two apart, or has to defer
  phase 2's `active = true` until the overlay exists. The slideshow button cannot reach the gap
  because the main window is alpha-0 through all of stage 1.

Behaviour that looks wrong but is deliberate: Shift+Delete deletes permanently with no confirmation;
the slideshow blur keeps `D2D1_BORDER_MODE_HARD` (`SOFT` darkens two screen edges); the slideshow
`SetTimer` in `UpdateEngine`'s phase-2 block is unguarded on purpose, since every image-change path
re-arms through `RestartSlideshowTimer`; the thumbnail rubber-band drag floats at the ends of the
strip; `F` / fullscreen does nothing when no image is loaded, so a fresh launch with no file stays
windowed; typing in the zoom box during a slideshow re-arms the 6 s interval. **`Esc` in fullscreen
quits the program**, not just fullscreen (`InitFullScreenExit`), which is what the "Mouse and
keyboard commands" dialog promises. **Deleting the last file in the folder also quits the program**,
and so does a directory-change notification that leaves the folder with no supported images.

Two decode limitations are documented in `Decoding.cpp` rather than fixed: only the **first frame of
an animated JXL** is decoded, and **AVIF `irot`/`imir` orientation is not applied**. EXIF
orientations 5-8 apply the rotation only, with no mirror, because the render pipeline has no flip support.

**A slideshow never shows the desktop capture as its background** - it always draws the blurred
version of the current image. So the fact that `CaptureDesktop` grabs the monitor's *work area*
while a slideshow overlay covers the whole *monitor* has no visible effect; do not "fix" it by
capturing `rcMonitor`.

### Smoke-test areas

These paths have been exercised on the real app, so a change that touches one of them needs a
**fresh** smoke test from the user - ask, don't assume. Re-ask for the specific interaction, not a
general "does it still work".

- Directory-watcher shutdown and folder switching; `Del` / `Shift+Del` / `Esc`-windowed / `F`;
  right-click metadata, Properties, and Copy image -> paste into Paint.
- Animated playback in every supported format, plus a fullscreen toggle and a resize mid-playback, a
  large file still decoding, and the slideshow prev-image cross-fade.
- Film strip: fill-in as a folder loads, click-to-jump (which also re-arms the slideshow timer), drag
  and coast, auto-centring, the strip after a fullscreen round trip, and the strip fading in over the
  corrupted-file error screen.
- Slideshow: start/stop by button and `F5`, prev/next while running, the pre-fade and the stop fade
  (fullscreen-to-fullscreen and windowed-to-windowed, taskbar from a slideshow, image re-fitted, no
  garbage flash at the resize), the cross-fade, the blurred background, and `Esc` mid-slideshow.
- Fullscreen lifecycle: entering and leaving by every path including double-click and `Esc`, the
  enter/exit dim, the exit fade, and opening a dialog or the right-click menu in fullscreen without
  dropping out.
- Vignettes: bars sliding in on zone entry, their height across a fullscreen entry, and the strip
  riding on the bottom bar's visibility.
- Widget behaviour: zoom in/out and the 1:1 button, pan-drag, the rotate buttons and the 90-degree
  snap, per-file view restore (open A, zoom, go to B, back to A), the typed-zoom readout (Enter
  commits, Esc cancels, clicking away), tooltips and their dwell delay, and every button's
  hold-repeat (including that Open / hamburger / slideshow do not auto-repeat).
- Launch paths: from the command line, by drag-drop, from a second instance (`WM_COPYDATA`), and the
  two persisted settings (`HQFilter`, `FitToScreen`) surviving a restart.
- Clean exit from both windowed and fullscreen (the container clearing in `WM_DESTROY`).
- **Verified most recently by the user:** corrupt file + Alt+Tab (window comes back launch-sized and
  centred); exiting fullscreen with the image panned flush to the right edge (no ~15 px shift);
  `F` twice quickly (no flash); the exit button's faster exit. Also covered by `_exit_harness.ps1`
  (11 exit scenarios) and `_fullscreen_probe.ps1` (P-A invisible window, P-B stage-1 gap, P-C
  work-area clamp).
- **Not yet exercised since the Thumbnails changes:** the new `WM_APP_DIRCHANGE` quit (delete or move
  out every image in the folder from Explorer while the app is open - the app should fade out and
  quit), and a directory change while fullscreen (the notification now goes to the main window).

Nothing multi-monitor has been verified at all - the development machine has one screen.

## 15. Working rules and local tools

### Rules

- **Report before changing.** Findings (a comment that disagrees with the code, or a bug) get reported
  to the user for a keep/change decision **before** any edit is made. Comment fixes and code fixes are
  separate decisions.
- **Encoding.** UTF-8 **without BOM**, and **no non-ASCII byte in any `.cpp`/`.h`**. MSVC is not given
  `/utf-8`, so it reads sources in the system ANSI code page - which is why every typographic character
  in a *string literal* is a `\uXXXX` escape (see `Metadata.cpp`) and why a glyph such as the slideshow
  stop button is written `L"\u25A0"`. `_bytes.ps1` printing nothing is the check. Sources are CRLF;
  if an editor writes LF-only, normalise it back.
- **Comments** follow the standing rule at the top of this file. They also state what the code *does*:
  a banner that names the wrong function, the wrong window count, or a format the code no longer
  supports is a bug in the comment.
- **Verify a comment-only edit** with `_diffcheck.ps1` ("code parts identical") and/or
  `verify_code.ps1` ("ALL BODIES CODE-IDENTICAL") before committing it, so a comment sweep cannot
  quietly delete a line of code.
- **Build baseline.** `_build.ps1` (full `/t:Rebuild`, Release x64) must report **0 errors** and
  **0 warnings**. It used to report four `LNK4286` warnings, all from `msvcprt.lib`; that library was
  dropped from the Release x64 `AdditionalDependencies` (it is the import lib for the DLL STL, and
  with `/MT` the static `libcpmt.lib` is what should be used). If `LNK4286` comes back, something has
  put `msvcprt.lib` back in the link. This did **not** remove the `MSVCP140.dll` import - see section
  13, that comes from `jxl.lib`. (The `Debug` configuration is broken by the prebuilt Release-CRT
  `jxl.lib`; Release x64 only.)
- **Keep tooling light** - the model runs on the user's machine and most of its RAM/VRAM is in use,
  and a full symbol enumeration (`SymEnumSymbols("*")`) is off the table. Never launch the app or
  drive the GUI without asking. Logic is verified with throwaway headless harnesses compiled against
  the real `.cpp` (confirm they fail before the fix, delete them afterwards); visual judgement is the
  user's. Harnesses live in `%TEMP%`, never in the repo.
- **Git.** This cycle's work is squashed into one commit on `master` (2 ahead of `origin/master`,
  unpushed) with safety branches `backup-before-squash-20261019` and `pre-squash-backup`; new fixes
  are amended into it rather than added as separate commits. Run `git` from the repo root (one
  directory up) - `git -C ..\.git` fails. `*.ps1`, `MODULE_AUDIT.md`, `TODO.md`, `.continue/`,
  `.localpilot/` and `.vscode/` are gitignored; `*.pre` snapshots are **not**, so delete them and
  `git add` the files you touched by name rather than `-A`.

### Local tools (gitignored - a fresh clone will not have them)

| Tool | What it's for |
|---|---|
| `_status.ps1` | **warm start**: per-file lines/bytes/non-ASCII/BOM, the main file's fwd-decl block, a per-TU definition count, blank-line seams. Run it first in a session. |
| `_build.ps1` | MSBuild `/t:Rebuild` -> `build.log`, then `EXIT=`, the error/warning counts and every error/warning line. |
| `_grep.ps1` | content search in ONE file: `-Pat <regex>` or `-Names "a,b"`; `-Src <file>` (defaults to the main cpp, so pass `-Src CODE_MAP.md` for the docs). |
| `xgrep.ps1` | `_grep` across the whole directory: `-Pat <regex>`, `-Filter "*.cpp,*.h"` (default) or `"*.md"`. Prints `file line: text`. Also good for non-ASCII: `-Pat "[^\x00-\x7F]"`. |
| `delrange.ps1` | delete an inclusive 1-based line range: `-File F -Start N -End M` with `-MustStart`/`-MustEnd` regex guards so the range cannot drift onto the wrong lines. Preserves the file's line endings. |
| `verify_code.ps1` | `-Module X -Ref Y` -> compares every function body **with comments stripped**, so a comment-only edit still reports `ALL BODIES CODE-IDENTICAL`. Copy `X` to `X.pre`, edit, compare, delete the `.pre`. |
| `_diffcheck.ps1` | `-Files "a,b"` -> runs `git diff -w -U0` and says whether the diff is comment-only. The cheap check for a small edit inside a big function. |
| `_bytes.ps1` | every distinct non-ASCII run in the tree with its code points and files. **No output = the tree is pure ASCII.** |
| `_nonascii.ps1` | per-file bytes / non-ASCII / BOM. **Needs `-Files "a,b,c"`** - with no arguments it scans nothing and prints `TOTAL = 0`, which looks like a pass and proves nothing. |
| `_peek.ps1` | `-File F -Start N -End M` - a line range with every line's non-ASCII code points tagged, so you see the file's **real** line wraps. Use it to copy a doc anchor exactly. |
| `split_helper.ps1` | `<mode>` is positional (`cat`, `catfn`, `bounds`, `map`, `slice`); the file is **`-Src`** (default: the main cpp), with `-StartLine`/`-EndLine` or `-Anchor`. The convenient way to read a range or a whole function of a big file. (`-File` is *not* a parameter - it errors out.) |
| `_exit_harness.ps1` | **crash harness for the exit paths**: launches the built exe in a `%TEMP%` sandbox and drives ~11 exit scenarios (windowed / fullscreen / failed load / animated mid-decode / slideshow / directory-change / delete-the-last-file) with `PostMessage(WM_CLOSE / Esc / F / Del / F5)` only - no mouse, no keyboard injection - then reports each process exit code and any Application-log WER entries. Clean = `0x00000000`; a crash shows as the exception code. **Launches real windows on the user's desktop - ask first.** |
| `_fullscreen_probe.ps1` | **state probe for the fullscreen lifecycle**: launches the exe, drives it with posted `WM_KEYDOWN(F)` / `WM_ACTIVATE(WA_INACTIVE, GetShellWindow())` / a posted `WM_LBUTTONDOWN`+`WM_MOUSEMOVE`+`WM_LBUTTONUP` drag only, then reads back the process's own windows - is `OverlayWindowClass` present, is the main window visible, is it `WS_EX_LAYERED` and at what alpha. P-A = failed load + focus loss (the invisible-window case), P-B = two `F` presses inside the stage-1 gap, P-C = pan the image flush against the right edge and exit (the only way to make the work-area clamp actually move the window). Note `WM_ACTIVATE` cannot reach the stage-1 gap. **Launches real windows - ask first.** |
| `verify_move.ps1` | `-Module X -Ref Y` -> `IDENTICAL`/`DIFFERENT`/`MISSING` per body, comments included: the blast radius of a code edit. Caveat: an insertion near the top of a long function prints its whole shifted tail - prefer `_diffcheck.ps1`. |

`_ascii.ps1`, `_blanks.ps1`, `_crlf.ps1`, `_id*.ps1`, `_mojibake.ps1`, `_narr.ps1`, `_show.ps1`,
`_splice*.ps1`, `_struct.ps1`, `_sweep.ps1`, `split_helper.ps1`'s splice modes and
`verify_move`'s `.bak` workflow are split-era leftovers with no current use - see MAP_HISTORY.md.

### PowerShell gotchas, already paid for

Single-dash params only (`--Flag` is a syntax error); pass a list as **one string** (`-Files "a,b"` -
array literals do not survive `-File` argv binding); **`$` is stripped** from
`powershell -Command "..."` sent through the shell tool, so anything containing a variable must live in
a `.ps1` and run with `-File`; parameter names are case-insensitive, so a `-Files` parameter and a
local `$files` are the same variable and the local silently wins; give a full rebuild 60-120 s; `&&`
is not a statement separator in this PowerShell version, use `;`. `files__edit_file` is atomic - one
bad anchor aborts every edit in the call without applying any of them, so grep the exact wrapped line
first, and re-read the file after a multi-edit call to confirm it landed. A `read_text_file` of a
markdown file **re-wraps** long paragraphs, so what it prints is not the file's line breaks - take doc
anchors from `_peek.ps1` or `_grep.ps1` output, never from a read of the whole file. Note `_grep` and
`xgrep` print non-ASCII as plain `-`, and `_peek` tags it as `<<U+XXXX`: neither is safe to copy as an
edit anchor in a file that contains real em dashes (the `.md` files do).
