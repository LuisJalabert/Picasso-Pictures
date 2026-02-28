#include "AnimatedButton.h"
#include <sstream>
#include <windows.h>
using namespace Microsoft::WRL;

bool AnimatedButton::Initialize(
    ID2D1RenderTarget* renderTarget,
    IDWriteFactory* dwriteFactory,
    const Config& config,
    Callback callback)
{
    m_config = config;
    m_callback = callback;
    m_dwriteFactory = dwriteFactory;

    UpdateLayout(renderTarget);
    return true;
}
void AnimatedButton::UpdateLayout(ID2D1RenderTarget* renderTarget)
{
    if (!renderTarget || !m_dwriteFactory)
        return;

    D2D1_SIZE_F rtSize = renderTarget->GetSize();

    //
    // ---- CAPTURE FULLSCREEN LAYOUT ONCE ----
    //
    if (!m_layoutCaptured)
    {
        float fsWidth  = m_config.referenceWidth;
        float fsHeight = m_config.referenceHeight;

        float centerX = fsWidth  * m_config.relativeX;
        float centerY = fsHeight * m_config.relativeY;

        float pixelWidth    = m_config.uiPixelScale * m_config.width;
        float pixelHeight   = m_config.uiPixelScale * m_config.height;

        m_pixelWidth  = pixelWidth;
        m_pixelHeight = pixelHeight;

        // Store horizontal offset relative to fullscreen center
        m_groupRelativeCenterX = centerX - (fsWidth * 0.5f);

        // Store bottom offset in pixels
        float bottomEdge = centerY + (pixelHeight * 0.5f);
        m_bottomOffset = fsHeight - bottomEdge;

        m_layoutCaptured = true;
    }

    //
    // ---- REPOSITION FOR CURRENT WINDOW ----
    //

    float newCenterX = (rtSize.width * 0.5f) + m_groupRelativeCenterX;
    float newBottomEdge = rtSize.height - m_bottomOffset;
    float newCenterY = newBottomEdge - (m_pixelHeight * 0.5f);

    m_rect = D2D1::RectF(
        newCenterX - m_pixelWidth  * 0.5f,
        newCenterY - m_pixelHeight * 0.5f,
        newCenterX + m_pixelWidth  * 0.5f,
        newCenterY + m_pixelHeight * 0.5f);

    //
    // ---- TEXT FORMAT (size stays constant) ----
    //

    m_textFormat.Reset();

    float pixelFontSize = m_config.uiPixelScale * m_config.fontSize;

    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        pixelFontSize,
        L"",
        &m_textFormat);

    if (SUCCEEDED(hr) && m_textFormat)
    {
        m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    if (m_config.id == BUTTON_ZOOM_11)
    {
        std::wstringstream ss;

        D2D1_SIZE_F rtSize = renderTarget->GetSize();

        ss << L"--- AnimatedButton::UpdateLayout ---\n\n";

        ss << L"RenderTarget Size:\n";
        ss << L"  Width  = " << rtSize.width  << L"\n";
        ss << L"  Height = " << rtSize.height << L"\n\n";

        ss << L"Reference Size:\n";
        ss << L"  RefWidth  = " << m_config.referenceWidth  << L"\n";
        ss << L"  RefHeight = " << m_config.referenceHeight << L"\n\n";

        ss << L"Pixel Size:\n";
        ss << L"  m_pixelWidth  = " << m_pixelWidth  << L"\n";
        ss << L"  m_pixelHeight = " << m_pixelHeight << L"\n\n";

        ss << L"Layout Capture:\n";
        ss << L"  m_layoutCaptured = " << (m_layoutCaptured ? L"true" : L"false") << L"\n";
        ss << L"  m_groupRelativeCenterX = " << m_groupRelativeCenterX << L"\n";
        ss << L"  m_bottomOffset = " << m_bottomOffset << L"\n\n";

        ss << L"Final Rect:\n";
        ss << L"  left   = " << m_rect.left   << L"\n";
        ss << L"  top    = " << m_rect.top    << L"\n";
        ss << L"  right  = " << m_rect.right  << L"\n";
        ss << L"  bottom = " << m_rect.bottom << L"\n";

        MessageBoxW(nullptr, ss.str().c_str(), L"UpdateLayout Debug", MB_OK);
}
}

/* 
void AnimatedButton::UpdateLayout(ID2D1RenderTarget* renderTarget)
{
    if (!renderTarget || !m_dwriteFactory)
        return;

    D2D1_SIZE_F rtSize = renderTarget->GetSize();

    float centerX = rtSize.width  * m_config.relativeX;
    float centerY = rtSize.height * m_config.relativeY;
    float uiScale = min(rtSize.width, rtSize.height);

    float pixelWidth = uiScale * m_config.width;
    float pixelHeight = uiScale * m_config.height;
    float pixelFontSize = uiScale * m_config.fontSize;

    m_pixelWidth  = pixelWidth;
    m_pixelHeight = pixelHeight;

    m_rect = D2D1::RectF(
        centerX - pixelWidth  * 0.5f,
        centerY - pixelHeight * 0.5f,
        centerX + pixelWidth  * 0.5f,
        centerY + pixelHeight * 0.5f);

    m_textFormat.Reset();

    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        pixelFontSize,
        L"",
        &m_textFormat);

    if (FAILED(hr) || !m_textFormat)
        return;

    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}
*/
void AnimatedButton::Update(float deltaTime)
{
    float smooth = 0.12f;

    m_currentScale += (m_targetScale - m_currentScale) * smooth;
    m_currentOpacity += (m_targetOpacity - m_currentOpacity) * smooth;
    m_visibility += (m_targetVisibility - m_visibility) * 0.08f;

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

    if (m_visibility <= 0.01f)
        return;

    float centerX = (m_rect.left + m_rect.right) * 0.5f;
    float centerY = (m_rect.top + m_rect.bottom) * 0.5f;

    float scaledW = m_pixelWidth * m_currentScale;
    float scaledH = m_pixelHeight * m_currentScale;

    D2D1_RECT_F drawRect = D2D1::RectF(
        centerX - scaledW * 0.5f,
        centerY - scaledH * 0.5f,
        centerX + scaledW * 0.5f,
        centerY + scaledH * 0.5f);

    float cornerRadius = scaledH * 0.3f;

    D2D1_ROUNDED_RECT rounded =
        D2D1::RoundedRect(drawRect, cornerRadius, cornerRadius);

    ComPtr<ID2D1SolidColorBrush> fillBrush;
    float baseOpacity = 0.25f * m_currentOpacity * m_visibility;

    renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.f, 1.f, 1.f, baseOpacity),
        &fillBrush);

    ComPtr<ID2D1SolidColorBrush> borderBrush;
    renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.f, 1.f, 1.f, 0.6f * m_visibility),
        &borderBrush);

    renderTarget->FillRoundedRectangle(&rounded, fillBrush.Get());

    ComPtr<ID2D1SolidColorBrush> highlightBrush;
    renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.f, 1.f, 1.f, 0.15f * m_visibility),
        &highlightBrush);

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
}

bool AnimatedButton::HitTest(float x, float y) const
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

bool AnimatedButton::OnMouseDown(float x, float y)
{
    if (HitTest(x, y))
    {
        m_pressed = true;
        m_holdActive = true;
        m_holdTime = 0.0f;

        m_targetScale = 0.92f;
        m_targetOpacity = 0.65f;

        return true;
    }
    return false;
}

bool AnimatedButton::OnMouseUp(float x, float y)
{
    if (!m_pressed)
        return false;

    m_pressed = false;
    m_holdActive = false;

    m_targetScale = 1.f;
    m_targetOpacity = 0.85f;

    if (HitTest(x, y) && m_callback)
        m_callback();

    return true;
}

void AnimatedButton::UpdateProximity(float mouseX, float mouseY, float windowWidth, float windowHeight)
{
    float zoneLeft   = windowWidth  * m_config.activationZone.left;
    float zoneTop    = windowHeight * m_config.activationZone.top;
    float zoneRight  = windowWidth  * m_config.activationZone.right;
    float zoneBottom = windowHeight * m_config.activationZone.bottom;

    if (mouseX >= zoneLeft && mouseX <= zoneRight &&
        mouseY >= zoneTop  && mouseY <= zoneBottom)
    {
        m_targetVisibility = 1.0f;
    }
    else
    {
        m_targetVisibility = 0.0f;
    }
}