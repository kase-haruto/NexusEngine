namespace NexusLauncher;

// プロジェクトの検索・並び替え・選択と、選択項目の管理操作を接続する。
internal sealed class ProjectBrowser : UserControl
{
    readonly TextBox search = new() { Dock = DockStyle.Fill, PlaceholderText = "プロジェクト名・保存場所・バージョンで検索" };
    readonly Button sort = new() { Width = 155, Height = 36, Text = "名前の昇順", Margin = new(0, 0, 8, 8) };
    int sortIndex;
    readonly FlowLayoutPanel grid = new() { Dock = DockStyle.Fill, AutoScroll = true, WrapContents = true, Padding = new(8) };
    readonly ProjectThumbnailCache thumbnails = new();
    readonly ToolTip paths = new();
    readonly System.Windows.Forms.Timer previewRefresh = new() { Interval = 2000 };
    ProjectCard? selectedCard;
    readonly Label count = new() { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft };
    readonly Label detail = new() { Dock = DockStyle.Fill, AutoEllipsis = true, Padding = new(12, 8, 12, 4) };
    readonly Button configuration = new() { Width = 108, Height = 36, Text = "Release" };
    readonly Button open = ActionButton("開く", 92), remove = ActionButton("一覧から外す", 120), delete = ActionButton("ごみ箱へ移動", 138);
    readonly Button migrate = ActionButton("エンジンを変更…", 146);
    readonly List<ProjectEntry> entries = [];

    public ProjectEntry? Selected => selectedCard?.Project;
    public string Configuration => configuration.Text;
    public event Action? OpenProject, AddProject, RefreshProjects, RemoveProject, DeleteProject, CreateProject, ChangeEngine;

    public ProjectBrowser()
    {
        Dock = DockStyle.Fill;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 5, Padding = new(24, 0, 24, 20) };
        layout.ColumnStyles.Add(new(SizeType.Percent, 100));
        layout.RowStyles.Add(new(SizeType.Absolute, 54)); layout.RowStyles.Add(new(SizeType.Absolute, 46));
        layout.RowStyles.Add(new(SizeType.Percent, 100)); layout.RowStyles.Add(new(SizeType.Absolute, 100)); layout.RowStyles.Add(new(SizeType.AutoSize));
        var toolbar = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false };
        var create = ActionButton("＋ 新規作成", 126); var add = ActionButton("既存を追加…", 132); var refresh = ActionButton("更新", 74);
        create.Click += (_, _) => CreateProject?.Invoke(); add.Click += (_, _) => AddProject?.Invoke(); refresh.Click += (_, _) => RefreshProjects?.Invoke();
        toolbar.Controls.AddRange([create, add, refresh, sort]);
        var filters = new Panel { Dock = DockStyle.Fill };
        Choice(sort, ["名前の昇順", "名前の降順", "エンジン順", "保存場所順"], i => { sortIndex = i; Rebuild(); });
        var searchField = new Panel { Dock = DockStyle.Fill, Padding = new(0, 0, 8, 0) };
        searchField.Controls.Add(search); searchField.Controls.Add(new Label { Text = "検索", Dock = DockStyle.Left, Width = 48, TextAlign = ContentAlignment.TopLeft, Padding = new(0, 4, 0, 0) });
        filters.Controls.Add(searchField);
        previewRefresh.Tick += (_, _) =>
        {
            if (Visible)
                grid.Invalidate(invalidateChildren: true);
        };
        previewRefresh.Start();
        search.TextChanged += (_, _) => Rebuild();
        Choice(configuration, ["Release", "Develop", "Debug"], _ => { });
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, WrapContents = true };
        actions.Controls.AddRange([configuration, open, migrate, remove, delete]);
        migrate.Click += (_, _) => ChangeEngine?.Invoke();
        open.Click += (_, _) => OpenProject?.Invoke(); remove.Click += (_, _) => RemoveProject?.Invoke(); delete.Click += (_, _) => DeleteProject?.Invoke();
        var content = new Panel { Dock = DockStyle.Fill };
        content.Controls.Add(grid); content.Controls.Add(count); count.Visible = false;
        layout.Controls.Add(toolbar, 0, 0); layout.Controls.Add(filters, 0, 1); layout.Controls.Add(content, 0, 2); layout.Controls.Add(detail, 0, 3); layout.Controls.Add(actions, 0, 4);
        Controls.Add(layout);
        UpdateSelection();
    }
    public void ApplyAccents()
    {
        grid.BackColor = LauncherTheme.Background;
        detail.BackColor = LauncherTheme.Panel;
        open.BackColor = LauncherTheme.Accent; open.ForeColor = Color.Black;
        delete.ForeColor = Color.FromArgb(255, 153, 140);
    }
    static Button ActionButton(string text, int width) => new() { Text = text, Width = width, Height = 36, Margin = new(0, 0, 8, 8) };
    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            previewRefresh.Dispose();
            paths.Dispose();
            thumbnails.Dispose();
        }
        base.Dispose(disposing);
    }
    public void SetProjects(IEnumerable<ProjectEntry> projects)
    {
        entries.Clear();
        entries.AddRange(projects);
        thumbnails.Reset();
        Rebuild();
    }
    void Rebuild()
    {
        var selected = Selected?.File;
        var query = search.Text.Trim();
        var filtered = entries.Where(p => $"{p.Name}\n{p.File}\n{p.Version}".Contains(query, StringComparison.CurrentCultureIgnoreCase));
        filtered = sortIndex switch { 1 => filtered.OrderByDescending(p => p.Name), 2 => filtered.OrderBy(p => p.Version).ThenBy(p => p.Name), 3 => filtered.OrderBy(p => p.File), _ => filtered.OrderBy(p => p.Name) };
        grid.SuspendLayout();
        selectedCard = null;
        ProjectCard? restoreSelection = null;
        foreach (ProjectCard card in grid.Controls.Cast<ProjectCard>().ToArray())
            card.Dispose();
        foreach (var project in filtered)
        {
            var card = new ProjectCard(project, thumbnails);
            paths.SetToolTip(card, project.File);
            card.Enter += (_, _) => Select(card);
            card.Click += (_, _) => Select(card);
            card.DoubleClick += (_, _) =>
            {
                Select(card);
                if (open.Enabled)
                    OpenProject?.Invoke();
            };
            card.KeyDown += (_, e) =>
            {
                if (e.KeyCode == Keys.Enter && open.Enabled)
                {
                    OpenProject?.Invoke();
                    e.SuppressKeyPress = true;
                }
                else if (e.KeyCode is Keys.Left or Keys.Right or Keys.Up or Keys.Down)
                {
                    MoveSelection(card, e.KeyCode);
                    e.SuppressKeyPress = true;
                }
            };
            grid.Controls.Add(card);
            if (string.Equals(project.File, selected, StringComparison.OrdinalIgnoreCase))
                restoreSelection = card;
        }
        grid.ResumeLayout();
        Select(restoreSelection ?? grid.Controls.OfType<ProjectCard>().FirstOrDefault());
        count.Visible = grid.Controls.Count == 0;
        count.Text = entries.Count == 0 ? "プロジェクトはまだありません。\n「新規作成」または「既存を追加…」から始めましょう。" : "検索に一致するプロジェクトがありません。";
        if (count.Visible) count.BringToFront();
    }
    internal void SelectProject(string file)
    {
        var card = grid.Controls.OfType<ProjectCard>().FirstOrDefault(item =>
            string.Equals(item.Project.File, file, StringComparison.OrdinalIgnoreCase));
        if (card != null)
        {
            Select(card);
            grid.ScrollControlIntoView(card);
        }
    }

    internal void RecordLaunch(ProjectEntry project, DateTimeOffset time)
    {
        var index = entries.FindIndex(item => string.Equals(item.File, project.File, StringComparison.OrdinalIgnoreCase));
        if (index < 0)
            return;
        entries[index] = entries[index] with { LastLaunch = time };
        foreach (var card in grid.Controls.OfType<ProjectCard>().Where(item =>
            string.Equals(item.Project.File, project.File, StringComparison.OrdinalIgnoreCase)))
        {
            card.Project = entries[index];
            card.Invalidate();
        }
    }

    void MoveSelection(ProjectCard current, Keys direction)
    {
        var cards = grid.Controls.OfType<ProjectCard>().ToArray();
        var index = Array.IndexOf(cards, current);
        var columns = cards.Count(card => card.Top == cards[0].Top);
        var offset = direction switch { Keys.Left => -1, Keys.Right => 1, Keys.Up => -columns, _ => columns };
        var target = index + offset;
        if (target >= 0 && target < cards.Length)
        {
            cards[target].Focus();
            grid.ScrollControlIntoView(cards[target]);
        }
    }

    void Select(ProjectCard? card)
    {
        if (selectedCard == card && card != null)
            return;
        if (selectedCard != null)
            selectedCard.Selected = false;
        selectedCard = card;
        if (selectedCard != null)
            selectedCard.Selected = true;
        UpdateSelection();
    }

    void UpdateSelection()
    {
        var p = Selected;
        open.Enabled = migrate.Enabled = delete.Enabled = p != null && File.Exists(p.File); remove.Enabled = p != null;
        if (p != null) configuration.Text = p.Configuration;
        detail.Text = p == null ? $"PROJECT LIBRARY   /   {entries.Count} プロジェクト\n\n一覧から選択すると詳細と起動・削除の操作が表示されます。" : $"{p.Name}   /   {p.EngineId} {p.Version} ({p.Association})\n{p.File}\nごみ箱へ移動すると、プロジェクトのフォルダー全体が対象になります。";
    }
    static void Choice(Button button, string[] choices, Action<int> changed)
    {
        var menu = new ContextMenuStrip();
        button.Disposed += (_, _) => menu.Dispose();
        button.Click += (_, _) =>
        {
            menu.Items.Clear(); menu.BackColor = LauncherTheme.Raised; menu.ForeColor = LauncherTheme.Text;
            for (int i = 0; i < choices.Length; i++)
            {
                var index = i;
                var item = new ToolStripMenuItem(choices[i]) { Checked = button.Text == choices[i] };
                item.Click += (_, _) => { button.Text = choices[index]; changed(index); };
                menu.Items.Add(item);
            }
            menu.Show(button, new Point(0, button.Height));
        };
    }
}
