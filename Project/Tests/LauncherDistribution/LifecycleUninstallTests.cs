using Microsoft.Win32;
using NexusLauncher;
using System.Text.Json;

internal static class LifecycleUninstallTests
{
    public static async Task<int> Run(string testRoot)
    {
        int passed = 0;
        var installBase = Path.Combine(testRoot, "LauncherApps");
        var old = Path.Combine(installBase, Guid.NewGuid().ToString("N"));
        var newer = Path.Combine(installBase, Guid.NewGuid().ToString("N"));
        var shortcuts = Path.Combine(testRoot, "Desktop"); Directory.CreateDirectory(shortcuts);
        var link = Path.Combine(shortcuts, "NexusEngine Launcher.lnk");
        var registryKey = @"Software\NexusEngine\UninstallTests\" + Guid.NewGuid().ToString("N");
        var helper = Path.Combine(AppContext.BaseDirectory, "Remove-Launcher.ps1");
        var powershell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell/v1.0/powershell.exe");
        void Install(string root)
        {
            Directory.CreateDirectory(root); Directory.CreateDirectory(Path.Combine(root, "Tools"));
            File.WriteAllText(Path.Combine(root, "NexusLauncher.exe"), "test executable");
            File.Copy(typeof(LauncherInstallation).Assembly.Location, Path.Combine(root, "NexusLauncher.dll"));
            File.WriteAllText(Path.Combine(root, "launcher.json"), "{}");
            File.WriteAllText(Path.Combine(root, "Tools/tool.dll"), "test DLL");
            File.Copy(helper, Path.Combine(root, "Remove-Launcher.ps1"));
            LauncherInstallation.Register(root, registryKey, installBase);
        }
        Task Remove(string root) => Distribution.Run(powershell,
            ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", helper, "-Root", root, "-InstallationBase", installBase,
             "-RegistryKey", registryKey, "-ShortcutDirectories", shortcuts, "-NoUI"], default);
        async Task Reject(string root)
        {
            try { await Remove(root); throw new Exception("Unsafe launcher uninstall accepted"); }
            catch (IOException) { }
            if (!File.Exists(Path.Combine(old, "NexusLauncher.exe"))) throw new Exception("Failed uninstall deleted the application");
            using var key = Registry.CurrentUser.OpenSubKey(registryKey);
            if (key is null) throw new Exception("Failed uninstall removed registration");
            passed++;
        }
        try
        {
            Install(old);
            using (var key = Registry.CurrentUser.OpenSubKey(registryKey)!)
                if ((string?)key.GetValue("InstallLocation") != old || !((string?)key.GetValue("UninstallString"))!.EndsWith(" --uninstall")) throw new Exception("Windows uninstall registration is incorrect");
            passed++;
            var extra = Path.Combine(old, "MyGame.nxproject"); File.WriteAllText(extra, "user project");
            await Reject(old); File.Delete(extra);
            var binary = Path.Combine(old, "NexusLauncher.exe"); File.AppendAllText(binary, "modified");
            await Reject(old); File.WriteAllText(binary, "test executable");
            using (var held = new FileStream(binary, FileMode.Open, FileAccess.Read, FileShare.Read)) await Reject(old);
            var marker = Path.Combine(old, LauncherInstallation.Marker);
            var original = File.ReadAllText(marker);
            var receipt = JsonSerializer.Deserialize<OwnedInstallation>(original, Distribution.Json)!;
            var outside = Path.Combine(testRoot, "OutsideProject.nxproject"); File.WriteAllText(outside, "keep");
            var badFiles = new Dictionary<string, string>(receipt.Files) { ["../OutsideProject.nxproject"] = new string('0', 64) };
            File.WriteAllText(marker, JsonSerializer.Serialize(receipt with { Files = badFiles }, Distribution.Json));
            await Reject(old); File.WriteAllText(marker, original);
            if (File.ReadAllText(outside) != "keep") throw new Exception("Outside project changed"); passed++;
            File.WriteAllText(marker, JsonSerializer.Serialize(receipt with { Root = testRoot }, Distribution.Json));
            await Reject(old); File.WriteAllText(marker, original);
            var outsideDirectory = Path.Combine(testRoot, "OutsideGame"); Directory.CreateDirectory(outsideDirectory);
            File.WriteAllText(Path.Combine(outsideDirectory, "Keep.nxproject"), "keep");
            var junction = Path.Combine(old, "ExternalGame");
            var createJunction = Path.Combine(testRoot, "Create-TestJunction.ps1");
            File.WriteAllText(createJunction, "param($Link,$Target)\nNew-Item -ItemType Junction -Path $Link -Target $Target | Out-Null\n");
            await Distribution.Run(powershell, ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", createJunction, "-Link", junction, "-Target", outsideDirectory], default);
            await Reject(old);
            try { using var plan = InstallationFiles.Prepare(receipt, true, LauncherInstallation.Marker); throw new Exception("Junction accepted by native deletion preflight"); }
            catch (IOException) { passed++; }
            Directory.Delete(junction, false);
            if (!File.Exists(Path.Combine(outsideDirectory, "Keep.nxproject"))) throw new Exception("Junction target deleted");
            // 新版のアプリ登録とショートカットを、旧版の削除で消さないことを検証する。
            Install(newer);
            var createLink = Path.Combine(testRoot, "Create-TestShortcut.ps1");
            File.WriteAllText(createLink, "param($Target,$Destination)\n$shell=New-Object -ComObject WScript.Shell\n$link=$shell.CreateShortcut($Destination)\n$link.TargetPath=$Target\n$link.Save()\n");
            await Distribution.Run(powershell, ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", createLink, "-Target", Path.Combine(newer, "NexusLauncher.exe"), "-Destination", link], default);
            await Remove(old);
            using (var key = Registry.CurrentUser.OpenSubKey(registryKey))
                if (Directory.Exists(old) || !Directory.Exists(newer) || !File.Exists(link) || (string?)key?.GetValue("InstallLocation") != newer) throw new Exception("Old-version uninstall affected the new version");
            passed++;
            await Remove(newer);
            using (var key = Registry.CurrentUser.OpenSubKey(registryKey))
                if (Directory.Exists(newer) || File.Exists(link) || key is not null || !File.Exists(outside)) throw new Exception("Launcher uninstall left files/registration or removed a project");
            passed++;
            return passed;
        }
        finally { Registry.CurrentUser.DeleteSubKeyTree(registryKey, false); }
    }
}
