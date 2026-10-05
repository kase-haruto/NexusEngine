#pragma once

// c++
#include <filesystem>
#include <memory>
#include <optional>

// engine
#include "Foundation/Error/Result.h"
#include "Level.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * LevelDocument
	 * - levelを編集して保存するための状態を所有するクラスとして設定
	 * - LevelDocumentはRuntimeのLevelと同じ構造を持つが、RuntimeのLevelは描画や更新に必要な情報のみを保持する
	 *---------------------------------------------------------------------------------------*/
	class LevelDocument {
	public:
		//===================================================================*/
		//                    public methods
		//===================================================================*/
		LevelDocument()	 = default;
		~LevelDocument() = default;

		/**
		 * @brief GUIを表示する
		 */
		void ShowGui();

		/**
		 * @brief LevelDocumentを生成する
		 * @return 失敗した場合の原因
		 */
		Result<void> Create(std::optional<std::string> name = std::nullopt);
		/**
		 * @brief LevelDocumentを生成する
		 * @param path
		 * @return エラーの原因
		 */
		Result<void> Open(const std::filesystem::path& path);
		/**
		 * @brief LevelDocumentを保存する
		 * @param path
		 * @return エラーの原因
		 */
		Result<void> Save();
		Result<void> SaveAs(const std::filesystem::path& path);
		/**
		 * @brief LevelDocumentを閉じる
		 */
		Level* GetLevel() noexcept { return level_.get(); }
		const Level* GetLevel() const noexcept { return level_.get(); }
		/**
		 * @brief LevelDocumentのファイルパスを取得する
		 */
		std::optional<std::filesystem::path> GetFilePath() const noexcept { return filePath_; }

		bool IsDirty() const noexcept;
		bool CanUndo() const noexcept;
		bool CanRedo() const noexcept;

	private:
		/**
		 * @brief 名前の入力と表示
		 * @return true:名前が変更された  false:名前は変更されなかった
		 */
		bool NameLabelGui();
		/**
		 * @brief ファイルパスの入力と表示
		 * @return true:ファイルパスが変更された  false:ファイルパスは変更されなかった
		 */
		bool FilePathLabelGui();
		/**
		 * @brief ConstructionFilePathの入力と表示
		 * @return true:ConstructionFilePathが変更された  false:ConstructionFilePathは変更されなかった
		 */
		void ConstructionFilePathForName();

	private:
		//===================================================================*/
		//                    private methods
		//===================================================================*/
		std::unique_ptr<Level>				 level_;	//< LevelDocumentが所有するLevelのインスタンス
		std::optional<std::filesystem::path> filePath_; //< Levelが保存されているファイルパス
	};

} // namespace NexusEngine
