#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include "Editor/ImGui/ImGuiRenderer.h"
#include "Editor/Gui/Widgets/EditorIconButton.h"
#include "Graphics/Renderer/GraphicsSystem.h"
#include "Graphics/Renderer/GraphicsContext.h"
#include "Foundation/Logging/Logger.h"
#include "ThirdParty/imgui/imgui_internal.h"

using namespace NexusEngine;
using Microsoft::WRL::ComPtr;
namespace {
    void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
    void Check(Result<void> result) { if(!result) throw std::runtime_error(result.error().GetMessageText()); }
    struct Window {
        HWND handle=CreateWindowExW(0,L"STATIC",L"ImGui viewport test",WS_OVERLAPPEDWINDOW,20,20,360,280,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        ~Window() { if(handle) DestroyWindow(handle); }
    };
    struct Extension final : IGraphicsRenderExtension {
        ImGuiRenderer gui;
        GraphicsSystem* graphics=nullptr;
        ComPtr<ID3D12InfoQueue> info;
        ImGuiID detached=0;
        bool open=true, merge=false, focusInput=false;
        float canvasFontScale=1;
        int records=0, submissions=0;
        std::array<char,64> input{};
        Result<void> Initialize(const GraphicsRenderExtensionContext& context) override {
            graphics=context.graphicsSystem;
            auto* device=static_cast<ID3D12Device*>(context.nativeDevice);
            if(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&info)))) {
                info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,FALSE);
                info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION,FALSE);
                D3D12_MESSAGE_ID advisory=D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE;
                D3D12_INFO_QUEUE_FILTER filter{}; filter.DenyList.NumIDs=1; filter.DenyList.pIDList=&advisory;
                info->AddStorageFilterEntries(&filter);
            }
            gui.SetFontDirectory(std::filesystem::absolute(L"Project/Resources/Assets/Fonts"));
            const auto icons = std::filesystem::absolute("Generated/ImGuiIcons");
            std::filesystem::create_directories(icons);
            // 実際に配置されたPNGもWICで読み込み、GPU登録と終了時の解放まで検証する。
            for(const auto& entry : std::filesystem::directory_iterator("Project/Resources/Editor/Icons"))
                if(entry.path().extension() == ".png") std::filesystem::copy_file(entry.path(),icons / entry.path().filename(),std::filesystem::copy_options::overwrite_existing);
            std::filesystem::copy_file("Project/Resources/Assets/Models/Sponza/white.png", icons / "test.action.png", std::filesystem::copy_options::overwrite_existing);
            std::ofstream(icons / "invalid.png", std::ios::trunc) << "invalid PNG";
            gui.SetIconDirectory(icons);
            auto initialized=gui.Initialize(context); if(!initialized) return initialized;
            Require(UI::FindEditorIcon("test.action") != nullptr, "Editor PNG icon was not loaded.");
            Require(UI::FindEditorIcon("file.save") && UI::FindEditorIcon("viewport.translate") && UI::FindEditorIcon("details.Material"), "Configured editor PNGs were not loaded.");
            Require(UI::FindEditorIcon("invalid") == nullptr, "Invalid PNG should use a text fallback.");
            Require((ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_ViewportsEnable)!=0,"Viewports are disabled.");
            Require(ImGui::GetPlatformIO().Renderer_RenderWindow && ImGui::GetPlatformIO().Platform_CreateWindow,"Platform backends are missing.");
            ImGui::GetIO().IniFilename=nullptr;
            // Exercise real HWND/swap chains without bringing test windows onto the user's desktop.
            ImGui::GetPlatformIO().Platform_ShowWindow=[](ImGuiViewport*){};
            return {};
        }
        void BeginFrame() override {
            gui.BeginFrame();
            auto* title = ImGui::FindWindowByName("###EditorTitleBar");
            Require(title && title->Active,"Title bar was not drawn.");
            bool fullTitleClip = false;
            for(const auto& command : title->DrawList->CmdBuffer)
                if(command.ElemCount && command.ClipRect.y <= title->Pos.y && command.ClipRect.w >= title->Pos.y+title->Size.y) fullTitleClip = true;
            Require(fullTitleClip,"Menu-bar clipping hides the logo or native window controls.");
            if(!open) return;
            const auto* main=ImGui::GetMainViewport();
            if(merge) {
                ImGui::SetNextWindowViewport(main->ID);
                ImGui::SetNextWindowPos({main->Pos.x+10,main->Pos.y+10});
            } else ImGui::SetNextWindowPos({main->Pos.x+500,main->Pos.y+60});
            ImGui::SetNextWindowSize({320,280},ImGuiCond_Once);
            if(ImGui::Begin("Detached panel",&open,ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::PushFont(nullptr,ImGui::GetStyle().FontSizeBase*canvasFontScale);
                if(focusInput) { ImGui::SetKeyboardFocusHere(); focusInput=false; }
                ImGui::InputText("Keyboard",input.data(),input.size());
                UI::EditorIconButton("test.action", "Icon###test");
                UI::EditorIconButton("invalid", "Fallback###fallback");
                const auto image=graphics->GetSceneViewportImage();
                ImGui::Image(ImTextureRef(image.backendReference),{190,120});
                detached=ImGui::GetWindowViewport()->ID;
                ImGui::PopFont();
            }
            ImGui::End();
        }
        void Record(GraphicsContext& context) override { gui.Record(context); ++records; }
        void AfterSubmit() override {
            Require(records==submissions+1,"Platform windows were rendered before main recording.");
            gui.AfterSubmit(); ++submissions;
        }
        void Shutdown() noexcept override { gui.Shutdown(); }
    };
    void MultiViewport(bool warp) {
        Window window; Require(window.handle!=nullptr,"Cannot create test window.");
        Extension extension;
        GraphicsSystem graphics;
        GraphicsSystemDesc desc; desc.shaderDirectory=L"Project/Shaders"; desc.enableVSync=false;
        desc.enableDebugLayer=true; desc.enableGpuBasedValidation=true; desc.useWarpAdapter=warp;
        Check(graphics.Initialize({window.handle,360,280},desc));
        Check(graphics.EnableSceneViewport()); Check(graphics.AttachRenderExtension(&extension));
        auto frame=[&] {
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            Check(graphics.RenderFrame());
        };
        for(int i=0;i<4;++i) frame();
        auto* viewport=ImGui::FindViewportByID(extension.detached);
        Require(viewport && viewport!=ImGui::GetMainViewport() && viewport->PlatformHandle && viewport->RendererUserData,"Detached HWND/render target were not created.");
        auto external=static_cast<HWND>(viewport->PlatformHandle);
        Require(IsWindow(external) && external!=window.handle,"Panel did not leave the main HWND.");
        // Canvas zoom uses sized fonts; exercise atlas updates in a detached viewport.
        for(float scale:{0.5f,2.0f,1.0f}) { extension.canvasFontScale=scale; frame(); frame(); }
        // Deliver a native character message to the detached platform WndProc.
        extension.focusInput=true; frame(); frame();
        SendMessageW(external,WM_CHAR,'X',0); frame();
        Require(std::strchr(extension.input.data(),'X')!=nullptr,"Detached window keyboard input failed.");
        SetWindowPos(external,nullptr,0,0,480,360,SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
        frame(); frame();
        viewport=ImGui::FindViewportByID(extension.detached);
        Require(viewport && viewport->Size.x>350,"Detached resize failed.");
        extension.merge=true;
        for(int i=0;i<4;++i) frame();
        Require(extension.detached==ImGui::GetMainViewport()->ID,"Panel did not return to the main viewport.");
        Require(!IsWindow(external),"Merged platform HWND leaked.");
        extension.merge=false;
        for(int i=0;i<4;++i) frame();
        viewport=ImGui::FindViewportByID(extension.detached);
        Require(viewport && viewport->PlatformHandle && viewport!=ImGui::GetMainViewport(),"Panel could not detach again.");
        external=static_cast<HWND>(viewport->PlatformHandle);
        SendMessageW(external,WM_CLOSE,0,0);
        for(int i=0;i<4;++i) frame();
        Require(!extension.open && !IsWindow(external) && IsWindow(window.handle),"Closing a detached panel closed the editor or leaked its HWND.");
        Require(extension.records==extension.submissions,"Platform frame lifecycle is incomplete.");
        extension.open=true;
        for(int i=0;i<4;++i) frame();
        viewport=ImGui::FindViewportByID(extension.detached);
        Require(viewport && viewport->PlatformHandle && viewport!=ImGui::GetMainViewport(),"Cannot reopen the detached panel.");
        auto shutdownWindow=static_cast<HWND>(viewport->PlatformHandle);
        Check(graphics.WaitForIdle());
        auto info=extension.info;
        graphics.Shutdown();
        Require(!IsWindow(shutdownWindow),"Application shutdown leaked a detached platform HWND.");
        Require(ImGui::GetCurrentContext()==nullptr,"ImGui context survived shutdown.");
        Require(UI::editorIcons.empty(), "Editor icon references survived shutdown.");
        if(info) for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
            SIZE_T bytes=0; info->GetMessage(i,nullptr,&bytes); std::vector<uint8_t> storage(bytes);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data()); info->GetMessage(i,message,&bytes);
            if(message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING) throw std::runtime_error(message->pDescription);
        }
        std::cout<<"PASS: native detached ImGui windows, scene texture, keyboard, resize, merge, close and GPU lifetime ("<<(warp?"WARP":"hardware")<<").\n";
    }
}
int main() {
    Logger::Get().InitializeDefaultSinks();
    try { MultiViewport(false); MultiViewport(true); return 0; }
    catch(const std::exception& error) { std::cerr<<"FAILED: "<<error.what()<<'\n'; return 1; }
}
