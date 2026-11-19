#pragma once
#include "IEditorPanel.h"
#include "Runtime/Input/InputMap.h"

namespace NexusEngine {
/** Project設定のdraftを編集する。Save時だけ原子的に公開し、Play中の定義は変えない。 */
class InputMapPanel final : public IEditorPanel {
public:
    explicit InputMapPanel(std::filesystem::path file);
    std::string_view Id() const noexcept override;
    std::string_view TitleKey() const noexcept override;
    std::array<float, 2> InitialSize() const noexcept override;
    void Draw(const EditorContext& context) override;
    bool HasUnsavedChanges() const;
    Result<void> SaveDraft();

  private:
    void Reload();
    void Save();
    std::filesystem::path file_;
    InputMap saved_, draft_;
    std::string error_;
    bool loaded_ = false;
    bool persisted_ = false; //< 既定設定のままでも初回保存を許可する
};
}
