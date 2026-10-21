#include "UITextBox.h"
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
    }

    //
    // ---- Focus border ----
    //
    // Deliberately NOT inside the backgroundAlpha block above: an editable box with
    // no background would otherwise get no focus feedback at all. m_focused alone is
    // the right condition - only an editable box can ever take focus.
    //
    if (m_focused)
    {
        ComPtr<ID2D1SolidColorBrush> borderBrush;
        if (SUCCEEDED(rt->CreateSolidColorBrush(
                D2D1::ColorF(1.f, 1.f, 1.f, m_visibility),
                &borderBrush)) && borderBrush)
        {
            rt->DrawRoundedRectangle(
                &rounded,
                borderBrush.Get(),
                borderThickness);
        }
    }

    //
    // ---- "Whole string selected" highlight ----
    //
    // Not a text selection - the box has no caret and no selection range. This
    // pulses while the entire string counts as selected, i.e. right after the box
    // gained focus and before the first keystroke replaces it.
    //
    if (m_focused && m_selectAllOnFocus && !m_text.empty())
    {
        // 0.5 .. 1.5, then scaled by the 0.25 below, so the pulse covers an alpha of
        // 0.125 .. 0.375. Not an alpha on its own despite the name.
        float pulse = 0.5f + 0.5f * sinf(m_focusPulseTime * 2.0f);
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

    DrawTooltip(rt);
}

void UITextBox::SetText(const std::wstring& text)
{
    m_text = text;
}

// The single place that decides what may be typed. There used to be a separate
// AcceptChar() describing these rules, but nothing called it and its rules did not
// match what was actually enforced (it allowed '-', which was never inserted), so
// it read as the spec while being dead code.
void UITextBox::OnChar(wchar_t c)
{
    if (!m_config.isEditable || !m_focused)
        return;

    // The whole string counts as selected right after the box gains focus, so the
    // first character typed replaces it.
    if (m_selectAllOnFocus)
    {
        m_text.clear();
        m_selectAllOnFocus = false;
    }

    // Control characters never insert. This is what keeps Enter, Escape and
    // Backspace out of the text: on the WM_CHAR path they arrive here as 13 / 27 / 8.
    if (c < 32)
        return;

    switch (m_config.inputMode)
    {
    case InputMode::Any:
        // No further filtering beyond the control-character check above. No
        // editable Any-mode box exists today.
        m_text += c;
        break;

    case InputMode::NumericInteger:
        if (c >= L'0' && c <= L'9')
            m_text += c;
        break;

    case InputMode::NumericFloat:
        // Digits plus at most one decimal point. A minus sign is deliberately not
        // accepted: the only editable box is the zoom readout, where a negative
        // value has no meaning (its callback clamps to 1..10000 anyway).
        if (c >= L'0' && c <= L'9')
            m_text += c;
        else if (c == L'.' && m_text.find(L'.') == std::wstring::npos)
            m_text += c;
        break;
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
        // While the whole string counts as selected, Backspace replaces that
        // "selection" instead of deleting a single character - what a box with a
        // real selection would do.
        if (m_selectAllOnFocus)
        {
            m_text.clear();
            m_selectAllOnFocus = false;
        }
        else
        {
            m_text.pop_back();
        }
    }
    else if (key == VK_RETURN)
    {
        // Enter commits: the callback applies the value and the box loses focus.
        // m_selectAllOnFocus is deliberately left set - nothing reads it while the
        // box is unfocused, and OnMouseDown re-sets it on the next focus click.
        if (m_callback)
            m_callback(m_text);

        m_focused = false;
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