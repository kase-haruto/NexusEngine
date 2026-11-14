#include "Public/Nexus/GameDebug.h"
#include "Runtime/Rendering/RenderSceneExtractor.h"
#include "Foundation/Logging/Logger.h"

namespace {
/** \brief ABIの検証・World寿命・描画抽出を実Bridgeで確認する。CLRやGPUは不要 */
void GameDebugOperations() {
    Level level;
    assert(level.LoadEmpty());
    GameApiBridge bridge(level);
    const auto api = GameApiBridge::Services();
    uint64_t client = 0, other = 0;
    assert(api.open(bridge.World(), &client) == NEXUS_GAME_OK);
    assert(api.open(bridge.World(), &other) == NEXUS_GAME_OK);
    const std::string command = NEXUS_DEBUG_COMMAND;
    const auto invoke = [&](uint64_t owner, NexusDebugRequestV1 header, std::span<const uint8_t> payload = {}, NexusEntityHandle target = {}, uint8_t* response = nullptr, uint32_t capacity = 0) {
        std::vector<uint8_t> request(sizeof(header) + payload.size());
        std::memcpy(request.data(), &header, sizeof(header));
        if(!payload.empty()) {
            std::memcpy(request.data() + sizeof(header), payload.data(), payload.size());
        }
        uint32_t length = 0;
        return api.invoke(owner, {command.data(), static_cast<uint32_t>(command.size())}, 1, target,
            {request.data(), static_cast<uint32_t>(request.size())}, response, capacity, &length);
    };
    struct Capture final : ILogSink {
        LogLevel level = LogLevel::Trace;
        std::string message;
        void Write(const LogRecord& record) noexcept override {
            level = record.level;
            message = record.message;
        }
    };
    auto sink = std::make_unique<Capture>();
    auto* capture = sink.get();
    assert(Logger::Get().AddSink(std::move(sink)));
    const uint8_t message[]{'t', 'e', 's', 't'};
    for(uint32_t severity = 0; severity < 3; ++severity) {
        assert(invoke(client, {1, severity, 0, 0}, message) == NEXUS_GAME_OK);
        assert(capture->message == "test");
        assert(capture->level == (severity == 0 ? LogLevel::Info : severity == 1 ? LogLevel::Warning : LogLevel::Error));
    }
    assert(Logger::Get().RemoveSink(capture));
    NexusDebugLineV1 line{{-1, 0, .5f}, {1, 0, .5f}, {1, 0, 0, 1}, 1};
    const auto payload = std::span{reinterpret_cast<const uint8_t*>(&line), sizeof(line)};
    assert(invoke(client, {2, 1, 0, 0}, payload) == NEXUS_GAME_OK);
    assert(invoke(other, {2, 1, 1, 0}, payload) == NEXUS_GAME_OK);
    RenderScene first, preview;
    RenderSceneExtractor extractor;
    assert(extractor.ExtractInto(*level.GetScene(), 1, first, false));
    assert(extractor.ExtractInto(*level.GetScene(), 1, preview, false));
    assert(first.debugLines.size() == 2 && preview.debugLines.size() == 2);
    level.GetScene()->AdvanceDebugDraw(.25f);
    assert(extractor.ExtractInto(*level.GetScene(), 1, first, false));
    assert(first.debugLines.size() == 1);
    assert(api.close(other) == NEXUS_GAME_OK);
    assert(extractor.ExtractInto(*level.GetScene(), 1, first, false));
    assert(first.debugLines.empty());
    auto invalid = line;
    invalid.color[0] = std::numeric_limits<float>::quiet_NaN();
    std::array<NexusDebugLineV1, 2> mixed{line, invalid};
    assert(invoke(client, {2, 2, 1, 0}, {reinterpret_cast<const uint8_t*>(mixed.data()), sizeof(mixed)}) == NEXUS_GAME_INVALID_ARGUMENT);
    assert(invoke(client, {2, 1, -1, 0}, payload) == NEXUS_GAME_INVALID_ARGUMENT);
    assert(invoke(client, {2, 2, 0, 0}, payload) == NEXUS_GAME_INVALID_ARGUMENT);
    assert(extractor.ExtractInto(*level.GetScene(), 1, first, false) && first.debugLines.empty());
    for(int index = 0; index < 1024; ++index) {
        assert(invoke(client, {2, 1, 1, 0}, payload) == NEXUS_GAME_OK);
    }
    assert(invoke(client, {2, 1, 1, 0}, payload) == NEXUS_GAME_FAILED);
    level.GetScene()->AdvanceDebugDraw(1);
    assert(extractor.ExtractInto(*level.GetScene(), 1, first, false) && first.debugLines.empty());
    auto parent = level.CreateEntity("Parent"), child = level.CreateEntity("Child");
    parent.GetComponent<TransformComponent>()->translation = {10, 0, 0};
    child.GetComponent<TransformComponent>()->translation = {1, 2, 3};
    child.AddComponent<HierarchyComponent>()->parent = parent.GetId();
    const NexusEntityHandle handle{bridge.World(), child.GetId().index, child.GetId().generation};
    Matrix4x4 matrix;
    assert(invoke(client, {4, 0, 0, 0}, {}, handle, reinterpret_cast<uint8_t*>(&matrix), sizeof(matrix)) == NEXUS_GAME_OK);
    assert(matrix.At(3, 0) == 11 && matrix.At(3, 1) == 2 && matrix.At(3, 2) == 3);
    parent.AddComponent<HierarchyComponent>()->parent = child.GetId();
    assert(invoke(client, {4, 0, 0, 0}, {}, handle, reinterpret_cast<uint8_t*>(&matrix), sizeof(matrix)) == NEXUS_GAME_FAILED);
    NexusGameStatus wrongThread = NEXUS_GAME_OK;
    std::thread worker([&] { wrongThread = invoke(client, {3, 0, 0, 0}); });
    worker.join();
    assert(wrongThread == NEXUS_GAME_WRONG_THREAD);
    assert(api.close(client) == NEXUS_GAME_OK);
    assert(invoke(client, {3, 0, 0, 0}) == NEXUS_GAME_INVALID_HANDLE);
    std::cout << "PASS: Debug severity, line validation/atomicity/limits, multi-view lifetime, client cleanup and fresh hierarchy matrix\n";
}
}
