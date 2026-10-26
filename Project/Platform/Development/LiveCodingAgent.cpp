#include "LiveCodingAgent.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Reflection/ReinstancingService.h"
#include "Foundation/Utility/String/ConvertString.h"
#include <utility>
#include <cstdlib>

#ifndef NEXUS_WITH_LIVEPP
#define NEXUS_WITH_LIVEPP 0
#endif

#if NEXUS_WITH_LIVEPP
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "API/x64/LPP_API_x64_CPP.h"
#endif

namespace NexusEngine {
#if NEXUS_WITH_LIVEPP
    // 同期 Agent のフックはフレーム境界の Reload 内で呼び出される。
    void BeforeLiveCodingPatch(lpp::LppHotReloadPrepatchHookId, const wchar_t*, const wchar_t* const*, unsigned int, const wchar_t* const*, unsigned int) {
        NEXUS_LOG_INFO("Live++", "Pre-patch: releasing reflected instances.");
        if(!ReinstancingService::Get().BeforePatch()) {
            // フックの前提が崩れたままパッチを適用すると旧レイアウトへ新コードがアクセスする。
            NEXUS_LOG_CRITICAL("Live++", "Pre-patch hook ran without a prepared snapshot; terminating before unsafe patching.");
            std::_Exit(EXIT_FAILURE);
        }
    }
    void AfterLiveCodingPatch(lpp::LppHotReloadPostpatchHookId, const wchar_t*, const wchar_t* const*, unsigned int, const wchar_t* const*, unsigned int) {
        NEXUS_LOG_INFO("Live++", "Post-patch: restoring reflected instances.");
        ReinstancingService::Get().AfterPatch();
    }
    void LiveCodingCompileError(lpp::LppCompileErrorHookId, const wchar_t*, const wchar_t*, const wchar_t* output) {
        if(output) NEXUS_LOG_ERROR("Live++", ConvertString(std::wstring(output)));
    }
    LPP_HOTRELOAD_PREPATCH_HOOK(BeforeLiveCodingPatch);
    LPP_HOTRELOAD_POSTPATCH_HOOK(AfterLiveCodingPatch);
    LPP_COMPILE_ERROR_HOOK(LiveCodingCompileError);
#endif
    class LiveCodingAgent::Impl final {
    public:
#if NEXUS_WITH_LIVEPP
        lpp::LppSynchronizedAgent agent {};
        std::wstring projectDirectory;
        std::wstring powershell;
        std::string generatorOptions;
        std::string lastError;
        ~Impl() {
            if(lpp::LppIsValidSynchronizedAgent(&agent))
                lpp::LppDestroySynchronizedAgent(&agent);
        }
#endif
    };

    LiveCodingAgent::LiveCodingAgent(bool enabled) {
        if(!enabled) return;
#if NEXUS_WITH_LIVEPP
        auto implementation = std::make_unique<Impl>();
        implementation->projectDirectory = ConvertString(std::string(NEXUS_PROJECT_DIRECTORY));
        wchar_t systemDirectory[MAX_PATH]{};
        if(GetSystemDirectoryW(systemDirectory, MAX_PATH) == 0) throw std::runtime_error("Cannot locate Windows PowerShell");
        implementation->powershell = (std::filesystem::path(systemDirectory) / L"WindowsPowerShell/v1.0/powershell.exe").wstring();
        implementation->generatorOptions = "-NoProfile -ExecutionPolicy Bypass -File \"" + std::string(NEXUS_PROJECT_DIRECTORY) +
            "/Tools/Reflection/Generate-Reflection.ps1\" -ProjectRoot \"" + std::string(NEXUS_PROJECT_DIRECTORY) + "\"";
        auto preferences = lpp::LppCreateDefaultProjectPreferences();
        preferences.hotReload.preBuild.isEnabled = true;
        preferences.hotReload.preBuild.executable = implementation->powershell.c_str();
        preferences.hotReload.preBuild.workingDirectory = implementation->projectDirectory.c_str();
        preferences.hotReload.preBuild.commandLineOptions = implementation->generatorOptions.c_str();
        implementation->agent = lpp::LppCreateSynchronizedAgentWithPreferences(nullptr, NEXUS_LIVEPP_DIRECTORY, &preferences);
        if(!lpp::LppIsValidSynchronizedAgent(&implementation->agent)) {
            NEXUS_LOG_WARNING("Live++", "Agent could not be loaded. Check the SDK path and Broker license; continuing without live coding.");
            return;
        }
        implementation->agent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_NONE, nullptr, nullptr);
        impl_ = std::move(implementation);
        auto& reinstancing = ReinstancingService::Get();
        const auto project = std::filesystem::path(impl_->projectDirectory);
        reinstancing.SetRecoveryPath(project / L"../Generated/LiveCoding" / (L"Recovery_" + std::to_wstring(GetCurrentProcessId()) + L".nxreload"));
        // 再生成しないホストのヘッダ変更は、パッチ適用前に保守的に拒否する。
        for(const auto& directory : {"Foundation", "Core", "Platform", "Graphics", "Runtime", "Editor/ImGui", "Editor/Gui/Core"}) {
            for(const auto& entry : std::filesystem::recursive_directory_iterator(project / directory)) {
                const auto relative = entry.path().lexically_relative(project).generic_string();
                if(relative.starts_with("Runtime/Scene/Components/") || relative.starts_with("Runtime/Reflection/Generated/")) continue;
                if(entry.is_regular_file() && entry.path().extension() == ".h") reinstancing.ProtectFile(entry.path());
            }
        }
        for(const auto& file : {"Editor/Core/EditorApplication.h", "Editor/Core/EditorSession.h", "Runtime/Scene/ComponentStorage.h",
            "Foundation/Reflection/ReflectionValue.h", "Foundation/Reflection/ReinstancingService.h", "Foundation/Math/Vector3.h", "Foundation/Math/Quaternion.h",
            "Graphics/Model/ModelAssetData.h", "Graphics/Model/ModelInstance.h", "Graphics/Renderer/RenderScene.h", "Editor/Services/EditorSettings.h"}) reinstancing.ProtectFile(project / file);
        NEXUS_LOG_INFO("Live++", "Synchronized live coding and reflected reinstancing enabled. Save source changes and press Ctrl+Alt+F11.");
#else
        NEXUS_LOG_INFO("Live++", "Live coding is disabled in this build. See Docs/LiveCoding.md for SDK setup.");
#endif
    }

    LiveCodingAgent::~LiveCodingAgent() = default;

    bool LiveCodingAgent::WantsReload() {
#if NEXUS_WITH_LIVEPP
        // pre-patch フックはパッチ読込より前に走るため、コンパイル開始前から同期する。
        return impl_ && impl_->agent.WantsReload(lpp::LPP_RELOAD_OPTION_SYNCHRONIZE_WITH_COMPILATION_AND_RELOAD);
#else
        return false;
#endif
    }

    void LiveCodingAgent::ScheduleReload() {
#if NEXUS_WITH_LIVEPP
        if(impl_) impl_->agent.ScheduleReload();
#endif
    }

    void LiveCodingAgent::Reload() {
#if NEXUS_WITH_LIVEPP
        if(impl_) {
            auto& reinstancing = ReinstancingService::Get();
            if(!reinstancing.Prepare()) {
                if(impl_->lastError != reinstancing.Error()) {
                    impl_->lastError = reinstancing.Error(); NEXUS_LOG_ERROR("Live++", impl_->lastError);
                }
                return;
            }
            impl_->lastError.clear();
            const auto generation = reinstancing.Generation();
            NEXUS_LOG_INFO("Live++", "Snapshot prepared; applying synchronized reload.");
            impl_->agent.Reload(lpp::LPP_RELOAD_BEHAVIOUR_WAIT_UNTIL_CHANGES_ARE_APPLIED);
            reinstancing.FinishAttempt();
            if(reinstancing.Generation() != generation) NEXUS_LOG_INFO("Live++", "Reload completed; reflected instances are ready.");
            else NEXUS_LOG_INFO("Live++", "No patch was applied; existing reflected instances retained.");
        }
#endif
    }
}
