#include "Editor/Gui/EditorChrome.h"
#include "Editor/Gui/Widgets/EditorIconButton.h"

namespace {
void EditorToolbarTests() {
    EditorToolbarRegistry toolbar;
    EditorCommandRegistry commands;
    LocalizationService text;
    assert(text.Load("Project/Resources/Editor/Localization"));
    bool enabled = true;
    int executions = 0;
    std::vector<int> order;

    // ツールバーが直接機能を呼ばず、Commandの実行処理と実行可否に従うことを検証する。
    EditorCommandRegistry::Command command;
    command.execute = [&] {
        ++executions;
    };
    command.canExecute = [&] {
        return enabled;
    };
    assert(commands.Register("test.action", std::move(command)));
    assert(toolbar.RegisterButton("test.button", "file.save", "test.action", 50));
    assert(!toolbar.RegisterButton("test.button", "file.save", "test.action", 60));
    assert(!toolbar.RegisterButton("", "file.save", "test.action"));
    assert(!toolbar.RegisterSection({"invalid", 0, -1, {}}));

    // 登録順と表示順を変え、orderによる並び替えと安定したID管理を確認する。
    assert(toolbar.RegisterSection({"later", 90, 24, [&](const auto&, const auto&) {
        order.push_back(90);
        ImGui::TextUnformatted("Later section");
    }}));
    assert(toolbar.RegisterSection({"earlier", 10, 24, [&](const auto&, const auto&) {
        order.push_back(10);
        ImGui::TextUnformatted("Earlier section");

        // 描画中の配列変更を拒否し、実行中のコールバックの寿命を維持する。
        assert(!toolbar.Unregister("earlier"));
        assert(!toolbar.RegisterButton("during.draw", "file.save", "test.action"));
    }}));

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = {1200, 800};
    io.DeltaTime = 1.0f / 60;
    io.IniFilename = nullptr;
    unsigned char* pixels;
    int width;
    int height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(1);

    const auto frame = [&](float windowWidth) {
        order.clear();
        io.DisplaySize.x = windowWidth;
        ImGui::NewFrame();
        DrawEditorToolbar(text, commands, "Test level", false, false, false, toolbar);
        ImGui::Render();
        assert((order == std::vector<int>{10, 90}));
    };
    frame(1200);
    frame(1200);
    auto* window = ImGui::FindWindowByName("###EditorToolbar");
    assert(window && window->Active);

    // 翻訳表示名に依存しないIDから、通常のボタンを実際に起動する。
    const auto sectionId = window->GetID("test.button");
    const auto buttonId = ImHashStr("button", 0, sectionId);
    ImGui::ActivateItemByID(buttonId);
    frame(1200);
    assert(executions == 1);
    enabled = false;
    ImGui::ActivateItemByID(buttonId);
    frame(1200);
    assert(executions == 1);

    // 同じ操作IDを保ったまま画像へ切り替え、Commandと無効状態も維持する。
    UI::SetEditorIcon("test.button", ImTextureRef(static_cast<ImTextureID>(7)));
    frame(1200);
    bool imageFound = false;
    for(const auto& draw : window->DrawList->CmdBuffer)
        if(draw.GetTexID() == 7 && draw.ElemCount > 0) imageFound = true;
    assert(imageFound);
    ImGui::ActivateItemByID(buttonId);
    frame(1200);
    assert(executions == 1);
    enabled = true;
    ImGui::ActivateItemByID(buttonId);
    frame(1200);
    assert(executions == 2);
    UI::ClearEditorIcons();
    enabled = false;

    // オフセットは未登録のセクションにも保持でき、不正値と変更のない更新を区別する。
    const auto revision = toolbar.LayoutRevision();
    assert(!toolbar.SetOffset("invalid", {std::numeric_limits<float>::quiet_NaN(), 0}));
    assert(toolbar.SetOffset("not.registered", {100000, -100000}));
    assert((toolbar.GetOffset("not.registered") == ToolbarOffset{kToolbarOffsetLimitX, -kToolbarOffsetLimitY}));
    const auto changedRevision = toolbar.LayoutRevision();
    assert(changedRevision > revision);
    assert(toolbar.SetOffset("not.registered", {100000, -100000}));
    assert(toolbar.LayoutRevision() == changedRevision);
    toolbar.ResetOffsets();

    ImVec2 sectionPosition{};
    ImVec2 sectionSize{};
    float coordinateScale = 1;
    assert(toolbar.RegisterSection({"positioned", 30, 8, [&](const auto&, const auto&) {
        // 操作できないボタンの上でも、配置用の右クリックを受け取れることを確認する。
        ImGui::BeginDisabled();
        ImGui::Button("Position test");
        sectionPosition = ImGui::GetItemRectMin();
        sectionSize = ImGui::GetItemRectSize();
        coordinateScale = ImGui::GetFontSize() / 18.0f;
        ImGui::EndDisabled();
    }}));
    frame(1200);
    frame(1200);
    const auto originalPosition = sectionPosition;
    assert(toolbar.SetOffset("positioned", {48, 24}));
    frame(1200);
    frame(1200);
    assert(std::abs(sectionPosition.x - originalPosition.x - 48 * coordinateScale) < 1);
    assert(std::abs(sectionPosition.y - originalPosition.y - 24 * coordinateScale) < 1);

    // 実際のマウスイベントでメニューを開き、機能の実行が発生しないことを検証する。
    io.AddMousePosEvent(sectionPosition.x + sectionSize.x * .5f, sectionPosition.y + sectionSize.y * .5f);
    frame(1200);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    frame(1200);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    frame(1200);
    auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
    assert(!popups.empty());
    const auto positionSectionId = window->GetID("positioned");
    assert(popups.back().PopupId == ImHashStr("SectionLayout", 0, positionSectionId));
    assert(executions == 2);
    frame(1200);

    // 配置メニューのリセットボタンを起動し、対象セクションだけが初期位置へ戻ることを確認する。
    auto* popup = popups.back().Window;
    assert(popup && popup->Active);
    ImGui::ActivateItemByID(popup->GetID("###ResetSection"));
    frame(1200);
    assert((toolbar.GetOffset("positioned") == ToolbarOffset{}));
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame(1200);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame(1200);
    assert(std::abs(sectionPosition.x - originalPosition.x) < 1);
    assert(std::abs(sectionPosition.y - originalPosition.y) < 1);

    // 前のPopupを閉じ、入力キューを消化してから独立したドラッグ操作を開始する。
    ImGui::ClosePopupToLevel(0, true);
    frame(1200); frame(1200);
    // Altドラッグは無効な操作も移動でき、リリース後もオフセットを保持する。
    io.AddKeyEvent(ImGuiMod_Alt, true);
    io.AddMousePosEvent(sectionPosition.x + sectionSize.x * .5f, sectionPosition.y + sectionSize.y * .5f);
    frame(1200);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame(1200);
    io.AddMousePosEvent(sectionPosition.x + sectionSize.x * .5f + 36, sectionPosition.y + sectionSize.y * .5f + 18);
    frame(1200);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame(1200);
    io.AddKeyEvent(ImGuiMod_Alt, false);
    frame(1200);
    assert(std::abs(toolbar.GetOffset("positioned")[0] - 36 / coordinateScale) < 1);
    assert(std::abs(toolbar.GetOffset("positioned")[1] - 18 / coordinateScale) < 1);
    assert(executions == 2);
    toolbar.ResetOffsets();

    // 大きな移動量を指定しても、画面外の左上へボタンを失わないことを確認する。
    assert(toolbar.SetOffset("positioned", {-kToolbarOffsetLimitX, -kToolbarOffsetLimitY}));
    frame(1200);
    assert(sectionPosition.x >= window->WorkRect.Min.x);
    assert(sectionPosition.y >= window->WorkRect.Min.y);
    toolbar.ResetOffsets();

    // 右端に配置した項目は、縮小・最大化・復元で折返しの基準が変わっても同じ行と端の距離を保つ。
    toolbar.SetPlacements({{"positioned", {16, 10, 1}}});
    frame(1200); frame(1200);
    const auto anchoredPosition = sectionPosition;
    const auto verifyAnchor = [&] {
        assert(std::abs(window->WorkRect.Max.x-sectionPosition.x-sectionSize.x-16*coordinateScale) < 1);
        assert(std::abs(sectionPosition.y-window->WorkRect.Min.y-10*coordinateScale) < 1);
        assert((toolbar.GetPlacements().at("positioned") == ToolbarPlacement{16,10,1}));
    };
    verifyAnchor();
    frame(220); frame(220); verifyAnchor();
    frame(1800); frame(1800); verifyAnchor();
    frame(80); frame(80);
    assert(sectionPosition.x >= window->WorkRect.Min.x);
    assert((toolbar.GetPlacements().at("positioned") == ToolbarPlacement{16,10,1}));
    frame(1200); frame(1200); verifyAnchor();
    assert(std::abs(sectionPosition.x-anchoredPosition.x) < 1 && std::abs(sectionPosition.y-anchoredPosition.y) < 1);
    const float previousFontScale = ImGui::GetStyle().FontScaleMain;
    ImGui::GetStyle().FontScaleMain = previousFontScale*1.5f;
    frame(1200); frame(1200); verifyAnchor();
    ImGui::GetStyle().FontScaleMain = previousFontScale;
    frame(1200); frame(1200); verifyAnchor();
    toolbar.ResetOffsets(); frame(1200); frame(1200);

    // 画面幅が不足すると共通ツールバーの高さが増え、DockSpaceと重ならないようにする。
    const float wideHeight = toolbar.Height();
    frame(220);
    frame(220);
    assert(toolbar.Height() > wideHeight);
    assert(window->Size.y >= toolbar.Height() - 1);

    assert(toolbar.Unregister("test.button"));
    assert(!toolbar.Unregister("test.button"));
    toolbar.Clear();
    assert(toolbar.Height() == 0);
    ImGui::DestroyContext();
    std::cout << "PASS: toolbar registration, commands, disabled-section context menu, offsets, reset and wrapping\n";
}

void EditorToolbarSettingsTests() {
    // 実際の個人設定を変更しないよう、設定の保存先をテスト専用ディレクトリへ切り替える。
    wchar_t* previous = nullptr;
    size_t length = 0;
    _wdupenv_s(&previous, &length, L"LOCALAPPDATA");
    const auto directory = std::filesystem::absolute("Generated/EditorTests/ToolbarUser").wstring();
    _wputenv_s(L"LOCALAPPDATA", directory.c_str());

    EditorSettingsStore settings;
    settings.Load(std::filesystem::absolute("Project"));
    settings.user.toolbarOffsets.clear();
    settings.user.toolbarPlacements = {{"file.save",{12,8,1}}};
    settings.user.toolbarOffsets["viewport.tools"] = {64, 12.5f};
    settings.user.toolbarOffsets["extension.tool"] = {-20, 30};
    settings.user.toolbarOffsets["initial.position"] = {};
    assert(settings.SaveUser());

    EditorSettingsStore restored;
    restored.Load(std::filesystem::absolute("Project"));
    assert((restored.user.toolbarOffsets.at("viewport.tools") == ToolbarOffset{64, 12.5f}));
    assert((restored.user.toolbarOffsets.at("extension.tool") == ToolbarOffset{-20, 30}));
    assert(!restored.user.toolbarOffsets.contains("initial.position"));
    assert((restored.user.toolbarPlacements.at("file.save") == ToolbarPlacement{12,8,1}));

    // 手編集された不正値を無視し、有限な範囲外の値はUIと同じ制約へ正規化する。
    {
        std::ofstream file(restored.UserDirectory() / "User.settings", std::ios::app);
        file << "toolbar.offset.invalid \"nan 1\"\n";
        file << "toolbar.offset.extra \"1 2 3\"\n";
        file << "toolbar.offset.clamped \"99999 -99999\"\n";
        file << "toolbar.placement.invalid \"nan 1 0\"\n";
        file << "toolbar.placement.extra \"1 2 1 4\"\n";
        file << "toolbar.placement.badAnchor \"1 2 3\"\n";
    }
    restored.Load(std::filesystem::absolute("Project"));
    assert(!restored.user.toolbarOffsets.contains("invalid"));
    assert(!restored.user.toolbarOffsets.contains("extra"));
    assert(!restored.user.toolbarPlacements.contains("invalid"));
    assert(!restored.user.toolbarPlacements.contains("extra"));
    assert(!restored.user.toolbarPlacements.contains("badAnchor"));
    assert((restored.user.toolbarOffsets.at("clamped") == ToolbarOffset{kToolbarOffsetLimitX, -kToolbarOffsetLimitY}));

    // 全項目をリセットした後の保存でも、旧オフセットが再読込されないことを確認する。
    restored.user.toolbarOffsets.clear();
    restored.user.toolbarPlacements.clear();
    assert(restored.SaveUser());
    restored.Load(std::filesystem::absolute("Project"));
    assert(restored.user.toolbarOffsets.empty());
    assert(restored.user.toolbarPlacements.empty());
    _wputenv_s(L"LOCALAPPDATA", previous ? previous : L"");
    std::free(previous);
    std::cout << "PASS: toolbar offset settings round-trip, invalid input and reset persistence\n";
}
}
