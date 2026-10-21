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

        InputMode inputMode = InputMode::Any;
    };

public:
    bool Initialize(IDWriteFactory* factory, ID2D1RenderTarget* renderTarget, const Config& config, Callback callback = nullptr);

    void Draw(ID2D1RenderTarget* rt);

    void SetText(const std::wstring& text);
    const std::wstring& GetText() const { return m_text; }

    // Typing. There is no caret and no in-box selection range: right after the box
    // gains focus the whole string counts as selected (m_selectAllOnFocus), and the
    // first character typed - or Backspace - replaces it. OnChar is the single place
    // that decides what may be inserted.
    void OnChar(wchar_t c);
    void OnKeyDown(WPARAM key);

    // Takes focus if the box is editable and the point is inside it. A non-editable
    // box returns false so the click falls through to the rest of WndProc, which is
    // also why m_focused can never be true for one.
    bool OnMouseDown(float x, float y);
    bool IsFocused() const { return m_focused; }

private:
    Config   m_config{};
    Callback m_callback{};

    std::wstring m_text;
};