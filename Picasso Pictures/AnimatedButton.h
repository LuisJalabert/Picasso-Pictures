#pragma once
#include "UIElement.h"
#include <functional>
#include <string>
#include <wrl/client.h>

class AnimatedButton : public UIElement
{
public:
    using Callback = std::function<void()>;
    // Consulted once per hold-repeat (never for the press itself). Return false to
    // drop that repeat; the pending repeat is retried on the next Update, so a
    // closed gate delays rather than cancels. Empty = every repeat is allowed.
    using RepeatGate = std::function<bool()>;

    struct Config
    {
        std::wstring text;
        std::wstring tooltip;       // initial tooltip only; empty = none. What is
                                    // drawn comes from the base class's own string,
                                    // set once here - see SetText for the text.
        UIElement::Layout layout;
        float fontSize = 0.02f;

        // Auto-repeat while the button is held: first repeat after m_repeatDelay,
        // then every m_repeatInterval. Turn this off for callbacks that toggle
        // state or open a modal dialog - holding those would toggle/spam them.
        bool holdEnabled = true;

        // Play the press-in / release-out scale + opacity change.
        bool pressAnimation = true;

        // Optional backpressure for hold-repeat, e.g. the image-navigation buttons
        // use the same one-nav-per-presented-frame gate the keyboard path uses.
        RepeatGate repeatGate;
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