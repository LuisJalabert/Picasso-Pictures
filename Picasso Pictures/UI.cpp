// UI.cpp : the widget initialisers, the image layout/fit rules, and the zoom
// commands.
//
// Linkage rules for this file:
//   * Nothing here is `static`, and every function another module calls is
//     called by its own name, so the definitions match UI.h as written.
//   * FitToWindowRelative and InitializeImageLayout take their default
//     arguments (preserveCenter = true, hard = false) on the UI.h declarations
//     ONLY - repeating them here would be C2522.
//
// Includes this module needs:
//   * "ViewMath.h"   - IsPointOverImage, used by ZoomIntoImage's cursor anchoring.
//   * "Fullscreen.h" - EnterSlideshowMode (slideshow button) and InitFullScreenExit
//                      (exit button).
//   * <cmath>        - pow() in ZoomIntoImage.

#include "UI.h"
#include "ViewMath.h"    // IsPointOverImage
#include "Fullscreen.h"  // EnterSlideshowMode / InitFullScreenExit

#include <cmath>         // pow (ZoomIntoImage)

// ============================================================================
// Navigation functions that live in the main file: they are called from a dozen
// WndProc sites plus wWinMain, so they are not widgets and are not declared in
// UI.h. Three UI button handlers call these three; DeleteCurrentImage is not
// declared because no UI handler calls it.
// ============================================================================
bool OpenImageFile(HWND hWnd);
void OpenNextImage(HWND hWnd);
void OpenPrevImage(HWND hWnd);



void SetZoomCentered(float newZoom, HWND hWnd, bool instant, bool preserveCenter)
{
    if (!g_view.bitmap) return;

    RECT rc;
    GetClientRect(hWnd, &rc);
    float windowWidth  = float(rc.right - rc.left);
    float windowHeight = float(rc.bottom - rc.top);

    D2D1_SIZE_F imgSize = g_view.bitmap->GetSize();

    if (!preserveCenter)
    {
        // Always center in window
        g_view.targetOffsetX = (windowWidth  - imgSize.width  * newZoom) * 0.5f;
        g_view.targetOffsetY = (windowHeight - imgSize.height * newZoom) * 0.5f;

        UpdateTargetZoom(newZoom);

        if (instant)
        {
            g_view.zoom = newZoom;
            g_view.offsetX = g_view.targetOffsetX;
            g_view.offsetY = g_view.targetOffsetY;
        }

        return;
    }

    // ---- Existing behavior: preserve current on-screen center ----

    if (instant)
    {
        g_view.zoom = newZoom;
        UpdateTargetZoom(newZoom);

        g_view.offsetX = (windowWidth  - imgSize.width  * newZoom) * 0.5f;
        g_view.offsetY = (windowHeight - imgSize.height * newZoom) * 0.5f;

        g_view.targetOffsetX = g_view.offsetX;
        g_view.targetOffsetY = g_view.offsetY;
        return;
    }

    float centerX = g_view.offsetX + imgSize.width  * g_view.zoom * 0.5f;
    float centerY = g_view.offsetY + imgSize.height * g_view.zoom * 0.5f;

    g_view.targetOffsetX = centerX - imgSize.width  * newZoom * 0.5f;
    g_view.targetOffsetY = centerY - imgSize.height * newZoom * 0.5f;

    UpdateTargetZoom(newZoom);
}


void FitToWindowRelative(HWND hWnd, float zoom, bool preserveCenter)
{
    if (!g_view.bitmap) return;

    RECT rc;
    GetClientRect(hWnd, &rc);

    float windowWidth  = (float)(rc.right - rc.left);
    float windowHeight = (float)(rc.bottom - rc.top);

    D2D1_SIZE_F imgSize = g_view.bitmap->GetSize();

    float scaleX = windowWidth  / imgSize.width;
    float scaleY = windowHeight / imgSize.height;

    float scale = min(scaleX, scaleY)*zoom;

    SetZoomCentered(scale, hWnd, false, preserveCenter);
}


void UpdateTargetZoom(float newZoom)
{
    if (!g_view.bitmap) return;
    g_view.targetZoom = newZoom;
}


void InitializeImageLayout(HWND hWnd, bool hard)
{
    if (!g_view.bitmap) return;
    if (g_view.restoredStateThisLoad && !g_slideshow.active)
    return;
    RECT rc;
    GetClientRect(hWnd, &rc);

    float windowWidth  = (float)(rc.right - rc.left);
    float windowHeight = (float)(rc.bottom - rc.top);

    D2D1_SIZE_F imgSize = g_view.bitmap->GetSize();

    // If the image will be displayed rotated 90 deg or 270 deg (from EXIF), its
    // effective on-screen footprint has swapped width and height.
    float fitW = imgSize.width;
    float fitH = imgSize.height;
    const float rotMod = fmodf(fabsf(g_view.exifRotation), 180.f);
    if (rotMod > 44.f && rotMod < 136.f)
        std::swap(fitW, fitH);

    float scale = 1.0f;

    if (hard)
        g_fullscreen.overlayAlpha = 1.0f;
    else
        g_fullscreen.overlayAlpha = 0.0f;

    // If image is larger than screen, scale down to ~95% of screen.
    // g_settings.openFitToScreen forces the same fit-to-screen scaling used by
    // slideshow mode even when the image would otherwise open at 100%.
    if (g_slideshow.active || g_settings.openFitToScreen || fitW > windowWidth || fitH > windowHeight)
    {
        float scaleX = (windowWidth  * 0.95f) / fitW;
        float scaleY = (windowHeight * 0.95f) / fitH;

        scale = min(scaleX, scaleY);
    }

    float windowCenterX = windowWidth / 2.0f;
    float windowCenterY = windowHeight / 2.0f;

    if (hard)
    {
        g_view.zoom = scale;          // Hard clamp to final scale

        UpdateTargetZoom(scale);

        // Target offsets for final scale
        g_view.targetOffsetX = windowCenterX - (imgSize.width  * g_view.targetZoom) / 2.0f;
        g_view.targetOffsetY = windowCenterY - (imgSize.height * g_view.targetZoom) / 2.0f;

        // Start offsets at final position since we're hard centering
        g_view.offsetX = g_view.targetOffsetX;
        g_view.offsetY = g_view.targetOffsetY;
    }
    else
    {
        g_view.zoom = 0.05f;             // Start tiny
        UpdateTargetZoom(scale);    // Animate to final scale

        // Start offsets so that image center is centered at window center at tiny zoom
        g_view.offsetX = windowCenterX - (imgSize.width  * g_view.zoom) / 2.0f;
        g_view.offsetY = windowCenterY - (imgSize.height * g_view.zoom) / 2.0f;

        // Target offsets for final scale
        g_view.targetOffsetX = windowCenterX - (imgSize.width  * g_view.targetZoom) / 2.0f;
        g_view.targetOffsetY = windowCenterY - (imgSize.height * g_view.targetZoom) / 2.0f;
    }
    // Reset rotation for new images, honouring any EXIF orientation tag.
    g_view.rotationAngle  = g_view.exifRotation;
    g_view.targetRotationAngle = g_view.exifRotation;
}

void InitializeMenuButtons()
{
    // -------------------------
    // Open File Button
    // -------------------------
    AnimatedButton::ActivationZone topLeftActivationZone;
    topLeftActivationZone.left   = 0.0f;
    topLeftActivationZone.top    = 0.0f;
    topLeftActivationZone.right  = 0.2f;
    topLeftActivationZone.bottom = 0.2f;

    AnimatedButton::Config openConfig;
    D2D1_SIZE_F size = g_devices.renderTarget->GetSize();
    float width = 0.07f;
    float height = 0.07f;
    // A 5%-of-height left margin, converted into normalized x: x.value is a
    // fraction of the render-target WIDTH, so the same pixel margin needs
    // 0.05 * height / width. Correct only for the surface it is captured on
    // (the windowed client at startup), which is fine because anchors are
    // captured once and never re-derived.
    openConfig.layout.x.value = 0.05f*float(size.height)/float(size.width);
    openConfig.layout.y.value = 0.05f;
    openConfig.layout.x.anchor = AnimatedButton::Anchor::OffsetFromStart;
    openConfig.layout.y.anchor = AnimatedButton::Anchor::OffsetFromStart;
    openConfig.layout.x.mode = AnimatedButton::PosMode::Normalized;
    openConfig.layout.y.mode = AnimatedButton::PosMode::Normalized;
    openConfig.layout.width = width;
    openConfig.layout.height = height;
    openConfig.fontSize = 0.04f;
    openConfig.text = L"\U0001F4C2";
    openConfig.tooltip = L"Open image  [O]";
    openConfig.layout.activationZone = topLeftActivationZone;
    openConfig.layout.referenceWidth  = size.width;
    openConfig.layout.referenceHeight = size.height;
    openConfig.layout.uiPixelScale = min(size.width, size.height);
    // No auto-repeat: the callback opens the modal file dialog, so holding the
    // button would stack dialogs. Same for the hamburger below (TrackPopupMenu).
    openConfig.holdEnabled = false;
    openConfig.pressAnimation = false;
    g_buttons[ButtonId::OPEN].Initialize(
        g_devices.renderTarget.Get(),
        g_devices.dwriteFactory.Get(),
        openConfig,
        []()
        {
            OpenImageFile(g_app.mainWindow);
        });
    g_buttons[ButtonId::OPEN].UpdateLayout(g_devices.renderTarget.Get());
    g_buttons[ButtonId::OPEN].SetForcedVisibility(true);
    

    // -------------------------
    // Hamburger Menu Button
    // -------------------------

    // Step the hamburger along by 5.6% of the render-target WIDTH. Note this one
    // is NOT height-normalised like the Open button's margin above, so the gap
    // between the two buttons is proportional to the surface they were captured
    // on, not equal to that margin in pixels.
    openConfig.layout.x.value += 0.056f;
    openConfig.text = L"\u2630";
    openConfig.tooltip = L"Menu";
    // Auto-repeat would reopen the popup menu while the previous TrackPopupMenu
    // is still pumping. Stated again here rather than relying on inheriting the
    // Open button's setting through the reused openConfig.
    openConfig.holdEnabled = false;
    g_buttons[ButtonId::HELP].Initialize(
        g_devices.renderTarget.Get(),
        g_devices.dwriteFactory.Get(),
        openConfig,
        []()
        {
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING,             static_cast<UINT_PTR>(MenuCommandId::SHORTCUTS), L"Keyboard shortcuts");
            AppendMenuW(hMenu, MF_STRING,             static_cast<UINT_PTR>(MenuCommandId::ABOUT),     L"About");
            AppendMenuW(hMenu, MF_SEPARATOR, 0,                            nullptr);
            AppendMenuW(hMenu, MF_STRING | (g_settings.useHQFilter ? MF_CHECKED : MF_UNCHECKED),
                                                      static_cast<UINT_PTR>(MenuCommandId::HQ_FILTER), L"High quality filter");
            AppendMenuW(hMenu, MF_STRING | (g_settings.openFitToScreen ? MF_CHECKED : MF_UNCHECKED),
                                                      static_cast<UINT_PTR>(MenuCommandId::FIT_TO_SCREEN), L"Open images fit to screen");
            AppendMenuW(hMenu, MF_SEPARATOR, 0,                            nullptr);
            AppendMenuW(hMenu, MF_STRING,             static_cast<UINT_PTR>(MenuCommandId::ASSOCIATE), L"Associate file types");

            POINT pt;
            GetCursorPos(&pt);
            // Anchor the menu (and shift foreground) to whichever window is
            // actually visible/interactive right now - the overlay in
            // fullscreen, the main window otherwise. Hardcoding g_app.mainWindow
            // here was the real problem: in fullscreen it permanently dragged
            // OS foreground/activation onto the invisible main window and
            // never gave it back, so the very next real click on the image
            // (landing on the overlay) triggered a genuine main->overlay
            // activation switch that looked like focus loss and exited
            // fullscreen - even though the menu interaction itself was fine.
            HWND ownerWnd = (g_view.isFullscreen && g_app.overlayWindow && IsWindow(g_app.overlayWindow))
                                ? g_app.overlayWindow
                                : g_app.mainWindow;
            // TrackPopupMenu's internal menu window isn't a window we own
            // (IsOwnedByOurWindow won't recognize it), and SetForegroundWindow
            // itself is what fires WA_INACTIVE (it steals activation) - so this
            // needs an explicit suppression scope, opened BEFORE shifting
            // foreground focus, covering both calls. It closes automatically
            // when the guard goes out of scope after TrackPopupMenu returns,
            // so a genuine focus loss (Alt+Tab, etc.) still exits fullscreen
            // normally afterward.
            {
                FullscreenExitSuppressor guard;
                SetForegroundWindow(ownerWnd);
                TrackPopupMenu(hMenu, TPM_LEFTBUTTON, pt.x, pt.y, 0, ownerWnd, nullptr);
            }
            DestroyMenu(hMenu);
        });
    g_buttons[ButtonId::HELP].UpdateLayout(g_devices.renderTarget.Get());
    g_buttons[ButtonId::HELP].SetForcedVisibility(true);

}

void InitializeButtons()
{
    //g_buttons.clear();
    g_buttons[ButtonId::OPEN].SetForcedVisibility(false);
    g_buttons[ButtonId::HELP].SetForcedVisibility(false);
    float width = 0.025f;
    float height = 0.025f;
    float fontSize = 0.011f;

    // -------------------------
    // Bottom activation zone
    // -------------------------
    AnimatedButton::ActivationZone bottomActivationZone;
    bottomActivationZone.left   = 0.0f;
    bottomActivationZone.top    = 0.8f;
    bottomActivationZone.right  = 1.0f;
    bottomActivationZone.bottom = 1.0f;

    // -------------------------
    // Zoom 1:1 Button
    // -------------------------
    AnimatedButton::Config buttonConfig;

    buttonConfig.layout.x.value = 0.49f;
    buttonConfig.layout.x.anchor = AnimatedButton::Anchor::OffsetFromCenter;
    buttonConfig.layout.x.mode = AnimatedButton::PosMode::Normalized;
    buttonConfig.layout.y.value = 0.93f;
    buttonConfig.layout.y.anchor = AnimatedButton::Anchor::OffsetFromEnd;
    buttonConfig.layout.y.mode = AnimatedButton::PosMode::Normalized;
    buttonConfig.layout.width = width;
    buttonConfig.layout.height = height;
    buttonConfig.fontSize = fontSize * 1.1f;
    buttonConfig.text = L"1:1";
    buttonConfig.tooltip = L"1:1 zoom";
    buttonConfig.layout.activationZone = bottomActivationZone;
    buttonConfig.layout.uiPixelScale = g_view.uiPixelScale;
    D2D1_SIZE_F size = g_devices.renderTarget->GetSize();

    buttonConfig.layout.referenceWidth  = size.width;
    buttonConfig.layout.referenceHeight = size.height;
    buttonConfig.layout.uiPixelScale    = min(size.width, size.height);

    g_buttons[ButtonId::ZOOM_11].Initialize(
        g_devices.renderTarget.Get(),
        g_devices.dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            MakeZoomVisible(g_devices.renderTargetWindow);
            SetZoomCentered(1.0f, g_devices.renderTargetWindow, false, false);
        });
    g_buttons[ButtonId::ZOOM_11].UpdateLayout(g_devices.renderTarget.Get());

 
    // -------------------------
    // Slide-show Button
    // -------------------------
    buttonConfig.layout.x.value = 0.51f;
    buttonConfig.fontSize = fontSize;
    buttonConfig.text = L"\u25B6";
    buttonConfig.tooltip = L"Start slideshow\n[F5]";

    // No auto-repeat: the callback toggles slideshow on/off, so holding this
    // button would start and stop a slideshow a dozen times a second.
    buttonConfig.holdEnabled = false;

    g_buttons[ButtonId::SLIDESHOW].Initialize(
        g_devices.renderTarget.Get(),
        g_devices.dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            // The glyph mirrors g_slideshow.active, so the same button is the
            // stop control while a slideshow is running.
            if (g_slideshow.active)
                ExitSlideshowMode();
            else
                EnterSlideshowMode();
        });
    g_buttons[ButtonId::SLIDESHOW].UpdateLayout(g_devices.renderTarget.Get());


    // -------------------------
    // Zoom In Button
    // -------------------------
    buttonConfig.layout.x.value = 0.53f;
    buttonConfig.fontSize = fontSize;
    buttonConfig.text = L"\u2795";
    buttonConfig.tooltip = L"Zoom in\n[W / \u2191]";

    // buttonConfig is reused for the rest of the row, so auto-repeat is back on:
    // zoom and rotate hold-repeat matches held W/S/Q/E on the keyboard.
    buttonConfig.holdEnabled = true;

    g_buttons[ButtonId::ZOOM_IN].Initialize(
        g_devices.renderTarget.Get(),
        g_devices.dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            ZoomIntoImage(g_devices.renderTargetWindow, 250, nullptr);
            MakeZoomVisible(g_devices.renderTargetWindow);

        });
    g_buttons[ButtonId::ZOOM_IN].UpdateLayout(g_devices.renderTarget.Get());

    // -------------------------
    // Zoom Out Button
    // -------------------------

    buttonConfig.layout.x.value = 0.47f;
    buttonConfig.text = L"\u2796";
    buttonConfig.tooltip = L"Zoom out [S / \u2193]";

    g_buttons[ButtonId::ZOOM_OUT].Initialize(
        g_devices.renderTarget.Get(),
        g_devices.dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            ZoomIntoImage(g_devices.renderTargetWindow, -250, nullptr);
            MakeZoomVisible(g_devices.renderTargetWindow);
        });
    g_buttons[ButtonId::ZOOM_OUT].UpdateLayout(g_devices.renderTarget.Get());

    // -------------------------
    // Rotate Left Button
    // -------------------------

    buttonConfig.layout.x.value = 0.45f;
    buttonConfig.fontSize = fontSize * 1.2f;
    buttonConfig.text = L"\u2B6F";
    buttonConfig.tooltip = L"Rotate left\n[Q]";

    g_buttons[ButtonId::ROTATE_LEFT].Initialize(
        g_devices.renderTarget.Get(),
        g_devices.dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            g_view.targetRotationAngle -= 90.0f;
        });
    g_buttons[ButtonId::ROTATE_LEFT].UpdateLayout(g_devices.renderTarget.Get());

    // -------------------------
    // Rotate Right Button
    // -------------------------

    buttonConfig.layout.x.value = 0.55f;
    buttonConfig.text = L"\u2B6E";
    buttonConfig.tooltip = L"Rotate right\n[E]";

    g_buttons[ButtonId::ROTATE_RIGHT].Initialize(
        g_devices.renderTarget.Get(),
        g_devices.dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            g_view.targetRotationAngle += 90.0f;
        });
    g_buttons[ButtonId::ROTATE_RIGHT].UpdateLayout(g_devices.renderTarget.Get());

    // -------------------------
    // Previous Image Button
    // -------------------------
    buttonConfig.layout.x.value = 0.43f;
    buttonConfig.fontSize = fontSize * 1.3f;
    buttonConfig.text = L"\u2B9C";
    buttonConfig.tooltip = L"Previous image\n[A / \u2190]";

    // A held prev/next button must not outrun the decoder. The keyboard path gates
    // auto-repeat on g_fullscreen.navPendingRender, which is only cleared after
    // Render presents a frame, so at most one navigation is in flight per rendered
    // frame. The button's own repeat cadence (AnimatedButton::m_repeatInterval)
    // stays as the rate cap; this adds the same backpressure on top.
    // Applies to PREVIOUS and NEXT both - buttonConfig is reused between them.
    buttonConfig.repeatGate = []()
    {
        if (g_fullscreen.navPendingRender)
            return false;
        g_fullscreen.navPendingRender = true;
        return true;
    };

    g_buttons[ButtonId::PREVIOUS].Initialize(
        g_devices.renderTarget.Get(),
        g_devices.dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            OpenPrevImage(g_devices.renderTargetWindow);
            RestartSlideshowTimer();
        });
    g_buttons[ButtonId::PREVIOUS].UpdateLayout(g_devices.renderTarget.Get());

    // -------------------------
    // Next Image Button
    // -------------------------

    buttonConfig.layout.x.value = 0.57f;
    buttonConfig.text = L"\u2B9E";
    buttonConfig.tooltip = L"Next image\n[D / \u2192 / Space]";
    
    g_buttons[ButtonId::NEXT].Initialize(
        g_devices.renderTarget.Get(),
        g_devices.dwriteFactory.Get(),
        buttonConfig,
        []()
        {
            OpenNextImage(g_devices.renderTargetWindow);
            RestartSlideshowTimer();
        });
    g_buttons[ButtonId::NEXT].UpdateLayout(g_devices.renderTarget.Get());
    // End of the nav pair: drop the gate so any button added after this one does
    // not inherit it (the Exit button uses its own exitConfig).
    buttonConfig.repeatGate = nullptr;

    // -------------------------
    // Exit Button
    // -------------------------
    AnimatedButton::ActivationZone topRightActivationZone;
    topRightActivationZone.left   = 0.9f;
    topRightActivationZone.top    = 0.0f;
    topRightActivationZone.right  = 1.0f;
    topRightActivationZone.bottom = 0.1f;

    // Exit button inset. x.value is a fraction of width and y.value of height, so
    // to get an inset that looks square in pixels the x fraction has to be scaled
    // by height/width. top * 9/16 hard-codes that ratio for a 16:9 surface - on a
    // different aspect the X glyph sits closer to (or further from) the right
    // edge in pixels than from the top. Left as-is: it is captured once per
    // fullscreen monitor, and the only display in use is 16:9.
    float top = 0.01f;
    float right = 1.0f - top * 9.0f / 16.0f;

    AnimatedButton::Config exitConfig;
    exitConfig.layout.x.value = right;
    exitConfig.layout.y.value = top;
    exitConfig.layout.x.anchor = AnimatedButton::Anchor::OffsetFromEnd;
    exitConfig.layout.y.anchor = AnimatedButton::Anchor::OffsetFromStart;
    exitConfig.layout.x.mode = AnimatedButton::PosMode::Normalized;
    exitConfig.layout.y.mode = AnimatedButton::PosMode::Normalized;
    exitConfig.layout.width = 0.036f;
    exitConfig.layout.height = 0.036f;
    exitConfig.fontSize = 0.012f;
    exitConfig.text = L"\u274C";
    exitConfig.tooltip = L"Exit\n[Esc]";
    exitConfig.layout.activationZone = topRightActivationZone;
    exitConfig.layout.referenceWidth  = size.width;
    exitConfig.layout.referenceHeight = size.height;
    exitConfig.layout.uiPixelScale = min(size.width, size.height);

    g_buttons[ButtonId::EXIT].Initialize(
        g_devices.renderTarget.Get(),
        g_devices.dwriteFactory.Get(),
        exitConfig,
        []()
        {
            if (!g_view.bitmap)
            {
                // Destroy the main window rather than posting a quit message:
                // WM_DESTROY is what runs the teardown - the background threads, the
                // device resources, the containers holding COM objects. Quitting the
                // loop from here would skip all of it. With no image there is also
                // nothing to shrink, so this quits at once instead of animating -
                // the same choice windowed Esc makes.
                DestroyWindow(g_app.mainWindow);
                return;
            }

            // Same shrink-to-a-point-then-quit as Esc in fullscreen: calling the one
            // function keeps the exit animation identical from both entry points
            // (it also owns the refresh-rate rescale of g_view.smooth).
            InitFullScreenExit();
        });
    g_buttons[ButtonId::EXIT].UpdateLayout(g_devices.renderTarget.Get());

    UpdateSlideshowButton();
}

// The glyph that replaces the play triangle while a slideshow runs.
static constexpr const wchar_t kSlideshowStopGlyph[] = L"\u25A0";

// The slideshow button shows play while no slideshow runs and pause while one
// does, so it doubles as the stop control. SetText only rewrites the config
// string that Draw reads each frame; SetTooltip rebuilds the tooltip's text
// format, so this belongs on a slideshow transition, not in the frame loop.
void UpdateSlideshowButton()
{
    auto it = g_buttons.find(ButtonId::SLIDESHOW);
    if (it == g_buttons.end())
        return;

    AnimatedButton& btn = it->second;

    if (g_slideshow.active)
    {
        btn.SetText(kSlideshowStopGlyph);
        btn.SetTooltip(L"Stop slideshow\n[F5]");
    }
    else
    {
        btn.SetText(L"\u25B6");
        btn.SetTooltip(L"Start slideshow\n[F5]");
    }
}

void InitializeImageInfoLabel()
{
    // Filename display.
    UITextBox::ActivationZone zone;
    zone.left   = 0.0f;
    zone.right  = 1.0f;
    zone.top    = 0.8f;
    zone.bottom = 1.0f;

    UITextBox::Config config;
    D2D1_SIZE_F size = g_devices.renderTarget->GetSize();

    config.layout.referenceWidth  = size.width;
    config.layout.referenceHeight = size.height;
    config.layout.x.value = 0.5f;
    config.layout.x.anchor = UITextBox::Anchor::OffsetFromCenter;
    config.layout.x.mode = UITextBox::PosMode::Normalized;
    config.layout.y.value = 0.97f;
    config.layout.y.anchor = UITextBox::Anchor::OffsetFromEnd;
    config.layout.y.mode = UITextBox::PosMode::Normalized;

    config.relativeFontSize = 0.016f;
    // NOT "the full window width": Layout::width is multiplied by uiPixelScale,
    // which is min(w,h), so 1.0f here is a box one full min-dimension across
    // (2160 px on a 4K display) - wider than the 1604 px windowed client. It is
    // invisible here (backgroundAlpha 0) and the text is centred in it, so the
    // only thing that matters is that it is comfortably wide enough.
    config.layout.width  = 1.0f;
    config.layout.height = 0.05f;

    config.backgroundAlpha = 0.0f;   // no box
    config.isEditable = false;      // display only

    config.layout.activationZone = zone;

    config.layout.uiPixelScale = g_view.uiPixelScale;

    g_textBoxes[TextBoxId::FILE_NAME].Initialize(
        g_devices.dwriteFactory.Get(),
        g_devices.renderTarget.Get(),
        config,
        nullptr);
    g_textBoxes[TextBoxId::FILE_NAME].UpdateLayout(g_devices.renderTarget.Get());

    // Zoom level display/input.
    zone.left   = 0.4f;
    zone.right  = 0.6f;
    zone.top    = 0.4f;
    zone.bottom = 0.6f;
    config.layout.activationZone = zone;
    config.layout.x.value = 0.5;
    config.layout.y.value = 0.5f;
    config.layout.y.anchor = UITextBox::Anchor::OffsetFromCenter;
    config.layout.width = 0.05f;
    config.layout.height = 0.025f;
    config.relativeFontSize = 0.012f;
    config.backgroundAlpha = 0.5f; // semi-transparent box for zoom display
    config.isEditable = true;
    config.inputMode = UITextBox::InputMode::NumericFloat;
    g_textBoxes[TextBoxId::ZOOM_INPUT].Initialize(
        g_devices.dwriteFactory.Get(),
        g_devices.renderTarget.Get(),
        config,
        [](const std::wstring& text)
            {
              if (text.empty())
                    return;

                try
                {
                    float value = std::stof(text);
                    if (value < 1.0f)
                    {
                        value = 1.0f;
                    } 
                    else if (value > 10000.0f)
                    {
                        value = 10000.0f;
                    } 

                    // Anchor the typed zoom on whatever is at the CENTRE OF THE
                    // SCREEN, the same way wheel zoom anchors on the cursor (see
                    // ZoomIntoImage). Anchoring on the image's own centre - what
                    // SetZoomCentered(preserveCenter = true) does - can shove the
                    // image off screen, because after a cursor-anchored wheel zoom
                    // the image centre is usually outside the viewport, and
                    // preserving an off-screen centre keeps it off screen.
                    if (!g_view.bitmap || g_view.zoom < 0.00001f)
                        return;   // the readout stays clickable over the load-error
                                  // screen, where there is no bitmap to measure

                    RECT rc;
                    GetClientRect(g_devices.renderTargetWindow, &rc);
                    const float viewCX = (float)(rc.right - rc.left) * 0.5f;
                    const float viewCY = (float)(rc.bottom - rc.top) * 0.5f;

                    const float newZoom = value / 100.0f;

                    // The image-space point currently sitting at the viewport centre;
                    // hold that point in place at the new zoom. Live zoom/offset are
                    // used on purpose, so this is correct mid-spring.
                    const float imgX = (viewCX - g_view.offsetX) / g_view.zoom;
                    const float imgY = (viewCY - g_view.offsetY) / g_view.zoom;

                    g_view.targetOffsetX = viewCX - imgX * newZoom;
                    g_view.targetOffsetY = viewCY - imgY * newZoom;
                    UpdateTargetZoom(newZoom);

                }
                catch (...)
                {
                    // invalid number -> ignore
                }
            }
        );
    g_textBoxes[TextBoxId::ZOOM_INPUT].UpdateLayout(g_devices.renderTarget.Get());
    g_textBoxes[TextBoxId::ZOOM_INPUT].SetTooltip(
        L"Zoom\nclick & type\n Enter to set");
}

bool ZoomIntoImage(HWND hWnd, short delta, POINT* optionalPt)
{
    if (g_app.isExiting || !g_view.bitmap)
        return false;

    bool useMouse = (optionalPt != nullptr);

    POINT pt{};
    if (useMouse)
    {
        // optionalPt arrives in screen coordinates (GetCursorPos in WM_MOUSEWHEEL);
        // the hit test and the offset math below both want client coordinates, so the
        // conversion happens exactly once, here.
        pt = *optionalPt;
        ScreenToClient(hWnd, &pt);
    }

    float zoomStep = delta / 120.0f;
    float zoomAmount = powf(1.1f, zoomStep);
    float newTargetZoom = g_view.targetZoom * zoomAmount;

    if (newTargetZoom < 0.01f) newTargetZoom = 0.01f;
    if (newTargetZoom > 100.0f) newTargetZoom = 100.0f;

    D2D1_SIZE_F imgSize = g_view.bitmap->GetSize();

    // Check if mouse is over the image as it is actually drawn (rotation
    // included), not over the unrotated rect the offsets describe.
    bool overImage = useMouse && IsPointOverImage(static_cast<float>(pt.x), static_cast<float>(pt.y));

    if (useMouse && overImage)
    {
        // zoom relative to cursor
        float imageX = (pt.x - g_view.offsetX) / g_view.zoom;
        float imageY = (pt.y - g_view.offsetY) / g_view.zoom;

        g_view.targetOffsetX = pt.x - imageX * newTargetZoom;
        g_view.targetOffsetY = pt.y - imageY * newTargetZoom;
    }
    else
    {
        // ALWAYS zoom from center
        float centerX = g_view.offsetX + imgSize.width  * g_view.zoom / 2.0f;
        float centerY = g_view.offsetY + imgSize.height * g_view.zoom / 2.0f;

        g_view.targetOffsetX = centerX - (imgSize.width  * newTargetZoom) / 2.0f;
        g_view.targetOffsetY = centerY - (imgSize.height * newTargetZoom) / 2.0f;
    }
    UpdateTargetZoom(newTargetZoom);
    return true;
}

void MakeZoomVisible(HWND hWnd)
{
    g_textBoxes[TextBoxId::ZOOM_INPUT].SetForcedVisibility(true);
    SetTimer(hWnd, static_cast<UINT_PTR>(TimerId::ZOOM_DISPLAY), 1000, nullptr);
}
