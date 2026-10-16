#pragma once
// Metadata.h : the EXIF / PNG-tEXt / WebP-XMP / JXL-box / AVIF-XMP+Exif
// readers, the metadata viewer window and the in-app Properties dialog.
//
// Read CODE_MAP.md section 9 before changing anything here - the reader
// layering is load-bearing:
//   * Every format's reader ends up in the SAME set of display helpers
//     (Append*Field for the metadata text view, Read*Value for the Properties
//     dialog), so a new format only needs a way to get at a TIFF blob or an
//     XMP packet. WIC's own metadata query reader is used for the formats WIC
//     can open; the raw chunk/box walkers exist because WIC cannot see PNG
//     tEXt/iTXt (arbitrary keywords from SD/ComfyUI), the WebP "XMP " RIFF
//     chunk at all, or JXL/AVIF boxes without a decode.
//   * ShowImageMetadata tries BOTH `/app1/ifd/...` and `/ifd/...` for every
//     tag - PNG and WebP have no APP1 segment.
//   * ReadJxlMetadataBoxes sanity-checks the TIFF header before trusting the
//     4-byte offset prefix, because hand-edited JXL files omit it.
//   * ShowMetadataViewerWindow has its OWN modal message pump (the viewer is
//     modeless-but-modal-in-spirit) plus its own accelerator table, and it
//     disables + re-foregrounds the owner around it; that pump is what lets
//     Esc and Ctrl+A work inside a 30k+ character edit control.
//   * ShowImageProperties pre-formats every field into ImagePropertiesFields
//     and passes it via lParam, so PropertiesDlgProc does no parsing and the
//     dialog layout is completely static (missing fields show an em dash
//     rather than hiding their row).
//   * The viewer and the Properties dialog are both OWNED by the main window,
//     which is what makes IsOwnedByOurWindow() in WM_ACTIVATE recognize them -
//     no manual FullscreenExitSuppressor needed there.
//
// Only the six functions with external linkage are declared here, and of those
// only ShowImageMetadata and ShowImageProperties are called from outside this
// file (both from WndProc's WM_COMMAND). The other four are declared anyway -
// they are plausible entry points and declaring them costs nothing. Everything
// else in Metadata.cpp is deliberately file-local and NOT declared here:
//   * the 27 `static` helpers (the Append*/Read* reader primitives, the raw
//     PNG/WebP/JXL/AVIF chunk-and-box walkers, the mini-XML helpers, the
//     format/size/date formatters and Latin1BytesToWide/Utf8BytesToWide),
//     which nothing outside this file calls;
//   * `struct PngTextField` and `struct ImagePropertiesFields`, which are
//     file-scope PODs used only inside this file;
//   * AppendRationalField's default argument, which stays on its definition
//     because the definition precedes every call and there is no second
//     declaration (a declaration here would be a second default -> C2522).

#include "Globals.h"

// ---- the two entry points WndProc calls ----
// Builds the whole metadata text (EXIF incl. GPS/orientation/colour space,
// camera, lens, dimensions, format, file size/date, PNG tEXt, WebP/JXL/AVIF
// XMP) and hands it to the viewer window.
void ShowImageMetadata(HWND hWnd);

// Fills an ImagePropertiesFields with pre-formatted display text and opens
// the in-app IDD_PROPERTIES dialog owned by hWnd.
void ShowImageProperties(HWND hWnd);

// ---- the viewer window ----
// Creates/uses L"MetadataViewerClass" (dark owner-draw, MetadataControlId::EDIT with
// EM_SETLIMITTEXT 0 to lift the ~30k edit cap), runs its own modal pump with an
// accelerator table (Esc closes, Ctrl+A selects all), and restores foreground +
// owner sensitivity on the way out.
void ShowMetadataViewerWindow(HWND owner, const std::wstring& title, const std::wstring& body);
LRESULT CALLBACK MetadataViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
ATOM RegisterMetadataViewerClass(HINSTANCE hInstance);

// ---- the Properties dialog ----
// IDD_PROPERTIES: 13 IDC_PROP_* statics, all fed from the lParam struct.
INT_PTR CALLBACK PropertiesDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
