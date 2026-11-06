using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using NexusLauncher;

internal static class Program
{
[STAThread]
static void Main()
{
ApplicationConfiguration.Initialize();
using var form = new Form { Text = "NexusEngine Launcher Setup", Width = 700, Height = 340, StartPosition = FormStartPosition.CenterScreen, Font = new("Yu Gothic UI", 10), FormBorderStyle = FormBorderStyle.FixedDialog, MaximizeBox = false };
var title = new Label { Text = "NEXUS  /  LAUNCHER SETUP", Left = 24, Top = 20, Width = 630, Height = 42, Font = new("Segoe UI", 18, FontStyle.Bold) };
var label = new Label { Text = "ランチャーをインストールします。エンジンは起動後に選択できます。", Left = 24, Top = 76, Width = 630, Height = 40 };
var destination = new TextBox { Left = 24, Top = 124, Width = 630, ReadOnly = true, Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "NexusEngineLauncher") };
var button = new Button { Text = "インストール", Left = 24, Top = 174, Width = 180, Height = 40 };
var result = new Label { Left = 24, Top = 230, Width = 630, Height = 50 };
form.Controls.AddRange([title, label, destination, button, result]);
LauncherTheme.Apply(form); button.BackColor = LauncherTheme.Accent; button.ForeColor = Color.Black;
bool installing = false;
form.FormClosing += (_, e) => e.Cancel = installing;
button.Click += async (_, _) =>
{
    button.Enabled = false; installing = true;
    using var lifecycleLock = new Mutex(false, @"Local\NexusEngine.LauncherLifecycle");
    bool locked = false;
    try
    {
        locked = lifecycleLock.WaitOne(0);
        if (!locked) throw new IOException("別のインストールまたはアンインストールが実行中です。完了後に再試行してください。");
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip") ?? throw new IOException("配布データがありません。Build-Distribution.ps1からビルドしてください。");
        var root = destination.Text;
        // 新しい版別フォルダーに配置し、実行中の版と以前の版を保持する。
        Directory.CreateDirectory(root);
        var version = Path.Combine(root, Guid.NewGuid().ToString("N")); Directory.CreateDirectory(version);
        using (var archive = new ZipArchive(stream))
        {
            foreach (var entry in archive.Entries)
            {
                var path = Distribution.Within(version, entry.FullName.Replace('/', Path.DirectorySeparatorChar));
                if (entry.FullName.EndsWith('/')) { Directory.CreateDirectory(path); continue; }
                Directory.CreateDirectory(Path.GetDirectoryName(path)!); entry.ExtractToFile(path, false);
            }
        }
        var launcher = Path.Combine(version, "NexusLauncher.exe");
        if (!File.Exists(launcher)) throw new IOException("ランチャーが含まれていません。");
        LauncherInstallation.Register(version);
        // ユーザー単位のショートカットを作成し、管理者権限やマシン全体の変更を必要としない。
        var shellType = Type.GetTypeFromProgID("WScript.Shell") ?? throw new IOException("ショートカットを作成できません。");
        dynamic shell = Activator.CreateInstance(shellType)!;
        var menu = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", "NexusEngine Launcher.lnk");
        dynamic shortcut = shell.CreateShortcut(menu); shortcut.TargetPath = launcher; shortcut.WorkingDirectory = version; shortcut.Save();
        var desktop = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "NexusEngine Launcher.lnk");
        dynamic desktopShortcut = shell.CreateShortcut(desktop); desktopShortcut.TargetPath = launcher; desktopShortcut.WorkingDirectory = version; desktopShortcut.Save();
        result.Text = "インストールが完了しました。ランチャーを起動します。";
        Process.Start(new ProcessStartInfo(launcher) { UseShellExecute = true, WorkingDirectory = version });
        await Task.Delay(500); installing = false; form.Close();
    }
    catch (Exception ex) { result.Text = ex.Message; button.Enabled = true; }
    finally { if (locked) lifecycleLock.ReleaseMutex(); installing = false; }
};
Application.Run(form);
}
}
