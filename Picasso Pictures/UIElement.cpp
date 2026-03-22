#include "UIElement.h"
#include <cmath>

void UIElement::SetLayout(const Layout& layout)
{
    m_layout = layout;
    // Do not auto-reset anchors; user can choose.
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

void UIElement::CaptureAxis(AxisCache& cache, float axisLen, float center, const Axis& spec)
{
    cache.captured = true;
    cache.offsetFromStartPx = center;
    cache.offsetFromEndPx = axisLen - center;
    cache.offsetFromCenterPx = axisLen/2 - center;
    cache.capturedValue = spec.value;
    cache.capturedMode = spec.mode;
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

    CaptureAxis(m_xCache, refW, cx, m_layout.x);
    CaptureAxis(m_yCache, refH, cy, m_layout.y);

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
    // Track whether the cursor is directly over the element's own visual rect.
    // This is used by the tooltip dwell timer � independent of the activation zone.
    m_mouseOverSelf = (mouseX >= m_rect.left && mouseX <= m_rect.right &&
                       mouseY >= m_rect.top  && mouseY <= m_rect.bottom);

    if (m_forcedVisibility)
    {
        m_targetVisibility = 1.0f;
        return;
    }

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

    if (mouseX >= zoneLeft && mouseX <= zoneRight && mouseY >= zoneTop && mouseY <= zoneBottom)
        m_targetVisibility = 1.0f;
    else
        m_targetVisibility = 0.0f;
}

void UIElement::UpdateVisibility(float fallOff)
{
    float effectiveTarget = m_targetVisibility;

    if (m_forcedVisibility || m_focused)
    {
        // Prevent decreasing visibility
        if (effectiveTarget < m_visibility)
            effectiveTarget = m_visibility;
    }

    m_visibility += (effectiveTarget - m_visibility) * fallOff;

    // ---- Tooltip dwell timer ----
    // Accumulate only while the cursor is physically over the element's own rect.
    // The activation zone (which drives m_visibility) is deliberately ignored here
    // so the tooltip never fires just because the mouse is somewhere in the zone.
    if (!m_tooltipText.empty())
    {
        if (m_mouseOverSelf)
            m_tooltipHoverTime += 0.016f;
        else
            m_tooltipHoverTime = 0.f;

        const float tooltipTarget = (m_tooltipHoverTime > 1.0f) ? m_visibility : 0.f;
        m_tooltipVisibility += (tooltipTarget - m_tooltipVisibility) * 0.10f;
    }

    // ---- Focus pulse animation ----
    if (m_focused && m_selectAllOnFocus)
    {
        m_focusPulseTime += 0.016f; // approx 60 FPS
    }
    else
    {
        m_focusPulseTime = 0.f;
    }
}

void UIElement::SetForcedVisibility(bool forced){
    m_forcedVisibility = forced; 
    if (m_forcedVisibility)
    {
        m_targetVisibility = 1.f; // Ensure it becomes visible
    } 
    else
    {
        m_targetVisibility = 0.f; // Allow it to fade out
    }
}

void UIElement::SetTooltip(const std::wstring& text)
{
    m_tooltipText = text;
    m_tooltipTextFormat.Reset();

    if (text.empty() || !m_dwriteFactory)
        return;

    float tooltipFontPx = max(1.f, m_layout.uiPixelScale * m_layout.tooltipFontSize);
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
}

void UIElement::DrawTooltip(ID2D1RenderTarget* rt)
{
    if (m_tooltipText.empty() || !m_tooltipTextFormat || !rt)
        return;
    if (m_tooltipVisibility <= 0.01f)
        return;

    const float tviz   = m_tooltipVisibility;
    const float fontPx = max(1.f, m_layout.uiPixelScale * m_layout.tooltipFontSize);
    const float tpadX  = fontPx * 1.1f;
    const float tpadY  = fontPx * 0.50f;

    // Measure text so the pill is exactly wide enough
    float tW = m_pixelWidth * 2.f;
    float tH = fontPx * 2.f;
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<IDWriteTextLayout> tLayout;
        if (SUCCEEDED(m_dwriteFactory->CreateTextLayout(
                m_tooltipText.c_str(), (UINT32)m_tooltipText.size(),
                m_tooltipTextFormat.Get(), 600.f, 200.f, &tLayout)))
        {
            DWRITE_TEXT_METRICS metrics{};
            if (SUCCEEDED(tLayout->GetMetrics(&metrics)))
            {
                tW = metrics.width  + tpadX * 2.f;
                tH = metrics.height + tpadY * 2.f;
            }
        }
    }

    const float cx = (m_rect.left + m_rect.right) * 0.5f;
    const float cy = (m_rect.top  + m_rect.bottom) * 0.5f;
    (void)cy;

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