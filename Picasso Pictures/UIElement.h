#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <string>

class UIElement
{
public:
    // How Axis::value is interpreted - see the Axis::value comment below.
    enum class PosMode  { Normalized, Pixels};
    // Which end of the axis the cached pixel offset is measured from once the
    // anchors have been captured - see ResolveAxisCenter.
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

        // Normalized: value is a fraction of the axis length (0..1 is the usual
        // range, but it is not clamped). Pixels: value is an absolute offset in
        // pixels from the centre of the axis (negative allowed).
        // ComputeCenterFromSpec is what applies this. Every call site in UI.cpp
        // uses Normalized, so Pixels is currently unused.
        float value = 0.5f;
    };

    struct Layout
    {
        Axis x;
        Axis y;

        // Relative scalars: the pixel size is uiPixelScale * width / height,
        // computed in InitializeLayout and UpdateLayout. Font size is deliberately
        // not a Layout member - each derived class keeps its own relative factor
        // and multiplies it by the same uiPixelScale (AnimatedButton::Config::
        // fontSize, UITextBox::Config::relativeFontSize).
        float width = 0.10f;
        float height = 0.05f;

        // Optional per-element nudges, always applied in pixels.
        float extraOffsetX = 0.f;
        float extraOffsetY = 0.f;

        // Hit padding in pixels: grows m_activationRect outwards from m_rect.
        // No call site sets it today, so the two rects are identical in practice.
        // They are not interchangeable: HitTest tests m_activationRect while
        // m_mouseOverSelf (and therefore the tooltip and hold-repeat) use m_rect,
        // so setting this makes "clickable" larger than "hovered".
        float activationPadPx = 0.f;

        // Activation zone (relative to current window) used by UpdateProximity
        ActivationZone activationZone;

        // UI scale factor in pixels: min(render-target width, height) as it was at
        // this element's Initialize(). Copied into m_layout there and only refreshed
        // by a later Initialize(), so a WM_SIZE resize never rescales the element -
        // UpdateLayout re-derives the same wPx/hPx from frozen values.
        // The two widget groups are initialized against different render targets, so
        // they get different scales: the menu buttons at startup (the windowed client)
        // and the 9 in-image buttons + both text boxes at fullscreen stage 2, which is
        // the ONLY place they are ever created. That is why their relative font factors
        // differ so much (0.04 vs 0.011) while the rendered glyph sizes are within
        // about 1.7x - the factors were hand-tuned against different scales, and the
        // symbols are deliberately tuned per glyph to sit right inside the pills.
        // Tooltip text does NOT use this; see tooltipFontSize below.
        float uiPixelScale = 1.0f;

        // Size the anchors are captured against. Every call site sets this to
        // renderTarget->GetSize() at Initialize time - the current window while
        // windowed, the overlay while fullscreen - never the monitor.
        // Left at 0, CaptureAnchorsOnce bails out, the element stays uncaptured and
        // is therefore recomputed proportionally on every layout.
        float referenceWidth = 0.f;
        float referenceHeight = 0.f;

        // Tooltip font size, relative to the process-wide scale g_view.uiPixelScale
        // (NOT to uiPixelScale above), so every element's tooltip renders at the same
        // pixel size regardless of when that element was Initialize()'d. Uniform by
        // design - do not add per-element overrides.
        float tooltipFontSize = 0.0144f;
    };

public:
    virtual ~UIElement() = default;

    // Copy a Layout in. Deliberately does NOT reset the anchor caches - see
    // ResetAnchors and the note on m_xCache / m_yCache below.
    void SetLayout(const Layout& layout);

    const Layout& GetLayout() const { return m_layout; }

    // Layout updates.
    // While an axis is uncaptured, UpdateLayout recomputes it from the Layout every
    // time, which keeps the element fully proportional to the current render target.
    // Captures using referenceWidth/referenceHeight; a no-op once both axes are
    // captured, and it bails out when referenceWidth/referenceHeight are 0.
    // Public but only ever called from inside this class.
    void CaptureAnchorsOnce(float rtWidth, float rtHeight);
    // Clears both axis caches and the zone cache so the next layout re-captures.
    // Nothing calls this today: fullscreen entry re-Initializes the buttons but
    // keeps the pixel offsets captured at startup, which is what pins the bottom
    // button row to a fixed pixel distance from the centre instead of a fraction.
    void ResetAnchors();

    // Update for the current render target size
    void UpdateLayoutForSize(float rtWidth, float rtHeight);

    // Handles pixel-size change detection, anchor capture, and layout update.
    // Calls EnsureTextFormat() when the pixel size changes. Only re-captures the
    // anchors if they have never been captured.
    void UpdateLayout(ID2D1RenderTarget* renderTarget);

    // Convenience for Initialize: sets pixel size, captures anchors, runs UpdateLayout + EnsureTextFormat.
    // Derived class must set m_pixelFontSize and call SetLayout() before calling this.
    void InitializeLayout(ID2D1RenderTarget* renderTarget);

    // Rects / hit testing. GetActivationRect has no callers outside this class;
    // HitTest is the one button presses go through.
    D2D1_RECT_F GetRect() const { return m_rect; }
    D2D1_RECT_F GetActivationRect() const { return m_activationRect; }
    bool HitTest(float x, float y) const;

    // Visibility / proximity (shared between button & textbox).
    // dt is the real frame time in seconds (UpdateEngine's dt, clamped to 0.03 s by
    // the message loop). falloff is written as the fraction one 60 Hz frame consumes
    // and is rescaled by SmoothAlpha, so the fades and the tooltip / focus-pulse
    // timers below take the same wall-clock time at any refresh rate. No default
    // argument on purpose: a caller that forgets dt must fail to compile rather than
    // silently reintroduce frame-rate-dependent timing.
    void UpdateProximity(float mouseX, float mouseY, float windowWidth, float windowHeight);
    void UpdateVisibility(float dt, float falloff);

    // Drive m_targetVisibility by hand. Effective until ClearTargetVisibility():
    // while an override is in force UpdateProximity keeps tracking m_mouseOverSelf
    // and the activation zone but stops writing the target, and UpdateVisibility
    // lets the element fade out even if it is forced/focused - the caller asked for
    // a specific value, so it is authoritative. Precedence is
    // override > forced visibility > activation zone.
    // SetForcedVisibility is the right tool for "always shown"; this one is for an
    // arbitrary level, e.g. dimming an element to a fixed alpha.
    void SetTargetVisibility(float v);
    // Hand control of the target back: the zone decision remembered from the last
    // UpdateProximity is applied straight away, so this does not wait for the next
    // WM_MOUSEMOVE.
    void ClearTargetVisibility();
    bool HasTargetVisibility() const { return m_targetOverridden; }
    float GetTargetVisibility() const { return m_targetVisibility; }
    float GetVisibility() const { return m_visibility; }

    void SetForcedVisibility(bool forced);
    bool IsForcedVisibility() const { return m_forcedVisibility; }   // no external callers

    // ---- Tooltip ----
    // Set a tooltip string on any element; empty = no tooltip.
    // Call SetTooltip() after Initialize() (the factory must already be set).
    void SetTooltip(const std::wstring& text);
    // Draw the tooltip once the dwell in UpdateVisibility has fired - about one
    // second with m_mouseOverSelf true. Call at the end of Draw().
    void DrawTooltip(ID2D1RenderTarget* rt);

protected:
    // Derived classes call this after setting pixel sizes.
    void SetPixelSize(float wPx, float hPx);

    float GetPixelWidth() const { return m_pixelWidth; }
    float GetPixelHeight() const { return m_pixelHeight; }

    // Rebuilds m_textFormat from m_dwriteFactory and m_pixelFontSize.
    // Called automatically by UpdateLayout on scale change and by InitializeLayout.
    void EnsureTextFormat();

    // (Re)builds m_tooltipTextLayout when it is missing or the tooltip font size has
    // changed since it was built. Called by SetTooltip and by DrawTooltip.
    void EnsureTooltipTextLayout();

    // Pixel size of the tooltip text: g_view.uiPixelScale * Layout::tooltipFontSize,
    // floored at 1. Shared by SetTooltip, EnsureTooltipTextLayout and DrawTooltip so
    // the measuring layout and the drawn text can never disagree.
    float TooltipFontPx() const;

    // Shared DWrite factory - set by derived class during Initialize.
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
    // Three owners of m_targetVisibility, in precedence order: the explicit
    // override below, then m_forcedVisibility, then the activation zone.
    // m_proximityTarget remembers what the zone decided the last time
    // UpdateProximity ran, even while something higher-ranked owns the target, so
    // ClearTargetVisibility can hand control back immediately.
    bool  m_targetOverridden = false;
    float m_proximityTarget  = 0.f;
    bool  m_forcedVisibility = false;
    // Focus state lives in the base class so UpdateVisibility can gate on it, but
    // only UITextBox ever sets it: for a button m_focused and m_selectAllOnFocus
    // stay false, so the focus branch there never runs.
    bool  m_focused = false;
    float m_focusPulseTime = 0.f;   // advanced by UpdateVisibility, read by UITextBox's highlight pulse
    bool  m_selectAllOnFocus = false;
    struct AxisCache
    {
        bool captured = false;

        // Pixel offsets for *center coordinate*.
        float offsetFromStartPx = 0.f; // centerX or centerY measured from 0
        float offsetFromCenterPx = 0.f; // centerX or centerY measured from center
        float offsetFromEndPx   = 0.f; // (axisLen - center) measured from axis end
    };

    // Once captured, these pixel offsets - not the normalized Layout values - are
    // what place the element, so positions stop scaling with the window even though
    // uiPixelScale may still be refreshed by a later Initialize().
    AxisCache m_xCache{};
    AxisCache m_yCache{};

    // Activation zone captured at CaptureAnchorsOnce() time.
    // Each boundary is stored as (Anchor, insetPx) and resolved by UpdateProximity's
    // resolveBoundary, which mirrors ResolveAxisCenter except for the sign:
    // ResolveAxisCenter returns axisLen*0.5 - offsetFromCenterPx, resolveBoundary
    // returns dim*0.5 + insetPx. They cannot disagree today only because
    // captureBoundary always stores inset 0 for the = 0.5 case.
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
    // Built once per tooltip string / font size and reused for the measuring,
    // because CreateTextLayout re-runs font fallback and text shaping. DWrite
    // objects come from the IDWriteFactory rather than the render target, so they
    // are device-independent and survive DiscardDeviceResources - unlike a cached
    // D2D brush, this one needs no release-on-device-loss plumbing.
    Microsoft::WRL::ComPtr<IDWriteTextLayout>          m_tooltipTextLayout;
    float m_tooltipFontPxUsed = 0.f;    // font px m_tooltipTextLayout was built with
    float m_tooltipVisibility = 0.f;
    float m_tooltipHoverTime  = 0.f;    // dwell accumulator; hits 1.0 after ~1 s at 60 Hz
    bool  m_mouseOverSelf     = false;  // true only when cursor is directly over m_rect

private:
    // Core
    static float ComputeCenterFromSpec(float axisLen, const Axis& spec);
    static D2D1_RECT_F ExpandRect(const D2D1_RECT_F& r, float pad);

    float ResolveAxisCenter(float axisLen, const Axis& spec, const AxisCache& cache) const;

    void CaptureAxis(AxisCache& cache, float axisLen, float center);
};