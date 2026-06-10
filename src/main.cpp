#include <windows.h>
#include "renderer.h"

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* renderer = reinterpret_cast<Renderer*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_LBUTTONDOWN:
        if (renderer) renderer->OnLButtonDown();
        return 0;
    case WM_KEYDOWN:
        if (renderer) renderer->OnKeyDown(wParam);
        return 0;
    case WM_KEYUP:
        if (renderer) renderer->OnKeyUp(wParam);
        return 0;
    case WM_KILLFOCUS:
        if (renderer) renderer->OnKillFocus();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    const wchar_t className[] = L"DirectX12CubeWindowClass";

    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    if (!RegisterClassEx(&wc)) return -1;

    constexpr UINT width = 800, height = 450;
    RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowEx(0, className, L"DX12 Cube - WASD Camera",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return -1;

    ShowWindow(hwnd, nCmdShow);

    Renderer renderer;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&renderer));

    try { renderer.Initialize(hwnd, width, height); }
    catch (const std::exception& ex)
    {
        MessageBoxA(hwnd, ex.what(), "Initialization Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            try { renderer.Update(); renderer.Render(); }
            catch (const std::exception& ex)
            {
                MessageBoxA(hwnd, ex.what(), "Runtime Error", MB_OK | MB_ICONERROR);
                return -1;
            }
        }
    }
    return static_cast<int>(msg.wParam);
}
