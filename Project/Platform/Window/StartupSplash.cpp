#include "StartupSplash.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <future>
#include <mutex>
#include <thread>

namespace NexusEngine {
namespace {
constexpr UINT StatusChanged = WM_APP + 81;
constexpr COLORREF Background = RGB(27,27,27), Accent = RGB(242,125,46);
std::atomic<uint32_t> nextClass{0};
void Fill(HDC dc,RECT rectangle,COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color); if(!brush) return;
    FillRect(dc,&rectangle,brush); DeleteObject(brush);
}
void Text(HDC dc,const std::wstring& text,RECT rectangle,int size,int weight,COLORREF color) {
    HFONT font = CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    const auto old = font ? SelectObject(dc,font) : nullptr;
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
    DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&rectangle,DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if(font) { SelectObject(dc,old); DeleteObject(font); }
}
}
class StartupSplash::Impl {
public:
    StartupSplashOptions options;
    std::wstring status = L"Starting...";
    std::wstring className;
    std::mutex mutex;
    std::atomic<HWND> window{nullptr};
    std::thread thread;

    ~Impl() { Close(); }
    void Close() noexcept {
        if(auto handle = window.load()) PostMessageW(handle,WM_CLOSE,0,0);
        if(thread.joinable()) thread.join();
    }
    ////////////////////////////
    // 外部画像・GPUを使わず、起動画面をダブルバッファで描画する
    ////////////////////////////
    void Draw(HDC dc,RECT client) {
        StartupSplashOptions display; std::wstring current;
        { std::lock_guard lock(mutex); display = options; current = status; }
        const auto handle = window.load();
        const float scale = handle ? static_cast<float>(GetDpiForWindow(handle)) / 96.0f : 1.0f;
        const auto px = [scale](int value) { return static_cast<int>(static_cast<float>(value) * scale); };
        Fill(dc,client,Background);
        Fill(dc,{0,0,client.right,px(3)},Accent);
        Fill(dc,{px(40),px(63),client.right-px(40),px(64)},RGB(47,47,47));
        Text(dc,L"NEXUS ENGINE",{px(40),px(24),client.right-px(40),px(54)},px(13),FW_SEMIBOLD,Accent);
        Fill(dc,{px(40),px(100),px(158),px(188)},RGB(35,35,35));
        HPEN pen = CreatePen(PS_SOLID,std::max(1,px(5)),Accent);
        if(pen) {
            const auto previous = SelectObject(dc,pen);
            const POINT n[]{{px(54),px(170)},{px(54),px(116)},{px(94),px(170)},{px(94),px(116)}};
            const POINT x[]{{px(108),px(116)},{px(144),px(170)}};
            const POINT back[]{{px(144),px(116)},{px(108),px(170)}};
            Polyline(dc,n,4); Polyline(dc,x,2); Polyline(dc,back,2);
            SelectObject(dc,previous); DeleteObject(pen);
        }
        Text(dc,display.title,{px(184),px(101),client.right-px(30),px(150)},px(35),FW_SEMIBOLD,RGB(235,235,235));
        Text(dc,display.subtitle,{px(186),px(151),client.right-px(40),px(182)},px(16),FW_NORMAL,RGB(153,153,153));
        Text(dc,current,{px(40),px(234),client.right-px(40),px(266)},px(15),FW_NORMAL,RGB(204,204,204));
        const int width = client.right - px(80), segment = px(90);
        Fill(dc,{px(40),px(283),client.right-px(40),px(286)},RGB(55,55,55));
        // 割合を捏造せず、実処理を待機している間だけ不定長のインジケーターを動かす。
        const int position = static_cast<int>((GetTickCount64() % 1800) * static_cast<ULONGLONG>(std::max(width-segment,1)) / 1800);
        Fill(dc,{px(40)+position,px(283),px(40)+position+segment,px(286)},Accent);
        Text(dc,display.version,{px(40),px(307),client.right-px(40),px(333)},px(12),FW_NORMAL,RGB(125,125,125));
    }
    void Paint(HWND handle,HDC provided = nullptr) {
        PAINTSTRUCT paint{};
        HDC target = provided ? provided : BeginPaint(handle,&paint);
        if(!target) return;
        RECT client{}; GetClientRect(handle,&client);
        HDC buffer = CreateCompatibleDC(target);
        HBITMAP bitmap = buffer ? CreateCompatibleBitmap(target,std::max(client.right,1L),std::max(client.bottom,1L)) : nullptr;
        if(bitmap) {
            const auto previous = SelectObject(buffer,bitmap); Draw(buffer,client);
            BitBlt(target,0,0,client.right,client.bottom,buffer,0,0,SRCCOPY);
            SelectObject(buffer,previous); DeleteObject(bitmap);
        } else Draw(target,client);
        if(buffer) DeleteDC(buffer);
        if(!provided) EndPaint(handle,&paint);
    }
    static LRESULT CALLBACK Procedure(HWND handle,UINT message,WPARAM wParam,LPARAM lParam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(handle,GWLP_USERDATA));
        if(message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(handle,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
            self->window.store(handle);
        }
        if(!self) return DefWindowProcW(handle,message,wParam,lParam);
        switch(message) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: self->Paint(handle); return 0;
        case WM_PRINTCLIENT: self->Paint(handle,reinterpret_cast<HDC>(wParam)); return 0;
        case StatusChanged: {
            std::wstring title;
            { std::lock_guard lock(self->mutex); title = self->options.title + L" - " + self->status; }
            SetWindowTextW(handle,title.c_str()); InvalidateRect(handle,nullptr,FALSE); return 0;
        }
        case WM_TIMER: InvalidateRect(handle,nullptr,FALSE); return 0;
        case WM_DPICHANGED: {
            const auto* rectangle = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(handle,nullptr,rectangle->left,rectangle->top,rectangle->right-rectangle->left,
                rectangle->bottom-rectangle->top,SWP_NOACTIVATE | SWP_NOZORDER); return 0;
        }
        case WM_CLOSE: DestroyWindow(handle); return 0;
        case WM_DESTROY: KillTimer(handle,1); self->window.store(nullptr); PostQuitMessage(0); return 0;
        default: return DefWindowProcW(handle,message,wParam,lParam);
        }
    }
};
StartupSplash::StartupSplash() = default;
StartupSplash::~StartupSplash() = default;
////////////////////////////
// GDIだけで動く専用の起動案内スレッドを開始する
////////////////////////////
Result<void> StartupSplash::Show(StartupSplashOptions options) {
    if(impl_) return std::unexpected(Error(ErrorCategory::Window,207,"Startup splash is already open."));
    // 後で作るEditor Windowと同じDPI基準を、最初のWindow生成前に確立する。
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto state = std::make_unique<Impl>(); state->options = std::move(options);
    state->className = L"NexusEngine.StartupSplash." + std::to_wstring(++nextClass);
    std::promise<Result<void>> ready; auto created = ready.get_future();
    auto* self = state.get();
    try {
        state->thread = std::thread([self,ready = std::move(ready)]() mutable {
            SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            const auto instance = GetModuleHandleW(nullptr);
            WNDCLASSEXW type{}; type.cbSize = sizeof(type); type.hInstance = instance;
            type.lpfnWndProc = Impl::Procedure; type.hCursor = LoadCursorW(nullptr,IDC_ARROW); type.lpszClassName = self->className.c_str();
            if(!RegisterClassExW(&type)) { ready.set_value(std::unexpected(Error(ErrorCategory::Window,207,"Cannot register startup splash.",GetLastError()))); return; }
            POINT cursor{}; GetCursorPos(&cursor); MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTOPRIMARY),&monitor);
            const int width = MulDiv(640,static_cast<int>(GetDpiForSystem()),96), height = MulDiv(360,static_cast<int>(GetDpiForSystem()),96);
            const int x = monitor.rcWork.left + (monitor.rcWork.right-monitor.rcWork.left-width)/2;
            const int y = monitor.rcWork.top + (monitor.rcWork.bottom-monitor.rcWork.top-height)/2;
            HWND handle = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,self->className.c_str(),self->options.title.c_str(),
                WS_POPUP,x,y,width,height,nullptr,nullptr,instance,self);
            if(!handle) {
                const auto error = GetLastError(); UnregisterClassW(self->className.c_str(),instance);
                ready.set_value(std::unexpected(Error(ErrorCategory::Window,207,"Cannot create startup splash.",error))); return;
            }
            const auto dpi = static_cast<int>(GetDpiForWindow(handle));
            const int actualWidth = MulDiv(640,dpi,96), actualHeight = MulDiv(360,dpi,96);
            SetWindowPos(handle,nullptr,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-actualWidth)/2,
                monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-actualHeight)/2,actualWidth,actualHeight,SWP_NOZORDER | SWP_NOACTIVATE);
            SetTimer(handle,1,30,nullptr);
            if(self->options.visible) ShowWindow(handle,SW_SHOWNOACTIVATE);
            UpdateWindow(handle); ready.set_value({});
            MSG message{};
            while(GetMessageW(&message,nullptr,0,0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
            UnregisterClassW(self->className.c_str(),instance);
        });
    } catch(const std::exception& exception) { return std::unexpected(Error(ErrorCategory::Window,207,exception.what())); }
    auto result = created.get(); if(!result) return result;
    impl_ = std::move(state); return {};
}
void StartupSplash::SetStatus(std::wstring_view text) {
    if(!impl_) return;
    { std::lock_guard lock(impl_->mutex); impl_->status.assign(text.substr(0,512)); }
    if(auto handle = impl_->window.load()) PostMessageW(handle,StatusChanged,0,0);
}
void StartupSplash::Close() noexcept { impl_.reset(); }
}
