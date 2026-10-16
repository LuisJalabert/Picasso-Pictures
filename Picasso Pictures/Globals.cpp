// Globals.cpp : single definition site for Picasso Pictures' global state.
//
// Every `extern` in Globals.h gets exactly one definition here, in the same
// order, so the whole set can be found in one place and every module reaches
// it via #include "Globals.h".
//
// Nothing in this file may depend on another global's initialisation: all
// initialisers here are compile-time constants, so there is no static
// init-order hazard regardless of which TU touches them first.
//
// The two std::thread members here (g_anim.decodeThread, g_thumb.loaderThread) and
// the watcher thread in Thumbnails.cpp are all joined explicitly during
// WM_DESTROY - a joinable std::thread destructing at exit calls std::terminate.

#include "Globals.h"

// ---- Identity / lifetime ----
// Values come from the member defaults in the AppIdentity declaration.
AppIdentity                                       g_app;

// ---- Factories / devices ----
// Values come from the member defaults in the DeviceResources declaration.
DeviceResources                                 g_devices;

// ---- View state ----
// Values come from the member defaults in the ViewState declaration.
ViewState                                            g_view;

// ---- Backgrounds ----
// Values come from the member defaults in the BackgroundArt declaration.
BackgroundArt                                     g_background;

// ---- Fullscreen lifecycle ----
// Values come from the member defaults in the FullscreenState declaration.
FullscreenState                                     g_fullscreen;

// ---- Current image / file list ----
// Values come from the member defaults in the ImageState declaration.
ImageState                                             g_image;

// ---- Animated-image playback state ----
// Values come from the member defaults in the AnimState declaration.
AnimState                                             g_anim;
// ---- UI controls ----
std::unordered_map<TextBoxId, UITextBox>          g_textBoxes;
std::unordered_map<ButtonId, AnimatedButton>      g_buttons;

// ---- D3D11 mip-mapped image rendering pipeline ----
// Values come from the member defaults in the RenderPipeline declaration.
RenderPipeline                                         g_pipeline;
// ---- Settings ----
// Values come from the member defaults in the AppSettings declaration.
AppSettings                                             g_settings;

// ---- Vignette animated visibility ----
// Values come from the member defaults in the VignetteState declaration.
VignetteState                                            g_vignette;

// ---- Thumbnail film strip ----
// Values come from the member defaults in the ThumbState declaration.
ThumbState                                                      g_thumb;

// ---- Slideshow ----
// Values come from the member defaults in the SlideshowState declaration.
SlideshowState                                      g_slideshow;