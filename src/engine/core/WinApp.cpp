#include "WinApp.h"
#include "imgui_impl_win32.h"
#include <stdexcept>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void WinApp::Initialize(HINSTANCE hInstance, int nCmdShow, int width,
                        int height, const std::wstring &title) {
    width_ = width;
    height_ = height;

    WNDCLASS wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    // ウィンドウクラス登録
    ATOM atom = RegisterClass(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("RegisterClass failed");
    }

    // ウィンドウ生成
    DWORD windowStyle = WS_OVERLAPPEDWINDOW;
    DWORD windowExStyle = 0;
    RECT windowRect{0, 0, width, height};
    if (!AdjustWindowRectEx(&windowRect, windowStyle, FALSE, windowExStyle)) {
        throw std::runtime_error("AdjustWindowRectEx failed");
    }
    hwnd_ = CreateWindowEx(
        windowExStyle, kClassName, title.c_str(), windowStyle, 100,
        50, windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top, nullptr, nullptr, hInstance,
        nullptr);

    if (!hwnd_) {
        throw std::runtime_error("CreateWindowEx failed");
    }

    RECT clientRect{};
    if (!GetClientRect(hwnd_, &clientRect)) {
        throw std::runtime_error("GetClientRect failed");
    }
    width_ = clientRect.right - clientRect.left;
    height_ = clientRect.bottom - clientRect.top;

    ShowWindow(hwnd_, nCmdShow);
}

bool WinApp::ProcessMessage() {
    MSG msg{};
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}
