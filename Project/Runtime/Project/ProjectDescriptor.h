#pragma once
#include <cstdint>
#include <string>

namespace NexusEngine {
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
};
}
