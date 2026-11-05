#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include "Foundation/Error/Result.h"
#include "Level.h"

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * LevelDocument
 * - Levelのロード・保存先とWorldの所有を管理する
 * - Editor UI、ImGui、Undo履歴に依存しない
 *---------------------------------------------------------------------------------------*/
class LevelDocument final {
public:
    /** \brief ホストが選択した登録方針を、作成・読込するLevelへ引き継ぐ */
    explicit LevelDocument(ReflectionRegistration reflection = {}) : reflection_(reflection) {}
    /** \brief 空Levelを作成する。成功時だけ現在のWorldを置換する */
    Result<void> Create(std::optional<std::string> name = std::nullopt);
    /** \brief 保存Levelを開く。失敗時は現在のdocumentを保持する */
    Result<void> Open(const std::filesystem::path& path);
    /** \brief 既存保存先へ保存する。保存先未設定時はSaveAsを要求する */
    Result<void> Save();
    /** \brief 指定した保存先へ保存し、成功時に保存先を更新する */
    Result<void> SaveAs(const std::filesystem::path& path);
    Level* GetLevel() noexcept { return level_.get(); }
    const Level* GetLevel() const noexcept { return level_.get(); }
    std::optional<std::filesystem::path> GetFilePath() const { return filePath_; }
private:
    ReflectionRegistration reflection_; //< Documentと再生Worldで共用する型登録方針
    std::unique_ptr<Level> level_; //< documentが所有するWorld
    std::optional<std::filesystem::path> filePath_; //< 保存成功時だけ更新する
};
}
