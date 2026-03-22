#include "AnimatedButton.h"
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

    // Register tooltip with the base class (no-op if empty)
    if (!m_config.tooltip.empty())
        SetTooltip(m_config.tooltip);

    return true;
}

void AnimatedButton::Update(float deltaTime)
{
    // Simple smoothing (same feel as you had)
    const float smoothScale = 0.12f;
    m_currentScale += (m_targetScale - m_currentScale) * smoothScale;
    m_currentOpacity += (m_targetOpacity - m_currentOpacity) * smoothScale;

    UpdateVisibility(0.08f);

    if (m_holdActive && m_callback)
    {
        m_holdTime += deltaTime;
        if (m_holdTime > m_repeatDelay)
        {
            m_callback();
            m_holdTime -= m_repeatInterval;
        }
    }
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

    ComPtr<ID2D1SolidColorBrush> fillBrush;
    const float baseOpacity = 0.25f * m_currentOpacity * v;

    renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.f, 1.f, 1.f, baseOpacity),
        fillBrush.GetAddressOf());

    ComPtr<ID2D1SolidColorBrush> borderBrush;
    renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.f, 1.f, 1.f, 0.6f * v),
        borderBrush.GetAddressOf());

    renderTarget->FillRoundedRectangle(&rounded, fillBrush.Get());

    // top highlight
    ComPtr<ID2D1SolidColorBrush> highlightBrush;
    renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.f, 1.f, 1.f, 0.15f * v),
        highlightBrush.GetAddressOf());

    D2D1_RECT_F highlightRect = drawRect;
    highlightRect.bottom = (drawRect.top + drawRect.bottom) * 0.5f;
    renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(highlightRect, 14.f, 14.f),
        highlightBrush.Get());

    renderTarget->DrawRoundedRectangle(&rounded, borderBrush.Get(), 1.0f);

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

    // click
    if (m_callback)
        m_callback();

    return true;
}

bool AnimatedButton::OnMouseUp(float x, float y)
{
    (void)x; (void)y;

    if (!m_pressed)
        return false;

    m_pressed = false;
    m_holdActive = false;

    m_targetScale = 1.f;
    m_targetOpacity = 0.85f;
    return true;
}