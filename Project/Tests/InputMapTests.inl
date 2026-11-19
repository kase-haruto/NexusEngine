// GameServicesTests.cppと同じ公開APIを使い、実機に依存せずframe境界を検証する。
void InputMapOperations() {
    const auto root = std::filesystem::absolute("Generated/InputMapTests");
    const auto path = root / "Config/InputMap.settings";
    auto map = InputMap::Defaults();
    assert(map.IsValid());
    assert(map.Save(path));
    const auto loaded = InputMap::Load(path);
    assert(loaded && *loaded == map);
    const auto missing = InputMap::Load(root / "Missing.settings");
    assert(missing && *missing == map);
    auto bad = map;
    bad.actions.push_back(bad.actions.front());
    assert(!bad.IsValid() && !bad.Save(path));
    assert(*InputMap::Load(path) == map);
    bad = map;
    bad.actions[0].bindings[0].slot = 4;
    assert(!bad.IsValid());
    bad = map;
    bad.actions[0].deadZone = std::numeric_limits<float>::quiet_NaN();
    assert(!bad.IsValid());
    bad = map;
    bad.actions[0].bindings[0].scale[0] = std::numeric_limits<float>::infinity();
    assert(!bad.IsValid());
    const auto invalidPath = root / "Invalid.settings";
    {
        std::ofstream file(invalidPath);
        file << "{\"version\":2,\"actions\":[]}";
    }
    assert(!InputMap::Load(invalidPath));
    {
        std::ofstream file(invalidPath);
        file << "{\"version\":1,\"actions\":[],\"unknown\":1}";
    }
    assert(!InputMap::Load(invalidPath));

    Level level, other;
    assert(level.LoadEmpty() && other.LoadEmpty());
    assert(level.SetResourceRoot(root));
    GameApiBridge bridge(level), otherBridge(other);
    const auto api = GameApiBridge::Services();
    uint64_t client = 0, foreign = 0;
    assert(GameApiBridge::OpenNativeServices(bridge.World(), &client) == NEXUS_GAME_OK);
    assert(GameApiBridge::OpenNativeServices(otherBridge.World(), &foreign) == NEXUS_GAME_OK);
    NexusGame::Input input({1, sizeof(NexusGameServicesContextV1), bridge.World(), client, api});
    NexusGame::Input otherInput({1, sizeof(NexusGameServicesContextV1), otherBridge.World(), foreign, api});
    NexusGame::ActionFrame actions, again;
    InputFrame raw;
    raw.focused = true;
    raw.Key(32, true);
    raw.Key('W', true);
    raw.Key('D', true);
    level.SetFrameInput(raw);
    assert(input.ReadActions(actions) == NEXUS_GAME_OK);
    {
        const auto& snapshot = level.InputActions();
        std::ofstream binary(root / "Actions.bin", std::ios::binary);
        binary.write(reinterpret_cast<const char*>(&snapshot), sizeof(snapshot));
        assert(binary.good());
    }
    assert(actions.Down("Jump") && actions.Pressed("Jump") && !actions.Released("Jump"));
    assert(actions.Value("Jump") == 1 && actions.Contains("Jump") && !actions.Contains("Missing"));
    assert(!actions.Down("Missing") && actions.Value("Missing") == 0);
    const auto move = actions.Vector2("Move");
    assert(std::abs(move[0] - .7071067f) < .0001f && std::abs(move[1] - .7071067f) < .0001f);
    assert(input.ReadActions(again) == NEXUS_GAME_OK && again.Pressed("Jump"));
    assert(otherInput.ReadActions(again) == NEXUS_GAME_OK && !again.Down("Jump"));
    raw.ClearTransitions();
    raw.gamepads[0].connected = true;
    raw.gamepads[0].down = NEXUS_PAD_A;
    raw.gamepads[0].pressed = NEXUS_PAD_A;
    level.SetFrameInput(raw);
    assert(input.ReadActions(actions) == NEXUS_GAME_OK && actions.Down("Jump") && !actions.Pressed("Jump"));
    raw.ClearTransitions();
    raw.Key(32, false);
    level.SetFrameInput(raw);
    assert(input.ReadActions(actions) == NEXUS_GAME_OK && actions.Down("Jump") && !actions.Released("Jump"));
    raw.ClearTransitions();
    raw.gamepads[0].connected = false;
    raw.gamepads[0].down = 0;
    level.SetFrameInput(raw);
    assert(input.ReadActions(actions) == NEXUS_GAME_OK && actions.Released("Jump") && !actions.Down("Jump"));
    raw.ClearTransitions();
    raw.Key(32, true);
    raw.Key(32, false);
    level.SetFrameInput(raw);
    assert(input.ReadActions(actions) == NEXUS_GAME_OK && actions.Pressed("Jump") && actions.Released("Jump") && !actions.Down("Jump"));
    raw.ClearTransitions();
    raw.Key(32, true);
    level.SetFrameInput(raw);
    raw.Key(27, true);
    level.SetFrameInput(raw);
    assert(input.SetContext("Menu") == NEXUS_GAME_OK);
    assert(input.ReadActions(actions) == NEXUS_GAME_OK && actions.Context() == "Gameplay" && actions.Down("Jump"));
    raw.ClearTransitions();
    raw.Key(13, true);
    level.SetFrameInput(raw);
    assert(input.ReadActions(actions) == NEXUS_GAME_OK && actions.Context() == "Menu");
    assert(actions.Released("Jump") && !actions.Down("Jump") && actions.Pressed("Confirm"));
    assert(actions.Down("Cancel") && !actions.Pressed("Cancel"));
    assert(input.SetContext("Unknown") == NEXUS_GAME_INVALID_ARGUMENT);
    assert(input.SetContext(std::string(64, 'A')) == NEXUS_GAME_INVALID_ARGUMENT);
    raw.ClearTransitions();
    raw.LoseFocus();
    level.SetFrameInput(raw);
    assert(input.ReadActions(actions) == NEXUS_GAME_OK && actions.Released("Confirm") && !actions.Down("Confirm"));
    raw.ClearTransitions();
    level.SetFrameInput(raw);
    assert(input.ReadActions(actions) == NEXUS_GAME_OK && !actions.Released("Confirm"));
    assert(input.SetContext("Gameplay") == NEXUS_GAME_OK);
    raw.focused = true;
    raw.Key(1, true);
    raw.gamepads[0].connected = true;
    raw.gamepads[0].leftStick = {.1f, 0};
    level.SetFrameInput(raw);
    assert(input.ReadActions(actions) == NEXUS_GAME_OK && actions.Pressed("Attack") && !actions.Down("Move"));

    NexusInputActionRequestV1 query {};
    query.operation = 2;
    uint32_t length = 0;
    assert(api.invoke(client, {NEXUS_INPUT_COMMAND, sizeof(NEXUS_INPUT_COMMAND) - 1}, 1, {},
        {reinterpret_cast<const uint8_t*>(&query), sizeof(query)}, nullptr, 0, &length) == NEXUS_GAME_BUFFER_TOO_SMALL);
    assert(length == sizeof(NexusInputActionsFrameV1));
    query.context[0] = 'X';
    assert(api.invoke(client, {NEXUS_INPUT_COMMAND, sizeof(NEXUS_INPUT_COMMAND) - 1}, 1, {},
        {reinterpret_cast<const uint8_t*>(&query), sizeof(query)}, nullptr, 0, &length) == NEXUS_GAME_INVALID_ARGUMENT);
    NexusGameStatus threaded = NEXUS_GAME_OK;
    std::thread worker([&] {
        NexusGame::ActionFrame frame;
        threaded = input.ReadActions(frame);
    });
    worker.join();
    assert(threaded == NEXUS_GAME_WRONG_THREAD);
    assert(api.close(client) == NEXUS_GAME_OK);
    assert(input.ReadActions(actions) == NEXUS_GAME_INVALID_HANDLE && !actions.Contains("Jump"));
    assert(api.close(foreign) == NEXUS_GAME_OK);

    // 任意slotのtriggerと反対方向キー、マウスdeltaも同じ定義評価へ合流する。
    InputMap custom;
    custom.actions = {
        {"Throttle", "Gameplay", InputActionType::Axis1D, .2f,
            {{InputBindingSource::RightTrigger, 0, 2, {1, 0}}, {InputBindingSource::Key, 'S', 0, {-1, 0}}}},
        {"Aim", "Gameplay", InputActionType::Axis2D, 0,
            {{InputBindingSource::MouseX, 0, 0, {.01f, 0}}, {InputBindingSource::MouseY, 0, 0, {0, -.01f}}}}
    };
    InputMapRuntime runtime;
    runtime.Configure(custom);
    raw = {};
    raw.focused = true;
    raw.gamepads[2].connected = true;
    raw.gamepads[2].rightTrigger = .5f;
    raw.deltaX = 20;
    raw.deltaY = -30;
    runtime.Advance(raw);
    assert(runtime.Frame().actions[0].value[0] == .5f);
    assert(std::abs(runtime.Frame().actions[1].value[0] - .2f) < .0001f);
    assert(std::abs(runtime.Frame().actions[1].value[1] - .3f) < .0001f);
    raw.Key('S', true);
    runtime.Advance(raw);
    assert(runtime.Frame().actions[0].value[0] == -.5f);
    // 独立したPlay sessionの構成では、保持・Contextをリセットする。
    runtime.Configure(custom);
    assert(runtime.Frame().actions[0].down == 0 && std::string_view(runtime.Frame().context) == "Gameplay");
    std::cout << "PASS: Input Map persistence/validation, shared C++ ABI, binding handoff, tap, focus, disconnect, contexts, axes, isolation and client/thread checks\n";
}
