#pragma once
// Dialogs.h : the two user-facing dialogs - the file-association writer, and the
// one dialog proc behind both the About box and the Keyboard-shortcuts box.
//
// Read CODE_MAP.md section 10 before changing anything here:
//   * AssociateFileTypes asks first and writes second: a TaskDialogIndirect
//     confirmation (TDF_ALLOW_DIALOG_CANCELLATION, sized to content) has to
//     answer OK before a single registry key is touched, so cancelling leaves
//     the machine exactly as it was. It then writes the HKCU ProgID, its
//     shell\open\command ("exe" "%1"), the RegisteredApplications +
//     Software\PicassoPictures\Capabilities keys and one
//     Software\Classes\<ext> -> ProgID value per extension, and finally
//     SHChangeNotify(SHCNE_ASSOCCHANGED) so Explorer picks it up without a
//     restart. The result TaskDialog reports whether every extension took.
//   * About is ONE dialog proc serving TWO templates: IDD_ABOUTBOX (IDM_ABOUT,
//     MenuCommandId::ABOUT) and IDD_COMMANDBOX (ID_HELP_COMMANDS,
//     MenuCommandId::SHORTCUTS). There is no separate "Keyboard shortcuts" function
//     anywhere in the project - the shortcuts box is the same CALLBACK.
//   * About's WM_NOTIFY branch is what makes the GitHub link live: it matches
//     IDC_SYSLINK_GITHUB + NM_CLICK and ShellExecuteW's pLink->item.szUrl, so
//     the URL comes from the SysLink control rather than a hard-coded string.
//   * Both dialogs are opened with hWnd as owner, which is what lets
//     IsOwnedByOurWindow() in WM_ACTIVATE recognize them and keep fullscreen
//     from being torn down when they take foreground (see Globals.h).
//
// Neither function is `static` and neither carries a default argument, so the two
// declarations below match the definitions exactly. Dialogs.cpp declares nothing
// of its own: both functions call only SDK code. `g_app.instance` is the only global
// either one touches (the task dialog's hInstance / g_app.instance argument).

#include "Globals.h"

// Asks, then registers the supported extensions (.jpg .jpeg .png .bmp .gif
// .tif .tiff .webp .avif .jxl) under HKCU and refreshes the shell.
// Called from WndProc's WM_COMMAND (MenuCommandId::ASSOCIATE).
void AssociateFileTypes(HWND hWnd);

// Dialog proc for IDD_ABOUTBOX and IDD_COMMANDBOX alike: WM_INITDIALOG,
// the SysLink NM_CLICK -> ShellExecuteW, and IDOK/IDCANCEL -> EndDialog.
// Passed as the DLGPROC of four DialogBox() calls in WndProc.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
