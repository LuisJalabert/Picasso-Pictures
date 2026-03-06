#pragma once
#include "UIElement.h"
#include <dwrite.h>
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

        // Layout
        UIElement::Layout layout;

        // Text
        float fontSize = 0.02f; // relative to uiPixelScale
    };

public:
    bool Initialize(
        ID2D1RenderTarget* renderTarget,
        IDWriteFactory* dwriteFactory,
        const Config& config,
        Callback callback);

    void UpdateLayout(ID2D1RenderTarget* renderTarget);
    void Update(float deltaTime);
    void Draw(ID2D1RenderTarget* renderTarget);

    bool OnMouseDown(float x, float y);
    bool OnMouseUp(float x, float y);

private:
    void EnsureTextFormat();

private:
    Config m_config{};
    Callback m_callback{};

    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;

    bool m_pressed = false;

    // Hold-repeat behavior (kept from your original)
    bool  m_holdActive = false;
    float m_holdTime = 0.0f;
    float m_repeatDelay = 0.4f;
    float m_repeatInterval = 0.08f;

    // Cached RT size for proximity zones and layout
    float m_lastRTW = 0.f;
    float m_lastRTH = 0.f;
};