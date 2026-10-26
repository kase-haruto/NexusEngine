#include "Runtime/Reflection/Generated/GeneratedReflection.h"
#include "Runtime/Reflection/ComponentReflection.h"
#include "Runtime/Reflection/ReflectedInspector.h"
#include "Foundation/Reflection/ReflectionArchive.h"
#include "Foundation/Reflection/ReinstancingService.h"
#include "Editor/Core/EditorWorkspace.h"
#include "ThirdParty/imgui/imgui.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>

namespace NexusEngine {
namespace ReinstancingTest {
struct OldComponent { float amount = 3; std::string removed = "old"; };
struct NewComponent { std::array<uint64_t, 8> padding{}; bool added = true; float amount = 7; };
struct IncompatibleComponent { std::string amount; };
struct UnknownComponent { int value = 1; };
}
template<> struct ReflectionTraits<ReinstancingTest::OldComponent> {
    static constexpr const char* Id = "TestComponent";
    template<class V> static void Visit(V&& visit) { visit("amount", &ReinstancingTest::OldComponent::amount); visit("removed", &ReinstancingTest::OldComponent::removed); }
};
template<> struct ReflectionTraits<ReinstancingTest::NewComponent> {
    static constexpr const char* Id = "TestComponent";
    static constexpr const char* Label = "test.component";
    template<class V> static void Visit(V&& visit) { visit("added", &ReinstancingTest::NewComponent::added); visit("amount", &ReinstancingTest::NewComponent::amount); }
};
template<> struct ReflectionTraits<ReinstancingTest::IncompatibleComponent> {
    static constexpr const char* Id = "TestComponent";
    template<class V> static void Visit(V&& visit) { visit("amount", &ReinstancingTest::IncompatibleComponent::amount); }
};
}

using namespace NexusEngine;
namespace {
template<class F> bool Throws(F&& action) {
    try { action(); return false; } catch(const std::exception&) { return true; }
}
void ComponentLayoutMigration() {
    using namespace ReinstancingTest;
    static_assert(sizeof(OldComponent) != sizeof(NewComponent));
    Scene scene;
    auto parent = scene.CreateEntity("parent");
    auto retired = scene.CreateEntity("retired"); const auto oldId = retired.GetId();
    assert(scene.DestroyEntity(retired));
    auto child = scene.CreateEntity("child"); assert(child.GetId().index == oldId.index && child.GetId().generation != oldId.generation);
    child.AddComponent<HierarchyComponent>()->parent = parent.GetId();
    child.AddComponent<PersistentIdComponent>()->value = {UINT64_MAX, UINT64_MAX - 1};
    child.AddComponent<OldComponent>()->amount = 123.5f;
    ComponentReflection oldRegistry; RegisterGeneratedComponents(oldRegistry); oldRegistry.Register<OldComponent>();
    auto snapshot = oldRegistry.Capture(scene);
    assert(Throws([&] { scene.ForEach<OldComponent>([&](Entity, OldComponent&) { scene.ReleaseComponentStorages(); }); }));
    assert(child.GetComponent<OldComponent>()->amount == 123.5f);
    scene.ReleaseComponentStorages();
    assert(child.IsAlive() && !scene.GetEntity(oldId).IsAlive());
    ComponentReflection nextRegistry; RegisterGeneratedComponents(nextRegistry); nextRegistry.Register<NewComponent>();
    nextRegistry.Restore(scene, snapshot);
    assert(child.GetComponent<NewComponent>()->amount == 123.5f && child.GetComponent<NewComponent>()->added);
    assert(child.GetComponent<HierarchyComponent>()->parent == parent.GetId());
    assert(child.GetComponent<PersistentIdComponent>()->value.high == UINT64_MAX);
    assert(child.GetComponent<NameComponent>()->name == "child");
    Scene recovered;
    nextRegistry.Restore(recovered, snapshot);
    assert(recovered.GetEntity(child.GetId()).GetComponent<NewComponent>()->amount == 123.5f);
    assert(!recovered.GetEntity(oldId).IsAlive());
    Scene retiredSlots;
    Scene::EntityLayout finalGeneration{{UINT32_MAX}, {0}, {0}};
    retiredSlots.RestoreEntityLayout(finalGeneration);
    const auto lastUse = retiredSlots.CreateEntity();
    assert(lastUse.GetId().generation == UINT32_MAX && retiredSlots.DestroyEntity(lastUse));
    assert(retiredSlots.CaptureEntityLayout().freeIndices.empty());
    for(int patch = 0; patch < 3; ++patch) {
        snapshot = nextRegistry.Capture(scene); scene.ReleaseComponentStorages(); nextRegistry.Restore(scene, snapshot);
        assert(child.GetComponent<NewComponent>()->amount == 123.5f);
    }
    assert(child.AddComponent<UnknownComponent>());
    assert(Throws([&] { static_cast<void>(nextRegistry.Capture(scene)); }));
    assert(child.GetComponent<NewComponent>() && child.GetComponent<UnknownComponent>());
    const auto oldValue = CaptureReflection(OldComponent{});
    IncompatibleComponent incompatible;
    assert(Throws([&] { RestoreReflection(oldValue, incompatible); }));

    TypeRegistry inspector; RegisterReflectedInspector<NewComponent>(inspector);
    const auto* type = inspector.Find("TestComponent"); assert(type && type->properties.size() == 2);
    assert(type->properties[1].set(child, PropertyValue(55.0f)));
    assert(child.GetComponent<NewComponent>()->amount == 55);
}
void ArchiveRoundTrip() {
    const auto original = CaptureReflection(PersistentId{UINT64_MAX, 42});
    std::stringstream file(std::ios::in | std::ios::out | std::ios::binary);
    WriteReflectionArchive(file, original); PersistentId restored{};
    RestoreReflection(ReadReflectionArchive(file), restored);
    assert(restored.high == UINT64_MAX && restored.low == 42);
    std::stringstream truncated(file.str().substr(0, 9));
    assert(Throws([&] { ReadReflectionArchive(truncated); }));
    ReflectionValue::Array array{CaptureReflection(1)}; std::array<int, 2> target{};
    assert(Throws([&] { RestoreReflection(ReflectionValue(array), target); }));
    uint8_t narrow = 0;
    assert(Throws([&] { RestoreReflection(CaptureReflection(uint64_t{256}), narrow); }));
    const std::vector<bool> flags{true, false, true}; std::vector<bool> restoredFlags;
    RestoreReflection(CaptureReflection(flags), restoredFlags); assert(restoredFlags == flags);
}
void ServiceLifecycle() {
    using namespace ReinstancingTest;
    struct Context {
        std::unique_ptr<OldComponent> old = std::make_unique<OldComponent>();
        std::unique_ptr<NewComponent> next;
        bool rejectCapture = false, rejectRestore = false;
        int releases = 0;
    } context;
    context.old->amount = 29;
    ReinstancingService service;
    service.SetRecoveryPath(std::filesystem::absolute("Generated/ReinstancingTests/Recovery.nxreload"));
    service.Register({"test", &context,
        [](void* raw) {
            auto& value = *static_cast<Context*>(raw);
            if(value.rejectCapture) throw std::runtime_error("capture rejected");
            return value.old ? CaptureReflection(*value.old) : CaptureReflection(*value.next);
        },
        [](void* raw) noexcept { auto& value = *static_cast<Context*>(raw); value.old.reset(); value.next.reset(); ++value.releases; },
        [](void* raw, const ReflectionValue& snapshot) {
            auto& value = *static_cast<Context*>(raw);
            if(value.rejectRestore) throw std::runtime_error("restore rejected");
            value.next = std::make_unique<NewComponent>(); RestoreReflection(snapshot, *value.next);
        }});
    context.rejectCapture = true;
    assert(!service.Prepare() && context.old && context.releases == 0);
    context.rejectCapture = false;
    // コンパイル失敗相当ではフックが走らず、確保済みオブジェクトを維持する。
    assert(service.Prepare()); service.FinishAttempt(); assert(context.old && context.releases == 0);
    for(int patch = 0; patch < 3; ++patch) {
        assert(service.Prepare()); service.BeforePatch(); assert(!context.old && !context.next);
        service.AfterPatch(); service.FinishAttempt();
        assert(context.next->amount == 29 && context.next->added);
    }
    assert(service.Generation() == 3);
    context.rejectRestore = true;
    assert(service.Prepare()); service.BeforePatch(); service.AfterPatch();
    assert(Throws([&] { service.FinishAttempt(); }));
    std::ifstream recovery("Generated/ReinstancingTests/Recovery.nxreload", std::ios::binary);
    auto persisted = ReadReflectionArchive(recovery); assert(persisted.At("test").At("type").data.index() == 5);
    service.Unregister(&context);

    const auto protectedPath = std::filesystem::absolute("Generated/ReinstancingTests/Host.h");
    std::ofstream(protectedPath) << "original";
    ReinstancingService protectedService; protectedService.ProtectFile(protectedPath);
    std::ofstream(protectedPath) << "changed";
    assert(!protectedService.Prepare());
}
void WorkspaceLifecycle() {
    // 個人設定と ImGui のファイル出力はテスト専用領域へ隔離する。
    wchar_t* previous = nullptr; size_t length = 0; _wdupenv_s(&previous, &length, L"LOCALAPPDATA");
    const auto local = std::filesystem::absolute("Generated/ReinstancingTests/User").wstring(); _wputenv_s(L"LOCALAPPDATA", local.c_str());
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    EditorSession session; assert(session.Create());
    auto entity = session.GetLevel()->CreateEntity("unsaved"); const auto id = entity.GetComponent<PersistentIdComponent>()->value;
    session.Select(id); session.MarkDirty(); assert(session.StartPlay()); session.TogglePause();
    auto* editScene = session.GetLevel()->GetScene(); auto* playScene = session.GetPlayLevel()->GetScene();
    auto workspace = std::make_unique<EditorWorkspace>(session); workspace->Initialize(std::filesystem::absolute("Project"));
    for(auto& panel : workspace->Panels().Entries()) if(panel.id == "Details") panel.isOpen = false;
    auto viewportState = workspace->ViewportEditor().CaptureForReinstancing();
    auto& camera = std::get<ReflectionValue::Object>(viewportState.data).at("camera");
    camera = CaptureReflection(std::array<float, 5>{3, 4, 5, .6f, .7f});
    workspace->ViewportEditor().RestoreAfterReinstancing(viewportState);
    auto workspaceState = workspace->CaptureForReinstancing();
    ComponentReflection registry; RegisterGeneratedComponents(registry);
    const auto edit = registry.Capture(*editScene), play = registry.Capture(*playScene);
    workspace->ReleaseForReinstancing(); workspace.reset();
    editScene->ReleaseComponentStorages(); playScene->ReleaseComponentStorages();
    registry.Restore(*editScene, edit); registry.Restore(*playScene, play); session.NotifyReinstanced();
    workspace = std::make_unique<EditorWorkspace>(session); workspace->Initialize(std::filesystem::absolute("Project"));
    workspace->RestoreAfterReinstancing(workspaceState);
    assert(session.IsDirty() && session.GetSelection() == id && session.IsPlaying() && session.IsPaused());
    assert(session.GetLevel()->GetScene() == editScene && session.GetPlayLevel()->GetScene() == playScene);
    assert(session.GetSelectedEntity().GetId() == entity.GetId());
    for(const auto& panel : workspace->Panels().Entries()) if(panel.id == "Details") assert(!panel.isOpen);
    auto after = workspace->ViewportEditor().CaptureForReinstancing(); std::array<float, 5> restoredCamera{};
    RestoreReflection(after.At("camera"), restoredCamera); assert(restoredCamera[0] == 3 && restoredCamera[4] == .7f);
    workspace->ReleaseForReinstancing(); workspace.reset();
    ImGui::DestroyContext(); _wputenv_s(L"LOCALAPPDATA", previous ? previous : L""); free(previous);
}
}
void RunReinstancingTests() {
    ComponentLayoutMigration(); ArchiveRoundTrip(); ServiceLifecycle(); WorkspaceLifecycle();
    std::cout << "PASS: reflection layout migration, archives, reinstancing failure paths and workspace state\n";
}
