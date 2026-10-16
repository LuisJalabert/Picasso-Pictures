// ViewMath.cpp : implementation of the view-geometry helpers declared in ViewMath.h.
//
// GetClientSizeF is file-local because only the two offset helpers below call it;
// the helpers declared in ViewMath.h have external linkage.

#include "ViewMath.h"

#include <cmath>    // cosf / sinf / fabsf (IsPointOverImage)

static bool GetClientSizeF(HWND hWnd, float& outW, float& outH)
{
    RECT rc{};
    if (!GetClientRect(hWnd, &rc)) return false;
    outW = float(rc.right - rc.left);
    outH = float(rc.bottom - rc.top);
    return outW > 0.f && outH > 0.f;
}

bool PanFromOffsets(HWND hWnd, float zoom, float offsetX, float offsetY, float imgW, float imgH, float& outPanX, float& outPanY)
{
    float winW = 0.f, winH = 0.f;
    if (!GetClientSizeF(hWnd, winW, winH) || zoom <= 0.00001f) return false;

    const float imgCenterX = offsetX + (imgW * zoom * 0.5f);
    const float imgCenterY = offsetY + (imgH * zoom * 0.5f);

    outPanX = imgCenterX - (winW * 0.5f);
    outPanY = imgCenterY - (winH * 0.5f);
    return true;
}

bool OffsetsFromPan(HWND hWnd, float zoom, float imgW, float imgH, float panX, float panY, float& outOffsetX, float& outOffsetY)
{
    float winW = 0.f, winH = 0.f;
    if (!GetClientSizeF(hWnd, winW, winH) || zoom <= 0.00001f) return false;

    outOffsetX = (winW * 0.5f) + panX - (imgW * zoom * 0.5f);
    outOffsetY = (winH * 0.5f) + panY - (imgH * zoom * 0.5f);
    return true;
}

bool IsPointOverImage(float x, float y)
{
    if (!g_view.bitmap) return false;

    const D2D1_SIZE_F imgSize = g_view.bitmap->GetSize();
    if (imgSize.width <= 0.f || imgSize.height <= 0.f) return false;

    const float halfW = imgSize.width  * g_view.zoom * 0.5f;
    const float halfH = imgSize.height * g_view.zoom * 0.5f;

    // Screen-space centre of the image: the pivot Render rotates about, and the
    // one point that stays put while the image turns.
    const float cx = g_view.offsetX + halfW;
    const float cy = g_view.offsetY + halfH;

    // Undo that rotation, then test the unrotated rect. D2D's
    // Matrix3x2F::Rotation(angle) maps x' = x*cos - y*sin, y' = x*sin + y*cos
    // relative to the pivot, so the inverse is the same map with -angle.
    const float theta = -g_view.rotationAngle * 3.14159265359f / 180.0f;
    const float dx = x - cx;
    const float dy = y - cy;
    const float rx = dx * cosf(theta) - dy * sinf(theta);
    const float ry = dx * sinf(theta) + dy * cosf(theta);

    return (fabsf(rx) <= halfW) && (fabsf(ry) <= halfH);
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

