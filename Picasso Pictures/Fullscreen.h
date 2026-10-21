#pragma once
// Fullscreen.h : fullscreen / overlay / slideshow lifecycle.
//
// Owns the two-stage fullscreen entry, the fade-driven exit, the black-cover
// window that hides D3D device recreation, the desktop capture, and the
// slideshow enter/exit flags.
//
// Read the lifecycle notes in CODE_MAP.md section 3 before changing anything
// here - the sequencing is load-bearing:
//   * EnterFullscreen is 2-stage (layered+alpha-0 main window, then a 150 ms
//     timer that captures the desktop, calls DiscardDeviceResources, creates
//     the overlay and re-uploads every GPU resource).
//   * ExitFullscreen only starts the fade; UpdateEngine drives
//     g_fullscreen.exitBgFadeAlpha and calls CompleteExitFullscreen when it reaches 1.
//   * DwmFlush() in CreateBlackCoverWindow is load-bearing: without it the
//     cover is not yet composited when the render target is destroyed.
//   * WM_ACTIVATE decides whether losing activation means "exit fullscreen";
//     it uses IsOwnedByOurWindow / FullscreenExitSuppressor from Globals.h.

#include "Globals.h"

// ---- window classes ----
// The overlay class deliberately reuses the main window's WndProc, so this
// file also needs WndProc to be declared (see Fullscreen.cpp).
ATOM RegisterOverlayClass(HINSTANCE hInstance);

// Plain black popup used to hide D3D device recreation. DefWindowProc +
// BLACK_BRUSH only - no D2D involved.
void RegisterBlackCoverClass(HINSTANCE hInstance);
void CreateBlackCoverWindow();
void DestroyBlackCoverWindow();

// ---- overlay window + desktop capture ----
HWND CreateOverlayWindow(HWND parent);
void CaptureDesktop(HWND hWnd);

// ---- fullscreen lifecycle ----
// NOTE: the default arguments live HERE and only here. The definition in
// Fullscreen.cpp must not repeat them (C2522).
void EnterFullscreen(bool preserveView = false, bool needsDelay = true);
void ExitFullscreen(bool immediate = false);

// Inner teardown; called from ExitFullscreen's immediate path and from
// UpdateEngine once the exit fade finishes. It has external linkage precisely
// because that caller lives in the main file. It also cancels a pending
// stage-1 entry timer, and it runs in full even with no image or no overlay:
// the main window is layered and fully transparent by that point, so skipping
// the rest of the teardown would leave the app running but invisible.
void CompleteExitFullscreen();

// Shrink-to-0.0005 + g_app.isExiting = true: this quits the whole program, it is
// not a fullscreen exit. Callers: Esc while fullscreen, the exit button, and
// deleting the last file in the folder. With no image on screen it only sets the
// flag, because there is nothing to shrink.
void InitFullScreenExit();

// ---- slideshow ----
// EnterSlideshowMode only sets g_slideshow.preFade; the phase machine in
// UpdateEngine does the rest. CreateSlideshowBgBitmap belongs to Rendering
// (it is a mip-extract + Gaussian blur), not here.
void EnterSlideshowMode();
void ExitSlideshowMode();

// Give the current image a full SLIDESHOW_INTERVAL_MS before the next advance.
// Every path that changes image while a slideshow runs has to call this - the
// prev/next buttons, any key, and a thumbnail click - or the advance that was
// already scheduled fires almost immediately on the new image. No-op outside
// slideshow mode. Starting a slideshow is a different thing: UpdateEngine's
// phase-2 block arms the timer for the first time.
void RestartSlideshowTimer();

// Moves the live fullscreen overlay between the two rects fullscreen can occupy:
// a slideshow covers the whole monitor, plain fullscreen only the work area.
// Called by UpdateEngine's stop fade at full black, never by the caller that
// stops the slideshow - the resize is meant to be invisible.
void ResizeOverlayToWorkArea();
