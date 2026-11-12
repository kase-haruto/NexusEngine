#pragma once
#include <filesystem>

namespace NexusEngine {
/** Managed backendの適用結果。Compile結果とは分け、Editorへ値だけで通知する。 */
enum class ManagedReloadState { Idle, Reloading, Succeeded, CompatibilityError, StateRestoreError };
/*-----------------------------------------------------------------------------------------
 * CSharpRuntimeSettings
 * - ホストが選択した公式HostingライブラリとManaged bootstrapの配置を値で保持する
 * - SDK探索、Build、Runtime所有は行わない。相対パスや既定Assemblyへのfallbackを許可しない
 *---------------------------------------------------------------------------------------*/
struct CSharpRuntimeSettings {
    std::filesystem::path nethostFile; //< 公式nethost.dllの絶対パス。Gameホスト隣接配置を基本とする
    std::filesystem::path runtimeConfig; //< Framework-dependent bootstrapのruntimeconfig.json
    std::filesystem::path bootstrapAssembly; //< 安定したBootstrap assembly。GameScriptsのReloadでは変更しない
    std::filesystem::path dotnetRoot; //< 任意の明示Runtime配置。空ならnethost公式のインストール探索を使う
    std::filesystem::path gameAssembly; //< 空ならPhase 3互換のBootstrapのみ。指定時は絶対GameScripts配置
    std::filesystem::path monoRoot; //< 指定時はMono backendを使用する。bin / lib / etcを含む明示SDKまたは配布Root
};
}
