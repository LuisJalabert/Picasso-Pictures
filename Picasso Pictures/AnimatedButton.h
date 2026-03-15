#pragma once
#include "UIElement.h"
#include <functional>
#include <string>
#include <wrl/client.h>

class AnimatedButton : public UIElement
{
public:
    using Callback = std::function<void()>;
    struct Config
    {
        std::wstring text;
        std::wstring tooltip;       // optional hover tooltip (empty = none)
        UIElement::Layout layout;
        float fontSize = 0.02f;
        bool holdEnabled = true;
        bool pressAnimation = true;
    };

    bool Initialize(
        ID2D1RenderTarget* renderTarget,
        IDWriteFactory* dwriteFactory,
        const Config& config,
        Callback callback);

    void Update(float deltaTime);
    void Draw(ID2D1RenderTarget* renderTarget);

    bool OnMouseDown(float x, float y);
    bool OnMouseUp(float x, float y);
    void SetText(const std::wstring& text) { m_config.text = text; }

    Config   m_config{};
    Callback m_callback{};
    private:
    bool  m_pressed    = false;
    bool  m_holdActive = false;
    float m_holdTime      = 0.0f;
    float m_repeatDelay   = 0.4f;
    float m_repeatInterval = 0.08f;
};