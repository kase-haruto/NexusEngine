using System.Text.Json;

namespace NexusLauncher;

/*-----------------------------------------------------------------------------------------
 * ProjectTemplateDescriptor / ProjectCreationService
 * - CLIが検証したTemplate Metadataを表示用データとして保持する
 * - Engine解決・Template展開は既存Project Toolに委譲し、Sceneを生成しない
 *---------------------------------------------------------------------------------------*/
internal sealed record ProjectTemplateDescriptor(string Id, string Name, string Description,
    string PreviewKind, string PreviewImage, string InitialLevel, string Error)
{
    public bool CanCreate => string.IsNullOrEmpty(Error) && !string.IsNullOrWhiteSpace(Id);
}
internal sealed record ProjectTemplateCatalog(int SchemaVersion, ProjectTemplateDescriptor[] Templates);

internal static class ProjectCreationService
{
    internal static string TemplatesRoot => Path.Combine(AppContext.BaseDirectory, "Templates");
    /** \brief 選択Engineの検証済みTemplate一覧を取得する。旧Toolの未対応を空Templateで隠さない */
    internal static async Task<ProjectTemplateDescriptor[]> Templates(string tool, Installed engine, CancellationToken token)
    {
        var json = await Distribution.Run(tool, ["template-catalog", engine.Manifest, TemplatesRoot], token);
        var catalog = JsonSerializer.Deserialize<ProjectTemplateCatalog>(json, Distribution.Json);
        if (catalog is not { SchemaVersion: 1, Templates: not null })
            throw new InvalidDataException("Template情報を取得できません。対応するProject Toolを使用してください。");
        return catalog.Templates;
    }

    /** \brief Project生成だけを実行する。登録先やUIの選択状態は所有しない */
    internal static Task<string> Create(string tool, Installed engine, ProjectTemplateDescriptor template,
        string parent, string name, CancellationToken token)
    {
        if (!template.CanCreate) throw new IOException("このTemplateでは作成できません。" + template.Error);
        return Distribution.Run(tool, ["create-project", Target(parent, name), name.Trim(), engine.Manifest, TemplatesRoot, template.Id], token);
    }

    internal static string Target(string parent, string name)
    {
        name = name.Trim();
        if (name.Length == 0 || name is "." or ".." || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || name.EndsWith('.'))
            throw new IOException("フォルダー名が不正です。");
        if (string.IsNullOrWhiteSpace(parent) || !Directory.Exists(parent)) throw new IOException("保存先の親フォルダーが見つかりません。");
        var target = Distribution.Within(Path.GetFullPath(parent), name);
        if (Directory.Exists(target) || File.Exists(target)) throw new IOException("保存先が既に存在します。");
        return target;
    }
}

/*-----------------------------------------------------------------------------------------
 * ProjectTemplatePreview
 * - MetadataのSource種別に応じて画像を読み込み、画像の寿命を呼出元へ渡す
 * - 将来のthumbnail SourceもUIカードやProject生成処理を変更せずに追加できる
 *---------------------------------------------------------------------------------------*/
internal static class ProjectTemplatePreview
{
    /** \brief 欠落・破損・未対応Sourceではnullを返し、Preview unavailableへFallbackする */
    internal static Image? Load(ProjectTemplateDescriptor template)
    {
        return template.PreviewKind is "image" or "thumbnail" ? LoadFile(template.PreviewImage) : null;
    }

    /** \brief TemplateカードとProjectカードで画像の読込み・検証を共有する */
    internal static Image? LoadFile(string path)
    {
        try
        {
            var file = new FileInfo(path);
            if (!file.Exists || file.Length > 8 * 1024 * 1024) return null;
            using var stream = File.Open(file.FullName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            using var image = Image.FromStream(stream, useEmbeddedColorManagement: true, validateImageData: true);
            if (image.Width > 4096 || image.Height > 4096) return null;
            // ファイルハンドルとGDI+の遅延読込をUIの寿命へ持ち込まない。
            return new Bitmap(image);
        }
        catch (Exception ex) when (ex is IOException or ArgumentException or OutOfMemoryException or UnauthorizedAccessException or System.Runtime.InteropServices.ExternalException)
        {
            return null;
        }
    }
}
