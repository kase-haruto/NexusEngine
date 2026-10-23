#pragma once
#include "Editor/Services/EditorCommandRegistry.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Services/NotificationService.h"

namespace NexusEngine {
/** \brief 共通CommandからToolbarを描く。ImGuiの作業領域を予約する */
void DrawEditorToolbar(const LocalizationService& text, const EditorCommandRegistry& commands,
    std::string_view document, bool dirty, bool playing, bool paused);
/** \brief 入力を妨げない期限付き操作通知を描く */
void DrawEditorNotifications(NotificationService& notifications);
}
