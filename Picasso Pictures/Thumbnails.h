#pragma once
// Thumbnails.h : the file list, the background thumbnail loader, the film
// strip draw, and the directory watcher.
//
// Read CODE_MAP.md section 7 before changing anything here - the threading is
// load-bearing:
//   * g_image.files / g_thumb.entries are rebuilt in two places, and both stop
//     the loader thread FIRST (StopThumbnailLoader) so nothing races with it:
//     BuildImageList here rebuilds the whole list from the directory, and
//     DeleteCurrentImage in the main file erases one entry, re-aligns the
//     entries and restarts the loader itself. Both snapshot the existing
//     thumbnails BY PATH before the list shifts, so a thumbnail follows its
//     file rather than its old index.
//   * The loader thread never touches g_thumb.entries: it publishes finished
//     thumbnails into g_thumb.readyQueue under g_thumb.readyMutex, and
//     UpdateEngine (still in the main file) drains ONE upload per frame.
//     That shared state is why g_thumb is declared in Globals.h.
//   * The watcher thread posts WM_APP_DIRCHANGE to g_app.mainWindow - always
//     that one, because it is the handle whose value cannot change while the
//     thread runs. WndProc is the same procedure for the main and the overlay
//     window class, so a fullscreen directory change is still handled; the
//     handler then calls BuildImageList again.
//   * DrawThumbnailStrip runs inside Render's BeginDraw/EndDraw, so it must
//     not create device resources beyond the throwaway brushes it already uses.
//
// All six functions have external linkage and none carries a default argument,
// so the declarations below match the definitions exactly.
//
// IsSupportedImage is a general file-type predicate that lives with the
// file-list/navigation code in the main file; Thumbnails.cpp declares it
// locally because BuildImageList and the watcher thread both call it.

#include "Globals.h"

// ---- the file list ----
// Rebuilds g_image.files from the directory containing `filename`, sorted with
// StrCmpLogicalW so the order matches Explorer exactly (natural sort: "img2"
// before "img10"), sets g_image.index to `filename` or to -1 when that file is
// no longer in the folder, re-aligns g_thumb.entries to the new indices by path
// (so existing thumbnails survive an insert or a delete), then (re)starts the
// directory watcher and the thumbnail loader.
// The folder is walked with the error_code overloads of directory_iterator and
// is_regular_file, so a gone or unreadable directory does not throw a
// filesystem_error through the caller. A walk that fails leaves g_image.files,
// g_thumb.entries, g_image.index, the watcher and the loader exactly as they
// were and returns without rebuilding anything - an empty list therefore always
// means "this folder really has no images", which is what WM_APP_DIRCHANGE uses
// to decide the view is gone.
// Called from wWinMain, OpenImageFile, WM_COPYDATA, WM_DROPFILES and
// WM_APP_DIRCHANGE.
void BuildImageList(const wchar_t* filename);

// ---- the background thumbnail loader ----
// Stops the previous thread, resizes g_thumb.entries, clears g_thumb.readyQueue, then
// spawns a thread that decodes only the empty slots in a spiral outward from
// g_image.index, so the visible part of the strip fills first. Fast path
// = embedded JPEG thumbnail when its short side is >= 180 px; slow path = full
// decode + HighQualityCubic scale to 180 px (AVIF/JXL go through the bundled
// libavif/libjxl decoders because WIC cannot open them). EXIF orientation is
// applied to the small bitmap via IWICBitmapFlipRotator.
void StartThumbnailLoader();
void StopThumbnailLoader();

// ---- the film strip ----
// Draws one rounded-rect slot per entry, centred on the current image and
// scrolled by g_thumb.scrollOffset; the current slot is undimmed, the rest are
// darkened 50%, and empty slots get a placeholder brush. One cached
// origin-centred geometry is reused for every slot by moving the render-target
// transform, so the vector holds that one shared geometry.
// Only called by Render, from Rendering.cpp.
void DrawThumbnailStrip(float visibility);

// ---- directory watcher ----
// Non-recursive ReadDirectoryChangesW on FILE_NOTIFY_CHANGE_FILE_NAME in a
// background thread; posts WM_APP_DIRCHANGE to g_app.mainWindow when a changed
// name is a supported image, and also when the read returns zero bytes, which
// means the notification buffer overflowed and the names are unknown.
// StartDirectoryWatcher stops the previous one first. The handle, the
// thread and the stop event are file-local to Thumbnails.cpp:
// StopDirectoryWatcher signals the stop event, joins the thread, and only then
// closes both handles, while the thread cancels and drains its own pending read
// on the way out - so the watch handle is never closed under a live request.
void StartDirectoryWatcher(const std::wstring& dir);
void StopDirectoryWatcher();
