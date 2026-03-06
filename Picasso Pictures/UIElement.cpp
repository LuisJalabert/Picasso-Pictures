#include "UIElement.h"
#include <cmath>

void UIElement::SetLayout(const Layout& layout)
{
    m_layout = layout;
    // Do not auto-reset anchors; user can choose.
}

void UIElement::ResetAnchors()
{
    m_xCache = AxisCache{};
    m_yCache = AxisCache{};
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
    if (m_forcedVisibility)
    {
        m_targetVisibility = 1.0f;
        return;
    }

    const auto& z = m_layout.activationZone;

    float zoneLeft = windowWidth * z.left;
    float zoneTop = windowHeight * z.top;
    float zoneRight = windowWidth * z.right;
    float zoneBottom = windowHeight * z.bottom;

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