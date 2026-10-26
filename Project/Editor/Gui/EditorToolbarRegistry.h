#pragma once
#include "Editor/Services/EditorCommandRegistry.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Services/ToolbarLayout.h"
#include <functional>
#include <string>
#include <vector>

namespace NexusEngine {
	// ツールバーの配置情報と描画拡張を保持する。機能の実行処理はCommandへ委譲する。
	class EditorToolbarRegistry final {
	public:
		using DrawFunction = std::function<void(const LocalizationService&, const EditorCommandRegistry&)>;
		struct Section {
			std::string id;
			int			order = 0;
			// 次の行へ送るかを判断するための最低幅。フォントサイズを単位として指定する。
			// セクション内の細かい配置と折り返しは、その機能の描画処理が担当する。
			float		 minimumWidthEm = 0;
			DrawFunction draw;
		};
		/** 同じIDの重複登録と描画中の登録は拒否する。orderが同じ場合は登録順に並べる。 */
		bool RegisterSection(Section section);
		/** 共通Commandを呼ぶボタンを追加する。実行可否・ShortcutをCommandから取得する。 */
		bool RegisterButton(std::string id, std::string labelKey, std::string command, int order = 500);
		bool Unregister(std::string_view id);
		void Clear();
		/** 自動配置位置に加える論理ピクセル単位のオフセットを設定する。非数は受け入れない。 */
		bool		  SetOffset(const std::string& id, ToolbarOffset offset);
		ToolbarOffset GetOffset(const std::string& id) const;
		/** 未登録の機能の配置も保持し、その機能が後から登録された場合に適用する。 */
		void				  SetOffsets(const ToolbarOffsets& offsets);
		const ToolbarOffsets& GetOffsets() const noexcept {
			return offsets_;
		}
		void   ResetOffsets();
		size_t LayoutRevision() const noexcept {
			return layoutRevision_;
		}
		void  Draw(const LocalizationService& text, const EditorCommandRegistry& commands);
		float Height() const noexcept {
			return height_;
		}
		void SetHeight(float height) noexcept {
			height_ = height;
		}

	private:
		struct Entry {
			Section section;
			// 通常のボタンは翻訳後の文字数から、折り返しに必要な幅を計算する。
			std::string labelKey;
			// 前フレームの実寸を使い、右端に寄せた項目が画面外へ隠れることを防ぐ。
			float previousWidth = 0;
		};
		std::vector<Entry> entries_;
		ToolbarOffsets	   offsets_;
		size_t			   layoutRevision_ = 0;
		bool			   drawing_		   = false;
		float			   height_		   = 0; //< 折り返し後の高さを次フレームの作業領域予約へ反映する
	};
} // namespace NexusEngine
