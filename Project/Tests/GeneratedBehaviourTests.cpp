#include "Runtime/Application/GameRuntime.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Project/ProjectJson.h"
#include "Runtime/Scene/Components/GameComponentData.h"
#include "Runtime/Reflection/NativeComponentMetadata.h"
#include "Foundation/Logging/Logger.h"
#include <cmath>
#include <iostream>
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
/** \brief 生成した実DLLのMetadataと登録経路からBehaviourを実行する。編集した関数本体も検証する */
int wmain(int argc, wchar_t** argv) {
    Logger::Get().InitializeDefaultSinks();
    try {
        Require(argc == 4, "Usage: NexusGeneratedBehaviourTests <Game.dll> <Engine.nxengine> <Project.nxproject>");
        auto engine = ProjectJson::Read(argv[2]);
        auto project = ProjectJson::Read(argv[3]);
        Require(engine && project, "Cannot read test fixture identity.");
        const auto& module = project->at("modules").at(0);
        const auto moduleId = module.at("id").get<std::string>();
        NativeGameModuleSettings settings{std::filesystem::absolute(argv[1]), engine->at("id"), engine->at("version"), project->at("projectId"), moduleId};
        settings.sourceDirectory = std::filesystem::absolute(argv[3]).parent_path() / ProjectJson::Path(module.at("path").get<std::string>());
        auto level = std::make_unique<Level>();
        Check(level->LoadEmpty());
        const auto attach = [&](const char* name) {
            auto entity = level->CreateEntity(name);
            auto* values = entity.AddComponent<GameComponentData>();
            values->propertyState = ProjectJson::Json{{"schemaVersion", 1}, {"types", {
                {moduleId + ".behaviours." + name, {{"version", 1}, {"values", {{"enabled", {{"Bool", true}}}}}}}
            }}}.dump();
            return entity.GetId();
        };
        const auto player = attach("PlayerController"), enemy = attach("EnemyController"), fixed = attach("FixedController");
        GameRuntime runtime;
        Require(runtime.SetNativeGameModule(settings), "Cannot configure generated Module.");
        Check(runtime.Start(std::move(level)));
        const auto metadata = runtime.GetLevel()->GetReflectionRegistration().nativeComponents;
        Require(metadata && metadata->Contains(moduleId + ".behaviours.PlayerController") &&
            metadata->Contains(moduleId + ".behaviours.EnemyController"), "Generated Behaviour metadata was not registered.");
        Check(runtime.Update(.25f));
        const auto* scene = runtime.GetLevel()->GetScene();
        Require(scene->GetComponent<TransformComponent>(player)->translation.x == 0, "Empty generated Behaviour moved its Entity.");
        Require(std::abs(scene->GetComponent<TransformComponent>(enemy)->translation.x - 7.5f) < 1e-5f,
            "Edited OnCreate/OnUpdate bodies did not execute through the generated registration.");
        Require(std::abs(scene->GetComponent<TransformComponent>(fixed)->translation.y - .75f) < 1e-5f,
            "Native OnFixedUpdate was not dispatched at 60Hz.");
        auto entity = runtime.GetLevel()->GetScene()->GetEntity(enemy);
        Require(entity.RemoveComponent<GameComponentData>(), "Cannot detach generated Behaviour.");
        Check(runtime.Update(.25f));
        runtime.Stop();
        Require(!runtime.IsRunning(), "Generated Behaviour did not stop cleanly.");
        std::cout << "PASS: generated DLL metadata/attachment, empty Behaviour, edited lifecycle functions, 60Hz OnFixedUpdate and clean detach/Stop.\n";
        return 0;
    } catch(const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
