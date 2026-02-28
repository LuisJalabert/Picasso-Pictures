#pragma once

#include <wrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <functional>
#include <string>
#include <algorithm>

class AnimatedButton
{
public:
    using Callback = std::function<void()>;

    enum ButtonId
    {
        BUTTON_NONE = 0,
        BUTTON_ZOOM_11,
        BUTTON_ZOOM_IN,
        BUTTON_ZOOM_OUT,
        BUTTON_ROTATE_LEFT,
        BUTTON_ROTATE_RIGHT,
        BUTTON_PREVIOUS,
        BUTTON_NEXT,
        BUTTON_EXIT
    };

    struct ActivationZone
    {
        float left   = 0.0f;
        float top    = 0.0f;
        float right  = 1.0f;
        float bottom = 1.0f;
    };

    struct Config
    {
        ButtonId id = BUTTON_NONE;

        float relativeX         = 0.5f;
        float relativeY         = 0.5f;
        float width             = 0.1f;
        float height            = 0.05f;
        float fontSize          = 0.02f;
        float uiPixelScale      = 1.0f;
        float referenceWidth  = 0.0f;
        float referenceHeight = 0.0f;

        std::wstring text;

        ActivationZone activationZone;
    };

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
    void UpdateProximity(float mouseX, float mouseY, float windowWidth, float windowHeight);
    bool HitTest(float x, float y) const;

    ButtonId GetId() const { return m_config.id; }

private:
    Config m_config;

    float m_visibility = 0.0f;
    float m_targetVisibility = 0.0f;

    D2D1_RECT_F m_rect = {};
    float m_pixelWidth = 1.0f;
    float m_pixelHeight = 1.0f;

    bool  m_holdActive = false;
    float m_holdTime = 0.0f;
    float m_repeatDelay = 0.4f;
    float m_repeatInterval = 0.08f;

    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;

    float m_currentScale = 1.f;
    float m_targetScale = 1.f;

    float m_currentOpacity = 0.85f;
    float m_targetOpacity = 0.85f;

    bool m_pressed = false;
    D2D1_RECT_F m_fullscreenRect = {};
    Callback m_callback;
    float m_groupRelativeCenterX = 0.0f;
    float m_bottomOffset = 0.0f;
    bool  m_layoutCaptured = false;
};