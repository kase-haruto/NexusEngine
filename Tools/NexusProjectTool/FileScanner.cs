namespace NexusProjectTool;

internal static class FileScanner
{
    // 対応拡張子をVisual Studio C++プロジェクトの項目種別へ変換する。
    private static readonly Dictionary<string, ItemKind> ExtensionKinds =
        new(StringComparer.OrdinalIgnoreCase)
        {
            [".c"] = ItemKind.ClCompile,
            [".cc"] = ItemKind.ClCompile,
            [".cpp"] = ItemKind.ClCompile,
            [".cxx"] = ItemKind.ClCompile,
            [".ixx"] = ItemKind.ClCompile,
            [".cppm"] = ItemKind.ClCompile,
            [".h"] = ItemKind.ClInclude,
            [".hh"] = ItemKind.ClInclude,
            [".hpp"] = ItemKind.ClInclude,
            [".hxx"] = ItemKind.ClInclude,
            [".inl"] = ItemKind.ClInclude,
            [".inc"] = ItemKind.ClInclude,
            [".ipp"] = ItemKind.ClInclude,
            [".rc"] = ItemKind.ResourceCompile,
            [".natvis"] = ItemKind.None,
            [".json"] = ItemKind.None,
            [".txt"] = ItemKind.None,
            [".md"] = ItemKind.None
        };

    // 設定されたルートを走査し、プロジェクト相対パスとフィルタを組み立てる。
    public static IReadOnlyList<SourceItem> Scan(
        string configDirectory,
        string projectDirectory,
        ProjectSyncConfig config)
    {
        var items = new Dictionary<string, SourceItem>(StringComparer.OrdinalIgnoreCase);
        var excluded = new HashSet<string>(
            config.ExcludeDirectories,
            StringComparer.OrdinalIgnoreCase);

        foreach (SourceRoot root in config.Roots)
        {
            string rootPath = Path.GetFullPath(root.Path, configDirectory);
            if (!Directory.Exists(rootPath))
            {
                throw new DirectoryNotFoundException($"Source root does not exist: {rootPath}");
            }

            foreach (string file in EnumerateFiles(rootPath, excluded))
            {
                if (!ExtensionKinds.TryGetValue(Path.GetExtension(file), out ItemKind kind))
                {
                    continue;
                }

                string projectPath = NormalizeProjectPath(
                    Path.GetRelativePath(projectDirectory, file));
                string relativeDirectory = Path.GetDirectoryName(
                    Path.GetRelativePath(rootPath, file)) ?? string.Empty;
                string filterPath = CombineFilter(root.Filter, relativeDirectory);

                if (!items.TryAdd(
                    projectPath,
                    new SourceItem(file, projectPath, filterPath, kind)))
                {
                    throw new InvalidDataException(
                        $"The file is included by more than one source root: {file}");
                }
            }
        }

        return items.Values
            .OrderBy(item => item.ProjectPath, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    // 除外ディレクトリへ入らずに、対象ルート以下のファイルを再帰列挙する。
    private static IEnumerable<string> EnumerateFiles(
        string root,
        HashSet<string> excluded)
    {
        var pending = new Stack<string>();
        pending.Push(root);

        while (pending.Count > 0)
        {
            string directory = pending.Pop();
            foreach (string file in Directory.EnumerateFiles(directory))
            {
                yield return Path.GetFullPath(file);
            }

            foreach (string child in Directory.EnumerateDirectories(directory))
            {
                if (!excluded.Contains(Path.GetFileName(child)))
                {
                    pending.Push(child);
                }
            }
        }
    }

    // MSBuildで扱いやすいWindows形式の相対パスへ統一する。
    private static string NormalizeProjectPath(string path) =>
        path.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);

    // 設定の基準フィルタと物理ディレクトリ階層を結合する。
    private static string CombineFilter(string baseFilter, string relativeDirectory)
    {
        string normalizedBase = baseFilter
            .Replace(Path.AltDirectorySeparatorChar, '\\')
            .Trim('\\');
        string normalizedRelative = relativeDirectory
            .Replace(Path.DirectorySeparatorChar, '\\')
            .Replace(Path.AltDirectorySeparatorChar, '\\')
            .Trim('\\', '.');

        return (normalizedBase, normalizedRelative) switch
        {
            ("", "") => string.Empty,
            (_, "") => normalizedBase,
            ("", _) => normalizedRelative,
            _ => $"{normalizedBase}\\{normalizedRelative}"
        };
    }
}
