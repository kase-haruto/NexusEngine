using System.Diagnostics;
using System.Security.Cryptography;

namespace NexusLauncher;

// エンジンの一覧と同じ形式を使い、検証済みのSetupだけを実行する。
public static class LauncherUpdates
{
    public static string CatalogUrl(Settings settings)
    {
        if (!string.IsNullOrWhiteSpace(settings.LauncherCatalogUrl))
            return Distribution.Https(settings.LauncherCatalogUrl).AbsoluteUri;
        return new Uri(Distribution.Https(settings.CatalogUrl), "launcher-releases.json").AbsoluteUri;
    }

    public static async Task<string> Download(Release release, string parent, IProgress<int> progress,
        CancellationToken token, HttpClient? client = null)
    {
        Distribution.Validate(release);
        if (release.Id != "nexus-launcher" || release.Configuration != "Release" || release.Bytes > 2L * 1024 * 1024 * 1024)
            throw new InvalidDataException("ランチャーの配布情報が不正です。");
        var directory = Path.Combine(Path.GetFullPath(parent), Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        var partial = Path.Combine(directory, "launcherSetup.download");
        var installer = Path.Combine(directory, "launcherSetup.exe");
        try
        {
            using var response = await Distribution.DownloadHttps(release.Url, token, client);
            response.EnsureSuccessStatusCode();
            await using (var source = await response.Content.ReadAsStreamAsync(token))
            await using (var output = new FileStream(partial, FileMode.CreateNew, FileAccess.Write, FileShare.None, 131072, true))
            {
                long total = 0;
                var block = new byte[131072];
                int read;
                while ((read = await source.ReadAsync(block, token)) != 0)
                {
                    total = checked(total + read);
                    if (total > release.Bytes) throw new InvalidDataException("Setupサイズが配布情報と一致しません。");
                    await output.WriteAsync(block.AsMemory(0, read), token);
                    progress.Report((int)(total * 100.0 / release.Bytes));
                }
                if (total != release.Bytes) throw new InvalidDataException("Setupのダウンロードが不完全です。");
            }
            await using (var input = File.OpenRead(partial))
                if (!Convert.ToHexString(await SHA256.HashDataAsync(input, token)).Equals(release.Sha256, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("SetupのSHA-256が一致しません。");
            token.ThrowIfCancellationRequested();
            File.Move(partial, installer);
            return installer;
        }
        catch
        {
            Directory.Delete(directory, true);
            throw;
        }
    }

    public static void StartInstaller(string installer)
    {
        // Setupは版別フォルダーへ配置する。起動元の終了を待ち、単一起動の競合を避ける。
        using var process = Process.Start(new ProcessStartInfo(installer) { UseShellExecute = true,
            WorkingDirectory = Path.GetDirectoryName(installer)!, ArgumentList = { "--wait-for", Environment.ProcessId.ToString() } })
            ?? throw new IOException("ランチャーのセットアップを起動できません。");
    }
}
