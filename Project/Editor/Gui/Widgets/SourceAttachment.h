#pragma once
#include "Editor/Gui/Panels/IEditorPanel.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/AssetCatalog.h"
#include "Editor/Services/SourceFileActions.h"
#include "Editor/Services/LocalizationService.h"
#include "Foundation/Logging/Logger.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "ThirdParty/imgui/imgui.h"
#include <cstring>
#include <optional>
#include <string>

namespace NexusEngine::UI {
/**
 * \brief 最後のImGui itemへのSource dropを型IDへ変換する。実際の構造変更はPanel描画後に行う
 * \param context 所有Threadの値MetadataとSource Root。Entity / Managed objectは借用しない
 * \return 一意に解決できた型ID。複数型のファイルはBrowserの右クリックで明示選択する
 */
inline std::optional<std::string> AcceptSourceAttachment(const EditorContext& context) {
    std::optional<std::string> result;
    if(!context.sources || context.session.IsPlaying() || !ImGui::BeginDragDropTarget()) return result;
    if(const auto* payload = ImGui::AcceptDragDropPayload("NEXUS_SOURCE_PATH")) {
        // Drag payloadはコピー値として検査し、外部producerの不正な長さや終端を許可しない。
        if(payload->Data && payload->DataSize > 1 && payload->DataSize <= 32769) {
            const auto* text = static_cast<const char*>(payload->Data);
            const auto length = static_cast<size_t>(payload->DataSize - 1);
            if(text[length] == '\0' && std::memchr(text, '\0', length) == nullptr) {
                const auto types = SourceFileActions::TypesFor(context.sources->Root(),
                    std::filesystem::path(ConvertString(std::string(text, length))), context.types);
                if(types.size() == 1) result = types.front();
                else NEXUS_LOG_WARNING("ContentBrowser", context.text.Text(types.empty() ? "source.noType" : "source.multipleTypes"));
            }
        }
    }
    ImGui::EndDragDropTarget();
    return result;
}
}
