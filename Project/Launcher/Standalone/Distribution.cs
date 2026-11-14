using System.Diagnostics;
using System.IO.Compression;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace NexusLauncher;

public sealed record Release(string Id, string Version, string Configuration, string Changes, string Url, string Sha256, long Bytes)
{
    public override string ToString() => $"{(Id == "nexus-launcher" ? "Launcher" : "NexusEngine")} {Version} ({Configuration})";
}
public sealed record Catalog(int SchemaVersion, Release[] Releases);
public sealed record Settings(string CatalogUrl, string ProjectTool, string? CatalogFile = null, string? LauncherCatalogUrl = null, string LauncherVersion = "1.0.0");
public sealed record Installed(string Manifest, string Id, string Version, string Configuration, string Association = "installed")
{
    public override string ToString() => $"{Version} ({Configuration}) — {Manifest}";
}
public sealed record ProjectEntry(string File, string Name, string Version, string Configuration, string EngineId = "nexus", string Association = "installed")
{
    public override string ToString() => $"{Name}  /  {Version} ({Configuration})  /  {File}";
}

public static class Distribution
{
    public static readonly JsonSerializerOptions Json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, PropertyNameCaseInsensitive = true, WriteIndented = true };
    private static readonly HttpClient Http = new(new HttpClientHandler { AllowAutoRedirect = false }) { Timeout = Timeout.InfiniteTimeSpan };
    public static string DataRoot => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "NexusEngine", "Launcher");
    public static Settings LoadSettings() => JsonSerializer.Deserialize<Settings>(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "launcher.json")), Json) ?? throw new InvalidDataException("launcher.json が不正です。");
    public static Uri Https(string value)
    {
        if (!Uri.TryCreate(value, UriKind.Absolute, out var uri) || uri.Scheme != "https" || !string.IsNullOrEmpty(uri.UserInfo))
            throw new InvalidDataException("配布URLには認証情報を含まないHTTPS URLを指定してください。");
        return uri;
    }
    internal static async Task<HttpResponseMessage> DownloadHttps(string url, CancellationToken token, HttpClient? client = null)
    {
        var address = Https(url);
        for (var redirects = 0; ; redirects++)
        {
            var response = await (client ?? Http).GetAsync(address, HttpCompletionOption.ResponseHeadersRead, token);
            if ((int)response.StatusCode is not (301 or 302 or 303 or 307 or 308)) return response;
            try
            {
                if (redirects >= 5) throw new InvalidDataException("配布URLの転送回数が上限を超えました。");
                var location = response.Headers.Location ?? throw new InvalidDataException("配布URLの転送先がありません。");
                address = Https(new Uri(address, location).AbsoluteUri);
            }
            finally { response.Dispose(); }
        }
    }
    public static async Task<Release[]> LoadCatalog(string url, CancellationToken token, HttpClient? client = null)
    {
        using var response = await DownloadHttps(url, token, client);
        response.EnsureSuccessStatusCode();
        await using var stream = await response.Content.ReadAsStreamAsync(token);
        using var buffer = new MemoryStream();
        var block = new byte[8192]; int count;
        while ((count = await stream.ReadAsync(block, token)) != 0)
        {
            if (buffer.Length + count > 2 * 1024 * 1024) throw new InvalidDataException("バージョン一覧が大きすぎます。");
            buffer.Write(block, 0, count);
        }
        return ParseCatalog(buffer.ToArray());
    }
    public static async Task<Release[]> LoadLocalCatalog(string file, CancellationToken token)
    {
        file = Path.GetFullPath(file);
        if (new FileInfo(file).Length > 2 * 1024 * 1024) throw new InvalidDataException("バージョン一覧が大きすぎます。");
        return ParseCatalog(await File.ReadAllBytesAsync(file, token).ConfigureAwait(false), Path.GetDirectoryName(file));
    }
    static Release[] ParseCatalog(byte[] bytes, string? localRoot = null)
    {
        var offset = bytes.Length >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
        var catalog = JsonSerializer.Deserialize<Catalog>(bytes.AsSpan(offset), Json) ?? throw new InvalidDataException("一覧が不正です。");
        if (catalog.SchemaVersion != 1 || catalog.Releases is null || catalog.Releases.Length > 256) throw new InvalidDataException("未対応の一覧形式です。");
        var identities = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var r in catalog.Releases)
        {
            Validate(r, localRoot);
            if (!identities.Add($"{r.Id}/{r.Version}/{r.Configuration}")) throw new InvalidDataException("バージョン一覧が重複しています。");
        }
        return catalog.Releases;
    }
    public static void Validate(Release r, string? localRoot = null)
    {
        if (r is null) throw new InvalidDataException("配布情報が不正です。");
        foreach (var part in new[] { r.Id, r.Version })
            if (part is null || !Regex.IsMatch(part, @"\A[a-zA-Z0-9][a-zA-Z0-9._-]{0,99}\z")) throw new InvalidDataException("不正なバージョン識別子です。");
        if (r.Configuration is not ("Debug" or "Develop" or "Release") || r.Changes is null || r.Bytes <= 0 || r.Sha256 is null || !Regex.IsMatch(r.Sha256, @"\A[0-9a-fA-F]{64}\z")) throw new InvalidDataException("配布情報が不正です。");
        if (localRoot is null) Https(r.Url);
        else Within(localRoot, r.Url);
    }
    public static string Within(string root, string relative)
    {
        if (string.IsNullOrWhiteSpace(relative) || relative.Contains(':') || Path.IsPathRooted(relative)) throw new InvalidDataException("相対パスが必要です。");
        var prefix = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var path = Path.GetFullPath(Path.Combine(prefix, relative));
        if (!path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("展開先の範囲外です。");
        return path;
    }
    public static void Extract(string zip, string root, long maximumBytes, CancellationToken token)
    {
        using var archive = ZipFile.OpenRead(zip);
        if (archive.Entries.Count > 200000) throw new InvalidDataException("ZIP内のファイルが多すぎます。");
        long total = 0;
        foreach (var entry in archive.Entries)
        {
            token.ThrowIfCancellationRequested();
            var destination = Within(root, entry.FullName.Replace('/', Path.DirectorySeparatorChar));
            if (((entry.ExternalAttributes >> 16) & 0xF000) == 0xA000 || (entry.ExternalAttributes & 0x400) != 0) throw new InvalidDataException("リンクは展開できません。");
            total = checked(total + entry.Length);
            if (total > maximumBytes) throw new InvalidDataException("ZIP展開サイズの上限を超えました。");
            if (entry.FullName.EndsWith('/')) { Directory.CreateDirectory(destination); continue; }
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            using var input = entry.Open();
            using var output = new FileStream(destination, FileMode.CreateNew);
            var block = new byte[131072]; int read;
            while ((read = input.Read(block)) > 0) { token.ThrowIfCancellationRequested(); output.Write(block, 0, read); }
        }
    }
    public static async Task<string> Install(Release release, string parent, string tool, IProgress<int> progress, CancellationToken token, HttpClient? client = null, string? engineRegistry = null, string? localCatalogRoot = null)
    {
        Validate(release, localCatalogRoot);
        parent = Path.GetFullPath(parent);
        Directory.CreateDirectory(parent);
        var destination = Within(parent, $"{release.Id}-{release.Version}-{release.Configuration}");
        if (Directory.Exists(destination) || File.Exists(destination)) throw new IOException("同じ保存先が存在します。別の場所を選択してください。");
        var staging = Within(parent, ".download-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(staging);
        var archive = Path.Combine(staging, "engine.zip");
        var content = Path.Combine(staging, "content");
        try
        {
            using var response = localCatalogRoot is null ? await DownloadHttps(release.Url, token, client) : null;
            response?.EnsureSuccessStatusCode();
            await using (var source = localCatalogRoot is null ? await response!.Content.ReadAsStreamAsync(token) : File.OpenRead(Within(localCatalogRoot, release.Url)))
            await using (var output = new FileStream(archive, FileMode.CreateNew, FileAccess.Write, FileShare.None, 131072, true))
            {
                long total = 0; var block = new byte[131072]; int read;
                while ((read = await source.ReadAsync(block, token)) > 0)
                {
                    total = checked(total + read);
                    if (total > release.Bytes) throw new InvalidDataException("ZIPサイズが配布情報と一致しません。");
                    await output.WriteAsync(block.AsMemory(0, read), token);
                    progress.Report((int)(total * 100.0 / release.Bytes));
                }
                if (total != release.Bytes) throw new InvalidDataException("ダウンロードが不完全です。");
            }
            await using (var input = File.OpenRead(archive))
                if (!Convert.ToHexString(await SHA256.HashDataAsync(input, token)).Equals(release.Sha256, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("SHA-256が一致しません。");
            Directory.CreateDirectory(content);
            Extract(archive, content, 100L * 1024 * 1024 * 1024, token);
            var manifest = Path.Combine(content, "NexusEngine.nxengine");
            using (var doc = JsonDocument.Parse(await File.ReadAllTextAsync(manifest, token)))
            {
                var root = doc.RootElement;
                if (root.GetProperty("association").GetString() != "installed" || root.GetProperty("id").GetString() != release.Id || root.GetProperty("version").GetString() != release.Version || root.GetProperty("configuration").GetString() != release.Configuration) throw new InvalidDataException("エンジンの識別情報が一致しません。");
            }
            token.ThrowIfCancellationRequested();
            Directory.Move(content, destination);
            var finalManifest = Path.Combine(destination, "NexusEngine.nxengine");
            var receipt = InstallationFiles.Capture(destination, "engine", $"{release.Id}|{release.Version}|{release.Configuration}");
            InstallationFiles.Save(EngineReceipt(finalManifest, engineRegistry), receipt);
            // ネイティブツールで全ファイルのinventoryを検証し、ロックとRegistry更新を委譲する。
            try { await Run(tool, engineRegistry is null ? ["engine-register", finalManifest] : ["engine-register", finalManifest, engineRegistry], token); }
            catch (Exception ex) { throw new IOException($"展開済みですが登録できませんでした。エンジン登録から再試行してください: {finalManifest}", ex); }
            return finalManifest;
        }
        finally { Directory.Delete(staging, true); }
    }
    public static string EngineReceipt(string manifest, string? engineRegistry = null)
    {
        var folder = engineRegistry is null ? Path.Combine(DataRoot, "Installations") : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(engineRegistry))!, "LauncherInstallations");
        var key = Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(InstallationFiles.Canonical(manifest).ToUpperInvariant())));
        return Path.Combine(folder, key + ".json");
    }
    public static async Task UninstallEngine(Installed engine, string tool, string? engineRegistry, CancellationToken token)
    {
        if (engine.Association != "installed") throw new IOException("Developmentエンジンのソースは削除できません。登録のみ解除してください。");
        var manifest = Path.GetFullPath(engine.Manifest);
        var root = Path.GetDirectoryName(manifest)!;
        if (!Path.GetFileName(manifest).Equals("NexusEngine.nxengine", StringComparison.OrdinalIgnoreCase)) throw new IOException("エンジンの配置を確認できません。");
        InstallationFiles.NoLinks(root);
        var receiptFile = EngineReceipt(manifest, engineRegistry);
        OwnedInstallation receipt;
        if (File.Exists(receiptFile)) receipt = InstallationFiles.Load(receiptFile, root, "engine");
        else
        {
            // 旧ランチャーで導入した版は、専用の版別フォルダーとSDK全体のinventory検証で確認する。
            if (!Path.GetFileName(root).Equals($"{engine.Id}-{engine.Version}-{engine.Configuration}", StringComparison.OrdinalIgnoreCase))
                throw new IOException("ランチャーの版別保存先として確認できません。登録のみ解除し、実ファイルは手動で管理してください。");
            await Run(tool, ["template-list", manifest], token);
            receipt = InstallationFiles.Capture(root, "engine", $"{engine.Id}|{engine.Version}|{engine.Configuration}");
        }
        if (receipt.Identity != $"{engine.Id}|{engine.Version}|{engine.Configuration}") throw new IOException("削除対象とインストール記録が一致しません。");
        token.ThrowIfCancellationRequested();
        using var deletion = InstallationFiles.Prepare(receipt);
        // 全ファイルをロック済みのため、登録解除から削除完了までは中断しない。
        await Run(tool, engineRegistry is null ? ["engine-remove", manifest] : ["engine-remove", manifest, engineRegistry], CancellationToken.None);
        try { deletion.Delete(); }
        catch (Exception ex) { throw new IOException($"登録は解除しましたが、一部のファイルを削除できませんでした。配置を確認してください: {root}", ex); }
        if (File.Exists(receiptFile)) File.Delete(receiptFile);
    }
    public static async Task<string> Run(string executable, IEnumerable<string> arguments, CancellationToken token)
    {
        var start = new ProcessStartInfo(executable) { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true, StandardOutputEncoding = System.Text.Encoding.UTF8, StandardErrorEncoding = System.Text.Encoding.UTF8, CreateNoWindow = true };
        start.Environment["GIT_TERMINAL_PROMPT"] = "0";
        foreach (var arg in arguments) start.ArgumentList.Add(arg);
        using var process = Process.Start(start) ?? throw new IOException("プロセスを起動できません。");
        var stdout = process.StandardOutput.ReadToEndAsync(); var stderr = process.StandardError.ReadToEndAsync();
        try { await process.WaitForExitAsync(token); }
        catch (OperationCanceledException) { if (!process.HasExited) process.Kill(true); await process.WaitForExitAsync(CancellationToken.None); throw; }
        var result = await stdout; var error = await stderr;
        if (process.ExitCode != 0) throw new IOException(string.IsNullOrWhiteSpace(error) ? result : error);
        return result.Trim();
    }
}
