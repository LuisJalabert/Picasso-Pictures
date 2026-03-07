#pragma once
#include "UIElement.h"
#include <functional>
#include <string>
#include <wrl/client.h>

class UITextBox : public UIElement
{
public:
    using Callback = std::function<void(const std::wstring&)>;

    enum class InputMode { Any, NumericFloat, NumericInteger };

    struct Config
    {
        UIElement::Layout layout;

        float relativeFontSize = 0.02f; // relative to uiPixelScale
        float backgroundAlpha = 0.0f;   // 0 = none

        bool isEditable = false;
        int id = 0;

        InputMode inputMode = InputMode::Any;
    };

public:
    bool Initialize(IDWriteFactory* factory, ID2D1RenderTarget* renderTarget, const Config& config, Callback callback = nullptr);

    void Draw(ID2D1RenderTarget* rt);

    void SetText(const std::wstring& text);
    const std::wstring& GetText() const { return m_text; }

    void OnChar(wchar_t c);
    void OnKeyDown(WPARAM key);

    bool OnMouseDown(float x, float y);
    bool IsFocused() const { return m_focused; };

private:
    bool AcceptChar(wchar_t c) const;

    Config   m_config{};
    Callback m_callback{};

    std::wstring m_text;
};