#pragma once
#include "Platform/Process/Process.h"
#include "Runtime/Project/ProjectContext.h"

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * SourceCodeEditor
 * - 開いているGameのSolutionを解決し、Visual Studioへ渡す
 * - C++ / C#ソース単体をOSの関連付け済みテキストエディタへ渡す
 * - EngineのSolutionやGameのビルド、EditorのWorldは操作しない
 *---------------------------------------------------------------------------------------*/
class SourceCodeEditor final {
public:
    /** \brief Project直下のSolutionを一意に解決する。欠損・曖昧・Root外リンクは拒否する */
    [[nodiscard]] static Result<std::filesystem::path> Solution(const ProjectContext& project);
    /** \brief vswhereでC++ツールを持つVisual Studio IDEを探す。照会ログはGameのBuild以下へ保存する */
    [[nodiscard]] static Result<std::filesystem::path> VisualStudio(const ProjectContext& project);
    /** \brief 選択GameのSolutionをIDEへ渡す。空のexecutableはVisual Studioを自動検出する */
    [[nodiscard]] static Result<Process> Open(const ProjectContext& project, const std::filesystem::path& executable = {});
    /** \brief ソースをOSの関連付け済みEditorで開く。関連付けがない場合はWindowsのNotepadを使う */
    [[nodiscard]] static Result<void> OpenFile(const std::filesystem::path& file);
};
}
