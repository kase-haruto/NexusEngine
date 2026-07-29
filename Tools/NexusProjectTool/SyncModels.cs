namespace NexusProjectTool;

// 拡張子から決定されるMSBuildの項目種別。
internal enum ItemKind
{
    ClCompile,
    ClInclude,
    ResourceCompile,
    None
}

// 走査で見つかった1ファイル分の同期情報。
internal sealed record SourceItem(
    string AbsolutePath,
    string ProjectPath,
    string FilterPath,
    ItemKind Kind);

// ユーザーへ表示する1件分の変更内容。
internal sealed record SyncChange(string Action, string Path);

// 1回の同期処理で検出された変更を保持する。
internal sealed class SyncResult
{
    public List<SyncChange> Changes { get; } = [];
    public bool HasChanges => Changes.Count > 0;
}

// コマンドラインから解析した実行オプション。
internal sealed record ToolOptions(
    string Command,
    string ConfigPath,
    bool DryRun);
