#pragma once
// ViewMath.h : view-geometry conversion helpers + the image hit test.
//
// The pan/zoom <-> offset maths every other module needs (Fullscreen,
// Rendering, UI and WndProc all call them), plus the one hit test built from
// the same transform.
//
// No calls into any other module, which keeps this header the base of the
// include graph. IsPointOverImage is the only function here that reads global
// view state (g_view.bitmap / g_view.offsetX / g_view.offsetY / g_view.zoom /
// g_view.rotationAngle) - it inverts exactly the transform described below.

#include "Globals.h"

// Image top-left + zoom -> image-centre-minus-window-centre, in pixels.
// false if the client area is degenerate or zoom ~ 0.
bool PanFromOffsets(HWND hWnd, float zoom, float offsetX, float offsetY, float imgW, float imgH, float& outPanX, float& outPanY);

// Inverse of PanFromOffsets - used whenever the render target changes size and
// the offsets have to be recomputed from the preserved pan.
bool OffsetsFromPan(HWND hWnd, float zoom, float imgW, float imgH, float panX, float panY, float& outOffsetX, float& outOffsetY);

// Does the client-space point (x, y) land on the image as it is actually
// drawn? g_view.offsetX/g_view.offsetY + g_view.zoom describe the UNROTATED rect, while Render
// draws the image rotated about its own centre - so a plain rect test is wrong
// as soon as g_view.rotationAngle is non-zero: at 90/270 it reports the empty
// side margins as image and the image itself as empty. This undoes the draw
// transform instead: rotate the point back about the image centre, then test
// the unrotated rect. False when no image is loaded.
bool IsPointOverImage(float x, float y);

// Refresh rate of the monitor hosting hWnd, via EnumDisplaySettings; 60 if unknown.
// (Not geometry, but it is a monitor query with no dependencies, so it lives here.)
int GetMonitorRefreshRate(HWND hWnd);
