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
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void WinApp::Initialize(HINSTANCE hInstance, int nCmdShow, int width,
                        int height, const std::wstring &title,
                        bool startFullscreen) {
    WNDCLASS wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = nullptr;

    // ウィンドウクラス登録
    ATOM atom = RegisterClass(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("RegisterClass failed");
    }

    // ウィンドウ生成
    RECT windowRect{0, 0, width, height};
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindowEx(0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           windowRect.right - windowRect.left,
                           windowRect.bottom - windowRect.top, nullptr, nullptr,
                           hInstance, nullptr);

    if (!hwnd_) {
        throw std::runtime_error("CreateWindowEx failed");
    }

    windowedStyle_ = static_cast<DWORD>(GetWindowLongPtr(hwnd_, GWL_STYLE));
    GetWindowPlacement(hwnd_, &windowedPlacement_);

    if (startFullscreen) {
        HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{sizeof(MONITORINFO)};
        if (GetMonitorInfoW(monitor, &monitorInfo)) {
            SetWindowLongPtr(hwnd_, GWL_STYLE,
                             static_cast<LONG_PTR>(windowedStyle_ &
                                                   ~WS_OVERLAPPEDWINDOW));
            SetWindowPos(hwnd_, HWND_TOP, monitorInfo.rcMonitor.left,
                         monitorInfo.rcMonitor.top,
                         monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                         monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            fullscreen_ = true;
        }
    }

    ShowWindow(hwnd_, nCmdShow);
    BringToFront();
    while (ShowCursor(FALSE) >= 0) {
    }
    UpdateClientSize();
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
    UpdateClientSize();
    return true;
}

int WinApp::GetWidth() const {
    const_cast<WinApp *>(this)->UpdateClientSize();
    return width_;
}

int WinApp::GetHeight() const {
    const_cast<WinApp *>(this)->UpdateClientSize();
    return height_;
}

void WinApp::UpdateClientSize() {
    if (!hwnd_) {
        return;
    }

    RECT clientRect{};
    if (!GetClientRect(hwnd_, &clientRect)) {
        return;
    }

    width_ = clientRect.right - clientRect.left;
    height_ = clientRect.bottom - clientRect.top;
}

void WinApp::BringToFront() {
    if (!hwnd_) {
        return;
    }

    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(hwnd_);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
}

void WinApp::ToggleFullscreen() {
    if (!hwnd_) {
        return;
    }

    if (!fullscreen_) {
        windowedStyle_ = static_cast<DWORD>(GetWindowLongPtr(hwnd_, GWL_STYLE));
        windowedPlacement_.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd_, &windowedPlacement_);

        HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{sizeof(MONITORINFO)};
        if (!GetMonitorInfoW(monitor, &monitorInfo)) {
            return;
        }

        SetWindowLongPtr(hwnd_, GWL_STYLE,
                         static_cast<LONG_PTR>(windowedStyle_ &
                                               ~WS_OVERLAPPEDWINDOW));
        SetWindowPos(hwnd_, HWND_TOP, monitorInfo.rcMonitor.left,
                     monitorInfo.rcMonitor.top,
                     monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                     monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        fullscreen_ = true;
    } else {
        SetWindowLongPtr(hwnd_, GWL_STYLE, static_cast<LONG_PTR>(windowedStyle_));
        SetWindowPlacement(hwnd_, &windowedPlacement_);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER |
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        fullscreen_ = false;
    }

    BringToFront();
    UpdateClientSize();
}
