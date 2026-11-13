#include "Platform/Window/ProjectWindow.h"
#include <iostream>
#include <stdexcept>

using namespace NexusEngine;
namespace {
    void Require(bool condition, const char* message) {
        if(!condition) throw std::runtime_error(message);
    }
    LRESULT Hit(HWND window, int x, int y) {
        POINT point{x, y}; ClientToScreen(window, &point);
        return SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y));
    }
}
int main() {
    try {
        ProjectWindow window;
        WindowDetail detail; detail.visible = false;
        Require(window.Initialize(detail).has_value(), "Window creation failed.");
        const auto handle = window.GetNativeHandle();
        ProjectWindow::ConfigureCustomTitleBar(handle, 38, 138);
        RECT client{}, outer{};
        GetClientRect(handle, &client); GetWindowRect(handle, &outer);
        Require(client.right == outer.right - outer.left && client.bottom == outer.bottom - outer.top,
            "Native frame still occupies client space.");
        Require(Hit(handle, 150, 20) == HTCAPTION, "Title cannot drag/double-click.");
        ProjectWindow::ConfigureTitleBarClientArea(handle,{52,8,400,32});
        Require(Hit(handle,150,20) == HTCLIENT,"Title menu cannot receive mouse input.");
        Require(Hit(handle,450,20) == HTCAPTION,"Caption beside the menu cannot drag.");
        Require(Hit(handle,150,35) == HTCAPTION,"Area outside the menu became client input.");
        Require(Hit(handle,150,1) == HTTOP,"Menu exclusion broke top-border resizing.");
        ProjectWindow::ConfigureTitleBarClientArea(handle,{});
        Require(Hit(handle, client.right - 25, 20) == HTCLIENT, "Window controls are not clickable.");
        Require(Hit(handle, 150, 100) == HTCLIENT, "Editor content became a drag area.");
        Require(Hit(handle, 1, 1) == HTTOPLEFT, "Top-left resize failed.");
        Require(Hit(handle, client.right - 1, client.bottom - 1) == HTBOTTOMRIGHT, "Bottom-right resize failed.");
        Require(Hit(handle, 1, 100) == HTLEFT, "Left resize failed.");
        Require(Hit(handle, 150, 1) == HTTOP, "Top resize failed.");
        ProjectWindow::ConfigureCustomTitleBar(handle, 76, 276);
        Require(Hit(handle, 150, 60) == HTCAPTION, "Scaled title cannot drag.");
        Require(Hit(handle, client.right - 200, 60) == HTCLIENT, "Scaled controls became a drag area.");
        ProjectWindow::ConfigureCustomTitleBar(handle, 38, 138);
        const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
        SetWindowLongPtrW(handle, GWL_STYLE, style | WS_MAXIMIZE);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        Require(GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE,
            "Cannot read monitor work area.");
        NCCALCSIZE_PARAMS layout{}; layout.rgrc[0] = monitor.rcMonitor;
        SendMessageW(handle, WM_NCCALCSIZE, TRUE, reinterpret_cast<LPARAM>(&layout));
        Require(EqualRect(&layout.rgrc[0], &monitor.rcWork) != FALSE, "Maximized window covers the taskbar.");
        Require(Hit(handle, 1, 20) == HTCAPTION, "Maximized title still resizes.");
        SetWindowLongPtrW(handle, GWL_STYLE, style);
        bool closeRequested = false;
        window.SetMessageHandler([](void* data, HWND, UINT message, WPARAM, LPARAM) {
            if(message != WM_CLOSE) return false;
            *static_cast<bool*>(data) = true;
            return true;
        }, &closeRequested);
        SendMessageW(handle, WM_SYSCOMMAND, SC_CLOSE, 0);
        Require(closeRequested && IsWindow(handle), "Custom close bypasses save confirmation.");
        ProjectWindow::ConfigureCustomTitleBar(handle, 0, 0);
        GetClientRect(handle, &client); GetWindowRect(handle, &outer);
        Require(client.bottom < outer.bottom - outer.top, "Native frame was not restored.");
        // External HWNDs may contain unrelated user data; configuring them must be harmless.
        HWND external = CreateWindowExW(0, L"STATIC", L"External", WS_OVERLAPPEDWINDOW,
            0, 0, 200, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Require(external != nullptr, "External window creation failed.");
        SetWindowLongPtrW(external, GWLP_USERDATA, 1);
        ProjectWindow::ConfigureCustomTitleBar(external, 38, 138);
        DestroyWindow(external);
        std::cout << "PASS: custom frame, drag region, controls, resize, close interception and restoration.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
