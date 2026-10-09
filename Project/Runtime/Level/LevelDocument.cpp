#include "LevelDocument.h"

// engine
#include "../../Foundation/Logging/Logger.h"
#include "LevelSerializer.h"

// external
#include <imgui.h>

using namespace NexusEngine;

namespace {
	const std::string kFilepathDir = "Resources/Assets/Levels";
}

/////////////////////////////////////////////////////////////////////////////////////////
//  DebugGuiを表示する
/////////////////////////////////////////////////////////////////////////////////////////
void NexusEngine::LevelDocument::ShowGui() {
	ImGui::Begin("LevelDocument");

	// Levelの名前を入力
	NameLabelGui();

	FilePathLabelGui();

	//// save load
	//if(ImGui::Button("保存")) {
	//	if(auto result = Save()) {
	//		NEXUS_LOG_INFO("LevelDocument", "LevelDocument saved successfully.");
	//	} else {
	//		NEXUS_LOG_ERROR("LevelDocument", "Failed to save LevelDocument.");
	//	}
	//}

	//ImGui::SameLine();

	//// levelを開く
	//if(ImGui::Button("開く")) {
	//	if(auto result = Open(filePath_.value_or("New Level.json"))) {
	//		NEXUS_LOG_INFO("LevelDocument", "LevelDocument opened successfully.");
	//	} else {
	//		NEXUS_LOG_ERROR("LevelDocument", "Failed to open LevelDocument.");
	//	}
	//}

	ImGui::End();
}

/////////////////////////////////////////////////////////////////////////////////////////
//  LevelDocumentの生成
/////////////////////////////////////////////////////////////////////////////////////////
Result<void> NexusEngine::LevelDocument::Create(std::optional<std::string> name) {
	// Levelを生成
	auto level = std::make_unique<Level>(name.value_or("New Level"));
	auto result = level->LoadEmpty();
	if(!result) return result;
	level_ = std::move(level);
	filePath_.reset();
	return {};
}

/////////////////////////////////////////////////////////////////////////////////////////
//  Levelを開く
/////////////////////////////////////////////////////////////////////////////////////////
Result<void> NexusEngine::LevelDocument::Open(const std::filesystem::path& path) {
	auto level = std::make_unique<Level>();
	auto result = level->Load(path);
	if(!result) return result;
	level_ = std::move(level);
	filePath_ = path;
	return {};
}
/////////////////////////////////////////////////////////////////////////////////////////
//  filePath_が設定されていればLevelを保存する
/////////////////////////////////////////////////////////////////////////////////////////
Result<void> NexusEngine::LevelDocument::Save() {
	// 名前からファイルパスを構築
	ConstructionFilePathForName();
	// Levelを保存
	if(filePath_.has_value()) {
		if(!LevelSerializer::Save(filePath_.value_or("New Level.json"), *level_)) {
			return std::unexpected(Error(ErrorCategory::General, 1, "Failed to save LevelDocument."));
		}
	} else {
		return std::unexpected(Error(ErrorCategory::General, 1, "Failed to dont setting file path."));
	}
	return {};
}

/////////////////////////////////////////////////////////////////////////////////////////
//  filePathを指定してLevelを保存する
/////////////////////////////////////////////////////////////////////////////////////////
Result<void> NexusEngine::LevelDocument::SaveAs(const std::filesystem::path& path) {
	// pathが空でなければLevelを保存
	if(!path.empty()) {
		if(!LevelSerializer::Save(path, *level_)) {
			return std::unexpected(Error(ErrorCategory::General, 1, "Failed to save LevelDocument."));
		}
		// 保存に成功したらfilePath_を更新する
		filePath_ = path;
	} else {
		return std::unexpected(Error(ErrorCategory::General, 1, "filePath is empty."));
	}

	return {};
}

/////////////////////////////////////////////////////////////////////////////////////////
//  level名前の入力と表示
/////////////////////////////////////////////////////////////////////////////////////////
bool NexusEngine::LevelDocument::NameLabelGui() {

	static char buf[128] = "New Level Name";

	ImGui::InputText("Level Name", buf, IM_ARRAYSIZE(buf));
	ImGui::SameLine();
	if(ImGui::Button("適用")) {
		level_->SetName(buf);
		return true;
	}
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////
//  パスを指定してLevelを保存する
/////////////////////////////////////////////////////////////////////////////////////////
bool NexusEngine::LevelDocument::FilePathLabelGui() {
	// ファイルパスを設定して保存
	static char buf[128] = "Resources/Assets/Levels/New Level.json";
	ImGui::InputText("ファイルパス", buf, IM_ARRAYSIZE(buf));

	ImGui::SameLine();

	if(ImGui::Button("保存")) {
		filePath_	= buf;
		auto result = Save();
		if(!result) {
			return false;
		}
		return true;
	}
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////
//  名前からファイルパスを構築
/////////////////////////////////////////////////////////////////////////////////////////
void NexusEngine::LevelDocument::ConstructionFilePathForName() {
	// 　ファイルパスを構築
	filePath_ = kFilepathDir + "/" + level_->GetName() + ".json";
}
