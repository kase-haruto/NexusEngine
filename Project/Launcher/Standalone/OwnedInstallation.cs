using System.Security.Cryptography;
using System.Text.Json;
using System.Diagnostics;

namespace NexusLauncher;

public sealed record OwnedInstallation(int SchemaVersion, string Kind, string Root, string Identity,
    Dictionary<string, string> Files, string[] Directories);

// 所有範囲・ハッシュ・リンク・ロックを全件検証してから、ファイル単位で削除する。
public static class InstallationFiles
{
    public static string Canonical(string path) => Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
    public static bool Same(string first, string second) => Canonical(first).Equals(Canonical(second), StringComparison.OrdinalIgnoreCase);
    public static void NoRunningExecutables(string root)
    {
        var prefix = Canonical(root) + Path.DirectorySeparatorChar;
        foreach (var process in Process.GetProcesses())
        {
            using (process)
            {
                string? executable;
                try { executable = process.MainModule?.FileName; }
                catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException) { continue; }
                if (executable != null && Path.GetFullPath(executable).StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                    throw new IOException($"この配置のアプリケーションが実行中です。終了してから再試行してください: {executable}");
            }
        }
    }
    public static void NoLinks(string path)
    {
        for (var current = Path.GetFullPath(path); !string.IsNullOrEmpty(current); current = Path.GetDirectoryName(current))
            if ((Directory.Exists(current) || File.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                throw new IOException($"リンクを含む配置は削除できません: {current}");
    }
    static (List<string> Files, List<string> Directories) Scan(string root)
    {
        NoLinks(root);
        var files = new List<string>(); var directories = new List<string>();
        var pending = new Stack<string>(); pending.Push(root);
        while (pending.TryPop(out var directory))
        {
            foreach (var entry in Directory.EnumerateFileSystemEntries(directory))
            {
                NoLinks(entry);
                if (Directory.Exists(entry)) { directories.Add(Path.GetRelativePath(root, entry)); pending.Push(entry); }
                else files.Add(Path.GetRelativePath(root, entry));
            }
        }
        return (files, directories);
    }
    public static OwnedInstallation Capture(string root, string kind, string identity)
    {
        root = Canonical(root);
        var tree = Scan(root);
        var files = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var relative in tree.Files)
        {
            using var stream = File.OpenRead(Distribution.Within(root, relative));
            files.Add(relative, Convert.ToHexString(SHA256.HashData(stream)));
        }
        return new(1, kind, root, identity, files, tree.Directories.ToArray());
    }
    public static void Save(string path, OwnedInstallation receipt)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try { File.WriteAllText(temporary, JsonSerializer.Serialize(receipt, Distribution.Json)); File.Move(temporary, path, false); }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
    public static OwnedInstallation Load(string path, string root, string kind)
    {
        NoLinks(path);
        var receipt = JsonSerializer.Deserialize<OwnedInstallation>(File.ReadAllText(path), Distribution.Json) ?? throw new InvalidDataException("インストール記録が不正です。");
        if (receipt.SchemaVersion != 1 || receipt.Kind != kind || !Same(receipt.Root, root) || receipt.Files is null || receipt.Directories is null || receipt.Files.Count == 0)
            throw new InvalidDataException("インストール記録と削除先が一致しません。");
        return receipt;
    }
    public static DeletionPlan Prepare(OwnedInstallation receipt, bool lockFiles = true, string? marker = null)
        => new(receipt, lockFiles, marker);

    public sealed class DeletionPlan : IDisposable
    {
        readonly List<(string Path, FileStream Stream)> files = [];
        readonly string[] directories;
        readonly string root;
        internal DeletionPlan(OwnedInstallation receipt, bool lockFiles, string? marker)
        {
            root = Canonical(receipt.Root);
            NoLinks(root);
            var expected = new Dictionary<string, string>(receipt.Files, StringComparer.OrdinalIgnoreCase);
            if (marker != null)
            {
                var path = Distribution.Within(root, marker);
                expected.Add(marker, Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))));
            }
            var allowedDirectories = new HashSet<string>(receipt.Directories, StringComparer.OrdinalIgnoreCase);
            foreach (var relative in expected.Keys.Concat(allowedDirectories)) Distribution.Within(root, relative);
            var tree = Scan(root);
            if (tree.Files.Any(file => !expected.ContainsKey(file)) || tree.Directories.Any(dir => !allowedDirectories.Contains(dir)))
                throw new IOException("追加されたファイルやフォルダーがあります。削除対象外のデータを移動してから再試行してください。");
            directories = tree.Directories.Select(dir => Distribution.Within(root, dir)).OrderByDescending(path => path.Length).ToArray();
            try
            {
                foreach (var relative in tree.Files)
                {
                    var path = Distribution.Within(root, relative);
                    if ((File.GetAttributes(path) & FileAttributes.ReadOnly) != 0) throw new IOException($"読み取り専用ファイルがあります: {path}");
                    var stream = new FileStream(path, FileMode.Open, FileAccess.Read, lockFiles ? FileShare.Delete : FileShare.ReadWrite | FileShare.Delete);
                    files.Add((path, stream));
                    if (!Convert.ToHexString(SHA256.HashData(stream)).Equals(expected[relative], StringComparison.OrdinalIgnoreCase))
                        throw new IOException($"インストール後に変更されたファイルを保護しました: {path}");
                }
                // ファイルのロックだけでは検出できない、実行中のEXEのメモリー配置も確認する。
                if (lockFiles) NoRunningExecutables(root);
            }
            catch { Dispose(); throw; }
        }
        public void Delete()
        {
            NoLinks(root);
            foreach (var file in files) { NoLinks(file.Path); File.Delete(file.Path); }
            Dispose();
            foreach (var directory in directories) Directory.Delete(directory, false);
            Directory.Delete(root, false);
        }
        public void Dispose() { foreach (var file in files) file.Stream.Dispose(); files.Clear(); }
    }
}
