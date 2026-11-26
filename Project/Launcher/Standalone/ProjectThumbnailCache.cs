using System.Text.Json;

namespace NexusLauncher;

/*-----------------------------------------------------------------------------------------
 * ProjectPreviewMetadata
 * - Editorと共有する画像・テンプレートの配置と、Launcherの起動日時を解決する
 *---------------------------------------------------------------------------------------*/
internal static class ProjectPreviewMetadata
{
    static string DirectoryFor(ProjectEntry project) =>
        Path.Combine(Path.GetDirectoryName(project.File)!, ".nexus-preview");

    internal static string ProjectImage(ProjectEntry project) =>
        Path.Combine(DirectoryFor(project), "ProjectThumbnail.png");

    /** \brief 保存済みのテンプレートIDとエンジンから共通キャッシュを解決する */
    internal static string? DefaultImage(ProjectEntry project)
    {
        if (!SafeSegment(project.TemplateId) || !SafeSegment(project.EngineId) || !SafeSegment(Version(project), version: true))
            return null;
        return Path.Combine(Distribution.LocalAppData,
            "NexusEngine", "Projects", "Thumbnails", project.EngineId, Version(project),
            $"ProjectThumbnail_{project.TemplateId}.png");
    }

    static string Version(ProjectEntry project) => string.IsNullOrEmpty(project.Version) ? "development" : project.Version;
    static bool SafeSegment(string? value, bool version = false) => !string.IsNullOrEmpty(value) && value[^1] != '.' &&
        value.All(c => char.IsAsciiLetterOrDigit(c) || c is '-' or '_' or '.' || (version && c == '+'));

    /** \brief 旧プロジェクトや壊れたメタデータでは取得できる項目だけを使う */
    internal static ProjectEntry Read(ProjectEntry project)
    {
        var template = ReadValue(Path.Combine(DirectoryFor(project), "Template.json"), root =>
            root.TryGetProperty("id", out var id) ? id.GetString() : null);
        var languages = ReadValue(project.File, root => root.TryGetProperty("modules", out var modules)
            ? string.Join(" / ", modules.EnumerateArray().Select(module =>
                module.GetProperty("language").GetString() switch { "Cpp" => "C++", "CSharp" => "C#", _ => "" })
                .Where(language => language.Length != 0).Distinct())
            : null);
        return project with { TemplateId = SafeSegment(template) ? template : null, Languages = languages, LastLaunch = ReadLaunch(project) };
    }

    /** \brief 配布済みテンプレート画像の相対参照を解決する */
    internal static string? BundledImage(ProjectEntry project)
    {
        if (!SafeSegment(project.TemplateId))
            return null;
        var root = ProjectCreationService.TemplatesRoot;
        return ReadValue(Path.Combine(root, $"{project.TemplateId}.nxtemplate"), json =>
        {
            if (!json.TryGetProperty("preview", out var preview) ||
                preview.GetProperty("kind").GetString() is not ("image" or "thumbnail"))
                return null;
            return Distribution.Within(root, preview.GetProperty("source").GetString() ?? "");
        });
    }

    static string? ReadValue(string file, Func<JsonElement, string?> read)
    {
        try
        {
            var info = new FileInfo(file);
            if (!info.Exists || info.Length > 1024 * 1024)
                return null;
            using var stream = File.Open(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            using var json = JsonDocument.Parse(stream);
            return read(json.RootElement);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or
            InvalidOperationException or ArgumentException or KeyNotFoundException)
        {
            return null;
        }
    }

    static DateTimeOffset? ReadLaunch(ProjectEntry project)
    {
        try
        {
            var file = new FileInfo(Path.Combine(DirectoryFor(project), "LastLaunch.txt"));
            return file.Exists && file.Length <= 64 && DateTimeOffset.TryParseExact(File.ReadAllText(file.FullName),
                "O", System.Globalization.CultureInfo.InvariantCulture, System.Globalization.DateTimeStyles.None, out var date)
                ? date : null;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    /** \brief 登録日時と混同しないよう、Editor起動成功時にだけ記録する */
    internal static DateTimeOffset RecordLaunch(ProjectEntry project)
    {
        var now = DateTimeOffset.UtcNow;
        var directory = DirectoryFor(project);
        var temporary = Path.Combine(directory, $"{Guid.NewGuid():N}.tmp");
        try
        {
            Directory.CreateDirectory(directory);
            File.WriteAllText(temporary, now.ToString("O", System.Globalization.CultureInfo.InvariantCulture));
            File.Move(temporary, Path.Combine(directory, "LastLaunch.txt"), overwrite: true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            try { File.Delete(temporary); }
            catch (Exception cleanup) when (cleanup is IOException or UnauthorizedAccessException) { }
        }
        return now;
    }
}

/*-----------------------------------------------------------------------------------------
 * ProjectThumbnailCache
 * - 可視カードから遅延取得し、更新・欠落・破損を検知する
 * - 表示用Bitmapを所有し、置換・一覧除外・画面破棄時に解放する
 *---------------------------------------------------------------------------------------*/
internal sealed class ProjectThumbnailCache : IDisposable
{
    readonly Dictionary<string, Entry> images = new(StringComparer.OrdinalIgnoreCase);
    sealed record Entry(long Size, DateTime Modified, DateTime Created, Image? Bitmap);

    /** \brief 先頭の正常な候補を表示し、なければカード側でアイコンを描く */
    internal Image? Find(ProjectEntry project, string? bundled) =>
        Load(ProjectPreviewMetadata.ProjectImage(project)) ??
        Load(ProjectPreviewMetadata.DefaultImage(project)) ??
        Load(bundled) ??
        Load(Path.Combine(AppContext.BaseDirectory, "PreviewAssets", "ProjectThumbnail_Default.png"));

    internal Image? Icon => Load(Path.Combine(AppContext.BaseDirectory, "PreviewAssets", "project.png"));

    /** \brief 一覧更新後に不要な画像とファイル状態を捨てる */
    internal void Reset()
    {
        foreach (var entry in images.Values)
            entry.Bitmap?.Dispose();
        images.Clear();
    }

    Image? Load(string? path)
    {
        if (path is null)
            return null;
        try
        {
            var file = new FileInfo(path);
            images.TryGetValue(path, out var previous);
            if (!file.Exists)
            {
                previous?.Bitmap?.Dispose();
                images.Remove(path);
                return null;
            }
            if (previous is not null && previous.Size == file.Length && previous.Modified == file.LastWriteTimeUtc &&
                previous.Created == file.CreationTimeUtc)
                return previous.Bitmap;

            var image = ProjectTemplatePreview.LoadFile(file.FullName);
            previous?.Bitmap?.Dispose();
            images[path] = new(file.Length, file.LastWriteTimeUtc, file.CreationTimeUtc, image);
            return image;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
        {
            return null;
        }
    }

    public void Dispose() => Reset();
}
