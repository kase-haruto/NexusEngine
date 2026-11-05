#include "Platform/Window/StartupSplash.h"
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace NexusEngine;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
HWND FindSplash() {
    HWND result = nullptr;
    EnumWindows([](HWND handle,LPARAM value) -> BOOL {
        DWORD process = 0; GetWindowThreadProcessId(handle,&process);
        if(process == GetCurrentProcessId()) {
            wchar_t name[128]{}; GetClassNameW(handle,name,128);
            if(std::wstring_view(name).starts_with(L"NexusEngine.StartupSplash.")) { *reinterpret_cast<HWND*>(value) = handle; return FALSE; }
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
void Capture(HWND window,const std::filesystem::path& file) {
    RECT area{}; Require(GetClientRect(window,&area),"Cannot query splash dimensions.");
    HDC target = GetDC(window), memory = CreateCompatibleDC(target);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = area.right; info.bmiHeader.biHeight = area.bottom;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP image = CreateDIBSection(target,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    Require(image && memory && pixels,"Cannot capture startup splash.");
    const auto previous = SelectObject(memory,image);
    SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);
    const auto size = static_cast<uint32_t>(area.right * area.bottom * 4);
    BITMAPFILEHEADER header{}; header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader); header.bfSize = header.bfOffBits + size;
    std::ofstream output(file,std::ios::binary);
    output.write(reinterpret_cast<const char*>(&header),sizeof(header));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));
    output.write(static_cast<const char*>(pixels),size);
    SelectObject(memory,previous); DeleteObject(image); DeleteDC(memory); ReleaseDC(window,target);
    Require(output.good(),"Cannot write splash preview.");
}
}
int wmain() {
    try {
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768);
        const auto root = std::filesystem::path(executable).parent_path() / "Validation" / std::to_wstring(GetTickCount64());
        std::filesystem::create_directories(root);
        StartupSplash splash;
        auto shown = splash.Show({L"NexusEngine",L"Editor",L"Development",false});
        Require(shown.has_value(),"Cannot create hidden startup splash.");
        Require(!splash.Show({}),"Duplicate splash was accepted.");
        const auto window = FindSplash(); Require(window && !IsWindowVisible(window),"Splash test unexpectedly displayed a window.");
        splash.SetStatus(L"Loading 日本語 Project...");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        for(;;) {
            wchar_t text[1024]{}; GetWindowTextW(window,text,1024);
            if(std::wstring_view(text).find(L"日本語 Project") != std::wstring_view::npos) break;
            if(std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Splash UI did not process progress while its owner waited.");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Capture(window,root / "Splash.bmp");
        splash.Close(); splash.Close(); Require(!FindSplash(),"Startup splash remained after close.");
        MSG message{}; Require(!PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE),"Splash close quit the application's main thread.");
        Require(splash.Show({L"NexusEngine",L"Project Browser",L"Development",false}).has_value(),"Cannot reopen startup splash.");
        splash.Close();
        std::cout << "PASS: GPU-free splash, Unicode progress on independent UI thread, duplicate protection, close/reopen and no main-thread quit. Preview: " << (root / "Splash.bmp").string() << '\n';
        return 0;
    } catch(const std::exception& exception) { std::cerr << exception.what() << '\n'; return 1; }
}
