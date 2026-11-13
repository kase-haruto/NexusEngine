using System.Text.Json;

namespace NexusLauncher;

internal static class Program
{
    [STAThread] static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        using var instance = new Mutex(true, @"Local\NexusEngine.DistributionLauncher", out var first);
        if (!first) { MessageBox.Show(args.Contains("--uninstall") ? "起動中のランチャーを終了してから、アンインストールを再実行してください。" : "ランチャーは既に起動しています。"); return; }
        try
        {
            if (args.Length == 1 && args[0] == "--uninstall") { LauncherInstallation.ConfirmAndUninstall(); return; }
            if (args.Length is 2 or 3 && args[0] == "--preview")
            {
                using var preview = new LauncherForm(true);
                preview.SavePreviews(args[1], args.Length == 3 ? args[2] : null); return;
            }
            Application.Run(new LauncherForm());
        }
        catch (Exception ex) { MessageBox.Show(ex.Message, "NexusEngine Launcher"); }
    }
}

public sealed class LauncherForm : Form
{
    readonly Settings settings = Distribution.LoadSettings();
    readonly Panel tabs = new() { Dock = DockStyle.Fill };
    readonly FlowLayoutPanel navigation = new() { Dock = DockStyle.Left, Width = 220, FlowDirection = FlowDirection.TopDown, WrapContents = false, Padding = new(16, 24, 16, 16) };
    readonly List<(Panel Page, Button Navigation)> pages = [];
    readonly Label pageTitle = new() { Dock = DockStyle.Top, Height = 90, Padding = new(30, 28, 0, 0) };
    readonly Label source = new() { Height = 56 };
    readonly Label installTarget = new() { Height = 44 };
    readonly ListBox inventory = new() { Height = 300 };
    readonly Label status = new() { Dock = DockStyle.Fill, Padding = new(24, 12, 12, 0), AutoEllipsis = true, Text = "準備完了" };
    readonly ProgressBar progress = new() { Dock = DockStyle.Bottom, Height = 8 };
    readonly Button cancel = new() { Text = "キャンセル", Dock = DockStyle.Right, Width = 140, Enabled = false };
    readonly ListBox releases = new() { Width = 720, Height = 156 };
    readonly TextBox changes = new() { Multiline = true, ReadOnly = true, Width = 720, Height = 130, ScrollBars = ScrollBars.Vertical };
    readonly TextBox location = new() { Width = 600, Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "NexusEngine", "Versions") };
    readonly TextBox catalog = new() { Width = 720 };
    readonly ComboBox engines = new() { Width = 720, DropDownStyle = ComboBoxStyle.DropDownList };
    readonly ProjectBrowser projects = new();
    readonly List<Installed> installed = [];
    readonly List<ProjectEntry> entries = [];
    string? localCatalogRoot;
    Control? downloadHeader, downloadVersions, catalogFetch;
    CancellationTokenSource? operation;
    string Tool => Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, settings.ProjectTool));
    string EngineRegistry => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "NexusEngine", "Engines", "Registry.json");
    string ProjectRegistry => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "NexusEngine", "Projects", "RecentProjects.json");
    public LauncherForm(bool preview = false)
    {
        Text = "NexusEngine Launcher"; ClientSize = new(1180, 820); MinimumSize = new(900, 640);
        StartPosition = FormStartPosition.CenterScreen; AutoScaleMode = AutoScaleMode.Dpi;
        Font = new("Yu Gothic UI", 10);
        var footer = new Panel { Dock = DockStyle.Bottom, Height = 48 };
        footer.Controls.Add(status); footer.Controls.Add(cancel);
        Controls.Add(tabs); Controls.Add(pageTitle); Controls.Add(navigation); Controls.Add(progress); Controls.Add(footer);
        navigation.Controls.Add(new Label { Text = "NEXUS", Font = new("Segoe UI", 24, FontStyle.Bold), Width = 184, Height = 46 });
        navigation.Controls.Add(new Label { Text = "ENGINE LAUNCHER", Width = 184, Height = 56 });
        pageTitle.Font = new("Yu Gothic UI", 21, FontStyle.Bold);
        cancel.Click += (_, _) => operation?.Cancel();
        FormClosing += (_, e) => { if (operation != null) { operation.Cancel(); e.Cancel = true; status.Text = "処理を中止しています。完了後に閉じてください。"; } };
        var library = Page("エンジンを入手", "ENGINE / DOWNLOADS");
        catalog.Text = settings.CatalogUrl;
        var overrideFile = Path.Combine(Distribution.DataRoot, "catalog.txt");
        if (!preview && File.Exists(overrideFile)) catalog.Text = File.ReadAllText(overrideFile);
        Add(library, "バージョン一覧URL (Cloudflare)", catalog);
        Button(library, "バージョン一覧を取得", () => Work(async token =>
        {
            var list = await Distribution.LoadCatalog(catalog.Text.Trim(), token);
            localCatalogRoot = null;
            releases.Items.Clear(); releases.Items.AddRange(list.Cast<object>().ToArray());
            Directory.CreateDirectory(Distribution.DataRoot); File.WriteAllText(overrideFile, catalog.Text.Trim());
            if (list.Length > 0) releases.SelectedIndex = 0;
        }));
        Add(library, "バージョン", releases);
        Add(library, "このバージョンで変更された点（下端をドラッグして高さ調整）", new ResizableNotesBox(changes));
        releases.SelectedIndexChanged += (_, _) => UpdateRelease();
        location.TextChanged += (_, _) => UpdateRelease();
        Add(library, "ダウンロード元", source);
        Add(library, "このバージョンの保存先", installTarget);
        Add(library, "保存先の親フォルダー", location);
        Button(library, "保存先を選択", () => { var path = Folder(); if (path != null) location.Text = path; });
        Button(library, "選択したバージョンをインストール", () => Work(async token =>
        {
            var release = releases.SelectedItem as Release ?? throw new IOException("バージョンを選択してください。");
            EnsureTool();
            var target = location.Text; var tool = Tool; var packageRoot = localCatalogRoot;
            var reporter = new Progress<int>(p => progress.Value = p);
            await Task.Run(() => Distribution.Install(release, target, tool, reporter, token, localCatalogRoot: packageRoot), token);
            RefreshLocal();
        }));
        var workspace = Page("プロジェクト", "WORKSPACE / PROJECTS");
        var workspacePage = workspace.Parent!;
        workspacePage.Controls.Remove(workspace); workspace.Dispose(); workspacePage.Controls.Add(projects);
        projects.AddProject += () =>
        {
            using var dialog = new OpenFileDialog { Filter = "Nexus project|*.nxproject" };
            if (dialog.ShowDialog() == DialogResult.OK) Work(async token => { EnsureTool(); await Distribution.Run(Tool, ["register", dialog.FileName], token); RefreshLocal(); });
        };
        projects.OpenProject += () => Work(async token =>
        {
            EnsureTool(); var p = projects.Selected ?? throw new IOException("プロジェクトを選択してください。");
            await Distribution.Run(Tool, ["open", p.File, EngineRegistry, projects.Configuration], token);
        });
        projects.RefreshProjects += () => Work(_ => { RefreshLocal(); return Task.CompletedTask; });
        projects.RemoveProject += () => RemoveSelectedProject(false);
        projects.DeleteProject += () => RemoveSelectedProject(true);
        projects.CreateProject += () => SelectPage(pages[2].Page);
        var create = Page("新規プロジェクト", "WORKSPACE / CREATE");
        Add(create, "使用するエンジン", engines);
        var name = new TextBox { Width = 400 }; var parent = new TextBox { Width = 600, Text = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments) };
        var template = new ComboBox { Width = 300, DropDownStyle = ComboBoxStyle.DropDownList };
        template.Items.AddRange(["blank", "basic-3d", "native-module", "managed-bootstrap", "managed-gameplay", "hybrid-gameplay"]); template.SelectedIndex = 0;
        Add(create, "新規プロジェクト名", name); Add(create, "親フォルダー", parent);
        Button(create, "プロジェクト保存先を選択", () => { var path = Folder(); if (path != null) parent.Text = path; });
        Add(create, "テンプレート", template);
        var initialLanguage = new ComboBox { Width = 420, DropDownStyle = ComboBoxStyle.DropDownList };
        initialLanguage.Items.AddRange(["C++ — Native Game Development", "C# — Managed Game Development"]); initialLanguage.SelectedIndex = 0;
        Add(create, "Initial Game Language", initialLanguage);
        create.Controls.Add(new Label { Width = 740, Height = 65, Text = "最初のGame Moduleの種類を選択します。Projectの言語は固定されません。HybridはC++とC#で同じWorldを操作します。Managed GameplayはProperty編集とReload C#に対応します。Managed Bootstrapは起動確認、blank / basic-3dのC#は宣言のみです。" });
        Button(create, "新規プロジェクトを作成", () => Work(async token =>
        {
            EnsureTool(); var engine = engines.SelectedItem as Installed ?? throw new IOException("エンジンを選択してください。");
            var target = ProjectTarget(parent.Text, name.Text);
            var language = initialLanguage.SelectedIndex == 0 ? "Cpp" : "CSharp";
            // C++の既定選択は旧CLIと同じ引数を維持し、既存の配布Toolでも作成できるようにする。
            List<string> createArguments = ["create-template", target, name.Text.Trim(), engine.Manifest, template.Text];
            if (language == "CSharp") createArguments.Add(language);
            var file = await Distribution.Run(Tool, createArguments, token);
            await Distribution.Run(Tool, ["register", file], token); RefreshLocal();
        }));
        var git = Page("Gitから追加", "WORKSPACE / SOURCE CONTROL");
        var url = new TextBox { Width = 720 }; var cloneParent = new TextBox { Width = 600, Text = parent.Text }; var folder = new TextBox { Width = 400 };
        Add(git, "GitリポジトリURL (HTTPS)", url); Add(git, "保存先の親フォルダー", cloneParent); Add(git, "新しいフォルダー名", folder);
        Button(git, "保存先を選択", () => { var path = Folder(); if (path != null) cloneParent.Text = path; });
        var hint = new Label { Width = 740, Height = 85, Text = "Git for Windowsが必要です。非公開リポジトリはGit Credential Managerで事前認証してください。クローン後、追加する.nxprojectを選択します。サブモジュールは自動取得しません。" };
        git.Controls.Add(hint);
        Button(git, "クローンしてプロジェクトを追加", () => Work(async token =>
        {
            EnsureTool(); Distribution.Https(url.Text.Trim());
            var target = ProjectTarget(cloneParent.Text, folder.Text);
            await Distribution.Run("git", ["-c", "credential.interactive=false", "clone", "--", url.Text.Trim(), target], token);
            using var dialog = new OpenFileDialog { InitialDirectory = target, Filter = "Nexus project|*.nxproject" };
            if (dialog.ShowDialog() != DialogResult.OK) { status.Text = $"クローン済みです。既存プロジェクト追加から登録できます: {target}"; return; }
            await Distribution.Run(Tool, ["register", dialog.FileName], token); RefreshLocal();
        }));
        var local = Page("インストール済み", "ENGINE / LIBRARY");
        Add(local, "エンジン一覧（バージョンとインストール場所）", inventory);
        Button(local, "一覧を更新", () => Work(_ => { RefreshLocal(); return Task.CompletedTask; }));
        Button(local, "既存エンジンを登録／登録を再試行", () =>
        {
            using var dialog = new OpenFileDialog { Filter = "Nexus engine|*.nxengine" };
            if (dialog.ShowDialog() == DialogResult.OK) Work(async token => { EnsureTool(); await Distribution.Run(Tool, ["engine-register", dialog.FileName], token); RefreshLocal(); });
        });
        Button(local, "選択したエンジンをアンインストール", () =>
        {
            var engine = inventory.SelectedItem as Installed;
            if (engine is null) { MessageBox.Show(this, "削除するエンジンを選択してください。"); return; }
            if (engine.Association != "installed") { MessageBox.Show(this, "Developmentエンジンのソースは削除しません。「登録のみ解除」を使ってください。"); return; }
            var affected = entries.Count(project => project.Association == engine.Association && project.EngineId == engine.Id && project.Version == engine.Version);
            if (MessageBox.Show(this, $"NexusEngine {engine.Version} ({engine.Configuration}) を削除しますか？\r\n\r\n{Path.GetDirectoryName(engine.Manifest)}\r\n\r\n同じ版を要求する登録済みプロジェクト: {affected} 件\r\nこの版を使用するプロジェクトは、再導入またはエンジン移行まで起動できなくなります。\r\nプロジェクト本体や別バージョンは削除しません。", "エンジンのアンインストール", MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) != DialogResult.Yes) return;
            Work(async token => { EnsureTool(); await Task.Run(() => Distribution.UninstallEngine(engine, Tool, EngineRegistry, token)); RefreshLocal(); });
        });
        Button(local, "選択したエンジンの登録のみ解除", () =>
        {
            var engine = inventory.SelectedItem as Installed;
            if (engine is null) { MessageBox.Show(this, "エンジンを選択してください。"); return; }
            if (MessageBox.Show(this, $"このエンジンの登録を解除しますか？実ファイルは残ります。\r\n\r\n{engine.Manifest}", "エンジンの登録解除", MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) != DialogResult.Yes) return;
            Work(async token => { EnsureTool(); await Distribution.Run(Tool, ["engine-remove", engine.Manifest, EngineRegistry], token); RefreshLocal(); });
        });
        var maintenance = Page("ランチャーの管理", "LAUNCHER / MAINTENANCE");
        maintenance.Controls.Add(new Label { AutoSize = true, Text = "ランチャーを削除しても、エンジン・プロジェクト・登録履歴は保持します。\r\nエンジンは「インストール済み」から個別に削除できます。", Margin = new(0, 12, 0, 20) });
        maintenance.Controls.Add(new Label { AutoSize = true, Text = $"配置: {AppContext.BaseDirectory}", Margin = new(0, 0, 0, 12) });
        Button(maintenance, "ランチャーをアンインストール", () =>
        {
            if (!LauncherInstallation.CanUninstall(AppContext.BaseDirectory))
            {
                MessageBox.Show(this, "このランチャーは開発・配布フォルダーから起動しています。\r\n新しいlauncherSetup.exeからインストールすると、Windowsのアプリ一覧とこの画面からアンインストールできます。"); return;
            }
            LauncherInstallation.ConfirmAndUninstall(this);
        });
        ArrangeDownloads(library);
        LauncherTheme.FormatItem = item => item switch
        {
            ProjectEntry p => (p.Name, $"{p.Version}  ·  {p.Configuration}  ·  {p.File}"),
            Installed i => ($"NexusEngine {i.Version}  /  {i.Configuration}", i.Manifest),
            Release r => ($"NexusEngine v{r.Version}", $"{r.Configuration}  ·  {r.Bytes / 1048576.0:F1} MiB"),
            _ => (item.ToString() ?? "", "")
        };
        LauncherTheme.Apply(this);
        projects.ApplyAccents();
        navigation.BackColor = LauncherTheme.Panel;
        foreach (Control child in navigation.Controls) child.BackColor = LauncherTheme.Panel;
        SelectPage(pages[1].Page);
        if (!preview) Shown += (_, _) => Work(async token =>
        {
            RefreshLocal();
            if (!string.IsNullOrWhiteSpace(settings.CatalogFile))
            {
                await LoadLocalReleases(settings.CatalogFile, token);
                SelectPage(pages[0].Page);
            }
        });
        else
        {
            releases.Items.Add(new Release("nexus", "1.0.0", "Release", "プロジェクトブラウザーとエンジン配布の改善\r\n複数バージョンを個別にインストールできます。", "https://downloads.example.com/nexus-1.0.0.zip", new string('a', 64), 512 * 1048576L));
            releases.SelectedIndex = 0;
            projects.SetProjects([
                new ProjectEntry(@"D:\NexusProjects\SampleGame\SampleGame.nxproject", "Sample Game", "1.1.1", "Release"),
                new ProjectEntry(@"D:\NexusProjects\サンプル\サンプル.nxproject", "サンプルプロジェクト", "1.0.0", "Release"),
                new ProjectEntry(@"D:\NexusProjects\Prototype\Prototype.nxproject", "Action Prototype", "1.1.0", "Develop")]);
            var sample = new Installed(@"D:\NexusEngines\nexus-0.1.5-Release\NexusEngine.nxengine", "nexus", "0.1.5", "Release");
            inventory.Items.Add(sample); engines.Items.Add(sample); engines.SelectedIndex = 0;
            status.Text = "UIプレビュー · サンプルデータ";
        }
    }
    void RemoveSelectedProject(bool recycle)
    {
        var project = projects.Selected;
        if (project == null) return;
        try
        {
            EnsureTool();
            var protectedPaths = installed.Select(i => Path.GetDirectoryName(i.Manifest)!).Append(AppContext.BaseDirectory).ToArray();
            var root = recycle ? ProjectRemoval.Validate(project, entries, protectedPaths) : null;
            var message = recycle
                ? $"「{project.Name}」のフォルダー全体をごみ箱へ移動しますか？\r\n\r\n{root}\r\n\r\nソース・アセット・生成ファイルを含みます。登録も一覧から外します。"
                : $"「{project.Name}」を一覧から外しますか？\r\n\r\n{project.File}\r\n\r\nプロジェクト本体は残ります。「既存を追加」から再登録できます。";
            if (MessageBox.Show(this, message, recycle ? "プロジェクトをごみ箱へ移動" : "プロジェクトの登録解除", MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) != DialogResult.Yes) return;
            Work(async token =>
            {
                if (recycle)
                {
                    root = ProjectRemoval.Validate(project, entries, protectedPaths);
                    Microsoft.VisualBasic.FileIO.FileSystem.DeleteDirectory(root, Microsoft.VisualBasic.FileIO.UIOption.OnlyErrorDialogs, Microsoft.VisualBasic.FileIO.RecycleOption.SendToRecycleBin, Microsoft.VisualBasic.FileIO.UICancelOption.ThrowException);
                }
                try { await Distribution.Run(Tool, ["remove", project.File, ProjectRegistry], CancellationToken.None); }
                catch (Exception ex) when (recycle) { throw new IOException("ごみ箱への移動は完了しましたが、登録解除に失敗しました。「一覧から外す」で再試行してください。\r\n" + ex.Message, ex); }
                finally { RefreshLocal(); }
                status.Text = recycle ? "プロジェクトをごみ箱へ移動しました。" : "プロジェクトを一覧から外しました。";
            });
        }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "プロジェクトを変更できません", MessageBoxButtons.OK, MessageBoxIcon.Warning); }
    }
    static string ProjectTarget(string parent, string name)
    {
        name = name.Trim();
        if (name.Length == 0 || name is "." or ".." || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || name.EndsWith('.')) throw new IOException("フォルダー名が不正です。");
        var target = Distribution.Within(Path.GetFullPath(parent), name);
        if (Directory.Exists(target) || File.Exists(target)) throw new IOException("保存先が既に存在します。");
        return target;
    }
    void EnsureTool() { if (!File.Exists(Tool)) throw new IOException("配布パッケージにTools/NexusProjectTool.exeがありません。Build-Distribution.ps1で作成してください。"); }
    void RefreshLocal()
    {
        installed.Clear(); entries.Clear(); engines.Items.Clear();
        if (File.Exists(EngineRegistry))
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(EngineRegistry));
            foreach (var e in doc.RootElement.GetProperty("engines").EnumerateArray())
                installed.Add(new(e.GetProperty("manifest").GetString()!, e.GetProperty("id").GetString()!, e.GetProperty("version").GetString()!, e.GetProperty("configuration").GetString()!, e.GetProperty("association").GetString()!));
        }
        if (File.Exists(ProjectRegistry))
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(ProjectRegistry));
            foreach (var p in doc.RootElement.GetProperty("projects").EnumerateArray())
            {
                var e = p.GetProperty("engine");
                var match = installed.FirstOrDefault(i => i.Association == e.GetProperty("association").GetString() && i.Id == e.GetProperty("id").GetString() && (string.IsNullOrEmpty(e.GetProperty("version").GetString()) || i.Version == e.GetProperty("version").GetString()));
                entries.Add(new(p.GetProperty("file").GetString()!, p.GetProperty("name").GetString()!, e.GetProperty("version").GetString()!, match?.Configuration ?? "Release", e.GetProperty("id").GetString()!, e.GetProperty("association").GetString()!));
            }
        }
        inventory.Items.Clear(); inventory.Items.AddRange(installed.Cast<object>().ToArray());
        engines.Items.AddRange(installed.Cast<object>().ToArray()); if (installed.Count > 0) engines.SelectedIndex = 0;
        projects.SetProjects(entries);
    }
    async void Work(Func<CancellationToken, Task> action)
    {
        if (operation != null) return;
        operation = new(); tabs.Enabled = false; navigation.Enabled = false; cancel.Enabled = true; progress.Value = 0; status.Text = "処理中…";
        try { await action(operation.Token); if (status.Text == "処理中…") status.Text = "完了しました。"; }
        catch (OperationCanceledException) { status.Text = "キャンセルしました。"; }
        catch (Exception ex) { status.Text = ex.Message; MessageBox.Show(this, ex.Message, "処理を完了できませんでした", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        finally { operation.Dispose(); operation = null; tabs.Enabled = true; navigation.Enabled = true; cancel.Enabled = false; }
    }
    void UpdateRelease()
    {
        if (releases.SelectedItem is not Release r) { changes.Clear(); source.Text = ""; installTarget.Text = ""; return; }
        changes.Text = r.Changes.ReplaceLineEndings(Environment.NewLine); source.Text = localCatalogRoot is null ? r.Url : Distribution.Within(localCatalogRoot, r.Url);
        try { installTarget.Text = Distribution.Within(Path.GetFullPath(location.Text), $"{r.Id}-{r.Version}-{r.Configuration}"); }
        catch { installTarget.Text = "保存先を確認してください。"; }
    }
    FlowLayoutPanel Page(string title, string subtitle)
    {
        var page = new Panel { Dock = DockStyle.Fill, Visible = false, Tag = title };
        var panel = new ResponsiveFlowPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true, Padding = new(30, 0, 30, 30) };
        panel.Controls.Add(new Label { Text = subtitle, AutoSize = true, Margin = new(0, 0, 0, 18) });
        page.Controls.Add(panel); tabs.Controls.Add(page);
        var button = new Button { Text = title, TextAlign = ContentAlignment.MiddleLeft, Width = 184, Height = 46, Margin = new(0, 0, 0, 6) };
        button.Click += (_, _) => SelectPage(page);
        navigation.Controls.Add(button); pages.Add((page, button));
        panel.SizeChanged += (_, _) =>
        {
            var width = Math.Max(260, panel.ClientSize.Width - panel.Padding.Horizontal - SystemInformation.VerticalScrollBarWidth - 8);
            foreach (Control control in panel.Controls) if (control is not System.Windows.Forms.Button && (control is not Label || !control.AutoSize)) control.Width = width;
        };
        return panel;
    }
    void SelectPage(Panel selected)
    {
        foreach (var item in pages)
        {
            item.Page.Visible = item.Page == selected;
            item.Navigation.BackColor = item.Page == selected ? LauncherTheme.Raised : LauncherTheme.Panel;
            item.Navigation.ForeColor = item.Page == selected ? LauncherTheme.Accent : LauncherTheme.Text;
        }
        pageTitle.Text = selected.Tag?.ToString();
    }
    void ArrangeDownloads(FlowLayoutPanel library)
    {
        var controls = library.Controls.Cast<Control>().ToArray();
        library.Controls.Clear();
        var page = library.Parent!;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 2, Padding = new(30, 0, 30, 20) };
        layout.ColumnStyles.Add(new(SizeType.Percent, 34)); layout.ColumnStyles.Add(new(SizeType.Percent, 66));
        layout.RowStyles.Add(new(SizeType.AutoSize)); layout.RowStyles.Add(new(SizeType.Percent, 100));
        // 現在のDPIとフォントに合わせて、各行に必要な高さを自動計測する。
        var header = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, ColumnCount = 2, RowCount = 4, Margin = new(0, 0, 0, 20) };
        header.ColumnStyles.Add(new(SizeType.Percent, 100)); header.ColumnStyles.Add(new(SizeType.Absolute, 210));
        for (int row = 0; row < 4; row++) header.RowStyles.Add(new(SizeType.AutoSize));
        var eyebrow = new Label { Text = "ENGINE / DOWNLOADS", AutoSize = true, Margin = new(0, 0, 0, 14) };
        var addressLabel = new Label { Text = "配布元のバージョン一覧URL（HTTPS）", AutoSize = true, Margin = new(0, 0, 0, 6) };
        header.Controls.Add(eyebrow, 0, 0); header.SetColumnSpan(eyebrow, 2);
        header.Controls.Add(addressLabel, 0, 1); header.SetColumnSpan(addressLabel, 2);
        catalog.Dock = DockStyle.Fill; catalog.Margin = new(0, 4, 12, 8);
        var fetch = controls[3]; fetch.Dock = DockStyle.Fill; fetch.Margin = new(0, 0, 0, 8); catalogFetch = fetch;
        header.Controls.Add(catalog, 0, 2); header.Controls.Add(fetch, 1, 2);
        var browse = new Button { Text = "ローカルの一覧を選択…", AutoSize = true, MinimumSize = new(210, 36), Margin = new(0, 0, 0, 0) };
        browse.Click += (_, _) =>
        {
            using var dialog = new OpenFileDialog { Filter = "Release catalog|*.json", FileName = settings.CatalogFile ?? "releases.local.json" };
            if (dialog.ShowDialog() == DialogResult.OK) Work(token => LoadLocalReleases(dialog.FileName, token));
        };
        header.Controls.Add(browse, 0, 3); header.SetColumnSpan(browse, 2);
        layout.Controls.Add(header, 0, 0); layout.SetColumnSpan(header, 2);
        var versions = new Panel { Dock = DockStyle.Fill, Padding = new(0, 0, 20, 0) };
        releases.Dock = DockStyle.Fill; versions.Controls.Add(releases);
        var versionLabel = new Label { Text = "エンジンのバージョン", Dock = DockStyle.Top, Height = 32 };
        versions.Controls.Add(versionLabel);
        downloadHeader = header; downloadVersions = versions;
        layout.Controls.Add(versions, 0, 1);
        var details = new ResponsiveFlowPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true };
        foreach (var control in controls.Skip(6).SkipLast(1)) details.Controls.Add(control);
        source.Height = 46; installTarget.Height = 46;
        details.SizeChanged += (_, _) =>
        {
            var width = Math.Max(220, details.ClientSize.Width - SystemInformation.VerticalScrollBarWidth - 8);
            foreach (Control control in details.Controls) if (control is not System.Windows.Forms.Button && (control is not Label || !control.AutoSize)) control.Width = width;
        };
        var right = new Panel { Dock = DockStyle.Fill };
        var actions = new Panel { Dock = DockStyle.Bottom, Height = 50, Padding = new(0, 8, 0, 0) };
        var install = controls.Last(); install.Dock = DockStyle.Left;
        actions.Controls.Add(install); right.Controls.Add(details); right.Controls.Add(actions);
        layout.Controls.Add(right, 1, 1);
        controls[0].Dispose(); controls[1].Dispose(); controls[4].Dispose();
        page.Controls.Remove(library); library.Dispose(); page.Controls.Add(layout);
    }
    async Task LoadLocalReleases(string file, CancellationToken token)
    {
        var list = await Distribution.LoadLocalCatalog(file, token);
        localCatalogRoot = Path.GetDirectoryName(Path.GetFullPath(file));
        releases.Items.Clear(); releases.Items.AddRange(list.Cast<object>().ToArray());
        if (list.Length > 0) releases.SelectedIndex = 0;
        status.Text = $"ローカル一覧: {list.Length} バージョン · {file}";
    }
    public void SavePreviews(string directory, string? catalogFile = null)
    {
        directory = Path.GetFullPath(directory);
        if (Directory.Exists(directory)) throw new IOException("プレビュー保存先が既に存在します。");
        Directory.CreateDirectory(directory);
        if (catalogFile != null)
        {
            var list = Distribution.LoadLocalCatalog(catalogFile, default).GetAwaiter().GetResult();
            localCatalogRoot = Path.GetDirectoryName(Path.GetFullPath(catalogFile));
            releases.Items.Clear(); releases.Items.AddRange(list.Cast<object>().ToArray());
            if (list.Length > 0) releases.SelectedIndex = 0;
        }
        Show();
        for (int size = 0; size < 2; size++)
        {
            ClientSize = size == 0 ? new(1180, 820) : new(900, 640);
            for (int i = 0; i < pages.Count; i++)
            {
                SelectPage(pages[i].Page); Application.DoEvents();
                if (i == 0)
                {
                    var headerRect = downloadHeader!.RectangleToScreen(downloadHeader.ClientRectangle);
                    var versionsRect = downloadVersions!.RectangleToScreen(downloadVersions.ClientRectangle);
                    var fetchRect = catalogFetch!.RectangleToScreen(catalogFetch.ClientRectangle);
                    if (headerRect.IntersectsWith(versionsRect) || !headerRect.Contains(fetchRect)) throw new InvalidOperationException("バージョン取得欄と一覧のレイアウトが重複しています。");
                }
                using var bitmap = new Bitmap(Width, Height);
                DrawToBitmap(bitmap, new Rectangle(0, 0, Width, Height));
                bitmap.Save(Path.Combine(directory, $"page-{i}-{ClientSize.Width}.png"));
            }
        }
        Close();
    }
    static void Add(FlowLayoutPanel panel, string label, Control control)
    {
        panel.Controls.Add(new Label { Text = label, AutoSize = true, Margin = new(0, 14, 0, 6) });
        control.Margin = new(0, 0, 0, 4); panel.Controls.Add(control);
    }
    static void Button(FlowLayoutPanel panel, string text, Action action)
    {
        var primary = text is "選択したバージョンをインストール" or "選択したプロジェクトを開く" or "新規プロジェクトを作成" or "クローンしてプロジェクトを追加";
        var button = new Button { Text = text, AutoSize = true, MinimumSize = new(200, 38), Margin = new(0, 10, 0, 4) };
        if (primary) button.HandleCreated += (_, _) => { button.BackColor = LauncherTheme.Accent; button.ForeColor = Color.FromArgb(20, 20, 20); };
        button.Click += (_, _) => action(); panel.Controls.Add(button);
    }
    static string? Folder() { using var dialog = new FolderBrowserDialog(); return dialog.ShowDialog() == DialogResult.OK ? dialog.SelectedPath : null; }
}
