#include "UIElement.h"
#include "ViewMath.h"   // SmoothAlpha
#include <cmath>

void UIElement::SetLayout(const Layout& layout)
{
    m_layout = layout;
    // Deliberately leaves m_xCache / m_yCache / m_zoneCache alone. Re-Initializing an
    // element - fullscreen stage 2 does that for the 9 in-image buttons and both text
    // boxes - therefore keeps the pixel offsets captured the first time, while
    // uiPixelScale is refreshed from the new render target. Call ResetAnchors() to
    // re-capture instead; nothing does today.
}

void UIElement::ResetAnchors()
{
    m_xCache    = AxisCache{};
    m_yCache    = AxisCache{};
    m_zoneCache = ZoneCache{};
}

void UIElement::SetPixelSize(float wPx, float hPx)
{
    m_pixelWidth = max(1.f, wPx);
    m_pixelHeight = max(1.f, hPx);
}

bool UIElement::HitTest(float x, float y) const
{
    const auto& r = m_activationRect;
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

float UIElement::ComputeCenterFromSpec(float axisLen, const Axis& spec)
{
    const float half = axisLen * 0.5f;
    if (spec.mode == PosMode::Normalized)
        return axisLen * spec.value;
    return half + spec.value;
}

float UIElement::ResolveAxisCenter(float axisLen, const Axis& spec, const AxisCache& cache) const
{
    // If not captured, safest behavior is recompute (prevents using 0 offsets).
    if (!cache.captured)
        return ComputeCenterFromSpec(axisLen, spec);

    switch (spec.anchor)
    {
    case Anchor::OffsetFromStart:
        return cache.offsetFromStartPx;

    case Anchor::OffsetFromEnd:
        return axisLen - cache.offsetFromEndPx;

    case Anchor::OffsetFromCenter:
        return axisLen * 0.5f - cache.offsetFromCenterPx;
    default:
        return ComputeCenterFromSpec(axisLen, spec);
    }
}

D2D1_RECT_F UIElement::ExpandRect(const D2D1_RECT_F& r, float pad)
{
    return D2D1::RectF(r.left - pad, r.top - pad, r.right + pad, r.bottom + pad);
}

void UIElement::CaptureAxis(AxisCache& cache, float axisLen, float center)
{
    cache.captured = true;
    cache.offsetFromStartPx = center;
    cache.offsetFromEndPx = axisLen - center;
    cache.offsetFromCenterPx = axisLen/2 - center;
}

void UIElement::CaptureAnchorsOnce(float rtWidth, float rtHeight)
{
    if (m_xCache.captured && m_yCache.captured)
        return;

    const float refW = m_layout.referenceWidth;
    const float refH = m_layout.referenceHeight;
    if (refW <= 0.f || refH <= 0.f)
        return;

    // Ensure pixel size is set before capture.
    // Typically derived classes call SetPixelSize() before CaptureAnchorsOnce().
    const float cx = ComputeCenterFromSpec(refW, m_layout.x) + m_layout.extraOffsetX;
    const float cy = ComputeCenterFromSpec(refH, m_layout.y) + m_layout.extraOffsetY;

    CaptureAxis(m_xCache, refW, cx);
    CaptureAxis(m_yCache, refH, cy);

    // Capture activation zone boundaries using the same Anchor pattern.
    // Each normalized value is mapped to an anchor + pixel inset so that
    // UpdateProximity resolves correctly at any window size.
    if (!m_zoneCache.captured)
    {
        auto captureBoundary = [](float normVal, float dimPx) -> ZoneBoundary
        {
            if (normVal > 0.5f)
                return { Anchor::OffsetFromEnd,    dimPx - normVal * dimPx };
            else if (normVal < 0.5f)
                return { Anchor::OffsetFromStart,  normVal * dimPx };
            else
                return { Anchor::OffsetFromCenter, 0.f };
        };
        const auto& z        = m_layout.activationZone;
        m_zoneCache.left     = captureBoundary(z.left,   refW);
        m_zoneCache.top      = captureBoundary(z.top,    refH);
        m_zoneCache.right    = captureBoundary(z.right,  refW);
        m_zoneCache.bottom   = captureBoundary(z.bottom, refH);
        m_zoneCache.captured = true;
    }
}

void UIElement::EnsureTextFormat()
{
    if (!m_dwriteFactory)
        return;

    m_textFormat.Reset();

    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        m_pixelFontSize,
        L"",
        m_textFormat.GetAddressOf());

    if (FAILED(hr) || !m_textFormat)
        return;

    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void UIElement::InitializeLayout(ID2D1RenderTarget* renderTarget)
{
    if (!renderTarget)
        return;

    const float wPx = m_layout.uiPixelScale * m_layout.width;
    const float hPx = m_layout.uiPixelScale * m_layout.height;
    SetPixelSize(wPx, hPx);

    const auto rtSize = renderTarget->GetSize();
    m_lastRTW = rtSize.width;
    m_lastRTH = rtSize.height;
    CaptureAnchorsOnce(m_lastRTW, m_lastRTH);
    UpdateLayoutForSize(m_lastRTW, m_lastRTH);
    EnsureTextFormat();
}

void UIElement::UpdateLayout(ID2D1RenderTarget* renderTarget)
{
    if (!renderTarget)
        return;

    const auto rtSize = renderTarget->GetSize();

    const float wPx = m_layout.uiPixelScale * m_layout.width;
    const float hPx = m_layout.uiPixelScale * m_layout.height;

    if (std::fabs(wPx - m_pixelWidth) > 0.5f || std::fabs(hPx - m_pixelHeight) > 0.5f)
    {
        SetPixelSize(wPx, hPx);
        EnsureTextFormat();
    }

    if (!m_xCache.captured || !m_yCache.captured)
        CaptureAnchorsOnce(rtSize.width, rtSize.height);

    UpdateLayoutForSize(rtSize.width, rtSize.height);
}

void UIElement::UpdateLayoutForSize(float rtWidth, float rtHeight)
{
    if (rtWidth <= 0.f || rtHeight <= 0.f)
        return;

    // Resolve center for each axis (anchor-aware).
    float cx = ResolveAxisCenter(rtWidth, m_layout.x, m_xCache);
    float cy = ResolveAxisCenter(rtHeight, m_layout.y, m_yCache);

    // Extra offsets are always applied in pixels.
    cx += m_layout.extraOffsetX;
    cy += m_layout.extraOffsetY;

    const float hw = m_pixelWidth * 0.5f;
    const float hh = m_pixelHeight * 0.5f;

    m_rect = D2D1::RectF(cx - hw, cy - hh, cx + hw, cy + hh);
    m_activationRect = ExpandRect(m_rect, m_layout.activationPadPx);
}


void UIElement::UpdateProximity(float mouseX, float mouseY, float windowWidth, float windowHeight)
{
    // Whether the cursor is directly over the element's own visual rect. Drives the
    // tooltip dwell timer and AnimatedButton's hold-repeat - deliberately
    // independent of the activation zone, which is much larger.
    m_mouseOverSelf = (mouseX >= m_rect.left && mouseX <= m_rect.right &&
                       mouseY >= m_rect.top  && mouseY <= m_rect.bottom);

    // Resolve zone boundaries: use captured pixel insets if available,
    // otherwise fall back to normalized computation (pre-fullscreen, windowed).
    auto resolveBoundary = [](const ZoneBoundary& b, float dim) -> float
    {
        switch (b.anchor)
        {
        case Anchor::OffsetFromEnd:    return dim - b.insetPx;
        case Anchor::OffsetFromCenter: return dim * 0.5f + b.insetPx;
        default:                       return b.insetPx;  // OffsetFromStart
        }
    };

    float zoneLeft, zoneTop, zoneRight, zoneBottom;
    if (m_zoneCache.captured)
    {
        zoneLeft   = resolveBoundary(m_zoneCache.left,   windowWidth);
        zoneTop    = resolveBoundary(m_zoneCache.top,    windowHeight);
        zoneRight  = resolveBoundary(m_zoneCache.right,  windowWidth);
        zoneBottom = resolveBoundary(m_zoneCache.bottom, windowHeight);
    }
    else
    {
        const auto& z = m_layout.activationZone;
        zoneLeft   = windowWidth  * z.left;
        zoneTop    = windowHeight * z.top;
        zoneRight  = windowWidth  * z.right;
        zoneBottom = windowHeight * z.bottom;
    }

    // The zone's own verdict, remembered every time whether or not it is the one
    // that wins, so ClearTargetVisibility can hand control back immediately.
    m_proximityTarget =
        (mouseX >= zoneLeft && mouseX <= zoneRight &&
         mouseY >= zoneTop  && mouseY <= zoneBottom) ? 1.0f : 0.0f;

    // Precedence: explicit override > forced visibility > activation zone.
    // An override owns the target until ClearTargetVisibility(), so a caller can
    // pin any level it wants and the mouse moving in and out of the zone will not
    // fight it. m_mouseOverSelf and the zone itself keep updating either way, so
    // tooltips and hold-repeat stay live under an override.
    if (m_targetOverridden)
        return;

    if (m_forcedVisibility)
    {
        m_targetVisibility = 1.0f;
        return;
    }

    m_targetVisibility = m_proximityTarget;
}

void UIElement::UpdateVisibility(float dt, float falloff)
{
    // Everything time-driven here works off dt (seconds), so the fades and the two
    // timers below take the same wall-clock time whatever the refresh rate is. They
    // used to add a hardcoded 0.016f per call, i.e. assume 60 Hz: on a 120 Hz panel the
    // tooltip dwell (m_tooltipHoverTime > 1.0f) fired in about 0.52 s instead of 1 s and
    // every fade ran twice as fast - noticeable because g_view.smooth had already been
    // normalized for the refresh rate in wWinMain, so zooming felt right while the UI
    // fades did not. SmoothAlpha returns falloff back unchanged at 60 Hz (to within
    // float rounding), so that case behaves as it always did; measured, the fade to
    // 95% takes 0.600 s at 60/120/144 Hz instead of 0.600 / 0.300 / 0.250 s, and the
    // dwell is a real 1 s instead of 1.04 / 0.52 / 0.43 s.
    const float alpha = SmoothAlpha(falloff, dt);

    float effectiveTarget = m_targetVisibility;

    // An explicit override outranks the "never fade out" rule below: the caller
    // asked for a specific level, including 0, and must be able to fade the element
    // out even while it is forced or focused.
    if (!m_targetOverridden && (m_forcedVisibility || m_focused))
    {
        // Prevent decreasing visibility
        if (effectiveTarget < m_visibility)
            effectiveTarget = m_visibility;
    }

    m_visibility += (effectiveTarget - m_visibility) * alpha;

    // ---- Tooltip dwell timer ----
    // Accumulate only while the cursor is physically over the element's own rect.
    // The activation zone (which drives m_visibility) is deliberately ignored here
    // so the tooltip never fires just because the mouse is somewhere in the zone.
    if (!m_tooltipText.empty())
    {
        if (m_mouseOverSelf)
            m_tooltipHoverTime += dt;   // seconds: the 1.0f threshold below is one second
        else
            m_tooltipHoverTime = 0.f;

        const float tooltipTarget = (m_tooltipHoverTime > 1.0f) ? m_visibility : 0.f;
        m_tooltipVisibility += (tooltipTarget - m_tooltipVisibility) * SmoothAlpha(0.10f, dt);
    }

    // ---- Focus pulse animation ----
    // m_focusPulseTime is real elapsed seconds, which is what UITextBox's
    // sinf(m_focusPulseTime * 2.0f) highlight pulse wants.
    if (m_focused && m_selectAllOnFocus)
    {
        m_focusPulseTime += dt;
    }
    else
    {
        m_focusPulseTime = 0.f;
    }
}

void UIElement::SetTargetVisibility(float v)
{
    m_targetVisibility = std::clamp(v, 0.f, 1.f);
    m_targetOverridden = true;
}

void UIElement::ClearTargetVisibility()
{
    if (!m_targetOverridden)
        return;

    m_targetOverridden = false;

    // Hand control back without waiting for the next WM_MOUSEMOVE: re-apply the
    // owner that now ranks highest, i.e. forced visibility or the remembered zone
    // verdict.
    if (m_forcedVisibility)
        m_targetVisibility = 1.f;
    else
        m_targetVisibility = m_proximityTarget;
}

void UIElement::SetForcedVisibility(bool forced){
    m_forcedVisibility = forced; 
    // An explicit override outranks this one, so it keeps its own value; the forced
    // flag still matters on its own, because it is what stops UpdateVisibility from
    // fading the element out.
    if (m_targetOverridden)
        return;

    if (m_forcedVisibility)
    {
        m_targetVisibility = 1.f; // Ensure it becomes visible
    } 
    else
    {
        // Hand control back to the activation zone rather than forcing 0. Forcing 0
        // was a bug: the zone verdict is only recomputed inside UpdateProximity, i.e.
        // on WM_MOUSEMOVE, so a still cursor left the element stuck invisible. That
        // is what made the zoom readout vanish when Enter committed a typed value -
        // its 1 s ZOOM_DISPLAY timer had already zeroed the target while the user was
        // typing, and only m_focused had been keeping the box up until then.
        m_targetVisibility = m_proximityTarget;
    }
}

float UIElement::TooltipFontPx() const
{
    // g_view.uiPixelScale, not m_layout.uiPixelScale: the latter is a per-element
    // snapshot taken at that element's Initialize(), and the two widget groups are
    // initialized against very different render targets (the menu buttons at startup,
    // against the windowed client; the 9 in-image buttons only at fullscreen stage 2,
    // against the monitor). Sizing tooltips from those gave the Open/hamburger tooltip
    // about 11% larger text than the bottom row's, on every display, permanently.
    return max(1.f, g_view.uiPixelScale * m_layout.tooltipFontSize);
}

void UIElement::EnsureTooltipTextLayout()
{
    if (m_tooltipText.empty() || !m_dwriteFactory || !m_tooltipTextFormat)
    {
        m_tooltipTextLayout.Reset();
        m_tooltipFontPxUsed = 0.f;
        return;
    }

    // Rebuild when the tooltip font size changed, i.e. when the process-wide scale
    // changed - which happens when fullscreen is entered on a different monitor.
    const float fontPx = TooltipFontPx();
    if (m_tooltipTextLayout && fontPx == m_tooltipFontPxUsed)
        return;

    m_tooltipTextLayout.Reset();
    m_tooltipFontPxUsed = 0.f;

    // Same 600 x 200 measuring box the per-frame layout used, so the metrics, and
    // therefore the pill size, are unchanged.
    if (SUCCEEDED(m_dwriteFactory->CreateTextLayout(
            m_tooltipText.c_str(), (UINT32)m_tooltipText.size(),
            m_tooltipTextFormat.Get(), 600.f, 200.f,
            m_tooltipTextLayout.GetAddressOf())))
        m_tooltipFontPxUsed = fontPx;
}

void UIElement::SetTooltip(const std::wstring& text)
{
    m_tooltipText = text;
    m_tooltipTextFormat.Reset();
    m_tooltipTextLayout.Reset();
    m_tooltipFontPxUsed = 0.f;

    if (text.empty() || !m_dwriteFactory)
        return;

    float tooltipFontPx = TooltipFontPx();
    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, tooltipFontPx, L"",
        m_tooltipTextFormat.GetAddressOf());

    if (SUCCEEDED(hr) && m_tooltipTextFormat)
    {
        m_tooltipTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_tooltipTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Build the measuring layout here as well as lazily in DrawTooltip, so the common
    // case - a tooltip set once at Initialize - never pays for it while drawing.
    EnsureTooltipTextLayout();
}

void UIElement::DrawTooltip(ID2D1RenderTarget* rt)
{
    if (m_tooltipText.empty() || !m_tooltipTextFormat || !rt)
        return;
    if (m_tooltipVisibility <= 0.01f)
        return;

    const float tviz   = m_tooltipVisibility;
    const float fontPx = TooltipFontPx();
    const float tpadX  = fontPx * 1.1f;
    const float tpadY  = fontPx * 0.50f;

    // Measure the text so the pill is only as wide as the string needs. The layout is
    // cached by EnsureTooltipTextLayout because building one re-runs font fallback and
    // text shaping. If it cannot be built or measured, fall back to roughly twice the
    // element width - a guess rather than a measurement, just wide enough to hold it.
    EnsureTooltipTextLayout();
    float tW = m_pixelWidth * 2.f;
    float tH = fontPx * 2.f;
    DWRITE_TEXT_METRICS metrics{};
    if (m_tooltipTextLayout && SUCCEEDED(m_tooltipTextLayout->GetMetrics(&metrics)))
    {
        tW = metrics.width  + tpadX * 2.f;
        tH = metrics.height + tpadY * 2.f;
    }

    const float cx = (m_rect.left + m_rect.right) * 0.5f;

    // Position above the element; flip below if too close to the top
    const float gap = m_pixelHeight * 0.3f;
    float tTop, tBottom;
    if (m_rect.top - tH - gap < 2.f)
    {
        tTop    = m_rect.bottom + gap;
        tBottom = tTop + tH;
    }
    else
    {
        tBottom = m_rect.top - gap;
        tTop    = tBottom - tH;
    }

    // Horizontal: centre on the element then clamp to render-target edges
    D2D1_SIZE_F rtSz = rt->GetSize();
    const float margin = 4.f;
    float tLeft  = cx - tW * 0.5f;
    float tRight = cx + tW * 0.5f;
    if (tRight > rtSz.width - margin)
    {
        tLeft  = rtSz.width - margin - tW;
        tRight = rtSz.width - margin;
    }
    if (tLeft < margin)
    {
        tLeft  = margin;
        tRight = margin + tW;
    }

    // Vertical: if flipped-below choice still clips the bottom, clamp it
    if (tBottom > rtSz.height - margin)
    {
        tBottom = rtSz.height - margin;
        tTop    = tBottom - tH;
    }

    D2D1_RECT_F   tRect = D2D1::RectF(tLeft, tTop, tRight, tBottom);
    D2D1_ROUNDED_RECT tRR = D2D1::RoundedRect(tRect, tH * 0.3f, tH * 0.3f);

    // Local brushes, allocated per call. Creating an ID2D1SolidColorBrush is cheap, and
    // caching one would add a device-resource lifetime rule spanning Rendering.cpp -
    // same reasoning as AnimatedButton::Draw. Only the tooltip's TextLayout is cached:
    // it is a device-independent DWrite object, and it is the expensive part.
    using Microsoft::WRL::ComPtr;
    ComPtr<ID2D1SolidColorBrush> tBg, tBorder, tText;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.05f, 0.05f, 0.05f, 0.6f * tviz), &tBg);
    rt->CreateSolidColorBrush(D2D1::ColorF(1.f,   1.f,   1.f,   0.2f * tviz), &tBorder);
    rt->CreateSolidColorBrush(D2D1::ColorF(1.f,   1.f,   1.f,   tviz),         &tText);

    if (tBg)     rt->FillRoundedRectangle(&tRR, tBg.Get());
    if (tBorder) rt->DrawRoundedRectangle(&tRR, tBorder.Get(), 1.0f);
    if (tText)   rt->DrawTextW(
        m_tooltipText.c_str(), (UINT32)m_tooltipText.size(),
        m_tooltipTextFormat.Get(), tRect, tText.Get());
}