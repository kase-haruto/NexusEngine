#include "LauncherWindow.h"
#include "LauncherApplication.h"
#include "Runtime/Application/ApplicationPaths.h"
#include "Platform/Window/StartupSplash.h"
#include "Foundation/Utility/String/ConvertString.h"
#include <iostream>
#include <chrono>

int wmain(int argc, wchar_t** argv) {
    using namespace NexusEngine;
    try {
        std::filesystem::path recent, engines, data, smoke;
        std::filesystem::path reviewProject, reviewTarget;
        int page = 0;
        int smokeFrames = 5;
        for(int index = 1; index < argc; ++index) {
            const std::wstring_view argument{argv[index]};
            if(index + 1 >= argc) throw std::runtime_error("Missing Launcher argument value.");
            if(argument == L"--recent-registry") recent = argv[++index];
            else if(argument == L"--engine-registry") engines = argv[++index];
            else if(argument == L"--data") data = argv[++index];
            else if(argument == L"--smoke") smoke = argv[++index];
            else if(argument == L"--smoke-frames") {
                smokeFrames = std::stoi(argv[++index]);
                if(smokeFrames < 5 || smokeFrames > 3000) throw std::runtime_error("Smoke frame count must be between 5 and 3000.");
            }
            else if(argument == L"--review-project") reviewProject = std::filesystem::absolute(argv[++index]);
            else if(argument == L"--review-target") reviewTarget = std::filesystem::absolute(argv[++index]);
            else if(argument == L"--page") {
                const std::wstring_view value = argv[++index];
                if(value == L"recent") page = 0;
                else if(value == L"new") page = 1;
                else if(value == L"engines") page = 2;
                else throw std::runtime_error("Unknown Launcher page.");
            }
            else throw std::runtime_error("Unknown Launcher argument.");
        }
        StartupSplash startup;
        if(smoke.empty()) {
            auto shown = startup.Show({L"NexusEngine",L"Project Browser",L"Launcher " + ConvertString(std::string(NEXUS_HOST_ENGINE_VERSION))});
            if(!shown) std::cerr << shown.error().GetMessageText() << '\n';
        }
        startup.SetStatus(L"Locating engine resources...");
        if(recent.empty()) { auto file = ProjectRegistry::DefaultFile(); if(!file) throw std::runtime_error(file.error().GetMessageText()); recent = *file; }
        if(engines.empty()) { auto file = EngineRegistry::DefaultFile(); if(!file) throw std::runtime_error(file.error().GetMessageText()); engines = *file; }
        if(data.empty()) {
            wchar_t executable[32768]{};
            const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
            if(!length || length == std::size(executable)) throw std::runtime_error("Cannot locate Launcher executable.");
            if(auto root = FindEngineRoot(std::filesystem::path(executable).parent_path())) data = *root;
        }
        if(!smoke.empty()) {
            smoke = std::filesystem::absolute(smoke);
            std::filesystem::create_directories(smoke.parent_path());
            if(std::filesystem::exists(smoke)) throw std::runtime_error("Smoke capture destination already exists.");
        }
        const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if(FAILED(result)) throw std::runtime_error("Cannot initialize Launcher COM apartment.");
        struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
        LauncherWindow window;
        startup.SetStatus(L"Preparing the project browser...");
        auto initialized = window.Initialize(data, false);
        if(!initialized) throw std::runtime_error(initialized.error().GetMessageText());
        LauncherApplication application(window.Handle(), recent, engines, page, !smoke.empty());
        startup.SetStatus(L"Loading projects and engine versions...");
        if(!reviewProject.empty()) application.ReviewEngine(reviewProject,reviewTarget);
        else if(!reviewTarget.empty()) throw std::runtime_error("--review-target requires --review-project.");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        std::optional<bool> busyTitle;
        bool browserReady = false;
        for(int frames = 0;; ++frames) {
            const bool capture = !smoke.empty() && frames >= smokeFrames && !application.Busy();
            auto frame = window.Frame([&] { application.Draw(); }, capture ? smoke : std::filesystem::path{});
            if(!frame) throw std::runtime_error(frame.error().GetMessageText());
            if(!*frame) break;
            if(!browserReady && !application.Busy()) {
                browserReady = true;
                if(smoke.empty()) window.Show();
                startup.Close();
            }
            if(!busyTitle || *busyTitle != application.Busy()) {
                busyTitle = application.Busy();
                SetWindowTextW(window.Handle(),*busyTitle ? L"NexusEngine Launcher - Working" : L"NexusEngine Launcher - Ready");
            }
            if(capture) return application.HasError() ? 1 : 0;
            if(!smoke.empty() && std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Launcher smoke timed out.");
        }
        return 0;
    } catch(const std::exception& exception) { std::cerr << exception.what() << '\n'; return 1; }
}
