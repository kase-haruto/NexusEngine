namespace NexusLauncher;

// Godotの一覧性とUEの選択内容の確認を組み合わせたプロジェクトブラウザー。
internal sealed class ProjectBrowser : UserControl
{
    readonly TextBox search = new() { Dock = DockStyle.Fill, PlaceholderText = "プロジェクト名・保存場所・バージョンで検索" };
    readonly Button sort = new() { Width = 155, Height = 36, Text = "名前の昇順", Margin = new(0, 0, 8, 8) };
    int sortIndex;
    readonly ListView list = new() { Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, MultiSelect = false, HideSelection = false, BorderStyle = BorderStyle.None, ShowItemToolTips = true };
    readonly Label count = new() { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft };
    readonly Label detail = new() { Dock = DockStyle.Fill, AutoEllipsis = true, Padding = new(12, 8, 12, 4) };
    readonly Button configuration = new() { Width = 108, Height = 36, Text = "Release" };
    readonly Button open = ActionButton("開く", 92), remove = ActionButton("一覧から外す", 120), delete = ActionButton("ごみ箱へ移動", 138);
    readonly List<ProjectEntry> entries = [];
    readonly ImageList rowHeight = new() { ImageSize = new(1, 42) };
    public ProjectEntry? Selected => list.SelectedItems.Count == 0 ? null : list.SelectedItems[0].Tag as ProjectEntry;
    public string Configuration => configuration.Text;
    public event Action? OpenProject, AddProject, RefreshProjects, RemoveProject, DeleteProject, CreateProject;

    public ProjectBrowser()
    {
        Dock = DockStyle.Fill;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 5, Padding = new(24, 0, 24, 20) };
        layout.ColumnStyles.Add(new(SizeType.Percent, 100));
        layout.RowStyles.Add(new(SizeType.Absolute, 54)); layout.RowStyles.Add(new(SizeType.Absolute, 46));
        layout.RowStyles.Add(new(SizeType.Percent, 100)); layout.RowStyles.Add(new(SizeType.Absolute, 100)); layout.RowStyles.Add(new(SizeType.Absolute, 50));
        var toolbar = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false };
        var create = ActionButton("＋ 新規作成", 126); var add = ActionButton("既存を追加…", 132); var refresh = ActionButton("更新", 74);
        create.Click += (_, _) => CreateProject?.Invoke(); add.Click += (_, _) => AddProject?.Invoke(); refresh.Click += (_, _) => RefreshProjects?.Invoke();
        toolbar.Controls.AddRange([create, add, refresh, sort]);
        var filters = new Panel { Dock = DockStyle.Fill };
        Choice(sort, ["名前の昇順", "名前の降順", "エンジン順", "保存場所順"], i => { sortIndex = i; Rebuild(); });
        var searchField = new Panel { Dock = DockStyle.Fill, Padding = new(0, 0, 8, 0) };
        searchField.Controls.Add(search); searchField.Controls.Add(new Label { Text = "検索", Dock = DockStyle.Left, Width = 48, TextAlign = ContentAlignment.TopLeft, Padding = new(0, 4, 0, 0) });
        filters.Controls.Add(searchField);
        list.SmallImageList = rowHeight;
        list.Columns.Add("プロジェクト", 210); list.Columns.Add("エンジン", 110); list.Columns.Add("状態", 130); list.Columns.Add("保存場所", 300);
        list.OwnerDraw = true;
        list.DrawColumnHeader += (_, e) =>
        {
            using var fill = new SolidBrush(LauncherTheme.Raised); e.Graphics.FillRectangle(fill, e.Bounds);
            TextRenderer.DrawText(e.Graphics, e.Header?.Text, list.Font, Rectangle.Inflate(e.Bounds, -8, 0), LauncherTheme.Muted, TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
        };
        list.DrawSubItem += (_, e) =>
        {
            var selected = e.Item?.Selected == true;
            using var fill = new SolidBrush(selected ? LauncherTheme.Raised : LauncherTheme.Panel); e.Graphics.FillRectangle(fill, e.Bounds);
            if (selected && e.ColumnIndex == 0) { using var accent = new SolidBrush(LauncherTheme.Accent); e.Graphics.FillRectangle(accent, e.Bounds.X, e.Bounds.Y, 3, e.Bounds.Height); }
            TextRenderer.DrawText(e.Graphics, e.SubItem?.Text, list.Font, Rectangle.Inflate(e.Bounds, -8, 0), selected ? LauncherTheme.Text : e.Item!.ForeColor, TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
        };
        list.SelectedIndexChanged += (_, _) => UpdateSelection();
        list.DoubleClick += (_, _) => { if (Selected != null && open.Enabled) OpenProject?.Invoke(); };
        list.KeyDown += (_, e) => { if (e.KeyCode == Keys.Enter && open.Enabled) { OpenProject?.Invoke(); e.Handled = true; } };
        list.Resize += (_, _) => { if (list.ClientSize.Width > 0) { list.Columns[0].Width = Math.Clamp(list.ClientSize.Width / 3, 150, 260); list.Columns[3].Width = Math.Max(150, list.ClientSize.Width - list.Columns[0].Width - 244); } };
        search.TextChanged += (_, _) => Rebuild();
        Choice(configuration, ["Release", "Develop", "Debug"], _ => { });
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false };
        actions.Controls.AddRange([configuration, open, remove, delete]);
        open.Click += (_, _) => OpenProject?.Invoke(); remove.Click += (_, _) => RemoveProject?.Invoke(); delete.Click += (_, _) => DeleteProject?.Invoke();
        var content = new Panel { Dock = DockStyle.Fill };
        content.Controls.Add(list); content.Controls.Add(count); count.Visible = false;
        layout.Controls.Add(toolbar, 0, 0); layout.Controls.Add(filters, 0, 1); layout.Controls.Add(content, 0, 2); layout.Controls.Add(detail, 0, 3); layout.Controls.Add(actions, 0, 4);
        Controls.Add(layout);
        UpdateSelection();
    }
    public void ApplyAccents()
    {
        list.BackColor = LauncherTheme.Panel; list.ForeColor = LauncherTheme.Text;
        detail.BackColor = LauncherTheme.Panel;
        open.BackColor = LauncherTheme.Accent; open.ForeColor = Color.Black;
        delete.ForeColor = Color.FromArgb(255, 153, 140);
    }
    static Button ActionButton(string text, int width) => new() { Text = text, Width = width, Height = 36, Margin = new(0, 0, 8, 8) };
    protected override void Dispose(bool disposing) { if (disposing) rowHeight.Dispose(); base.Dispose(disposing); }
    public void SetProjects(IEnumerable<ProjectEntry> projects)
    {
        entries.Clear(); entries.AddRange(projects); Rebuild();
    }
    void Rebuild()
    {
        var selected = Selected?.File;
        var query = search.Text.Trim();
        var filtered = entries.Where(p => $"{p.Name}\n{p.File}\n{p.Version}".Contains(query, StringComparison.CurrentCultureIgnoreCase));
        filtered = sortIndex switch { 1 => filtered.OrderByDescending(p => p.Name), 2 => filtered.OrderBy(p => p.Version).ThenBy(p => p.Name), 3 => filtered.OrderBy(p => p.File), _ => filtered.OrderBy(p => p.Name) };
        list.BeginUpdate(); list.Items.Clear();
        foreach (var p in filtered)
        {
            var item = new ListViewItem(p.Name) { Tag = p, ToolTipText = p.File };
            item.SubItems.AddRange([string.IsNullOrEmpty(p.Version) ? "Development" : $"v{p.Version}", File.Exists(p.File) ? "利用可能" : "見つかりません", Path.GetDirectoryName(p.File) ?? p.File]);
            if (!File.Exists(p.File)) item.ForeColor = LauncherTheme.Muted;
            list.Items.Add(item); if (p.File == selected) item.Selected = true;
        }
        list.EndUpdate();
        if (list.Items.Count > 0 && list.SelectedItems.Count == 0) list.Items[0].Selected = true;
        count.Visible = list.Items.Count == 0;
        count.Text = entries.Count == 0 ? "プロジェクトはまだありません。\n「新規作成」または「既存を追加…」から始めましょう。" : "検索に一致するプロジェクトがありません。";
        if (count.Visible) count.BringToFront();
        UpdateSelection();
    }
    void UpdateSelection()
    {
        var p = Selected;
        open.Enabled = delete.Enabled = p != null && File.Exists(p.File); remove.Enabled = p != null;
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
