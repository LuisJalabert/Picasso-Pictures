#include "UITextBox.h"
#include <cwctype>
#include <cmath>

using Microsoft::WRL::ComPtr;

bool UITextBox::Initialize( IDWriteFactory* factory,
                            ID2D1RenderTarget* renderTarget,
                            const Config& config, 
                            Callback callback)
{
    if (!factory)
        return false;

    m_config        = config;
    m_callback      = std::move(callback);
    m_dwriteFactory = factory;

    SetLayout(m_config.layout);
    m_pixelFontSize = m_config.layout.uiPixelScale * m_config.relativeFontSize;
    InitializeLayout(renderTarget);

    return true;
}

void UITextBox::Draw(ID2D1RenderTarget* rt)
{
    if (!rt || !m_textFormat) 
        return;

    if (m_visibility <= 0.01f) 
        return;

    // ---- Use layout rect directly (already resolution scaled) ----
    float centerX = (m_rect.left + m_rect.right) * 0.5f;
    float centerY = (m_rect.top + m_rect.bottom) * 0.5f;

    const float scaledW = GetPixelWidth() * m_currentScale;
    const float scaledH = GetPixelHeight() * m_currentScale;

    D2D1_RECT_F drawRect = D2D1::RectF(
        centerX - scaledW * 0.5f,
        centerY - scaledH * 0.5f,
        centerX + scaledW * 0.5f,
        centerY + scaledH * 0.5f);

    // Resolution-independent rounding
    float cornerRadius = scaledH * 0.3f;
    float borderThickness = scaledH * 0.06f;

    D2D1_ROUNDED_RECT rounded =
        D2D1::RoundedRect(drawRect, cornerRadius, cornerRadius);

    //
    // ---- Background (optional) ----
    //
    if (m_config.backgroundAlpha > 0.0f)
    {
        ComPtr<ID2D1SolidColorBrush> bg;
        rt->CreateSolidColorBrush(
            D2D1::ColorF(0.f, 0.f, 0.f,
                        m_config.backgroundAlpha * m_visibility),
            &bg);

        if (bg)
            rt->FillRoundedRectangle(&rounded, bg.Get());

        float borderAlpha = m_focused ? 1.0f : 0.0f;
        ComPtr<ID2D1SolidColorBrush> borderBrush;
        rt->CreateSolidColorBrush(
            D2D1::ColorF(1.f, 1.f, 1.f, borderAlpha * m_visibility),
            &borderBrush);

        if (borderBrush)
        {
            rt->DrawRoundedRectangle(
                &rounded,
                borderBrush.Get(),
                borderThickness);
        }
    }

    //
    // ---- Selection Highlight (full box highlight) ----
    //
    if (m_focused && m_selectAllOnFocus && !m_text.empty())
    {
        float pulse = 0.5f + 0.5f * sinf(m_focusPulseTime*2.0);
        float highlightAlpha = 0.5f + 1.0f * pulse;
        ComPtr<ID2D1SolidColorBrush> highlight;
        rt->CreateSolidColorBrush(
            D2D1::ColorF(0.8f, 0.8f, 0.8f,
                        0.25f * highlightAlpha * m_visibility),
            &highlight);

        if (highlight)
        {
            rt->FillRoundedRectangle(&rounded, highlight.Get());
        }
    }

    //
    // ---- Text ----
    //
    ComPtr<ID2D1SolidColorBrush> textBrush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(1.f, 1.f, 1.f, m_visibility),
        &textBrush);

    if (textBrush)
    {
        std::wstring displayText = m_text;

        if (m_config.isEditable)
            displayText += L" %";

        rt->DrawTextW(
            displayText.c_str(),
            (UINT32)displayText.length(),
            m_textFormat.Get(),
            drawRect,
            textBrush.Get());
    }
}

void UITextBox::SetText(const std::wstring& text)
{
    m_text = text;
}

bool UITextBox::AcceptChar(wchar_t c) const
{
    if (!m_config.isEditable)
        return false;

    if (c == L'\r' || c == L'\n')
        return false;

    switch (m_config.inputMode)
    {
    case InputMode::Any:
        return (c >= 32);

    case InputMode::NumericInteger:
        return (std::iswdigit(c) || c == L'-');

    case InputMode::NumericFloat:
        return (std::iswdigit(c) || c == L'-' || c == L'.');

    default:
        return false;
    }
}

void UITextBox::OnChar(wchar_t c)
{
    if (!m_config.isEditable || !m_focused)
        return;
    if (m_selectAllOnFocus)
    {
        m_text.clear();
        m_selectAllOnFocus = false;
    }
    if (c < 32)
        return;

    if (m_config.inputMode == InputMode::Any)
    {
        m_text += c;
    }
    else if (m_config.inputMode == InputMode::NumericInteger)
    {
        if (c >= L'0' && c <= L'9')
            m_text += c;
    }
    else if (m_config.inputMode == InputMode::NumericFloat)
    {
        if ((c >= L'0' && c <= L'9') ||
            (c == L'.' && m_text.find(L'.') == std::wstring::npos))
        {
            m_text += c;
        }

        // Ignore percentage symbol silently
        else if (c == L'%')
        {
            return;
        }
    }
}

void UITextBox::OnKeyDown(WPARAM key)
{
    if (!m_config.isEditable || !m_focused)
        return;

    if (key == VK_ESCAPE)
    {
        m_focused = false;
        m_selectAllOnFocus = false;
        return;
    }
    if (key == VK_BACK && !m_text.empty())
    {
        m_text.pop_back();
    }
    else if (key == VK_RETURN)
    {
        if (m_callback)
            m_callback(m_text);

        m_focused = false; // optional: lose focus after enter
    }
}

bool UITextBox::OnMouseDown(float x, float y)
{
    if (!m_config.isEditable)
        return false;

    if (HitTest(x, y))
    {
        m_focused = true;
        m_selectAllOnFocus = true;
        return true;
    }

    m_focused = false;
    return false;
}