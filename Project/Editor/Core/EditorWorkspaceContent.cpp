#include "EditorWorkspace.h"
#include <algorithm>
#include "Editor/Services/ContentBrowserServices.h"
#include "Editor/Gui/Panels/InputMapPanel.h"
#include "Runtime/Project/ProjectContext.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Foundation/Utility/String/ConvertString.h"

namespace NexusEngine {
std::shared_ptr<ContentBrowserServices> EditorWorkspace::ContentServices() {
    return contentBrowser_;
}
void EditorWorkspace::InitializeContentServices(const std::filesystem::path &root,
                                                const std::filesystem::path &engineRoot,
                                                const ProjectContext *project) {
    contentBrowser_ = std::make_shared<ContentBrowserServices>();
    auto &service = *contentBrowser_;
    service.settings = &settings_;
    service.contentChanged = [this] {
        assetEditor_.RefreshPreviews(false);
    };
    service.showInspector = [this] {
        panels_.Open("Details");
    };
    const auto config = project ? project->Config() : root / "Config";
    service.library.Initialize(&settings_, AssetCatalog::Utf8(assets_->Root()), config);
    service.AddRoot("Content", "browser.assets", "content", *assets_);
    service.AddRoot("Source", "browser.source", "source", *sources_, false, true);
    service.AddRoot("Levels", "asset.level", "asset_level", *levels_);
    service.AddOwnedRoot("Config", "Config", "settings", config, true, false, false, false);
    std::error_code error;
    const auto engineAssets = engineRoot / "Resources/Assets";
    if (!engineRoot.empty() && std::filesystem::is_directory(engineAssets, error) && !error &&
        !SameFilePath(engineAssets, assets_->Root())) {
        service.AddOwnedRoot("Engine", "cb.engineContent", "content", engineAssets, true, false, true, false);
    }
    const auto plugins = root / "Plugins";
    if (std::filesystem::is_directory(plugins, error) && !error) {
        std::filesystem::directory_iterator iterator(plugins,
                                                     std::filesystem::directory_options::skip_permission_denied, error),
            end;
        for (; !error && iterator != end; iterator.increment(error)) {
            if (iterator->is_symlink(error) || !iterator->is_directory(error)) {
                error.clear();
                continue;
            }
            auto content = iterator->path() / "Content";
            if (!std::filesystem::is_directory(content, error)) {
                error.clear();
                content = iterator->path() / "Assets";
            }
            if (!std::filesystem::is_directory(content, error)) {
                error.clear();
                continue;
            }
            service.AddOwnedRoot("Plugin:" + AssetCatalog::Utf8(iterator->path().filename()),
                                 AssetCatalog::Utf8(iterator->path().filename()), "plugin", content, true, false, false,
                                 true);
        }
    }
    const auto monitor = settings_.user.contentBrowser.find(AssetCatalog::Utf8(assets_->Root()) + ".monitor");
    if (monitor == settings_.user.contentBrowser.end() || monitor->second != "off") {
        // OS eventだけを待つ。未対応root・大量通知時はCatalogが手動Refreshへ戻す。
        for (auto &source : service.roots) {
            if (!source.engine && !source.plugin) {
                source.catalog->SetAutomaticRefresh(true);
            }
        }
    }
    service.documents.Register(
        {"Level", "Level",
         [this] {
             auto *document = session_.GetLevelDocument();
             return document && document->GetFilePath() ? *document->GetFilePath() : std::filesystem::path{};
         },
         [this] {
             return documents_.IsDirty();
         },
         [this]() -> Result<void> {
             if (documents_.Save()) {
                 return {};
             }
             return std::unexpected(
                 Error(ErrorCategory::Resource, 298,
                       documents_.Error().empty() ? "Choose a Level Save As destination." : documents_.Error()));
         }});
    service.documents.Register({"Animation", "Animation",
                                [this] {
                                    return assets_->Root() / ConvertString(animationStudio_.reference);
                                },
                                [this] {
                                    return animationStudio_.dirty;
                                },
                                [this] {
                                    return animationStudio_.Save();
                                }});
    service.documents.Register({"Curve", "Curve",
                                [this] {
                                    return assets_->Root() / ConvertString(curveDocument_.Reference());
                                },
                                [this] {
                                    return curveDocument_.IsDirty();
                                },
                                [this] {
                                    return curveDocument_.Save(curveDocument_.Reference());
                                }});
    service.documents.Register({"Rig", "Rig / Layer",
                                [this] {
                                    return assets_->Root() / ConvertString(rigDocument_.Reference());
                                },
                                [this] {
                                    return rigDocument_.IsDirty();
                                },
                                [this] {
                                    return rigDocument_.Save(rigDocument_.Reference());
                                }});
    service.documents.Register({"RenderGraph", "Render Graph",
                                [config] {
                                    return config / "RenderPipeline.json";
                                },
                                [this] {
                                    return renderGraph_.IsDirty();
                                },
                                [this] {
                                    return renderGraph_.Save();
                                }});
    const auto input = [this]() -> InputMapPanel * {
        for (auto &entry : panels_.Entries()) {
            if (entry.id == "InputMap") {
                return dynamic_cast<InputMapPanel *>(entry.panel.get());
            }
        }
        return nullptr;
    };
    service.documents.Register({"InputMap", "Input Map",
                                [config] {
                                    return config / "InputMap.settings";
                                },
                                [input] {
                                    auto *panel = input();
                                    return panel && panel->HasUnsavedChanges();
                                },
                                [input]() -> Result<void> {
                                    auto *panel = input();
                                    return panel ? panel->SaveDraft() : Result<void>{};
                                }});
    commands_.Register("file.saveAll", {[this] {
                                            Report(contentBrowser_->documents.SaveAll());
                                        },
                                        [this] {
                                            return !session_.IsPlaying() && contentBrowser_->documents.AnyDirty();
                                        }});
    commands_.Register("browser.new", {[this] {
                                           for (const auto &entry : panels_.Entries()) {
                                               if (entry.id.starts_with("ContentBrowser") && !entry.isOpen) {
                                                   panels_.Open(entry.id);
                                                   return;
                                               }
                                           }
                                       },
                                       [this] {
                                           return std::ranges::any_of(panels_.Entries(), [](const auto &value) {
                                               return value.id.starts_with("ContentBrowser") && !value.isOpen;
                                           });
                                       }});
    menu_.RegisterTool({"cb.newBrowser", "browser.new"});
}
} // namespace NexusEngine
