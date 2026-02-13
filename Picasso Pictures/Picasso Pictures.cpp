// Picasso Pictures.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "resource.h"

#include <windows.h>
#include <objidl.h>
#include <commdlg.h>
#include <algorithm>
#include <windowsx.h>
#include <d2d1.h>
#include <wincodec.h>


#define MAX_LOADSTRING 100

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")

// Global Variables:
HINSTANCE               hInst;                              // current instance
WCHAR                   szTitle[MAX_LOADSTRING];            // The title bar text
WCHAR                   szWindowClass[MAX_LOADSTRING];      // the main window class name
ID2D1Factory*           g_d2dFactory = nullptr;
ID2D1HwndRenderTarget*  g_renderTarget = nullptr;
IWICImagingFactory*     g_wicFactory = nullptr;
ID2D1Bitmap*            g_d2dBitmap = nullptr;
float                   g_zoom = 1.0f;                      // Current zoom level
float                   g_offsetX = 0.0f;                   // Pan offset X
float                   g_offsetY = 0.0f;                   // Pan offset Y
bool                    g_isDragging = false;
POINT                   g_lastMouse = {};
float                   g_targetZoom = 1.0f;
float                   g_targetOffsetX = 0.0f;
float                   g_targetOffsetY = 0.0f;
bool                    g_isAnimating = false;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);


int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitialize(nullptr);

    D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        &g_d2dFactory
    );

    CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&g_wicFactory)
    );

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_PICASSOPICTURES, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Create window (loads image inside)
    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(
        hInstance,
        MAKEINTRESOURCE(IDC_PICASSOPICTURES)
    );

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PICASSOPICTURES));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_PICASSOPICTURES);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(
    szWindowClass,
    szTitle,
    WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    800,     // width
    600,     // height
    nullptr,
    nullptr,
    hInstance,
    nullptr
    );

    // Center window on screen at startup
    RECT rc;
    GetWindowRect(hWnd, &rc);

    int windowWidth  = rc.right - rc.left;
    int windowHeight = rc.bottom - rc.top;

    int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    int posX = (screenWidth  - windowWidth)  / 2;
    int posY = (screenHeight - windowHeight) / 2;

    SetWindowPos(
        hWnd,
        nullptr,
        posX,
        posY,
        0,
        0,
        SWP_NOSIZE | SWP_NOZORDER
    );

   if (!hWnd)
   {
      return FALSE;
   }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

   return TRUE;
}

void FitToWindow(HWND hWnd)
{
    if (!g_d2dBitmap) return;

    RECT rc;
    GetClientRect(hWnd, &rc);

    float windowWidth  = (float)(rc.right - rc.left);
    float windowHeight = (float)(rc.bottom - rc.top);

    D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

    float scaleX = windowWidth  / imgSize.width;
    float scaleY = windowHeight / imgSize.height;

    g_zoom = min(scaleX, scaleY);

    // Center image
    g_offsetX = (windowWidth  - imgSize.width  * g_zoom) / 2.0f;
    g_offsetY = (windowHeight - imgSize.height * g_zoom) / 2.0f;

    InvalidateRect(hWnd, nullptr, FALSE);
}


void CreateRenderTarget(HWND hWnd)
{
    if (!g_renderTarget)
    {
        RECT rc;
        GetClientRect(hWnd, &rc);

        D2D1_SIZE_U size = D2D1::SizeU(
            rc.right - rc.left,
            rc.bottom - rc.top
        );

        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();

        props.dpiX = 96.0f;
        props.dpiY = 96.0f;

        g_d2dFactory->CreateHwndRenderTarget(
            props,
            D2D1::HwndRenderTargetProperties(hWnd, size),
            &g_renderTarget
        );
    }
}

bool LoadImageD2D(HWND hWnd, const wchar_t* filename)
{
    // If we already have a bitmap loaded, release it
    if (g_d2dBitmap)
    {
        g_d2dBitmap->Release();
        g_d2dBitmap = nullptr;
    }

    // Make sure render target exists
    CreateRenderTarget(hWnd);

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    // Create decoder from file
    HRESULT hr = g_wicFactory->CreateDecoderFromFilename(
        filename,
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder
    );

    if (FAILED(hr)) return false;

    // Get first image frame
    decoder->GetFrame(0, &frame);

    // Create format converter
    g_wicFactory->CreateFormatConverter(&converter);

    // Convert to 32-bit BGRA format required by Direct2D
    converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom
    );
    // Create Direct2D bitmap from WIC bitmap
    g_renderTarget->CreateBitmapFromWicBitmap(
        converter,
        nullptr,
        &g_d2dBitmap
    );
    if (g_d2dBitmap)
    {
        D2D1_SIZE_F size = g_d2dBitmap->GetSize();
        // Reset view state for new image
        g_zoom = 1.0f;
        g_offsetX = 0.0f;
        g_offsetY = 0.0f;

        int width  = (int)(size.width);
        int height = (int)(size.height);

        RECT rc = { 0, 0, width, height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

        int windowWidth  = rc.right - rc.left;
        int windowHeight = rc.bottom - rc.top;

        // Get screen size
        int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);

        // Center position
        int posX = (screenWidth  - windowWidth)  / 2;
        int posY = (screenHeight - windowHeight) / 2;

        // Clamp so window never starts off-screen
        posX = max(0, posX);
        posY = max(0, posY);

        SetWindowPos(
            hWnd,
            nullptr,
            posX,
            posY,
            windowWidth,
            windowHeight,
            SWP_NOZORDER | SWP_NOACTIVATE
        );

    }

    // Release temporary COM objects
    converter->Release();
    frame->Release();
    decoder->Release();

    // Force redraw
    InvalidateRect(hWnd, nullptr, FALSE);
    return g_d2dBitmap != nullptr;
}

bool OpenImageFile(HWND hWnd)
{
    // Buffer that will receive the selected file path
    wchar_t fileName[MAX_PATH] = { 0 };

    // Structure used by the Windows file open dialog
    OPENFILENAME ofn = {};

    ofn.lStructSize = sizeof(ofn);          // Required size
    ofn.hwndOwner = hWnd;                  // Parent window
    ofn.lpstrFile = fileName;              // Output buffer
    ofn.nMaxFile = MAX_PATH;               // Buffer size

    // File filter (double-null terminated!)
    ofn.lpstrFilter =
        L"Image Files (*.jpg;*.png;*.bmp)\0*.jpg;*.png;*.bmp\0"
        L"All Files (*.*)\0*.*\0";

    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;
    ofn.lpstrDefExt = L"jpg";

    // Show dialog
    if (!GetOpenFileName(&ofn))
        return false;   // User cancelled

    // Load image using Direct2D/WIC
    if (!LoadImageD2D(hWnd, fileName))
    {
        MessageBox(hWnd, L"Failed to load image.", L"Error", MB_ICONERROR);
        return false;
    }

    return true;
}


//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_OPEN:
                OpenImageFile(hWnd);
                break;
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);

            CreateRenderTarget(hWnd);
            g_renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            g_renderTarget->BeginDraw();

            g_renderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));

            if (g_d2dBitmap)
            {
                D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

                // Apply zoom transform
                D2D1::Matrix3x2F transform =
                    D2D1::Matrix3x2F::Scale(g_zoom, g_zoom) *
                    D2D1::Matrix3x2F::Translation(g_offsetX, g_offsetY);


                g_renderTarget->SetTransform(transform);

                D2D1_RECT_F destRect =
                    D2D1::RectF(0, 0, imgSize.width, imgSize.height);

                #if defined(D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC)
                    g_renderTarget->SetInterpolationMode(
                        D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
                #else
                    g_renderTarget->DrawBitmap(
                        g_d2dBitmap,
                        destRect,
                        1.0f,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
                    );
                #endif
                // Reset transform
                g_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
            }

            g_renderTarget->EndDraw();

            EndPaint(hWnd, &ps);
        }
        break;
        case WM_SIZE:
            if (g_renderTarget)
            {
                UINT width = LOWORD(lParam);
                UINT height = HIWORD(lParam);
                g_renderTarget->Resize(D2D1::SizeU(width, height));
                FitToWindow(hWnd);
            }
        break;

        break;
        case WM_DESTROY:

            if (g_d2dBitmap) g_d2dBitmap->Release();
            if (g_renderTarget) g_renderTarget->Release();
            if (g_d2dFactory) g_d2dFactory->Release();
            if (g_wicFactory) g_wicFactory->Release();

            CoUninitialize();
            PostQuitMessage(0);
            break;
       
        case WM_MOUSEWHEEL:
        {
            if (!g_d2dBitmap) break;

            short delta = GET_WHEEL_DELTA_WPARAM(wParam);

            float zoomStep = delta / 120.0f;
            float zoomAmount = powf(1.1f, zoomStep);

            float newTargetZoom = g_targetZoom * zoomAmount;

            if (newTargetZoom < 0.05f) newTargetZoom = 0.05f;
            if (newTargetZoom > 50.0f) newTargetZoom = 50.0f;

            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hWnd, &pt);

            // Convert cursor to image space using CURRENT zoom
            float imageX = (pt.x - g_offsetX) / g_zoom;
            float imageY = (pt.y - g_offsetY) / g_zoom;

            // Compute target offsets using target zoom
            float newTargetOffsetX = pt.x - imageX * newTargetZoom;
            float newTargetOffsetY = pt.y - imageY * newTargetZoom;

            g_targetZoom = newTargetZoom;
            g_targetOffsetX = newTargetOffsetX;
            g_targetOffsetY = newTargetOffsetY;

            if (!g_isAnimating)
            {
                SetTimer(hWnd, 1, 16, nullptr); // ~60 FPS
                g_isAnimating = true;
            }
        }
        break;


        case WM_LBUTTONDOWN:
        {
            g_isDragging = true;
            SetCapture(hWnd);

            g_lastMouse.x = GET_X_LPARAM(lParam);
            g_lastMouse.y = GET_Y_LPARAM(lParam);
        }
        break;

        case WM_MOUSEMOVE:
        {
            if (g_isDragging)
            {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);

                g_offsetX += (pt.x - g_lastMouse.x);
                g_offsetY += (pt.y - g_lastMouse.y);

                g_lastMouse = pt;

                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        break;

        case WM_LBUTTONUP:
        {
            g_isDragging = false;
            ReleaseCapture();
        }
        break;

        case WM_LBUTTONDBLCLK:
        {
            if (!g_d2dBitmap) break;

            RECT rc;
            GetClientRect(hWnd, &rc);

            float windowWidth  = (float)(rc.right - rc.left);
            float windowHeight = (float)(rc.bottom - rc.top);

            D2D1_SIZE_F imgSize = g_d2dBitmap->GetSize();

            g_zoom = 1.0f;

            g_offsetX = (windowWidth  - imgSize.width)  / 2.0f;
            g_offsetY = (windowHeight - imgSize.height) / 2.0f;

            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;

    case WM_TIMER:
    {
        const float smooth = 0.18f;  // smaller = more inertia

        g_zoom += (g_targetZoom - g_zoom) * smooth;
        g_offsetX += (g_targetOffsetX - g_offsetX) * smooth;
        g_offsetY += (g_targetOffsetY - g_offsetY) * smooth;

        // Stop when very close
        if (fabs(g_zoom - g_targetZoom) < 0.001f &&
            fabs(g_offsetX - g_targetOffsetX) < 0.5f &&
            fabs(g_offsetY - g_targetOffsetY) < 0.5f)
        {
            g_zoom = g_targetZoom;
            g_offsetX = g_targetOffsetX;
            g_offsetY = g_targetOffsetY;

            KillTimer(hWnd, 1);
            g_isAnimating = false;
        }

        InvalidateRect(hWnd, nullptr, FALSE);
    }
    break;


    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

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
