#include "SourceAttachment.h"
#include "Editor/Services/ContentDragPayload.h"
#include <algorithm>

namespace NexusEngine::UI {
std::vector<std::string> AcceptMultipleSourceAttachments(const EditorContext &context) {
    std::vector<std::string> result;
    if (!context.sources || context.session.IsPlaying() || !ImGui::BeginDragDropTarget()) {
        return result;
    }
    if (const auto *payload = ImGui::AcceptDragDropPayload("NEXUS_CONTENT_ITEMS")) {
        const auto files = ContentDragPayload::Decode(payload->Data, static_cast<size_t>(payload->DataSize));
        if (files) {
            for (const auto &file : *files) {
                const auto types = SourceFileActions::TypesFor(context.sources->Root(), file, context.types);
                if (types.size() != 1) {
                    result.clear();
                    NEXUS_LOG_WARNING("ContentBrowser",
                                      context.text.Text(types.empty() ? "source.noType" : "source.multipleTypes"));
                    break;
                }
                if (std::ranges::find(result, types.front()) == result.end()) {
                    result.push_back(types.front());
                }
            }
        }
    }
    ImGui::EndDragDropTarget();
    return result;
}
} // namespace NexusEngine::UI
