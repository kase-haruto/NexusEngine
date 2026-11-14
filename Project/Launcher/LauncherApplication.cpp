#include "LauncherApplication.h"
#include "LauncherWidgets.h"
#include "Tools/Project/ProjectBuild.h"
#include "Platform/Window/FileDialog.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cctype>

namespace NexusEngine {
namespace {
constexpr const char* Configurations[]{"Debug", "Develop", "Release"};
// 小さいWindowでは入力labelの領域を残し、隣の操作を次の行へ送る。
float ContentRight() { return ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x; }
void FitInput(const char* label) {
    const auto available = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label).x - ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemWidth(std::min(ImGui::CalcItemWidth(),std::max(80.0f,available)));
}
void ContinueIfFits(float right, float width) {
    if(right - ImGui::GetItemRectMax().x >= width + ImGui::GetStyle().ItemSpacing.x) ImGui::SameLine();
}
bool Browse(float right, const char* id) {
    ContinueIfFits(right,ImGui::CalcTextSize(id,nullptr,true).x + ImGui::GetStyle().FramePadding.x * 2);
    return ImGui::Button(id);
}
std::string Utf8(const std::filesystem::path& file) { const auto text = file.generic_u8string(); return {reinterpret_cast<const char*>(text.data()), text.size()}; }
std::filesystem::path Path(const char* text) { return ConvertString(std::string(text)); }
Result<LauncherSnapshot> ReadSnapshot(const std::filesystem::path& recent, const std::filesystem::path& engines) {
    LauncherSnapshot snapshot;
    auto projects = ProjectRegistry(recent).Read(); if(!projects) return std::unexpected(projects.error()); snapshot.projects = std::move(*projects);
    auto registered = EngineRegistry(engines).Read(); if(!registered) return std::unexpected(registered.error());
    for(auto& registration : *registered) {
        LauncherEngine item; item.registration = std::move(registration);
        if(item.registration.installation) {
            auto templates = ProjectTemplates::List(item.registration.installation->templates);
            if(templates) {
                // シーンの初期状態だけを選ばせる。開発BackendのサンプルはCLIで引き続き生成できる。
                for(auto& projectTemplate : *templates) {
                    if(projectTemplate.id == "blank" || projectTemplate.id == "basic-3d") {
                        item.templates.push_back(std::move(projectTemplate));
                    }
                }
            }
            else item.registration.error = templates.error().GetMessageText();
        }
        snapshot.engines.push_back(std::move(item));
    }
    auto selected = EngineRegistry(engines).Default();
    if(selected) snapshot.defaultEngine = std::move(*selected);
    else snapshot.warning = selected.error().GetMessageText();
    return snapshot;
}
std::string LastUsed(int64_t seconds) {
    const time_t value = static_cast<time_t>(seconds); tm time{}; char buffer[32]{};
    if(gmtime_s(&time, &value) != 0) return "Unknown";
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M UTC", &time); return buffer;
}
}
LauncherApplication::LauncherApplication(HWND owner, std::filesystem::path recentFile, std::filesystem::path engineFile, int page, bool smoke)
    : owner_(owner), recentFile_(std::move(recentFile)), engineFile_(std::move(engineFile)), smoke_(smoke), tab_(page) { Refresh(); }
LauncherApplication::~LauncherApplication() {
    // 起動済みEditorは独立させる。停止対象はこのworkerが所有するビルドJobだけ。
    stop_.request_stop(); if(task_.valid()) task_.wait();
}
void LauncherApplication::Begin(std::string message, Operation operation) {
    if(Busy()) return;
    error_.clear(); working_ = std::move(message); stop_ = std::stop_source{};
    task_ = std::async(std::launch::async, [recent = recentFile_, engines = engineFile_, operation = std::move(operation), token = stop_.get_token()]() -> Result<LauncherSnapshot> {
        try {
            auto result = operation(token); if(!result) return std::unexpected(result.error());
            return ReadSnapshot(recent, engines);
        } catch(const std::exception& exception) { return std::unexpected(Error(ErrorCategory::Framework, 202, exception.what())); }
    });
}
void LauncherApplication::Refresh() { Begin("Refreshing projects and engines...", [](std::stop_token) -> Result<void> { return {}; }); }
void LauncherApplication::OpenProject(std::filesystem::path file) {
    Begin("Building and opening project...", [file = std::move(file), recent = recentFile_, engines = engineFile_, configuration = std::string(Configurations[configuration_]), smoke = smoke_](std::stop_token stop) -> Result<void> {
        auto project = ProjectService::Open(file); if(!project) return std::unexpected(project.error());
        auto engine = EngineRegistry(engines).Resolve(project->Descriptor().engine, configuration); if(!engine) return std::unexpected(engine.error());
        if(std::filesystem::exists(project->Config() / "BuildTargets.json")) {
            auto uuid = CreateUuid(); if(!uuid) return std::unexpected(uuid.error());
            auto built = ProjectBuild::Build(*project, *engine, ProjectHostKind::Editor, project->Root() / "Build/Logs" / (*uuid + ".log"), stop);
            if(!built) return std::unexpected(built.error());
        }
        if(stop.stop_requested()) return std::unexpected(Error(ErrorCategory::Framework, 202, "Opening cancelled."));
        ProjectLaunchOptions options;
        if(smoke) {
            auto uuid = CreateUuid(); if(!uuid) return std::unexpected(uuid.error());
            options.hidden = true; options.frames = 3;
            options.log = project->Root() / "Build/Logs" / (*uuid + ".editor.log");
        }
        auto launched = ProjectBuild::Launch(*project, *engine, ProjectHostKind::Editor, options); if(!launched) return std::unexpected(launched.error());
        return ProjectRegistry(recent).Register(*project);
    });
}
void LauncherApplication::PickProject() {
    auto picked = FileDialog::Open(owner_);
    if(!picked) error_ = picked.error().GetMessageText();
    else if(*picked) OpenProject(**picked);
}
void LauncherApplication::PickPath(std::array<char,4096>& buffer, bool folder) {
    auto picked = folder ? FileDialog::Folder(owner_) : FileDialog::Open(owner_, true);
    if(!picked) error_ = picked.error().GetMessageText();
    else if(*picked) {
        const auto text = Utf8(**picked);
        if(text.size() >= buffer.size()) error_ = "The selected path is too long for this field.";
        else { std::copy(text.begin(), text.end(), buffer.begin()); buffer[text.size()] = 0; }
    }
}
void LauncherApplication::Draw() {
    if(Busy() && task_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = task_.get();
        if(!result) error_ = result.error().GetMessageText();
        else {
            const auto previous = engine_ >= 0 && engine_ < static_cast<int>(snapshot_.engines.size()) ? snapshot_.engines[engine_].registration.manifest : std::filesystem::path{};
            snapshot_ = std::move(*result);
            error_ = snapshot_.warning;
            engine_ = -1;
            for(size_t index = 0; index < snapshot_.engines.size(); ++index)
                if(!previous.empty() && SameFilePath(previous, snapshot_.engines[index].registration.manifest)) engine_ = static_cast<int>(index);
            if(engine_ < 0) {
                for(size_t index = 0; index < snapshot_.engines.size(); ++index) {
                    const auto& engine = snapshot_.engines[index].registration;
                    if(snapshot_.defaultEngine && SameFilePath(engine.manifest, snapshot_.defaultEngine->manifest)) { engine_ = static_cast<int>(index); break; }
                }
            }
            template_ = 0;
        }
        working_.clear();
    }
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("NexusEngine Launcher", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    const float scale = LauncherUI::Scale();
    if(!Busy()) {
        if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N)) tab_ = 1;
        if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O)) PickProject();
        if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F)) { tab_ = 0; focusSearch_ = true; }
    }
    // 狭いWindowでも本文の幅を保つ。縮小時のナビゲーション名はtooltipで補う。
    const bool compact = viewport->WorkSize.x < 850 * scale;
    ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
    ImGui::BeginChild("Navigation",{(compact ? 64 : 204) * scale,0});
    LauncherUI::Gap(20);
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark),compact ? "NX" : "NEXUS ENGINE");
    if(!compact) ImGui::TextDisabled("PROJECT BROWSER");
    LauncherUI::Gap(36);
    ImGui::BeginDisabled(Busy());
    for(int index = 0; index < 3; ++index) {
        constexpr const char* tabs[]{"Projects", "Create project", "Engine library"};
        constexpr const char* shortTabs[]{"P", "+", "E"};
        if(ImGui::Selectable(compact ? shortTabs[index] : tabs[index],tab_ == index,0,{0,38 * scale})) tab_ = index;
        if(compact && ImGui::IsItemHovered()) ImGui::SetTooltip("%s",tabs[index]);
    }
    ImGui::EndDisabled();
    if(!compact) {
        LauncherUI::Gap(36); ImGui::Separator(); LauncherUI::Gap();
        ImGui::TextDisabled("LOCAL WORKSPACE");
        LauncherUI::Gap(6);
        ImGui::TextDisabled("%zu projects",snapshot_.projects.size());
        ImGui::TextDisabled("%zu engines",snapshot_.engines.size());
        LauncherUI::Gap(18);
        ImGui::TextWrapped("Open a project to start editing. Launch Visual Studio from the Editor's Tools menu.");
    }
    ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::SameLine(0,0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{28 * scale,24 * scale});
    ImGui::BeginChild("Workspace",{0,0},ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::BeginDisabled(Busy());
    if(tab_ == 0) DrawProjects(); else if(tab_ == 1) DrawNewProject(); else DrawEngines();
    ImGui::EndDisabled();
    if(Busy()) {
        ImGui::Separator(); ImGui::TextWrapped("%s", working_.c_str());
        if(ImGui::Button("Cancel operation")) stop_.request_stop();
    }
    if(!error_.empty()) {
        ImGui::Separator(); ImGui::PushStyleColor(ImGuiCol_Text, {1,0.55f,0.5f,1});
        ImGui::TextWrapped("%s", error_.c_str()); ImGui::PopStyleColor();
        if(ImGui::Button("Dismiss")) error_.clear();
    }
    std::vector<const RegisteredEngine*> engines;
    for(const auto& entry : snapshot_.engines) engines.push_back(&entry.registration);
    migration_.Draw(owner_,engines,Busy(),error_,[this](std::string message,Operation operation) { Begin(std::move(message),std::move(operation)); });
    ImGui::EndChild(); ImGui::PopStyleVar();
    ImGui::End();
}
void LauncherApplication::DrawProjects() {
    const float scale = LauncherUI::Scale();
    LauncherUI::Title("Projects","Pick up where you left off, or start something new.");
    const auto right = ContentRight();
    if(LauncherUI::Primary("+ New project",{144 * scale,36 * scale})) tab_ = 1;
    ContinueIfFits(right,130 * scale);
    if(ImGui::Button("Open project...",{130 * scale,36 * scale})) PickProject();
    ContinueIfFits(right,80 * scale);
    if(ImGui::Button("Refresh",{80 * scale,36 * scale})) Refresh();
    LauncherUI::Gap(16);
    ImGui::SetNextItemWidth(std::max(120 * scale,ImGui::GetContentRegionAvail().x - 144 * scale));
    if(focusSearch_) { ImGui::SetKeyboardFocusHere(); focusSearch_ = false; }
    ImGui::InputTextWithHint("##search", "Search projects...  Ctrl+F", search_.data(), search_.size());
    ImGui::SameLine(); ImGui::SetNextItemWidth(128 * scale);
    ImGui::Combo("##Build configuration", &configuration_, Configurations, 3);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Build configuration must match the registered engine.");
    LauncherUI::Label("RECENT PROJECTS"); LauncherUI::Gap(8);
    ImGui::BeginChild("Project cards", {0,std::max(220 * scale,ImGui::GetContentRegionAvail().y - 78 * scale)}, ImGuiChildFlags_None);
    const std::string query = search_.data();
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (270 * scale)));
    size_t matches = 0;
    if(ImGui::BeginTable("Recent grid", columns, ImGuiTableFlags_SizingStretchSame)) {
        for(const auto& project : snapshot_.projects) {
            const auto file = Utf8(project.file); const std::string haystack = project.name + " " + file;
            if(!query.empty() && std::search(haystack.begin(), haystack.end(), query.begin(), query.end(), [](char left, char right) {
                return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
            }) == haystack.end()) continue;
            ++matches;
            ImGui::TableNextColumn(); ImGui::PushID(file.c_str());
            ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{14 * scale,14 * scale});
            ImGui::BeginChild("Card", {0,338 * scale}, ImGuiChildFlags_AlwaysUseWindowPadding);
            LauncherUI::Cover(102); LauncherUI::Gap(8);
            ImGui::TextWrapped("%s", project.name.c_str());
            ImGui::TextDisabled("%s  %s", project.engine.association == "development" ? "Development" : "Installed", project.engine.version.c_str());
            LauncherUI::PathLine(file.c_str());
            ImGui::TextDisabled("%s", LastUsed(project.lastUsedUtc).c_str());
            if(project.state != RecentProjectState::Available) ImGui::TextColored({1,0.65f,0.4f,1}, "%s", project.state == RecentProjectState::Missing ? "Project moved or missing" : "Invalid project descriptor");
            ImGui::BeginDisabled(project.state != RecentProjectState::Available);
            if(LauncherUI::Primary("Open",{80 * scale,30 * scale})) OpenProject(project.file);
            ImGui::EndDisabled(); ImGui::SameLine();
            if(ImGui::Button("...",{36 * scale,30 * scale})) ImGui::OpenPopup("Project actions");
            if(ImGui::BeginPopup("Project actions")) {
            ImGui::BeginDisabled(project.state != RecentProjectState::Available);
            if(ImGui::MenuItem("Change engine...")) migration_.Open(project.file);
            ImGui::EndDisabled();
            if(ImGui::MenuItem("Remove from recent")) Begin("Removing recent entry...", [registry = recentFile_, file = project.file](std::stop_token) { return ProjectRegistry(registry).Remove(file); });
            ImGui::EndPopup();
            }
            ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor(); ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if(!matches) {
        LauncherUI::Gap(32);
        ImGui::TextUnformatted(query.empty() ? "Your next project starts here" : "No matching projects");
        ImGui::TextWrapped(query.empty() ? "Create a project, or open an existing .nxproject file to add it to this workspace." : "Try a different project name or folder.");
    }
    ImGui::EndChild();
}
void LauncherApplication::DrawNewProject() {
    const float scale = LauncherUI::Scale();
    LauncherUI::Title("Create a project","Choose a starting point. Make it your own.");
    // 既定Windowと125% DPIでも入力欄を横へ残す。初期言語を長いTemplate一覧の下へ隠さない。
    const bool wide = ImGui::GetContentRegionAvail().x > 600 * scale;
    if(!ImGui::BeginTable("Creation layout",wide ? 2 : 1,ImGuiTableFlags_SizingStretchSame)) return;
    ImGui::TableNextColumn();
    ImGui::TextDisabled("01  STARTING TEMPLATE"); LauncherUI::Gap(8);
    const LauncherEngine* selectedEngine = engine_ >= 0 && engine_ < static_cast<int>(snapshot_.engines.size()) ? &snapshot_.engines[engine_] : nullptr;
    // 一覧だけをscrollし、Module種類を増やしてもProject名・Location・言語の位置を維持する。
    const float templateHeight = wide ? std::max(220 * scale, ImGui::GetContentRegionAvail().y - 40 * scale) : 220 * scale;
    ImGui::BeginChild("Project template list", {0,templateHeight});
    if(selectedEngine) {
        for(size_t index = 0; index < selectedEngine->templates.size(); ++index) {
            const auto& item = selectedEngine->templates[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{14 * scale,12 * scale});
            ImGui::BeginChild("Template",{0,178 * scale},ImGuiChildFlags_AlwaysUseWindowPadding);
            LauncherUI::Cover(70,item.id != "blank");
            if(ImGui::Selectable(item.name.c_str(),template_ == static_cast<int>(index),0,{0,26 * scale})) template_ = static_cast<int>(index);
            ImGui::TextWrapped("%s",item.description.c_str());
            ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor(); ImGui::PopID();
            LauncherUI::Gap(8);
        }
    } else {
        LauncherUI::Gap(12);
        ImGui::TextWrapped("Choose an engine to see its project templates.");
        if(ImGui::Button("Go to engine library")) tab_ = 2;
    }
    ImGui::EndChild();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("02  PROJECT DETAILS");
    LauncherUI::Label("Project name"); ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##Project name","MyGame",name_.data(),name_.size());
    LauncherUI::Label("Location"); ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##Parent folder","Parent folder",location_.data(),location_.size());
    if(ImGui::Button("Browse folders...")) PickPath(location_,true);
    LauncherUI::Label("Initial Game Language"); ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##Initial Game Language", &initialLanguage_, "C++ — Native Game Development\0C# — Managed Game Development\0");
    ImGui::TextWrapped("Selects the first Game Module. Additional languages can be added to the same project.");
    // 初期言語と実行可能なテンプレートを区別し、宣言だけのProjectをGameplay環境と誤認させない。
    if(initialLanguage_ == 1) ImGui::TextWrapped("Managed Gameplay generates C# game logic for the shared ECS. Building requires .NET 10 SDK and an explicit x64 Host Pack; running requires .NET 10 Runtime. Managed Bootstrap checks startup only; blank/basic-3d create module declarations only. C++ modules can be added later.");
    LauncherUI::Label("Engine version");
    std::string selected = "Select an engine";
    if(engine_ >= 0 && engine_ < static_cast<int>(snapshot_.engines.size())) {
        const auto& engine = snapshot_.engines[engine_].registration;
        selected = engine.identity.id + " / " + engine.identity.version + " / " + engine.identity.association + " / " + engine.configuration;
    }
    ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##Engine", selected.c_str())) {
        for(size_t index = 0; index < snapshot_.engines.size(); ++index) {
            const auto& engine = snapshot_.engines[index].registration;
            const auto label = engine.identity.id + " / " + engine.identity.version + " / " + engine.identity.association + " / " + engine.configuration + "##" + std::to_string(index);
            ImGui::BeginDisabled(!engine.installation || !engine.error.empty());
            if(ImGui::Selectable(label.c_str(), engine_ == static_cast<int>(index))) { engine_ = static_cast<int>(index); template_ = 0; }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    // Combo変更後は選択先を取り直し、別EngineのTemplateを同じframeに使わない。
    selectedEngine = engine_ >= 0 && engine_ < static_cast<int>(snapshot_.engines.size()) ? &snapshot_.engines[engine_] : nullptr;
    LauncherUI::Gap(12);
    ImGui::TextWrapped("The engine version is saved with this project. You can change it later from Projects.");
    LauncherUI::Gap(20); ImGui::Separator(); LauncherUI::Gap(12);
    const bool valid = name_[0] && location_[0] && selectedEngine && selectedEngine->registration.installation &&
        template_ >= 0 && template_ < static_cast<int>(selectedEngine->templates.size());
    ImGui::BeginDisabled(!valid);
    if(LauncherUI::Primary("Create Project",{ImGui::GetContentRegionAvail().x,40 * scale})) {
        const auto engine = *selectedEngine->registration.installation;
        for(int index = 0; index < 3; ++index) if(engine.configuration == Configurations[index]) configuration_ = index;
        const auto projectTemplate = selectedEngine->templates[template_];
        ProjectDescriptor descriptor; descriptor.name = name_.data(); descriptor.engine = engine.identity;
        const auto destination = Path(location_.data()) / Path(name_.data());
        const auto language = initialLanguage_ == 0 ? ProjectModuleLanguage::Cpp : ProjectModuleLanguage::CSharp;
        // workerへは値を渡し、生成中のUI変更から選択済みModuleを保護する。
        Begin("Creating project...", [engine, projectTemplate, descriptor, destination, language, recent = recentFile_](std::stop_token) -> Result<void> {
            auto project = ProjectTemplates::Create(destination, descriptor, projectTemplate, engine, language); if(!project) return std::unexpected(project.error());
            return ProjectRegistry(recent).Register(*project);
        });
        tab_ = 0;
    }
    ImGui::EndDisabled();
    LauncherUI::Gap(8);
    ImGui::TextDisabled("Creates a new folder inside the selected location.");
    ImGui::EndTable();
}
void LauncherApplication::DrawEngines() {
    const float scale = LauncherUI::Scale();
    LauncherUI::Title("Engine library","Manage the versions available on this computer.");
    ImGui::TextWrapped("Projects store an engine ID and version. This library tells NexusEngine where to find them on this PC.");
    LauncherUI::Gap(12);
    const auto right = ContentRight();
    // 登録フォームは必要なときだけ展開し、普段は利用可能な版を主役にする。
    if(ImGui::CollapsingHeader("Add an engine",snapshot_.engines.empty() ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None)) {
    LauncherUI::Gap(8);
    FitInput("Engine manifest");
    ImGui::InputText("Engine manifest", manifest_.data(), manifest_.size());
    if(Browse(right,"Browse...##engine")) PickPath(manifest_, false);
    ImGui::BeginDisabled(!manifest_[0]);
    if(ImGui::Button("Register Engine")) Begin("Validating engine...", [registry = engineFile_, file = Path(manifest_.data())](std::stop_token) { return EngineRegistry(registry).Register(file); });
    ImGui::EndDisabled();
    FitInput("Development repository");
    ImGui::InputText("Development repository", development_.data(), development_.size());
    if(Browse(right,"Browse...##development")) PickPath(development_, true);
    ImGui::SetNextItemWidth(140); ImGui::Combo("Development configuration", &configuration_, Configurations, 3);
    ImGui::BeginDisabled(!development_[0]);
    if(ImGui::Button("Register Development")) Begin("Registering source engine...", [registry = engineFile_, repository = Path(development_.data()), configuration = std::string(Configurations[configuration_])](std::stop_token) -> Result<void> {
        auto manifest = EngineManifest::DescribeDevelopment(repository, configuration);
        if(!manifest) return std::unexpected(manifest.error());
        return EngineRegistry(registry).Register(*manifest);
    });
    ImGui::EndDisabled();
    LauncherUI::Gap(12);
    }
    LauncherUI::Label("AVAILABLE ENGINES"); LauncherUI::Gap(8);
    ImGui::BeginChild("Engines", {0,std::max(180 * ImGui::GetStyle().FontScaleDpi,ImGui::GetContentRegionAvail().y - 100)});
    for(const auto& item : snapshot_.engines) {
        const auto& engine = item.registration; const auto file = Utf8(engine.manifest); ImGui::PushID(file.c_str());
        ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{16 * scale,14 * scale});
        ImGui::BeginChild("Engine",{0,196 * scale},ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark),"%s",engine.identity.association == "development" ? "DEVELOPMENT" : "INSTALLED");
        ImGui::SameLine(); ImGui::TextDisabled("%s",engine.configuration.c_str());
        LauncherUI::Gap(6);
        ImGui::TextWrapped("NexusEngine %s",engine.identity.version.c_str());
        ImGui::TextDisabled("%s",engine.identity.id.c_str());
        ImGui::TextWrapped("%s", file.c_str());
        if(snapshot_.defaultEngine && SameFilePath(snapshot_.defaultEngine->manifest, engine.manifest)) ImGui::TextColored({0.5f,0.85f,0.7f,1}, "Default for new projects");
        if(!engine.error.empty()) ImGui::TextWrapped("%s", engine.error.c_str());
        ImGui::BeginDisabled(!engine.installation);
        if(ImGui::Button("Make default")) Begin("Saving default engine...", [registry = engineFile_, file = engine.manifest](std::stop_token) { return EngineRegistry(registry).SetDefault(file); });
        ImGui::EndDisabled(); ImGui::SameLine();
        if(ImGui::Button("Remove registration")) Begin("Removing engine registration...", [registry = engineFile_, file = engine.manifest](std::stop_token) { return EngineRegistry(registry).Remove(file); });
        ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor(); LauncherUI::Gap(8); ImGui::PopID();
    }
    if(snapshot_.engines.empty()) ImGui::TextWrapped("Register a Development repository or an Installed Engine manifest. Required versions are never substituted automatically.");
    ImGui::EndChild();
}
}
