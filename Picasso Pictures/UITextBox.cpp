#include "UITextBox.h"
#include <algorithm>

using namespace Microsoft::WRL;


bool UITextBox::Initialize(
    IDWriteFactory* factory,
    const Config& config,
    Callback callback)
{
    m_factory = factory;
    m_config = config;
    m_callback = callback;
    return true;
}

void UITextBox::UpdateLayout(ID2D1RenderTarget* rt)
{
    if (!rt || !m_factory)
        return;

    D2D1_SIZE_F rtSize = rt->GetSize();

    //
    // ---- CAPTURE FULLSCREEN LAYOUT ONCE ----
    //
    if (!m_layoutCaptured)
    {
        float fsWidth  = rtSize.width;
        float fsHeight = rtSize.height;

        float centerX = fsWidth  * m_config.relativeX;
        float centerY = fsHeight * m_config.relativeY;

        float pixelWidth    = m_config.uiPixelScale * m_config.width;
        float pixelHeight   = m_config.uiPixelScale * m_config.height;
        float pixelFontSize = m_config.uiPixelScale * m_config.relativeFontSize;

        m_pixelWidth    = pixelWidth;
        m_pixelHeight   = pixelHeight;
        m_pixelFontSize = pixelFontSize;

        // Store horizontal offset from center
        m_groupRelativeCenterX = centerX - (fsWidth * 0.5f);

        // Store bottom offset
        float bottomEdge = centerY + (pixelHeight * 0.5f);
        m_bottomOffset = fsHeight - bottomEdge;

        m_layoutCaptured = true;
    }

    //
    // ---- REPOSITION USING STORED PIXEL VALUES ----
    //

    float newCenterX = (rtSize.width * 0.5f) + m_groupRelativeCenterX;
    float newBottomEdge = rtSize.height - m_bottomOffset;
    float newCenterY = newBottomEdge - (m_pixelHeight * 0.5f);

    m_rect = D2D1::RectF(
        newCenterX - m_pixelWidth * 0.5f,
        newCenterY - m_pixelHeight * 0.5f,
        newCenterX + m_pixelWidth * 0.5f,
        newCenterY + m_pixelHeight * 0.5f);

    //
    // ---- TEXT FORMAT (font size stays fixed) ----
    //

    m_textFormat.Reset();

    HRESULT hr = m_factory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        m_pixelFontSize,
        L"",
        &m_textFormat);

    if (SUCCEEDED(hr) && m_textFormat)
    {
        m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void UITextBox::SetForcedVisibility(bool forced){
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

void UITextBox::UpdateVisibility(float fallOff)
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

void UITextBox::UpdateProximity(float mouseX, float mouseY, float windowWidth, float windowHeight)
{
    float left   = windowWidth  * m_config.activationZone.left;
    float top    = windowHeight * m_config.activationZone.top;
    float right  = windowWidth  * m_config.activationZone.right;
    float bottom = windowHeight * m_config.activationZone.bottom;

    if (mouseX >= left && mouseX <= right &&
        mouseY >= top  && mouseY <= bottom)
    {
        m_targetVisibility = 1.f;
    }
    else
    {
        m_targetVisibility = 0.f;
    }
}
 
void UITextBox::SetTargetVisibility(float visibility) 
{ 
    m_targetVisibility = visibility; 
}

 void UITextBox::Draw(ID2D1RenderTarget* rt)
{
    if (!rt || !m_textFormat) 
        return;

    if (m_visibility <= 0.01f) 
        return;

    //
    // ---- Use layout rect directly (already resolution scaled) ----
    //
    float centerX = (m_rect.left + m_rect.right) * 0.5f;
    float centerY = (m_rect.top + m_rect.bottom) * 0.5f;

    float scaledW = m_pixelWidth;
    float scaledH = m_pixelHeight;

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

const std::wstring& UITextBox::GetText() const
{
    return m_text;
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

bool UITextBox::HitTest(float x, float y) const
{
    float centerX = (m_rect.left + m_rect.right) * 0.5f;
    float centerY = (m_rect.top + m_rect.bottom) * 0.5f;

    float scaledW = m_pixelWidth * m_currentScale;
    float scaledH = m_pixelHeight * m_currentScale;

    D2D1_RECT_F scaledRect = D2D1::RectF(
        centerX - scaledW * 0.5f,
        centerY - scaledH * 0.5f,
        centerX + scaledW * 0.5f,
        centerY + scaledH * 0.5f);

    return x >= scaledRect.left &&
           x <= scaledRect.right &&
           y >= scaledRect.top &&
           y <= scaledRect.bottom;
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