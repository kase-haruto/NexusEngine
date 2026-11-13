using System.Text.Json;

namespace NexusLauncher;

public static class ProjectRemoval
{
    public static string Validate(ProjectEntry project, IEnumerable<ProjectEntry> projects, IEnumerable<string> protectedDirectories)
    {
        var file = Path.GetFullPath(project.File);
        var root = Path.GetDirectoryName(file)!;
        bool Contains(string parent, string child) => child.Equals(parent, StringComparison.OrdinalIgnoreCase) || child.StartsWith(Path.TrimEndingDirectorySeparator(parent) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
        void Reject() => throw new InvalidDataException("この場所はフォルダーごと削除できません。「一覧から外す」を使用してください。");
        if (!File.Exists(file) || !Path.GetExtension(file).Equals(".nxproject", StringComparison.OrdinalIgnoreCase)) Reject();
        using var descriptor = JsonDocument.Parse(File.ReadAllText(file));
        if (descriptor.RootElement.ValueKind != JsonValueKind.Object || !descriptor.RootElement.TryGetProperty("engine", out _)) Reject();
        var special = new[] { Environment.SpecialFolder.UserProfile, Environment.SpecialFolder.DesktopDirectory, Environment.SpecialFolder.MyDocuments, Environment.SpecialFolder.LocalApplicationData, Environment.SpecialFolder.ApplicationData, Environment.SpecialFolder.Windows, Environment.SpecialFolder.ProgramFiles, Environment.SpecialFolder.ProgramFilesX86 };
        foreach (var path in protectedDirectories.Where(p => !string.IsNullOrWhiteSpace(p)))
            if (Contains(Path.GetFullPath(path), root)) Reject();
        foreach (var folder in new[] { Environment.SpecialFolder.Windows, Environment.SpecialFolder.ProgramFiles, Environment.SpecialFolder.ProgramFilesX86 })
        {
            var path = Environment.GetFolderPath(folder);
            if (path.Length > 0 && Contains(Path.GetFullPath(path), root)) Reject();
        }
        var protectedPaths = protectedDirectories.Concat(special.Select(Environment.GetFolderPath)).Append(Path.GetPathRoot(root)!);
        foreach (var path in protectedPaths.Where(p => !string.IsNullOrWhiteSpace(p))) if (Contains(root, Path.GetFullPath(path))) Reject();
        foreach (var p in projects) if (!Path.GetFullPath(p.File).Equals(file, StringComparison.OrdinalIgnoreCase) && Contains(root, Path.GetFullPath(p.File))) Reject();
        // ジャンクションとリンクをたどらない。共有フォルダー内の別プロジェクトも保護。
        for (var ancestor = new DirectoryInfo(root); ancestor != null; ancestor = ancestor.Parent)
            if ((ancestor.Attributes & FileAttributes.ReparsePoint) != 0) Reject();
        var pending = new Stack<string>(); pending.Push(root);
        while (pending.TryPop(out var directory))
        {
            foreach (var path in Directory.EnumerateFileSystemEntries(directory))
            {
                var attributes = File.GetAttributes(path);
                if ((attributes & FileAttributes.ReparsePoint) != 0) Reject();
                if ((attributes & FileAttributes.Directory) != 0) pending.Push(path);
                else if (Path.GetExtension(path).Equals(".nxengine", StringComparison.OrdinalIgnoreCase) || (Path.GetExtension(path).Equals(".nxproject", StringComparison.OrdinalIgnoreCase) && !path.Equals(file, StringComparison.OrdinalIgnoreCase))) Reject();
            }
        }
        return root;
    }
}
