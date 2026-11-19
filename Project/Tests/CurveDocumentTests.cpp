#include "Editor/Services/CurveEditDocument.h"
#include <stdexcept>
#include <array>
#include <cmath>
#include <limits>
#include <fstream>
#include "Platform/FileSystem/AtomicFile.h"
#include "Editor/Services/AnimationRigDocument.h"
#include "Platform/FileSystem/AssetIdentity.h"

namespace NexusEngine::Tests {
namespace {
void CurveCheck(bool condition, const char* message) {
    if(!condition) {
        throw std::runtime_error(message);
    }
}
}
/** \brief 連続編集、Undo分岐、保存位置、不変preview snapshotを検証する。 */
void RunCurveDocumentTests() {
    const auto root = std::filesystem::absolute("Generated/CurveDocumentTests") / CreateUuid().value();
    std::filesystem::create_directories(root);
    CurveEditDocument document;
    document.Initialize(root);
    document.New();
    CurveCheck(document.Save("saved.curve").has_value(), "Curve document save failed");
    CurveCheck(!document.IsDirty(), "Saved curve is dirty");
    std::shared_ptr<const FloatCurve> snapshot;
    int notifications = 0;
    document.changed = [&](const auto&, auto curve) {
        snapshot = curve;
        ++notifications;
    };
    document.BeginEdit();
    for(int i = 1; i <= 20; ++i) {
        auto keys = document.Value().Keys();
        keys[1].y = 1 + i * .1;
        CurveCheck(document.SetKeys(keys).has_value(), "Curve continuous edit failed");
    }
    document.EndEdit();
    const auto edited = snapshot;
    CurveCheck(edited && edited->Evaluate(1) == 3 && notifications == 20, "Draft preview not published");
    CurveCheck(document.Undo() && document.Value().Evaluate(1) == 1 && !document.IsDirty(), "Drag was not one undo");
    CurveCheck(edited->Evaluate(1) == 3, "Undo mutated immutable snapshot");
    CurveCheck(document.Redo() && document.IsDirty(), "Curve redo failed");
    CurveCheck(document.Save("saved.curve").has_value(), "Curve save after redo failed");
    CurveCheck(document.Undo() && document.IsDirty(), "Undo across save position became clean");
    auto keys = document.Value().Keys();
    keys[1].y = 4;
    CurveCheck(document.SetKeys(keys).has_value(), "Branch edit failed");
    document.EndEdit();
    CurveCheck(!document.Redo() && document.IsDirty(), "Undo branch retained stale redo");
    CurveCheck(!document.Save("../outside.curve"), "Curve save escaped Assets");
    CurveCheck(document.Open(root / "saved.curve").has_value(), "Curve reload failed");
    CurveCheck(!document.IsDirty() && document.Value().Evaluate(1) == 3, "Reload did not restore saved state");
    CurveCheck(LoadFloatCurve(root / "saved.curve")->Evaluate(1) == 3, "Unsaved branch overwrote asset");
    const auto beforePaste = document.Value();
    const std::array<size_t, 2> selection{0, 1};
    const auto copied = document.CopyKeys(selection);
    CurveCheck(copied.has_value(), "Curve key copy failed");
    const auto pasted = document.PasteKeys(*copied, 2);
    CurveCheck(pasted && pasted->size() == 2 && document.Value().Keys().size() == 4, "Curve paste lost keys");
    CurveCheck(document.Undo() && document.Value() == beforePaste, "Curve paste was not one undo");
    CurveCheck(document.Redo() && document.Value().Keys().size() == 4, "Curve paste redo failed");
    const auto beforeBadPaste = document.Value();
    CurveCheck(!document.PasteKeys("not curve json", 0), "Invalid clipboard accepted");
    CurveCheck(!document.PasteKeys(*copied, std::numeric_limits<double>::infinity()), "Infinite paste accepted");
    CurveCheck(document.Value() == beforeBadPaste, "Rejected paste changed draft");
    const auto replacement = document.PasteKeys(*copied, 0);
    CurveCheck(replacement && document.Value().Keys().size() == 4, "Paste duplicate X was not replaced");
    for(int preset = 0; preset < 6; ++preset) {
        CurveCheck(document.SetPreset(preset).has_value(), "Curve preset rejected");
        CurveCheck(ParseFloatCurve(SerializeFloatCurve(document.Value())).has_value(), "Preset text roundtrip failed");
    }
    CurveCheck(!document.SetPreset(6), "Invalid preset accepted");
    CurveCheck(document.SetPreset(3).has_value() && std::abs(document.Value().Evaluate(.5) - .25) < 1e-9, "Ease in is incorrect");
    // OSのlong-path設定に依存せず、SDK inventoryと同じ深さのfileを検証する。
    auto deep = root;
    for(int depth = 0; depth < 12; ++depth) {
        deep /= "InventoryLongPathSegment";
    }
    const auto nativeDirectory = std::filesystem::path(L"\\\\?\\" + deep.native());
    std::filesystem::create_directories(nativeDirectory);
    const auto deepFile = deep / "payload.bin";
    {
        std::ofstream output(std::filesystem::path(L"\\\\?\\" + deepFile.native()), std::ios::binary);
        output << "inventory long path";
    }
    const auto shortFile = root / "payload.bin";
    {
        std::ofstream output(shortFile, std::ios::binary);
        output << "inventory long path";
    }
    const auto longDigest = Sha256File(deepFile);
    const auto shortDigest = Sha256File(shortFile);
    CurveCheck(longDigest && shortDigest && *longDigest == *shortDigest, "Long path inventory SHA-256 failed");
    AnimationRigDocument rigDocument;
    rigDocument.Initialize(root);
    rigDocument.New(true);
    rigDocument.BeginEdit();
    AnimationLayerDefinition layer;
    layer.clip = "Shared.nxanim";
    std::get<AnimationLayerDocument>(rigDocument.Draft()).layers.push_back(layer);
    rigDocument.EndEdit();
    CurveCheck(rigDocument.Save("test.nxlayers").has_value() && !rigDocument.IsDirty(), "Layer document save failed");
    rigDocument.BeginEdit();
    std::get<AnimationLayerDocument>(rigDocument.Draft()).layers[0].weight = .25f;
    rigDocument.EndEdit();
    CurveCheck(rigDocument.IsDirty() && rigDocument.Undo() && !rigDocument.IsDirty(), "Layer document undo/save position failed");
    CurveCheck(rigDocument.Redo() && rigDocument.IsDirty(), "Layer document redo failed");
    CurveCheck(!rigDocument.Save("../outside.nxlayers"), "Layer document escaped root");
    CurveCheck(rigDocument.Open(root / "test.nxlayers").has_value() && !rigDocument.IsDirty(), "Layer reload failed");
    rigDocument.New(false);
    auto& profile = std::get<RetargetProfile>(rigDocument.Draft());
    profile.sourceModel = "Source.glb";
    profile.targetModel = "Target.glb";
    profile.joints = {{"4:Root/", "4:Root/"}};
    const auto savedRevision = rigDocument.SavedRevision();
    CurveCheck(rigDocument.Save("test.nxretarget").has_value(), "Retarget document save failed");
    CurveCheck(rigDocument.SavedRevision() == savedRevision + 1, "Rig save did not publish an asset revision");
    CurveCheck(rigDocument.Open(root / "test.nxretarget").has_value() && !rigDocument.IsDirty(), "Retarget document reload failed");
    const auto identifiedFile = root / "identity.curve";
    CurveCheck(SaveFloatCurve(identifiedFile, document.Value()).has_value(), "Identity curve creation failed");
    const auto identity = EnsureAssetIdentity(root, identifiedFile);
    CurveCheck(identity && IsAssetIdentity(*identity), "Asset UUID was not issued");
    CurveCheck(EnsureAssetIdentity(root, identifiedFile).value() == *identity, "Asset UUID changed on repeat registration");
    CurveCheck(SameAssetReference(root, *identity, "identity.curve"), "Path and UUID do not refer to the same asset");
    const auto moved = root / "renamed.curve";
    CurveCheck(RenameAssetWithIdentity(root, identifiedFile, moved).has_value(), "Asset/sidecar rename failed");
    const auto resolved = ResolveAssetIdentity(root, *identity);
    CurveCheck(resolved && SameFilePath(*resolved, moved), "Asset UUID did not follow rename");
    CurveCheck(SameAssetReference(root, "identity.curve", *identity), "Legacy path history did not follow rename");
    CurveCheck(!RenameAssetWithIdentity(root, moved, root / "saved.curve"), "Asset rename overwrote an existing file");
    CurveCheck(!ResolveAssetIdentity(root, "asset://../outside"), "Invalid asset URI accepted");
    CurveCheck(document.Open(moved).has_value(), "Moved curve could not be opened");
    const auto draftBeforeMove = document.Value();
    auto editedKeys = document.Value().Keys();
    editedKeys[0].y += 1;
    CurveCheck(document.SetKeys(editedKeys).has_value(), "Draft edit before rename failed");
    document.EndEdit();
    CurveCheck(RenameAssetWithIdentity(root, moved, root / "moved-again.curve").has_value(), "Second asset rename failed");
    document.RefreshLocation();
    CurveCheck(document.Reference() == "moved-again.curve" && document.IsDirty(), "Open draft lost identity/dirty state after rename");
    CurveCheck(document.Undo() && document.Value() == draftBeforeMove, "Rename broke curve undo");
}
}
