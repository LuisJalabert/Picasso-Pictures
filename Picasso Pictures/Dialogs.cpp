// Dialogs.cpp : the file-association writer and the About / Keyboard-shortcuts
// dialog proc.
//
// Includes this module needs:
//   * <commctrl.h> + comctl32.lib - AssociateFileTypes builds a
//     TASKDIALOGCONFIG and calls TaskDialogIndirect/TaskDialog; About reads
//     NMHDR/NMLINK/NM_CLICK off WM_NOTIFY. The main file has no commctrl.h
//     include of its own, so every module that calls a TaskDialog* has to
//     include it here itself.
//   * <shellapi.h> - About opens the SysLink URL with ShellExecuteW.
//   * <shlobj.h> - AssociateFileTypes broadcasts SHCNE_ASSOCCHANGED with
//     SHChangeNotify. On this SDK that prototype is NOT in shellapi.h at all,
//     it is in ShlObj.h. shell32.lib is also in the main file's pragma list;
//     it is repeated here so this TU stays self-contained.
//   * <cstdio> - swprintf_s builds the "exe" "%1" open command.
//   * <cwchar> - wcslen sizes each REG_SZ value written by the RegSetStr lambda.
//   * Registry APIs (RegCreateKeyExW / RegSetValueExW / RegCloseKey) come from
//     windows.h via Globals.h, and advapi32.lib is one of the linker's default
//     libraries, so no pragma is needed for it.
//
// Neither function calls anything that lives in another module:
// AssociateFileTypes only touches g_app.instance, and About touches nothing global at
// all.

#include "Dialogs.h"

#include <commctrl.h>   // TASKDIALOGCONFIG, TaskDialogIndirect/TaskDialog, NMHDR/NMLINK/NM_CLICK
#include <shellapi.h>   // ShellExecuteW (the SysLink URL)
#include <shlobj.h>     // SHChangeNotify + SHCNE_ASSOCCHANGED/SHCNF_IDLIST (ShlObj.h, NOT shellapi.h)
#include <cstdio>       // swprintf_s
#include <cwchar>       // wcslen

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

void AssociateFileTypes(HWND hWnd)
{
    // Extensions we will associate, shown to the user before anything is written
    static const wchar_t* exts[] = {
        L".jpg", L".jpeg", L".png", L".bmp",
        L".gif", L".tif",  L".tiff", L".webp",
        L".avif", L".jxl"
    };

    // ----------------------------------------------------------------
    // 1. Confirmation dialog via TaskDialog
    // ----------------------------------------------------------------
    std::wstring extList;
    for (auto ext : exts)
        extList += std::wstring(ext) + L"  ";

    std::wstring detail =
        L"The following file types will be associated with Picasso Pictures:\n\n"
        + extList +
        L"\n\nAfter this change, double-clicking any of these files in Explorer "
        L"will open them in Picasso Pictures.";

    TASKDIALOGCONFIG tdc       = {};
    tdc.cbSize                 = sizeof(tdc);
    tdc.hwndParent             = hWnd;
    tdc.hInstance              = g_app.instance;
    tdc.dwFlags                = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    tdc.pszWindowTitle         = L"Associate file types";
    tdc.pszMainIcon            = TD_INFORMATION_ICON;
    tdc.pszMainInstruction     = L"Set Picasso Pictures as the default image viewer?";
    tdc.pszContent             = detail.c_str();
    tdc.dwCommonButtons        = TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON;

    int button = 0;
    if (FAILED(TaskDialogIndirect(&tdc, &button, nullptr, nullptr)) ||
        button != IDOK)
        return;  // user cancelled

    // ----------------------------------------------------------------
    // 2. Write registry entries
    // ----------------------------------------------------------------
    wchar_t exePath[MAX_PATH] = {};
    DWORD pathLen = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (pathLen == 0 || pathLen >= MAX_PATH)
    {
        // 0 = failed, MAX_PATH = the path did not fit and was truncated. Either way
        // the open command written below would be empty or cut off mid-path, which is
        // a broken association that the result dialog would still have reported as a
        // success - so nothing is registered at all.
        TaskDialog(hWnd, g_app.instance,
            L"File association",
            L"Could not determine the program path",
            L"Picasso Pictures could not find its own executable, so no file "
            L"associations were changed.",
            TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
        return;
    }

    static const wchar_t* PROG_ID      = L"PicassoPictures.ImageFile";
    static const wchar_t* FRIENDLY     = L"Picasso Pictures Image";
    static const wchar_t* OPEN_CMD_FMT = L"\"%s\" \"%%1\"";

    wchar_t openCmd[MAX_PATH + 8] = {};
    swprintf_s(openCmd, MAX_PATH + 8, OPEN_CMD_FMT, exePath);

    auto RegSetStr = [](const wchar_t* keyPath, const wchar_t* valueName, const wchar_t* data) -> bool
    {
        HKEY hKey = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                            &hKey, nullptr) != ERROR_SUCCESS)
            return false;
        DWORD cb = (DWORD)((wcslen(data) + 1) * sizeof(wchar_t));
        bool ok = RegSetValueExW(hKey, valueName, 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(data), cb) == ERROR_SUCCESS;
        RegCloseKey(hKey);
        return ok;
    };

    bool allOk = true;

    // ProgID + open command. These two are what actually make double-clicking a file
    // work, so their failure has to reach the result dialog - it used to track only
    // the per-extension writes below and could announce "Done!" with the ProgID or
    // its open command missing.
    std::wstring progBase = std::wstring(L"Software\\Classes\\") + PROG_ID;
    if (!RegSetStr(progBase.c_str(),                               nullptr, FRIENDLY)) allOk = false;
    if (!RegSetStr((progBase + L"\\shell\\open\\command").c_str(), nullptr, openCmd))   allOk = false;

    // App capabilities (shows up in Windows Settings -> Default Apps)
    static const wchar_t* CAP_KEY = L"Software\\PicassoPictures\\Capabilities";
    if (!RegSetStr(L"Software\\RegisteredApplications", L"PicassoPictures", CAP_KEY))  allOk = false;
    if (!RegSetStr(CAP_KEY, L"ApplicationName",         L"Picasso Pictures"))          allOk = false;
    if (!RegSetStr(CAP_KEY, L"ApplicationDescription",  L"Lightweight image viewer"))  allOk = false;

    // Per-extension association
    for (auto ext : exts)
    {
        std::wstring extKey = std::wstring(L"Software\\Classes\\") + ext;
        std::wstring capKey = std::wstring(CAP_KEY) + L"\\FileAssociations";

        if (!RegSetStr(extKey.c_str(), nullptr, PROG_ID))
            allOk = false;

        RegSetStr(capKey.c_str(), ext, PROG_ID);
    }

    // Flush the shell cache
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    // ----------------------------------------------------------------
    // 3. Result dialog
    // ----------------------------------------------------------------
    if (allOk)
    {
        TaskDialog(hWnd, g_app.instance,
            L"File association",
            L"Done!",
            L"Picasso Pictures is now the default viewer for all supported image types.",
            TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
    }
    else
    {
        TaskDialog(hWnd, g_app.instance,
            L"File association",
            L"Some types could not be associated",
            L"One or more registry values could not be written.\n"
            L"Everything here goes under the current user's own registry (HKCU), so "
            L"this is not something running as administrator would fix - a group "
            L"policy or a per-user file-type lock is the usual cause.",
            TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
    }
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_NOTIFY:
    {
        NMHDR* pHdr = reinterpret_cast<NMHDR*>(lParam);
        if (pHdr->idFrom == IDC_SYSLINK_GITHUB && pHdr->code == NM_CLICK)
        {
            NMLINK* pLink = reinterpret_cast<NMLINK*>(lParam);
            ShellExecuteW(hDlg, L"open", pLink->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
        }
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
