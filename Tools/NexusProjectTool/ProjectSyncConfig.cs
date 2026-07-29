using System.Text.Json;

namespace NexusProjectTool;

internal sealed class ProjectSyncConfig
{
    public string Project { get; init; } = string.Empty;
    public List<SourceRoot> Roots { get; init; } = [];
    public List<string> ExcludeDirectories { get; init; } = [];

    // JSON設定を読み込み、同期に必要な必須項目を検証する。
    public static ProjectSyncConfig Load(string configPath)
    {
        string json = File.ReadAllText(configPath);
        ProjectSyncConfig? config = JsonSerializer.Deserialize<ProjectSyncConfig>(
            json,
            new JsonSerializerOptions
            {
                PropertyNameCaseInsensitive = true,
                ReadCommentHandling = JsonCommentHandling.Skip,
                AllowTrailingCommas = true
            });

        if (config is null || string.IsNullOrWhiteSpace(config.Project))
        {
            throw new InvalidDataException("The configuration must specify 'project'.");
        }

        if (config.Roots.Count == 0)
        {
            throw new InvalidDataException("The configuration must contain at least one source root.");
        }

        return config;
    }
}

// 物理ディレクトリとVisual Studio上の基準フィルタとの対応を表す。
internal sealed class SourceRoot
{
    public string Path { get; init; } = string.Empty;
    public string Filter { get; init; } = string.Empty;
}
