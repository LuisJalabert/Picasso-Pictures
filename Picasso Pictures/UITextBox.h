#pragma once

#include <wrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <functional>
#include <string>

using namespace Microsoft::WRL;

class UITextBox
{
public:
    using Callback = std::function<void(const std::wstring&)>;

    enum class InputMode
    {
        Any,
        NumericFloat,
        NumericInteger
    };
    struct ActivationZone
    {
        float left   = 0.f;
        float top    = 0.8f;
        float right  = 1.f;
        float bottom = 1.f;
    };

    struct Config
    {
        float relativeX = 0.5f;
        float relativeY = 0.5f;
        float relativeFontSize = 0.02f;

        float width = 0.2f;      // relative width (for background box)
        float height = 0.05f;    // relative height

        float backgroundAlpha = 0.0f;  // 0 = none

        bool isEditable = false;
        int  id         = 0;
        ActivationZone activationZone;
        InputMode inputMode = InputMode::Any;
        float uiPixelScale      = 1.0f;
    };

    bool Initialize(
        IDWriteFactory* factory,
        const Config& config,
        Callback callback = nullptr);


    void UpdateLayout(ID2D1RenderTarget* renderTarget);
    void UpdateVisibility();
    void UpdateProximity(float mouseX, float mouseY, float windowWidth, float windowHeight);
    void Draw(ID2D1RenderTarget* rt);
    // Editing
    void SetText(const std::wstring& text);
    const std::wstring& GetText() const;
    void OnChar(wchar_t c);
    void OnKeyDown(WPARAM key);
    bool HitTest(float x, float) const;
    bool OnMouseDown(float x, float y);
    bool IsFocused() const { return m_focused; }

private:
    Config m_config;

    float m_visibility = 0.f;
    float m_targetVisibility = 0.f;
    float m_currentScale = 1.f;
    float m_pixelFontSize = 16.f;
    float m_pixelWidth = 200.f;
    float m_pixelHeight = 50.f;
    bool m_focused = false;
    bool m_selectAllOnFocus = false;
    float m_focusPulseTime = 0.f;
    bool m_layoutCaptured = false;
    float m_groupRelativeCenterX = 0.0f;
    float m_bottomOffset = 0.0f;

    std::wstring m_text;
    D2D1_RECT_F m_rect = {};
    Callback m_callback;

    Microsoft::WRL::ComPtr<IDWriteFactory> m_factory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;
};