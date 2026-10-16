#pragma once
// UI.h : the widget initialisers, the image layout/fit rules, and the zoom
// commands.
//
// Read CODE_MAP.md section 8 before changing anything here - the ordering and
// the shared globals are load-bearing:
//   * The three Initialize* functions build the whole widget set once, at
//     startup, against the *initial* render target: InitializeMenuButtons
//     (Open + hamburger, always visible), InitializeButtons (the 9 in-image
//     buttons, each with its own ActivationZone + normalized anchor +
//     uiPixelScale = min(w,h)) and InitializeImageInfoLabel (filename display
//     + the numeric zoom box). WM_SIZE only calls UpdateLayout on them - never
//     re-Initialize - so any per-button state has to survive a resize.
//   * The button click handlers are lambdas that call straight back into the
//     navigation code (OpenImageFile / OpenPrevImage / OpenNextImage) and into
//     Fullscreen (EnterSlideshowMode). That is why UI sits at the TOP of the
//     include graph: it is the module with the most edges out.
//   * InitializeImageLayout is the fit rule used at load time, on WM_SIZE and
//     after a fullscreen round trip. hard=true snaps, hard=false starts at
//     g_view.zoom = 0.05f and springs in; it early-outs when
//     g_view.restoredStateThisLoad is set, so a saved per-file view wins.
//   * SetZoomCentered / FitToWindowRelative / UpdateTargetZoom only ever move
//     the *target* values - UpdateEngine (still in the main file) does the
//     spring. Never write g_view.zoom directly from here.
//
// Linkage rules for this module:
//   * Nothing in it is `static`.
//   * The two DEFAULT ARGUMENTS below belong to these declarations and nowhere
//     else: FitToWindowRelative(..., bool preserveCenter = true) and
//     InitializeImageLayout(..., bool hard = false). Both are called with that
//     argument omitted - from UI.cpp, from the main file and from Rendering.cpp
//     - so the default has to be on the visible declaration every caller sees;
//     repeating it on a definition is C2522.
//
// Deliberately NOT here: OpenImageFile / OpenNextImage / OpenPrevImage /
// DeleteCurrentImage. They are navigation, not widgets, and they are called
// from a dozen WndProc sites plus wWinMain, so they stay in the main file;
// UI.cpp declares the three it calls itself.

#include "Globals.h"

// ---- zoom / fit primitives (target-value writers) ----
// Set the target zoom about a specific client point (or the client centre when
// preserveCenter is false). instant=true skips the spring and jumps.
void SetZoomCentered(float newZoom, HWND hWnd, bool instant, bool preserveCenter);

// Fit the image to `zoom` * window size; preserveCenter keeps the current
// centre instead of re-centring the image in the window.
void FitToWindowRelative(HWND hWnd, float zoom, bool preserveCenter = true);

// Recompute g_view.targetZoom from the current fit (used after a resize or a
// fullscreen switch so the spring lands on the new window size).
void UpdateTargetZoom(float newZoom);

// ---- the fit rule ----
// Opens at 100% unless the image is oversized or g_settings.openFitToScreen, in which
// case it fits at 95%. hard=false starts at g_view.zoom = 0.05f and springs in.
// Called from wWinMain, Render (Rendering.cpp), WM_SIZE and
// DeleteCurrentImage.
void InitializeImageLayout(HWND hWnd, bool hard = false);

// ---- the widget set (called once, in this order, from InitInstance) ----
void InitializeMenuButtons();      // Open (folder glyph) + hamburger, always visible
void InitializeButtons();          // the 9 in-image buttons + their activation zones
void InitializeImageInfoLabel();   // TextBoxId::FILE_NAME + TextBoxId::ZOOM_INPUT

// Re-sync the slideshow button's glyph and tooltip with g_slideshow.active
// (play while stopped, pause while running). Called at every slideshow
// transition, and at the end of InitializeButtons - which rebuilds the button
// from its config text - so a slideshow already running survives that rebuild.
void UpdateSlideshowButton();

// ---- the zoom commands ----
// Wheel/button zoom: pow(1.1, delta/120), clamped 0.01-100, anchored to the
// cursor when the cursor is over the image.
// optionalPt lets WM_MOUSEWHEEL pass a point without a second GetCursorPos.
bool ZoomIntoImage(HWND hWnd, short delta, POINT* optionalPt);

// Show the zoom readout and re-arm the 1 s auto-hide timer.
void MakeZoomVisible(HWND hWnd);
