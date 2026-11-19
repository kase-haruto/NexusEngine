#include "Runtime/Curves/CurvePreviewBridge.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/CurveBindingComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Platform/FileSystem/AtomicFile.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <fstream>

namespace NexusEngine::Tests {
int RunCurveIpcChild(const std::string& endpoint, const std::filesystem::path& root) {
    Level level;
    if(!level.Load(root / "test.nxlevel")) {
        return 2;
    }
    level.Animations().SetRoot(root);
    CurvePreviewReceiver receiver(endpoint);
    bool draftSeen = false, firstRestored = false, secondDraft = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while(std::chrono::steady_clock::now() < deadline) {
        receiver.Poll(level);
        if(!level.Update(0)) {
            return 3;
        }
        bool restored = false;
        level.GetScene()->ForEach<CameraComponent>([&](Entity, const auto& camera) {
            draftSeen |= std::abs(camera.verticalFieldOfViewRadians - 1.25f) < 1e-5f;
            restored |= draftSeen && std::abs(camera.verticalFieldOfViewRadians - .75f) < 1e-5f;
        });
        if(draftSeen && !firstRestored) {
            std::ofstream(root / "draft.flag") << "ready";
        }
        if(restored && !firstRestored) {
            firstRestored = true;
            std::ofstream(root / "restored.flag") << "ready";
        } else if(firstRestored) {
            level.GetScene()->ForEach<CameraComponent>([&](Entity, const auto& camera) {
                secondDraft |= std::abs(camera.verticalFieldOfViewRadians - 1.25f) < 1e-5f;
            });
            if(secondDraft) {
                std::ofstream(root / "reconnected.flag") << "ready";
            }
            if(secondDraft && restored) {
                return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return draftSeen ? 5 : 4;
}
void RunCurveIpcTests() {
    const auto id = CreateUuid().value();
    const auto root = std::filesystem::absolute("Generated/CurveIpcTests") / id;
    std::filesystem::create_directories(root);
    FloatCurve saved;
    if(!saved.SetKeys({{0, .5}, {1, 1.5}}) || !SaveFloatCurve(root / "value.curve", saved)) {
        throw std::runtime_error("IPC fixture Curve failed");
    }
    Level level;
    if(!level.LoadEmpty()) {
        throw std::runtime_error("IPC fixture Level failed");
    }
    auto entity = level.CreateEntity("IPC Camera");
    if(!entity.AddComponent<CameraComponent>()) {
        throw std::runtime_error("IPC camera creation failed");
    }
    auto* binding = entity.AddComponent<CurveBindingComponent>();
    binding->curveAsset = "value.curve";
    binding->manual = true;
    binding->playing = false;
    const auto persistent = entity.GetComponent<PersistentIdComponent>()->value;
    if(!level.Save(root / "test.nxlevel")) {
        throw std::runtime_error("IPC fixture save failed");
    }
    auto sender = std::make_unique<CurvePreviewSender>(id);
    FloatCurve draft;
    std::vector<CurveKey> transferKeys;
    for(int index = 0; index <= 6000; ++index) {
        const double x = index / 6000.0;
        transferKeys.push_back({x, 1 + x});
    }
    if(!draft.SetKeys(std::move(transferKeys))) {
        throw std::runtime_error("IPC draft failed");
    }
    sender->SetDraft("value.curve", &draft);
    sender->Select(persistent);
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const std::wstring endpoint(id.begin(), id.end());
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --curve-ipc-child " + endpoint + L" \"" + root.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // テスト自身のexeだけを子processとして起動し、異常時もこのhandleだけを終了する。
    if(!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        throw std::runtime_error("IPC child process could not start");
    }
    CloseHandle(process.hThread);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    bool feedback = false;
    while(std::chrono::steady_clock::now() < deadline) {
        sender->Poll();
        if(sender->State().found) {
            sender->Command(CurvePreviewAction::Seek, .25);
            if(std::abs(sender->State().input - .25) < 1e-5 && std::filesystem::exists(root / "draft.flag")) {
                feedback = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // Runtimeは接続喪失を検出して保存値へ復帰する。Editorがdiskを変更していないことも照合する。
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sender.reset();
    const auto restoredDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while(std::chrono::steady_clock::now() < restoredDeadline && !std::filesystem::exists(root / "restored.flag")) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    sender = std::make_unique<CurvePreviewSender>(id);
    sender->SetDraft("value.curve", &draft);
    sender->Select(persistent);
    const auto reconnectedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while(std::chrono::steady_clock::now() < reconnectedDeadline && !std::filesystem::exists(root / "reconnected.flag")) {
        sender->Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    sender.reset();
    const DWORD wait = WaitForSingleObject(process.hProcess, 5000);
    DWORD code = 1;
    if(wait == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &code);
    } else {
        TerminateProcess(process.hProcess, 99);
        WaitForSingleObject(process.hProcess, 1000);
    }
    CloseHandle(process.hProcess);
    const auto disk = LoadFloatCurve(root / "value.curve");
    if(!feedback || code != 0 || !disk || *disk != saved) {
        throw std::runtime_error("Separate-process Curve preview/seek/disconnect test failed: child=" + std::to_string(code));
    }
}
}
