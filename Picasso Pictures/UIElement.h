#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <string>

class UIElement
{
public:
    enum class PosMode  { Normalized, Pixels};
    enum class Anchor   { OffsetFromStart, OffsetFromEnd, OffsetFromCenter};

    struct ActivationZone
    {
        float left = 0.f;
        float top = 0.f;
        float right = 1.f;
        float bottom = 1.f;
    };

    struct Axis
    {
        PosMode     mode = PosMode::Normalized;
        Anchor      anchor = Anchor::OffsetFromCenter;

        // If mode == Relative:        value in [0..1] (not clamped by default)
        // If mode == PixelFromCenter: value in pixels from center (negative allowed)
        float value = 0.5f;
    };

    struct Layout
    {
        Axis x;
        Axis y;

        // These are *relative* scalars multiplied by uiPixelScale.
        // Matches your existing pattern: width/height/fontSize defined relative to ui scale.
        float width = 0.10f;
        float height = 0.05f;

        // Optional per-element nudges (in pixels)
        float extraOffsetX = 0.f;
        float extraOffsetY = 0.f;

        // Hit padding (pixels)
        float activationPadPx = 0.f;

        // Activation zone (relative to current window) used by UpdateProximity
        ActivationZone activationZone;

        // UI scale factor in pixels (you currently set this to min(w,h) or similar)
        float uiPixelScale = 1.0f;

        // Reference size used for anchor capture.
        // In your app, you often set these to the fullscreen reference (monitor w/h)
        float referenceWidth = 0.f;
        float referenceHeight = 0.f;

        // Tooltip font size — relative to uiPixelScale, same for all elements.
        // Override only if you need a custom size; the default matches a comfortable
        // small label that stays readable at typical screen sizes.
        float tooltipFontSize = 0.013f;
    };

public:
    virtual ~UIElement() = default;

    // Call after you fill a Layout.
    void SetLayout(const Layout& layout);

    const Layout& GetLayout() const { return m_layout; }

    // Layout updates
    // - If anchors are not captured, UpdateLayout uses "Recompute" behavior for safety.
    void CaptureAnchorsOnce(float rtWidth, float rtHeight); // captures using referenceWidth/referenceHeight
    void ResetAnchors();       // call if you want to recapture (e.g. entering fullscreen)

    // Capture / release the activation zone in absolute pixels.
    // Call CaptureZone() once after the fullscreen render target is ready,
    // and ReleaseZone() when exiting fullscreen so windowed mode uses
    // the normalized activation zone instead.
    void CaptureZone(float refW, float refH);
    void ReleaseZone();

    // Update for the current render target size
    void UpdateLayoutForSize(float rtWidth, float rtHeight);

    // Handles pixel-size change detection, anchor capture, and layout update.
    // Calls EnsureTextFormat() when the pixel size changes.
    void UpdateLayout(ID2D1RenderTarget* renderTarget);

    // Convenience for Initialize: sets pixel size, captures anchors, runs UpdateLayout + EnsureTextFormat.
    // Derived class must set m_pixelFontSize and call SetLayout() before calling this.
    void InitializeLayout(ID2D1RenderTarget* renderTarget);

    // Rects / hit testing
    D2D1_RECT_F GetRect() const { return m_rect; }
    D2D1_RECT_F GetActivationRect() const { return m_activationRect; }
    bool HitTest(float x, float y) const;

    // Visibility / proximity (shared between button & textbox)
    void UpdateProximity(float mouseX, float mouseY, float windowWidth, float windowHeight);
    void UpdateVisibility(float falloff = 0.08f);

    void SetTargetVisibility(float v) { m_targetVisibility = std::clamp(v, 0.f, 1.f); }
    float GetVisibility() const { return m_visibility; }

    void SetForcedVisibility(bool forced);
    bool IsForcedVisibility() const { return m_forcedVisibility; }

    // ---- Tooltip ----
    // Set a tooltip string on any element; empty = no tooltip.
    // Call SetTooltip() after Initialize() (the factory must already be set).
    void SetTooltip(const std::wstring& text);
    // Draw the tooltip if the dwell timer has fired. Call at the end of Draw().
    void DrawTooltip(ID2D1RenderTarget* rt);

protected:
    // Derived classes call this after setting pixel sizes.
    void SetPixelSize(float wPx, float hPx);

    float GetPixelWidth() const { return m_pixelWidth; }
    float GetPixelHeight() const { return m_pixelHeight; }

    // Rebuilds m_textFormat from m_dwriteFactory and m_pixelFontSize.
    // Called automatically by UpdateLayout on scale change and by InitializeLayout.
    void EnsureTextFormat();

    // Shared DWrite factory � set by derived class during Initialize.
    Microsoft::WRL::ComPtr<IDWriteFactory>     m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>  m_textFormat;

    // Set by derived class before calling InitializeLayout / UpdateLayout.
    float m_pixelFontSize = 16.f;

    // Cached RT size (updated by UpdateLayout).
    float m_lastRTW = 0.f;
    float m_lastRTH = 0.f;

    // Common animation knobs
    float m_currentScale = 1.f;
    float m_targetScale = 1.f;

    float m_currentOpacity = 0.85f;
    float m_targetOpacity = 0.85f;

    Layout m_layout{};

    D2D1_RECT_F m_rect{};
    D2D1_RECT_F m_activationRect{};

    float m_pixelWidth = 1.f;
    float m_pixelHeight = 1.f;

    // visibility driver
    float m_visibility = 0.f;
    float m_targetVisibility = 0.f;
    bool  m_forcedVisibility = false;
    bool  m_focused = false;
    float m_focusPulseTime = 0.f;
    bool  m_selectAllOnFocus = false;
    struct AxisCache
    {
        bool captured = false;

        // Pixel offsets for *center coordinate*.
        float offsetFromStartPx = 0.f; // centerX or centerY measured from 0
        float offsetFromCenterPx = 0.f; // centerX or centerY measured from center
        float offsetFromEndPx   = 0.f; // (axisLen - center) measured from axis end

        // Snapshot of the spec used if you choose to "freeze" recompute (we recompute from Layout by default)
        float capturedValue = 0.f;
        PosMode capturedMode = PosMode::Normalized;
    };

    AxisCache m_xCache{};
    AxisCache m_yCache{};

    // Activation zone captured at CaptureAnchorsOnce() time.
    // Each boundary is stored as (Anchor, insetPx) and resolved with the same
    // switch as ResolveAxisCenter, so it behaves consistently with the axis caches.
    // Anchor is derived from the normalized spec value:
    //   < 0.5  -> OffsetFromStart   (inset from left/top)
    //   > 0.5  -> OffsetFromEnd     (inset from right/bottom)
    //   = 0.5  -> OffsetFromCenter  (inset from centre)
    struct ZoneBoundary
    {
        Anchor anchor  = Anchor::OffsetFromStart;
        float  insetPx = 0.f;
    };
    struct ZoneCache
    {
        bool         captured = false;
        ZoneBoundary left, top, right, bottom;
    };
    ZoneCache m_zoneCache{};

    // ---- Tooltip state ----
    std::wstring                                       m_tooltipText;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>          m_tooltipTextFormat;
    float m_tooltipVisibility = 0.f;
    float m_tooltipHoverTime  = 0.f;
    bool  m_mouseOverSelf     = false;  // true only when cursor is directly over m_rect

private:
    // Core
    static float ComputeCenterFromSpec(float axisLen, const Axis& spec);
    static D2D1_RECT_F ExpandRect(const D2D1_RECT_F& r, float pad);

    float ResolveAxisCenter(float axisLen, const Axis& spec, const AxisCache& cache) const;

    void CaptureAxis(AxisCache& cache, float axisLen, float center, const Axis& spec);
};