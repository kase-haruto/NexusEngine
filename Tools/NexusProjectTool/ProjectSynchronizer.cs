using System.Security.Cryptography;
using System.Text;
using System.Xml;
using System.Xml.Linq;

namespace NexusProjectTool;

internal sealed class ProjectSynchronizer
{
    // Visual Studio C++プロジェクトが使用するMSBuild XML名前空間。
    private static readonly XNamespace MsBuild =
        "http://schemas.microsoft.com/developer/msbuild/2003";

    private readonly string _projectPath;
    private readonly string _filtersPath;
    private readonly IReadOnlyList<SourceItem> _sourceItems;
    private readonly IReadOnlyList<string> _managedRoots;

    public ProjectSynchronizer(
        string projectPath,
        IReadOnlyList<SourceItem> sourceItems,
        IReadOnlyList<string> managedRoots)
    {
        _projectPath = projectPath;
        _filtersPath = projectPath + ".filters";
        _sourceItems = sourceItems;
        _managedRoots = managedRoots;
    }

    // プロジェクト本体とfiltersファイルの差分を計算し、必要な場合だけ保存する。
    public SyncResult Synchronize(bool writeChanges)
    {
        XDocument project = LoadDocument(_projectPath);
        XDocument filters = File.Exists(_filtersPath)
            ? LoadDocument(_filtersPath)
            : CreateFiltersDocument();
        string originalProject = project.ToString(SaveOptions.DisableFormatting);
        string originalFilters = filters.ToString(SaveOptions.DisableFormatting);
        var result = new SyncResult();

        SynchronizeProject(project, result);
        SynchronizeFilters(filters, result);

        if (writeChanges && result.HasChanges)
        {
            if (!string.Equals(
                    originalProject,
                    project.ToString(SaveOptions.DisableFormatting),
                    StringComparison.Ordinal))
            {
                SaveAtomically(project, _projectPath);
            }

            if (!string.Equals(
                    originalFilters,
                    filters.ToString(SaveOptions.DisableFormatting),
                    StringComparison.Ordinal))
            {
                SaveAtomically(filters, _filtersPath);
            }
        }

        return result;
    }

    // .vcxproj内の管理対象項目を、実ファイル一覧に一致させる。
    private void SynchronizeProject(XDocument document, SyncResult result)
    {
        XElement root = document.Root
            ?? throw new InvalidDataException("The project XML has no root element.");
        var desired = _sourceItems.ToDictionary(
            item => item.ProjectPath,
            StringComparer.OrdinalIgnoreCase);
        var existing = GetProjectItems(root).ToList();

        foreach (XElement element in existing)
        {
            string include = (string?)element.Attribute("Include") ?? string.Empty;
            if (!IsManaged(include) || desired.ContainsKey(include))
            {
                continue;
            }

            element.Remove();
            result.Changes.Add(new SyncChange("Remove project item", include));
        }

        foreach (SourceItem item in _sourceItems)
        {
            XElement? element = existing.FirstOrDefault(candidate =>
                PathsEqual((string?)candidate.Attribute("Include"), item.ProjectPath));
            string expectedName = GetElementName(item.Kind);

            if (element is not null && element.Name.LocalName == expectedName)
            {
                continue;
            }

            if (element is not null)
            {
                element.Remove();
                result.Changes.Add(new SyncChange("Change item type", item.ProjectPath));
            }
            else
            {
                result.Changes.Add(new SyncChange("Add project item", item.ProjectPath));
            }

            GetOrCreateItemGroup(root, expectedName)
                .Add(new XElement(MsBuild + expectedName,
                    new XAttribute("Include", item.ProjectPath)));
        }

        RemoveEmptyItemGroups(root);
        SortManagedItems(root);
    }

    // .vcxproj.filters内の項目配置とフィルタ定義を同期する。
    private void SynchronizeFilters(XDocument document, SyncResult result)
    {
        XElement root = document.Root
            ?? throw new InvalidDataException("The filters XML has no root element.");
        var requiredFilters = GetRequiredFilters();
        var filterDefinitions = root
            .Descendants(MsBuild + "Filter")
            .Where(element => element.Attribute("Include") is not null)
            .ToList();

        foreach (string filter in requiredFilters)
        {
            if (filterDefinitions.Any(element =>
                    string.Equals(
                        (string?)element.Attribute("Include"),
                        filter,
                        StringComparison.OrdinalIgnoreCase)))
            {
                continue;
            }

            GetOrCreateFilterDefinitionGroup(root).Add(
                new XElement(MsBuild + "Filter",
                    new XAttribute("Include", filter),
                    new XElement(
                        MsBuild + "UniqueIdentifier",
                        CreateDeterministicGuid(filter))));
            result.Changes.Add(new SyncChange("Add filter", filter));
        }

        var existingItems = GetProjectItems(root).ToList();
        foreach (XElement element in existingItems)
        {
            string include = (string?)element.Attribute("Include") ?? string.Empty;
            SourceItem? desired = _sourceItems.FirstOrDefault(
                item => PathsEqual(item.ProjectPath, include));

            if (!IsManaged(include))
            {
                continue;
            }

            if (desired is null)
            {
                element.Remove();
                result.Changes.Add(new SyncChange("Remove filter item", include));
                continue;
            }

            string expectedName = GetElementName(desired.Kind);
            string currentFilter = element.Element(MsBuild + "Filter")?.Value ?? string.Empty;
            if (element.Name.LocalName == expectedName &&
                string.Equals(
                    currentFilter,
                    desired.FilterPath,
                    StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            element.Remove();
            AddFilterItem(root, desired);
            result.Changes.Add(new SyncChange("Update filter item", include));
        }

        foreach (SourceItem item in _sourceItems)
        {
            if (GetProjectItems(root).Any(element =>
                    PathsEqual((string?)element.Attribute("Include"), item.ProjectPath)))
            {
                continue;
            }

            AddFilterItem(root, item);
            result.Changes.Add(new SyncChange("Add filter item", item.ProjectPath));
        }

        RemoveUnusedManagedFilters(root, requiredFilters, result);
        RemoveEmptyItemGroups(root);
        SortManagedItems(root);
    }

    // 1ファイル分のfilters項目を、適切なMSBuild項目グループへ追加する。
    private void AddFilterItem(XElement root, SourceItem item)
    {
        string elementName = GetElementName(item.Kind);
        var element = new XElement(
            MsBuild + elementName,
            new XAttribute("Include", item.ProjectPath));
        if (item.FilterPath.Length > 0)
        {
            element.Add(new XElement(MsBuild + "Filter", item.FilterPath));
        }

        GetOrCreateItemGroup(root, elementName).Add(element);
    }

    // 子階層を含め、現在必要なすべてのフィルタ名を求める。
    private HashSet<string> GetRequiredFilters()
    {
        var filters = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string filterPath in _sourceItems
                     .Select(item => item.FilterPath)
                     .Where(path => path.Length > 0))
        {
            string[] parts = filterPath.Split('\\');
            for (int index = 1; index <= parts.Length; ++index)
            {
                filters.Add(string.Join('\\', parts.Take(index)));
            }
        }

        return filters;
    }

    // 管理対象となっていたフィルタのうち、現在使われていないものを削除する。
    private void RemoveUnusedManagedFilters(
        XElement root,
        HashSet<string> requiredFilters,
        SyncResult result)
    {
        foreach (XElement filter in root.Descendants(MsBuild + "Filter")
                     .Where(element => element.Attribute("Include") is not null)
                     .ToList())
        {
            string name = (string)filter.Attribute("Include")!;
            if (IsManagedFilter(name) && !requiredFilters.Contains(name))
            {
                filter.Remove();
                result.Changes.Add(new SyncChange("Remove filter", name));
            }
        }
    }

    // プロジェクト項目が設定された物理ルートの配下かを安全に判定する。
    private bool IsManaged(string projectRelativePath)
    {
        if (projectRelativePath.IndexOfAny(['$', '*', '?']) >= 0)
        {
            return false;
        }

        try
        {
            string fullPath = Path.GetFullPath(
                projectRelativePath,
                Path.GetDirectoryName(_projectPath)!);
            return _managedRoots.Any(root => IsPathWithin(fullPath, root));
        }
        catch (Exception exception)
            when (exception is ArgumentException or NotSupportedException)
        {
            return false;
        }
    }

    // フィルタ名が現在の管理対象階層に含まれるかを判定する。
    private bool IsManagedFilter(string filter)
    {
        foreach (SourceItem item in _sourceItems)
        {
            if (string.Equals(item.FilterPath, filter, StringComparison.OrdinalIgnoreCase) ||
                item.FilterPath.StartsWith(filter + "\\", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }

        return false;
    }

    // パス自身またはその子が指定ルート内にあるかを判定する。
    private static bool IsPathWithin(string path, string root)
    {
        string normalizedRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root));
        string normalizedPath = Path.GetFullPath(path);
        return normalizedPath.Equals(normalizedRoot, StringComparison.OrdinalIgnoreCase) ||
               normalizedPath.StartsWith(
                   normalizedRoot + Path.DirectorySeparatorChar,
                   StringComparison.OrdinalIgnoreCase);
    }

    // コンパイル・ヘッダー・リソース・その他に該当するXML項目だけを列挙する。
    private static IEnumerable<XElement> GetProjectItems(XElement root) =>
        root.Descendants().Where(element =>
            element.Name.Namespace == MsBuild &&
            element.Attribute("Include") is not null &&
            Enum.TryParse<ItemKind>(element.Name.LocalName, out _));

    // 同じ項目種別のItemGroupを取得し、存在しなければtargets読込前へ作成する。
    private static XElement GetOrCreateItemGroup(XElement root, string elementName)
    {
        XElement? group = root.Elements(MsBuild + "ItemGroup")
            .FirstOrDefault(candidate =>
                candidate.Elements(MsBuild + elementName).Any() &&
                candidate.Attribute("Label") is null);
        if (group is not null)
        {
            return group;
        }

        group = new XElement(MsBuild + "ItemGroup");
        XElement? targetsImport = root.Elements(MsBuild + "Import")
            .FirstOrDefault(element =>
                ((string?)element.Attribute("Project"))?.Contains(
                    "Microsoft.Cpp.targets",
                    StringComparison.OrdinalIgnoreCase) == true);
        if (targetsImport is null)
        {
            root.Add(group);
        }
        else
        {
            targetsImport.AddBeforeSelf(group);
        }

        return group;
    }

    // フィルタ定義用のItemGroupを取得し、存在しなければ先頭へ作成する。
    private static XElement GetOrCreateFilterDefinitionGroup(XElement root)
    {
        XElement? group = root.Elements(MsBuild + "ItemGroup")
            .FirstOrDefault(candidate =>
                candidate.Elements(MsBuild + "Filter")
                    .Any(element => element.Attribute("Include") is not null));
        if (group is not null)
        {
            return group;
        }

        group = new XElement(MsBuild + "ItemGroup");
        root.AddFirst(group);
        return group;
    }

    // 管理対象項目だけを相対パス順に並べ、Git差分を安定させる。
    private void SortManagedItems(XElement root)
    {
        foreach (XElement group in root.Elements(MsBuild + "ItemGroup"))
        {
            List<XElement> managed = group.Elements()
                .Where(element =>
                    element.Attribute("Include") is not null &&
                    Enum.TryParse<ItemKind>(element.Name.LocalName, out _) &&
                    IsManaged((string)element.Attribute("Include")!))
                .ToList();
            if (managed.Count < 2)
            {
                continue;
            }

            foreach (XElement element in managed)
            {
                element.Remove();
            }

            foreach (XElement element in managed.OrderBy(
                         element => (string?)element.Attribute("Include"),
                         StringComparer.OrdinalIgnoreCase))
            {
                group.Add(element);
            }
        }
    }

    // 同期によって空になったラベルなしItemGroupを取り除く。
    private static void RemoveEmptyItemGroups(XElement root)
    {
        foreach (XElement group in root.Elements(MsBuild + "ItemGroup")
                     .Where(group => !group.Elements().Any() &&
                                     group.Attribute("Label") is null)
                     .ToList())
        {
            group.Remove();
        }
    }

    // 内部の項目種別をMSBuild要素名へ変換する。
    private static string GetElementName(ItemKind kind) => kind.ToString();

    // 区切り文字と大文字小文字の違いを無視してプロジェクトパスを比較する。
    private static bool PathsEqual(string? left, string right) =>
        left is not null &&
        string.Equals(
            left.Replace('/', '\\'),
            right.Replace('/', '\\'),
            StringComparison.OrdinalIgnoreCase);

    // フィルタ名から毎回同じGUIDを生成し、環境ごとの不要な差分を防ぐ。
    private static string CreateDeterministicGuid(string filter)
    {
        byte[] hash = SHA256.HashData(
            Encoding.UTF8.GetBytes($"NexusProjectTool:{filter.ToUpperInvariant()}"));
        Span<byte> guidBytes = hash.AsSpan(0, 16);
        guidBytes[6] = (byte)((guidBytes[6] & 0x0f) | 0x50);
        guidBytes[8] = (byte)((guidBytes[8] & 0x3f) | 0x80);
        return $"{{{new Guid(guidBytes).ToString().ToUpperInvariant()}}}";
    }

    // XMLを意味解析しやすい形式で読み込む。
    private static XDocument LoadDocument(string path) =>
        XDocument.Load(path);

    // filtersファイルが存在しない場合の最小XMLを生成する。
    private static XDocument CreateFiltersDocument() =>
        new(
            new XDeclaration("1.0", "utf-8", null),
            new XElement(
                MsBuild + "Project",
                new XAttribute("ToolsVersion", "4.0")));

    // 一時ファイルへ保存・再読込検証してから、元ファイルを置き換える。
    private static void SaveAtomically(XDocument document, string destination)
    {
        string tempPath = destination + ".tmp";
        var settings = new XmlWriterSettings
        {
            Encoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false),
            Indent = true,
            IndentChars = "  ",
            NewLineChars = Environment.NewLine,
            NewLineHandling = NewLineHandling.Replace
        };

        try
        {
            using (XmlWriter writer = XmlWriter.Create(tempPath, settings))
            {
                document.Save(writer);
            }

            _ = XDocument.Load(tempPath);
            File.Move(tempPath, destination, overwrite: true);
        }
        finally
        {
            if (File.Exists(tempPath))
            {
                File.Delete(tempPath);
            }
        }
    }
}
