#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace NexusEngine {
/** Gameコードの実装言語。Project全体の言語を制限する設定ではない。 */
enum class ProjectModuleLanguage { Cpp, CSharp };
/** Moduleの用途。ロード方式やDLL ABIは後続のRuntime境界で定義する。 */
enum class ProjectModuleType { Game };

/*-----------------------------------------------------------------------------------------
 * ProjectModuleDescriptor
 * - Project内のGame Moduleの安定ID、実装言語、Source配置を保持する
 * - RuntimeやAssemblyを所有しない値型。LevelやAssetはProject全体で共有する
 *---------------------------------------------------------------------------------------*/
struct ProjectModuleDescriptor {
    std::string id; //< Project内で一意。表示名や配置を変更しても維持する
    std::string name; //< Editorと開発ツールに表示する名前
    ProjectModuleLanguage language = ProjectModuleLanguage::Cpp; //< このModuleの実装言語
    std::string path; //< Project Root相対のSource Directory
    ProjectModuleType type = ProjectModuleType::Game; //< Moduleの用途
    std::vector<std::string> dependencies{}; //< 先に初期化するModule ID。内部C++型や言語間link依存は表さない
};

/*-----------------------------------------------------------------------------------------
 * ProjectEngineRequirement
 * - 共有Projectが要求するEngineの識別情報を保持する
 * - マシン固有のインストール場所や解決処理は持たない
 *---------------------------------------------------------------------------------------*/
struct ProjectEngineRequirement {
    std::string association = "development"; //< developmentまたはinstalled
    std::string id = "nexus-development"; //< マシン間で共有するEngine識別子
    std::string version; //< 空文字は明示的なDevelopment追従。Installedでは必須
};

struct ProjectDirectories {
    std::string assets = "Assets"; //< Project Root相対のAsset保存先
    std::string levels = "Levels"; //< Project Root相対のLevel保存先
    std::string source = "Source"; //< Game固有コードの保存先
    std::string config = "Config"; //< Project共有設定の保存先
};

/*-----------------------------------------------------------------------------------------
 * ProjectDescriptor
 * - nxprojectへ保存する共有メタデータを保持する
 * - UI、ファイル操作、Engineの自動切替は担当しない
 *---------------------------------------------------------------------------------------*/
struct ProjectDescriptor {
    uint32_t schemaVersion = 1; //< 未対応スキーマを暗黙に読み替えない
    std::string projectId; //< 移動しても維持するUUID
    std::string name; //< Projectの表示名
    std::string projectVersion = "0.1.0"; //< Game自身のバージョン
    ProjectEngineRequirement engine; //< Engineの要求条件
    ProjectDirectories paths; //< Root相対の保存先
    std::string startupLevel = "Levels/Startup.level"; //< Root相対の起動Level
    std::vector<ProjectModuleDescriptor> modules; //< 空なら従来のProject。複数言語でもWorldを分離しない
};
}
