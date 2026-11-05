#include "EnginePublisher.h"
#include "Runtime/Project/ProjectSerializer.h"
#include "Platform/Process/Process.h"
#include <Windows.h>
#include <chrono>
#include <thread>

namespace NexusEngine {
Result<EngineInstallation> EnginePublisher::Publish(const EngineInstallation& source, const std::filesystem::path& destination,
    const ProjectEngineRequirement& installed, const std::filesystem::path& log, std::stop_token stop) {
    const auto invalid = [](const std::string& message) { return std::unexpected(Error(ErrorCategory::FileSystem, 203, message)); };
    if(source.identity.association != "development" || installed.association != "installed") return invalid("Publishing requires Development input and an explicit Installed identity.");
    ProjectDescriptor descriptor; descriptor.name = "SDK"; descriptor.projectId = "11111111-1111-4111-8111-111111111111"; descriptor.engine = installed;
    auto valid = ProjectSerializer::Validate(descriptor); if(!valid) return std::unexpected(valid.error());
    auto current = EngineManifest::Load(source.manifest); if(!current) return std::unexpected(current.error());
    if(current->buildId != source.buildId) return invalid("Development manifest changed. Select it again before publishing.");
    wchar_t system[32768]{}; const auto length = GetSystemDirectoryW(system, static_cast<UINT>(std::size(system)));
    if(!length || length >= std::size(system)) return invalid("Cannot locate Windows PowerShell.");
    const auto output = std::filesystem::weakly_canonical(std::filesystem::absolute(destination));
    auto process = Process::Start(std::filesystem::path(system) / "WindowsPowerShell/v1.0/powershell.exe",
        {L"-NoProfile",L"-ExecutionPolicy",L"Bypass",L"-File",(source.sdk / "Tools/Engine/Publish-Engine.ps1").wstring(),
         L"-SourceManifest",source.manifest.wstring(),L"-Destination",output.wstring(),L"-EngineId",std::wstring(installed.id.begin(),installed.id.end()),
         L"-Version",std::wstring(installed.version.begin(),installed.version.end())}, source.root, true, log, true);
    if(!process) return std::unexpected(process.error());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
    for(;;) {
        auto state = process->Poll(); if(!state) return std::unexpected(state.error());
        if(*state) {
            if(**state != 0) return invalid("Engine publishing failed. Inspect the publishing log.");
            return EngineManifest::Load(output / "NexusEngine.nxengine");
        }
        if(stop.stop_requested() || std::chrono::steady_clock::now() > deadline) {
            auto ended = process->Terminate(); if(!ended) return std::unexpected(ended.error());
            return invalid("Engine publishing cancelled. Only the owned build job was stopped; unfinished staging may need inspection.");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
}
