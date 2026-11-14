#include "Runtime/Application/GameRuntime.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/ScriptComponent.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Logging/BufferedLogSink.h"
#include <iostream>
#include "Runtime/Reflection/ManagedScriptMetadata.h"
#include <algorithm>
#include <stdexcept>

using namespace NexusEngine;
namespace {
    void Require(bool condition, const char* message) {
        if(!condition) {
            throw std::runtime_error(message);
        }
    }
    void Check(const Result<void>& result) {
        if(!result) {
            throw std::runtime_error(result.error().GetMessageText());
        }
    }
}
/** \brief 実CLRでC# facadeから描画値・Logger・階層行列へ接続し、Reload/Stop時の失効も検証する */
int wmain(int argc, wchar_t** argv) {
    auto logs = std::make_shared<BufferedLogBuffer>();
    auto sink = std::make_unique<BufferedLogSink>(logs);
    const auto* observer = sink.get();
    Require(Logger::Get().AddSink(std::move(sink)), "Cannot capture diagnostics.");
    struct RemoveSink {
        const ILogSink* sink;
        ~RemoveSink() { Logger::Get().RemoveSink(sink); }
    } remove{observer};
    try {
        Require(argc == 4, "Usage: NexusMathDebugTests <nethost.dll> <Bootstrap.dll> <GameScripts.dll>");
        CSharpRuntimeSettings settings;
        settings.nethostFile = std::filesystem::absolute(argv[1]);
        settings.bootstrapAssembly = std::filesystem::absolute(argv[2]);
        settings.runtimeConfig = settings.bootstrapAssembly;
        settings.runtimeConfig.replace_extension(".runtimeconfig.json");
        settings.gameAssembly = std::filesystem::absolute(argv[3]);
        auto metadataFile = settings.gameAssembly;
        metadataFile.replace_extension(".metadata.json");
        const auto exposed = ManagedScriptMetadata::Load(metadataFile);
        Require(static_cast<bool>(exposed), "Cannot load exported reference metadata.");
        TypeRegistry types;
        Require((*exposed)->Register(types), "Cannot register reference metadata.");
        const auto* reflected = types.Find("ManagedScript:fixture.math-debug");
        Require(reflected != nullptr, "Exported script type is missing.");
        const auto entityProperty = std::ranges::find(reflected->properties, "SavedEntity", &PropertyDescriptor::id);
        const auto componentProperty = std::ranges::find(reflected->properties, "SavedTransform", &PropertyDescriptor::id);
        Require(entityProperty != reflected->properties.end() && entityProperty->kind == PropertyKind::EntityReference &&
            componentProperty != reflected->properties.end() && componentProperty->kind == PropertyKind::ComponentReference,
            "Editor reference picker metadata is incorrect.");
        auto level = std::make_unique<Level>();
        Check(level->LoadEmpty());
        auto parent = level->CreateEntity("Parent"), child = level->CreateEntity("CSharp");
        auto* transform = parent.GetComponent<TransformComponent>();
        transform->translation = {10, 0, 0};
        transform->scale = {2, 3, 4};
        transform->rotation = Quaternion::FromEulerRadians({0, 1.57079632679f, 0});
        // Local +Zを4倍して+Xへ回転するので、期待するWorld位置は12になるようLocalを.5にする。
        child.GetComponent<TransformComponent>()->translation = {0, 0, .5f};
        child.AddComponent<HierarchyComponent>()->parent = parent.GetId();
        child.AddComponent<ScriptComponent>()->scriptType = "fixture.math-debug";
        GameRuntime runtime;
        Require(runtime.SetCSharpRuntime(settings), "Cannot configure Managed runtime.");
        Check(runtime.Start(std::move(level)));
        std::vector<DebugDrawLine> lines;
        runtime.GetLevel()->GetScene()->CopyDebugLines(lines);
        Require(lines.size() == 65, "C# shapes did not reach the Scene or OnCreate failed.");
        Check(runtime.ReloadCSharp(settings.gameAssembly));
        runtime.GetLevel()->GetScene()->CopyDebugLines(lines);
        Require(lines.size() == 65, "Reload retained old session lines.");
        Check(runtime.Update(.1f));
        runtime.GetLevel()->GetScene()->CopyDebugLines(lines);
        Require(lines.size() == 1, "Managed Clear/Draw failed.");
        auto* scene = runtime.GetLevel()->GetScene();
        Require(scene->Time().fixedTick == 6 && scene->Time().frame == 1, "Fixed step count is wrong.");
        bool timers = false;
        scene->ForEach<ScriptComponent>([&](Entity, const ScriptComponent& component) {
            timers = component.propertyState.find("\"TimerCalls\":{\"Int\":1}") != std::string::npos &&
                component.propertyState.find("\"UnscaledCalls\":{\"Int\":1}") != std::string::npos;
        });
        Require(timers, "Timers failed or retired Reload callbacks leaked.");
        runtime.TogglePause();
        Check(runtime.Update(1));
        Require(scene->Time().frame == 1, "Pause advanced the clock.");
        runtime.TogglePause();
        Check(runtime.Update(.1f));
        Require(scene->Time().fixedTick == 6 && scene->Time().frame == 2, "TimeScale 0 ran physics.");
        scene->ForEach<ScriptComponent>([&](Entity, const ScriptComponent& component) {
            Require(component.propertyState.find("\"UnscaledCalls\":{\"Int\":2}") != std::string::npos,
                "Unscaled timer did not run with TimeScale 0.");
            Require(component.propertyState.find("\"TimerCalls\":{\"Int\":1}") != std::string::npos,
                "One-shot timer repeated.");
        });
        Check(runtime.Update(.1f));
        Require(scene->Time().fixedTick == 12 && scene->Time().frame == 3, "Restoring TimeScale failed.");
        const auto elapsed = scene->Time().elapsed;
        Check(runtime.ReloadCSharp(settings.gameAssembly));
        scene = runtime.GetLevel()->GetScene();
        Require(scene->Time().frame == 3 && scene->Time().elapsed == elapsed, "Reload reset the clock.");
        Check(runtime.Update(.1f));
        Require(scene->Time().fixedTick == 18, "Reload lost fixed steps.");
        const auto records = logs->Snapshot();
        bool info = false, warning = false, error = false, stack = false, truncated = false;
        for(const auto& record : records) {
            info |= record.level == LogLevel::Info && record.message == "MathDebug.Info";
            warning |= record.level == LogLevel::Warning && record.message == "MathDebug.Warning";
            error |= record.level == LogLevel::Error && record.message == "MathDebug.Error";
            stack |= record.level == LogLevel::Error && record.message.find("MathDebugProbe.OnCreate") != std::string::npos;
            truncated |= record.message.find("[truncated]") != std::string::npos;
        }
        Require(info && warning && error && stack && truncated, "C# log severity/exception stack/Unicode truncation failed.");
        runtime.Stop();
        Require(!runtime.IsRunning(), "Runtime failed to stop.");
        std::cout << "PASS: actual C# references, timers, cancellation, fixed update, time scale, pause, Reload clock/lifetime, math and debug.\n";
        return 0;
    } catch(const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        for(const auto& record : logs->Snapshot()) {
            std::cerr << record.message << '\n';
        }
        return 1;
    }
}
