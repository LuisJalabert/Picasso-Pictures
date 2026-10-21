#include "AnimatedButton.h"
#include "ViewMath.h"   // SmoothAlpha
#include <sstream>
#include <cmath>

using Microsoft::WRL::ComPtr;

bool AnimatedButton::Initialize(
    ID2D1RenderTarget* renderTarget,
    IDWriteFactory* dwriteFactory,
    const Config& config,
    Callback callback)
{
    if (!renderTarget || !dwriteFactory)
        return false;

    m_config   = config;
    m_callback = std::move(callback);
    m_dwriteFactory = dwriteFactory;

    SetLayout(m_config.layout);
    m_pixelFontSize = m_config.layout.uiPixelScale * m_config.fontSize;
    InitializeLayout(renderTarget);

    // Hand the string to the base class, which owns what DrawTooltip actually
    // renders. m_config.tooltip is only the initial value, so an empty one here
    // simply means "no tooltip" - it cannot clear a tooltip set later via SetTooltip.
    if (!m_config.tooltip.empty())
        SetTooltip(m_config.tooltip);

    return true;
}

void AnimatedButton::Update(float deltaTime)
{
    // Scale/opacity spring toward their targets. The falloff here (0.12) is
    // deliberately steeper than the visibility falloff below (0.08), which is the
    // same one the vignettes use in UpdateEngine. Both are per-60Hz-frame fractions
    // rescaled by deltaTime through SmoothAlpha, so a press animation takes the same
    // real time at any refresh rate.
    const float alpha = SmoothAlpha(0.12f, deltaTime);
    m_currentScale += (m_targetScale - m_currentScale) * alpha;
    m_currentOpacity += (m_targetOpacity - m_currentOpacity) * alpha;

    UpdateVisibility(deltaTime, 0.08f);

    // m_pressed is the other half of the invariant: auto-repeat only ever runs for a
    // press that is still physically held down. If a release is ever lost to some
    // other path, the repeat stops instead of running free on hover alone.
    if (!m_holdActive || !m_pressed || !m_callback)
        return;

    // Hold-repeat continues only while the cursor is still over the button.
    // m_mouseOverSelf is refreshed by UpdateProximity on every WM_MOUSEMOVE, and
    // dragging back onto the button re-arms the repeat - same as a Windows button.
    if (!m_mouseOverSelf)
        return;

    m_holdTime += deltaTime;
    if (m_holdTime <= m_repeatDelay)
        return;

    // A closed gate delays the repeat instead of cancelling it: m_holdTime keeps
    // accumulating, so the repeat fires as soon as the gate opens again.
    if (m_config.repeatGate && !m_config.repeatGate())
        return;

    m_callback();
    m_holdTime -= m_repeatInterval;
}

void AnimatedButton::Draw(ID2D1RenderTarget* renderTarget)
{
    if (!renderTarget || !m_textFormat)
        return;

    if (GetVisibility() <= 0.01f)
        return;

    const float v = GetVisibility();

    const float cx = (m_rect.left + m_rect.right) * 0.5f;
    const float cy = (m_rect.top + m_rect.bottom) * 0.5f;

    const float scaledW = GetPixelWidth() * m_currentScale;
    const float scaledH = GetPixelHeight() * m_currentScale;

    D2D1_RECT_F drawRect = D2D1::RectF(
        cx - scaledW * 0.5f,
        cy - scaledH * 0.5f,
        cx + scaledW * 0.5f,
        cy + scaledH * 0.5f);

    float cornerRadius = scaledH * 0.3f;
    D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(drawRect, cornerRadius, cornerRadius);

    // Three local brushes, allocated per draw on purpose. The Direct2D brush
    // overview states that creating an ID2D1SolidColorBrush is "relatively
    // inexpensive" and may be done every time you render, so caching them buys
    // nothing here - while a brush cached in this class would become a device
    // resource that DiscardDeviceResources (Rendering.cpp) has to release before
    // the render target that made it, i.e. a cross-module lifetime rule bought
    // for no gain. The alpha goes straight into the colour, so there is also no
    // shared brush state to re-set between draw calls.
    ComPtr<ID2D1SolidColorBrush> fillBrush, borderBrush, highlightBrush;
    const HRESULT hrFill   = renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.f, 1.f, 1.f, 0.25f * m_currentOpacity * v), fillBrush.GetAddressOf());
    const HRESULT hrBorder = renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.f, 1.f, 1.f, 0.6f * v),                     borderBrush.GetAddressOf());
    const HRESULT hrHilite = renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.f, 1.f, 1.f, 0.15f * v),                    highlightBrush.GetAddressOf());

    // A failed or absent brush means the device is in trouble: skip the button
    // for this frame rather than hand null brushes to the draw calls. Those calls
    // return void, so anything they reject is only reported by the next EndDraw.
    if (FAILED(hrFill) || FAILED(hrBorder) || FAILED(hrHilite) ||
        !fillBrush || !borderBrush || !highlightBrush)
        return;

    renderTarget->FillRoundedRectangle(&rounded, fillBrush.Get());

    // Top highlight - the upper half of the pill. The corner radius here is a fixed
    // 14 px, the one dimension in this class that does not scale with uiPixelScale.
    D2D1_RECT_F highlightRect = drawRect;
    highlightRect.bottom = (drawRect.top + drawRect.bottom) * 0.5f;
    renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(highlightRect, 14.f, 14.f),
        highlightBrush.Get());

    renderTarget->DrawRoundedRectangle(&rounded, borderBrush.Get(), 2.0f);

    renderTarget->DrawTextW(
        m_config.text.c_str(),
        (UINT32)m_config.text.length(),
        m_textFormat.Get(),
        drawRect,
        borderBrush.Get());

    DrawTooltip(renderTarget);
}

bool AnimatedButton::OnMouseDown(float x, float y)
{
    if (!HitTest(x, y))
        return false;

    m_pressed = true;
    m_holdActive = m_config.holdEnabled;
    m_holdTime = 0.0f;

    if (m_config.pressAnimation)
    {
        m_targetScale   = 0.92f;
        m_targetOpacity = 0.65f;
    }

    // Press activation: the action fires here, on the way down, not on release.
    // WM_LBUTTONDBLCLK runs the same OnMouseDown loop as WM_LBUTTONDOWN, so a
    // double-click on a button deliberately lands two activations - that is the
    // intended behaviour, not a double-fire to suppress.
    if (m_callback)
        m_callback();

    return true;
}

bool AnimatedButton::OnMouseUp(float x, float y)
{
    // The release position is not used: the press always ends, wherever the cursor
    // is, and whether auto-repeat is still wanted is decided by m_mouseOverSelf in
    // Update rather than by these coordinates.
    (void)x; (void)y;

    if (!m_pressed)
        return false;

    m_pressed = false;
    m_holdActive = false;

    m_targetScale = 1.f;
    m_targetOpacity = 0.85f;
    return true;
}