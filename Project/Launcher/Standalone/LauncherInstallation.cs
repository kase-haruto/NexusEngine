using System.Diagnostics;
using Microsoft.Win32;

namespace NexusLauncher;

public static class LauncherInstallation
{
    public const string Marker = ".nexus-launcher-install.json";
    public const string UninstallKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\NexusEngineLauncher";
    public static string InstallationBase => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "NexusEngineLauncher");
    public static void ValidateRoot(string root, string? installationBase = null)
    {
        root = InstallationFiles.Canonical(root);
        if (!InstallationFiles.Same(Path.GetDirectoryName(root)!, installationBase ?? InstallationBase) || !Guid.TryParseExact(Path.GetFileName(root), "N", out _))
            throw new IOException("セットアップでインストールしたランチャー配置だけを削除できます。");
        InstallationFiles.NoLinks(root);
    }
    public static void Register(string root, string registryKey = UninstallKey, string? installationBase = null)
    {
        ValidateRoot(root, installationBase);
        if (!File.Exists(Path.Combine(root, "NexusLauncher.exe")) || !File.Exists(Path.Combine(root, "Remove-Launcher.ps1")))
            throw new IOException("ランチャーまたはアンインストール処理が不足しています。");
        var receipt = InstallationFiles.Capture(root, "launcher", Path.GetFileName(root));
        InstallationFiles.Save(Path.Combine(root, Marker), receipt);
        using var key = Registry.CurrentUser.CreateSubKey(registryKey);
        key.SetValue("DisplayName", "NexusEngine Launcher");
        key.SetValue("DisplayVersion", FileVersionInfo.GetVersionInfo(Path.Combine(root, "NexusLauncher.dll")).ProductVersion?.Split('+')[0] ?? "1.0.0");
        key.SetValue("Publisher", "NexusEngine");
        key.SetValue("InstallLocation", InstallationFiles.Canonical(root));
        key.SetValue("DisplayIcon", Path.Combine(root, "NexusLauncher.exe"));
        key.SetValue("UninstallString", $"\"{Path.Combine(root, "NexusLauncher.exe")}\" --uninstall");
        key.SetValue("InstallDate", DateTime.Now.ToString("yyyyMMdd"));
        key.SetValue("NoModify", 1, RegistryValueKind.DWord);
        key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
        key.SetValue("EstimatedSize", (int)Math.Min(int.MaxValue, receipt.Files.Keys.Sum(file => new FileInfo(Distribution.Within(root, file)).Length) / 1024), RegistryValueKind.DWord);
    }
    public static bool CanUninstall(string root)
    {
        try { ValidateRoot(root); return File.Exists(Path.Combine(root, Marker)); }
        catch (IOException) { return false; }
    }
    public static void ConfirmAndUninstall(IWin32Window? owner = null)
    {
        try
        {
            var root = InstallationFiles.Canonical(AppContext.BaseDirectory);
            ValidateRoot(root);
            var receipt = InstallationFiles.Load(Path.Combine(root, Marker), root, "launcher");
            if (receipt.Identity != Path.GetFileName(root)) throw new IOException("インストールIDが一致しません。");
            if (MessageBox.Show(owner, $"このランチャーをアンインストールしますか？\r\n\r\n{root}\r\n\r\nエンジン、プロジェクト、登録履歴は残ります。\r\nランチャー終了後に、この配置のファイルと対応するショートカットを削除します。", "NexusEngine Launcherのアンインストール", MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) != DialogResult.Yes) return;
            using (InstallationFiles.Prepare(receipt, false, Marker)) { }
            var job = Path.Combine(Path.GetTempPath(), "NexusLauncherRemoval", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(job);
            var script = Path.Combine(job, "Remove-Launcher.ps1");
            File.Copy(Path.Combine(root, "Remove-Launcher.ps1"), script, false);
            var start = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell/v1.0/powershell.exe"))
            {
                UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = job
            };
            foreach (var argument in new[] { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script, "-Root", root, "-ParentPid", Environment.ProcessId.ToString() }) start.ArgumentList.Add(argument);
            using var helper = Process.Start(start) ?? throw new IOException("アンインストール処理を開始できません。");
            Application.Exit();
        }
        catch (Exception ex) { MessageBox.Show(owner, ex.Message, "アンインストールできませんでした", MessageBoxButtons.OK, MessageBoxIcon.Error); }
    }
}
