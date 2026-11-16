using System.Text.Json;

namespace NexusLauncher;

// 互換性検証・原子的な適用・復旧は既存のネイティブツールに委譲する。
// この画面はレビューした計画だけを適用し、変更後のビルド成功とは区別して表示する。
internal sealed class ProjectMigrationDialog : Form
{
    readonly ProjectEntry project;
    readonly string tool;
    readonly ComboBox target = new() { Dock = DockStyle.Fill, DropDownStyle = ComboBoxStyle.DropDownList };
    readonly TextBox review = new() { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both, WordWrap = false };
    readonly CheckBox acknowledge = new() { AutoSize = true, Text = "このプロジェクトのEditor / Runtimeを閉じました。再ビルドと動作確認が必要なことを理解しました。" };
    readonly Button inspect = new() { Text = "互換性を確認", AutoSize = true };
    readonly Button apply = new() { Text = "確認した変更を適用", AutoSize = true, Enabled = false };
    readonly Button restore = new() { Text = "復旧ファイルから元に戻す…", AutoSize = true };
    readonly Label state = new() { AutoSize = true, Text = "移行先のエンジンを選択してください。未導入の版はランチャーでインストールしてください。" };
    string? plan;
    bool busy;

    public ProjectMigrationDialog(ProjectEntry project, IEnumerable<Installed> engines, string tool)
    {
        this.project = project;
        this.tool = tool;
        Text = $"エンジンの変更 — {project.Name}";
        ClientSize = new(900, 640);
        MinimumSize = new(760, 580);
        Font = new("Yu Gothic UI", 10);
        AutoScaleMode = AutoScaleMode.Dpi;
        StartPosition = FormStartPosition.CenterParent;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 7, Padding = new(20) };
        layout.ColumnStyles.Add(new(SizeType.Percent, 100));
        for (var row = 0; row < 7; row++) layout.RowStyles.Add(new(row == 3 ? SizeType.Percent : SizeType.AutoSize, row == 3 ? 100 : 0));
        layout.Controls.Add(new Label { AutoSize = true, MaximumSize = new(700, 0), Text = $"{project.Name}\n画面を開いた時点: {project.EngineId} {project.Version} ({project.Association})\n{project.File}" }, 0, 0);
        layout.Controls.Add(target, 0, 1);
        layout.Controls.Add(inspect, 0, 2);
        layout.Controls.Add(review, 0, 3);
        acknowledge.MaximumSize = new(700, 0);
        layout.Controls.Add(acknowledge, 0, 4);
        var actions = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill };
        actions.Controls.AddRange([apply, restore]);
        layout.Controls.Add(actions, 0, 5);
        state.MaximumSize = new(700, 0);
        layout.Controls.Add(state, 0, 6);
        Controls.Add(layout);
        // 明示的に移行先を選ぶ。版の文字列順やRegistryの順序から最新版を推測しない。
        target.Items.AddRange(engines.Where(e => File.Exists(e.Manifest) &&
            !(e.Id == project.EngineId && e.Version == project.Version && e.Association == project.Association)).Cast<object>().ToArray());
        target.SelectedIndexChanged += (_, _) => InvalidateReview();
        acknowledge.CheckedChanged += (_, _) => UpdateActions();
        inspect.Click += async (_, _) => await Run(Prepare);
        apply.Click += async (_, _) => await Run(Apply);
        restore.Click += async (_, _) =>
        {
            using var file = new OpenFileDialog { Filter = "移行の復旧記録 (Recovery.json)|Recovery.json", InitialDirectory = Path.GetDirectoryName(project.File) };
            if (file.ShowDialog(this) != DialogResult.OK) return;
            if (MessageBox.Show(this, "このプロジェクトのEditor / Runtimeを閉じてください。\n元のエンジン設定へ戻した後、旧版での再ビルドが必要です。\n\n復旧しますか？", "元のエンジン設定へ復旧", MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) != DialogResult.Yes) return;
            await Run(async () =>
            {
                await Distribution.Run(tool, ["migration-restore", project.File, file.FileName], CancellationToken.None);
                InvalidateReview();
                state.Text = "元のエンジン設定に復旧しました。旧版で再ビルドしてください。";
                await Register();
            });
        };
        FormClosing += (_, e) => { if (busy) e.Cancel = true; };
        LauncherTheme.Apply(this);
        UpdateActions();
    }

    void InvalidateReview()
    {
        plan = null;
        review.Clear();
        acknowledge.Checked = false;
        UpdateActions();
    }

    void UpdateActions()
    {
        target.Enabled = restore.Enabled = !busy;
        inspect.Enabled = !busy && target.SelectedItem is Installed;
        acknowledge.Enabled = !busy && plan != null;
        apply.Enabled = !busy && plan != null && acknowledge.Checked;
    }

    async Task Prepare()
    {
        InvalidateReview();
        var engine = target.SelectedItem as Installed ?? throw new IOException("移行先を選択してください。");
        // Projectの共有フォルダーにレビュー用ファイルを作らず、失敗した計画も診断用に残す。
        var directory = Path.Combine(Distribution.DataRoot, "MigrationReviews", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        var file = Path.Combine(directory, "Review.json");
        var output = await Distribution.Run(tool, ["migration-plan", project.File, engine.Manifest, file], CancellationToken.None);
        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(file));
        var data = document.RootElement;
        string Engine(string key)
        {
            var value = data.GetProperty(key);
            return $"{value.GetProperty("id").GetString()} {value.GetProperty("version").GetString()} ({value.GetProperty("association").GetString()})";
        }
        review.Text = $"変更前: {Engine("previous")}\r\n変更後: {Engine("target")}\r\n\r\n" +
            "設定ファイルのエンジン指定を変更します。Level・Asset・Sourceは変換しません。\r\n" +
            "形式の事前検査に合格しても、API・インポーター・ゲーム固有データの互換性は保証されません。\r\n" +
            "適用前に元の設定と復旧記録をプロジェクト内へ保存します。\r\n\r\n" + output.ReplaceLineEndings("\r\n");
        plan = file;
        state.Text = "互換性の事前確認が完了しました。内容を確認してから適用してください。";
    }

    async Task Apply()
    {
        var reviewed = plan ?? throw new IOException("互換性を再確認してください。");
        // 適用中の強制終了を避け、ネイティブ側でレビュー後の変更を再検証して保存する。
        plan = null;
        var output = await Distribution.Run(tool, ["migration-apply", reviewed, "--accept-api-risk"], CancellationToken.None);
        review.AppendText("\r\n\r\n" + output.ReplaceLineEndings("\r\n"));
        state.Text = "エンジン設定を変更しました。一覧の「開く」から再ビルドし、動作を確認してください。";
        acknowledge.Checked = false;
        await Register();
    }

    async Task Register()
    {
        // 設定変更とRecent更新は別処理。登録失敗を移行失敗として再適用させない。
        try { await Distribution.Run(tool, ["register", project.File], CancellationToken.None); }
        catch (Exception ex) { state.Text += " 一覧の更新に失敗しました。「既存を追加」から再登録してください。"; MessageBox.Show(this, ex.Message, "一覧の更新"); }
    }

    async Task Run(Func<Task> action)
    {
        if (busy) return;
        busy = true;
        UpdateActions();
        try { await action(); }
        catch (Exception ex)
        {
            plan = null;
            state.Text = "処理を完了できませんでした。適用する場合は互換性を再確認してください。";
            MessageBox.Show(this, ex.Message, "エンジンの変更", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally { busy = false; UpdateActions(); }
    }
}
