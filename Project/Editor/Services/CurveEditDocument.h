#pragma once
#include "Runtime/Curves/CurveSerializer.h"
#include "EditorHistory.h"
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace NexusEngine {
/** \brief 使用先が解釈する任意のpreview。Curve Editorは用途やcomponent型を知らない。 */
struct CurvePreviewContext {
    std::string label; //< 使用先の補足単位。assetには保存しない
    std::function<std::optional<double>()> input; //< 接続が有効な時の現在X
    std::function<void()> play, pause, stop; //< 使用先への要求
    std::function<void(double)> seek; //< 使用先のXへ移動する要求
};
/** \brief 汎用Curveの編集寿命・履歴・保存を管理する。使用先は変更通知だけを借用する。 */
class CurveEditDocument final {
public:
    CurveEditDocument();
    CurveEditDocument(const CurveEditDocument&) = delete;
    CurveEditDocument& operator=(const CurveEditDocument&) = delete;
    void Initialize(std::filesystem::path root);
    void New();
    Result<void> Open(const std::filesystem::path& file);
    Result<void> Save(const std::string& reference);
    void BeginEdit();
    Result<void> SetKeys(std::vector<CurveKey> keys);
    /** \brief 実効接線を含めて選択範囲をコピーする。用途や表示状態を含めない。 */
    Result<std::string> CopyKeys(std::span<const size_t> selection) const;
    /** \brief 先頭Xを指定位置へ移動し貼付。重複Xは置換し、操作全体を1回Undoとする。 */
    Result<std::vector<size_t>> PasteKeys(std::string_view text, double x);
    /** \brief 0 Constant / 1 Linear / 2 Smooth / 3 Ease In / 4 Ease Out / 5 Pulse。 */
    Result<void> SetPreset(int preset);
    void EndEdit();
    bool Undo();
    bool Redo();
    bool CanUndo() const noexcept;
    bool CanRedo() const noexcept;
    const FloatCurve& Value() const noexcept;
    const std::string& Reference() const noexcept;
    const std::string& AssetReference() const noexcept;
    void RefreshLocation();
    bool IsDirty() const noexcept;
    bool HasSavedFile() const noexcept;
    size_t Revision() const noexcept;
    /** \brief asset参照と不変draftを通知する。nullptrは旧previewの解除。componentを所有しない。 */
    std::function<void(const std::string&, std::shared_ptr<const FloatCurve>)> changed;
    CurvePreviewContext preview; //< 借用callback。assetやcomponentの所有権を受け取らない
    bool focused = false; //< global shortcutへ同じ入力を二重送信しないためのEditor状態
private:
    void Publish();
    void ReleasePreview();
    FloatCurve value_; //< 保存済みassetとは別所有の編集draft
    std::optional<FloatCurve> before_; //< ドラッグ全体を一つの履歴へまとめる
    EditorHistory history_; //< 共通履歴実装を再利用する
    std::filesystem::path root_; //< 保存可能なAssets範囲
    std::string reference_ = "Curves/NewCurve.curve"; //< 現在の安全な相対参照
    std::string identity_; //< 保存済みassetの安定参照。表示pathと分離する
    size_t revision_ = 0; //< Undo/未保存編集も含めたpreview無効化番号
    bool fresh_ = true; //< 新規未保存の既定値もdirtyとして扱う
    bool persisted_ = false; //< 同じ参照名でも新規draftと既存fileを区別する
};
}
