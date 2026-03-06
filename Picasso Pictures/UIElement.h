#pragma once
#include <d2d1.h>
#include <wrl/client.h>
#include <algorithm>

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

    // Update for the current render target size
    void UpdateLayoutForSize(float rtWidth, float rtHeight);

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

protected:
    // Derived classes call this after setting pixel sizes.
    void SetPixelSize(float wPx, float hPx);

    float GetPixelWidth() const { return m_pixelWidth; }
    float GetPixelHeight() const { return m_pixelHeight; }

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

private:
    // Core
    static float ComputeCenterFromSpec(float axisLen, const Axis& spec);
    static D2D1_RECT_F ExpandRect(const D2D1_RECT_F& r, float pad);

    float ResolveAxisCenter(float axisLen, const Axis& spec, const AxisCache& cache) const;

    void CaptureAxis(AxisCache& cache, float axisLen, float center, const Axis& spec);
};