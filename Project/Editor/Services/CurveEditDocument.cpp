#include "CurveEditDocument.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include <algorithm>
#include <cmath>

namespace NexusEngine {
CurveEditDocument::CurveEditDocument() {
    New();
    // 起動時の空の編集タブだけでは終了確認を出さない。
    fresh_ = false;
}
void CurveEditDocument::Initialize(std::filesystem::path root) {
    root_ = std::move(root);
}
void CurveEditDocument::ReleasePreview() {
    if(changed) {
        changed(AssetReference(), nullptr);
    }
}
void CurveEditDocument::Publish() {
    ++revision_;
    if(changed) {
        changed(AssetReference(), std::make_shared<const FloatCurve>(value_));
    }
}
void CurveEditDocument::New() {
    ReleasePreview();
    before_.reset();
    history_.Reset();
    value_ = {};
    // 初期表示から二つのkeyを直接操作できる状態にする。
    (void)value_.SetKeys({{0, 1}, {1, 1}});
    reference_ = "Curves/NewCurve.curve";
    identity_.clear();
    // 新規draftの既定名が既存assetと衝突し、previewを意図せず置換するのを防ぐ。
    for(size_t index = 2; !root_.empty() && std::filesystem::exists(root_ / reference_) && index < 10000; ++index) {
        reference_ = "Curves/NewCurve" + std::to_string(index) + ".curve";
    }
    fresh_ = true;
    persisted_ = false;
    Publish();
}
Result<void> CurveEditDocument::Open(const std::filesystem::path& file) {
    const auto relative = file.lexically_relative(root_).generic_u8string();
    const std::string reference(reinterpret_cast<const char*>(relative.data()), relative.size());
    auto path = ResolveFileWithinRoot(root_, reference);
    if(!path || path->extension() != ".curve") {
        return std::unexpected(Error(ErrorCategory::Resource, 392, "Choose a .curve inside Assets"));
    }
    auto candidate = LoadFloatCurve(*path);
    if(!candidate) {
        return std::unexpected(candidate.error());
    }
    ReleasePreview();
    value_ = std::move(*candidate);
    reference_ = reference;
    auto identity = EnsureAssetIdentity(root_, *path);
    identity_ = identity ? *identity : "";
    before_.reset();
    history_.Reset();
    fresh_ = false;
    persisted_ = true;
    Publish();
    return {};
}
Result<void> CurveEditDocument::Save(const std::string& reference) {
    EndEdit();
    const bool currentLocation = reference == reference_;
    RefreshLocation();
    const auto destination = currentLocation ? reference_ : reference;
    auto path = ResolveFileWithinRoot(root_, destination);
    if(!path || path->extension() != ".curve") {
        return std::unexpected(Error(ErrorCategory::Resource, 392, "Save requires a .curve inside Assets"));
    }
    std::error_code error;
    std::filesystem::create_directories(path->parent_path(), error);
    if(error) {
        return std::unexpected(Error(ErrorCategory::FileSystem, 392, error.message()));
    }
    auto result = SaveFloatCurve(*path, value_);
    if(result) {
        ReleasePreview();
        reference_ = destination;
        auto identity = EnsureAssetIdentity(root_, *path);
        identity_ = identity ? *identity : "";
        fresh_ = false;
        persisted_ = true;
        history_.MarkSaved();
        Publish();
    }
    return result;
}
void CurveEditDocument::BeginEdit() {
    if(!before_) {
        before_ = value_;
    }
}
Result<void> CurveEditDocument::SetKeys(std::vector<CurveKey> keys) {
    auto candidate = value_;
    auto result = candidate.SetKeys(std::move(keys));
    if(!result) {
        return result;
    }
    BeginEdit();
    value_ = std::move(candidate);
    Publish();
    return {};
}
Result<std::string> CurveEditDocument::CopyKeys(std::span<const size_t> selection) const {
    std::vector<CurveKey> keys;
    for(size_t index : selection) {
        if(index >= value_.Keys().size()) {
            return std::unexpected(Error(ErrorCategory::Resource, 392, "Curve: invalid key selection"));
        }
        auto key = value_.Keys()[index];
        // 部分選択でAutoの隣接keyが失われても、コピー元の実効勾配を維持する。
        key.inTangent = value_.Tangent(index, true);
        key.outTangent = value_.Tangent(index, false);
        key.tangentMode = CurveTangentMode::Broken;
        keys.push_back(key);
    }
    std::ranges::sort(keys, {}, &CurveKey::x);
    FloatCurve copied;
    auto valid = copied.SetKeys(std::move(keys));
    if(!valid) {
        return std::unexpected(valid.error());
    }
    return SerializeFloatCurve(copied);
}
Result<std::vector<size_t>> CurveEditDocument::PasteKeys(std::string_view text, double x) {
    auto copied = ParseFloatCurve(text);
    if(!copied) {
        return std::unexpected(copied.error());
    }
    if(!std::isfinite(x) || copied->Keys().empty()) {
        return std::unexpected(Error(ErrorCategory::Resource, 392, "Curve: paste requires keys and finite X"));
    }
    auto keys = value_.Keys();
    std::vector<double> positions;
    for(auto key : copied->Keys()) {
        key.x = x + (key.x - copied->Keys().front().x);
        positions.push_back(key.x);
        std::erase_if(keys, [&](const CurveKey& previous) {
            return std::abs(previous.x - key.x) < 1e-12;
        });
        keys.push_back(key);
    }
    std::ranges::sort(keys, {}, &CurveKey::x);
    // 検証を完了してから履歴を閉じる。壊れたclipboardはdraftを変更しない。
    FloatCurve candidate;
    auto valid = candidate.SetKeys(keys);
    if(!valid) {
        return std::unexpected(valid.error());
    }
    EndEdit();
    auto changedKeys = SetKeys(std::move(keys));
    if(!changedKeys) {
        return std::unexpected(changedKeys.error());
    }
    EndEdit();
    std::vector<size_t> selection;
    for(size_t index = 0; index < value_.Keys().size(); ++index) {
        if(std::ranges::find(positions, value_.Keys()[index].x) != positions.end()) {
            selection.push_back(index);
        }
    }
    return selection;
}
Result<void> CurveEditDocument::SetPreset(int preset) {
    if(preset < 0 || preset > 5) {
        return std::unexpected(Error(ErrorCategory::Resource, 392, "Curve: unknown preset"));
    }
    std::vector<CurveKey> keys{{0, 0}, {1, 1}};
    if(preset == 0) {
        keys.front().y = 1;
    } else if(preset >= 2) {
        keys.front().interpolation = CurveInterpolation::Cubic;
        keys.front().outTangent = preset == 4 ? 2 : 0;
        keys.back().inTangent = preset == 3 ? 2 : 0;
        if(preset == 5) {
            keys = {{0, 0}, {.5, 1}, {1, 0}};
            for(auto& key : keys) {
                key.interpolation = CurveInterpolation::Cubic;
            }
        }
    }
    EndEdit();
    auto result = SetKeys(std::move(keys));
    EndEdit();
    return result;
}
void CurveEditDocument::EndEdit() {
    if(!before_) {
        return;
    }
    const auto oldValue = *before_;
    const auto newValue = value_;
    before_.reset();
    if(oldValue == newValue) {
        return;
    }
    history_.Record({"curve.edit", [this, oldValue] {
        value_ = oldValue;
        Publish();
        return true;
    }, [this, newValue] {
        value_ = newValue;
        Publish();
        return true;
    }});
}
bool CurveEditDocument::Undo() {
    EndEdit();
    return history_.Undo();
}
bool CurveEditDocument::Redo() {
    EndEdit();
    return history_.Redo();
}
bool CurveEditDocument::CanUndo() const noexcept {
    return (before_ && *before_ != value_) || history_.CanUndo();
}
bool CurveEditDocument::CanRedo() const noexcept {
    return !before_ && history_.CanRedo();
}
const FloatCurve& CurveEditDocument::Value() const noexcept {
    return value_;
}
const std::string& CurveEditDocument::Reference() const noexcept {
    return reference_;
}
const std::string& CurveEditDocument::AssetReference() const noexcept {
    return identity_.empty() ? reference_ : identity_;
}
void CurveEditDocument::RefreshLocation() {
    if(identity_.empty()) {
        return;
    }
    auto path = ResolveFileWithinRoot(root_, identity_);
    if(!path) {
        return;
    }
    const auto encoded = path->lexically_relative(std::filesystem::weakly_canonical(root_)).generic_u8string();
    const std::string relative(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    if(relative != reference_) {
        if(changed) {
            changed(reference_, nullptr);
        }
        reference_ = relative;
        Publish();
    }
}
bool CurveEditDocument::IsDirty() const noexcept {
    return fresh_ || history_.IsDirty();
}
size_t CurveEditDocument::Revision() const noexcept {
    return revision_;
}
bool CurveEditDocument::HasSavedFile() const noexcept {
    return persisted_;
}
}
