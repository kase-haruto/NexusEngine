namespace NexusLauncher;

/*-----------------------------------------------------------------------------------------
 * ProjectCreationPanel
 * - Templateカード・選択Preview・Project入力を所有する配布Launcher用画面
 * - Engine検証と生成はサービスへ委譲する。Runtime Sceneへは依存しない
 *---------------------------------------------------------------------------------------*/
internal sealed class ProjectCreationPanel : UserControl
{
    readonly ResponsiveFlowPanel layout = new() { Dock = DockStyle.Fill, AutoScroll = true, FlowDirection = FlowDirection.TopDown, WrapContents = false, Padding = new(30, 0, 30, 24) };
    readonly FlowLayoutPanel cards = new() { AutoSize = true, WrapContents = true, Margin = new(0) };
    readonly PictureBox preview = new() { Height = 260, SizeMode = PictureBoxSizeMode.Zoom, BackColor = LauncherTheme.Panel };
    readonly Label previewFallback = new() { Dock = DockStyle.Fill, Text = "▧  Preview unavailable", TextAlign = ContentAlignment.MiddleCenter };
    readonly Label description = new() { AutoSize = true };
    readonly Label notice = new() { AutoSize = true, Text = "エンジンを選択してください。" };
    readonly Label engineStatus = new() { AutoSize = true };
    readonly TextBox name = new() { PlaceholderText = "MyProject" };
    readonly TextBox parent = new() { Text = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments) };
    readonly Button create = new() { Text = "＋  Create Project", AutoSize = true, MinimumSize = new(220, 42) };
    internal ProjectTemplateDescriptor? Selected { get; private set; }
    internal IReadOnlyList<TemplateCard> Cards => cards.Controls.OfType<TemplateCard>().ToArray();
    internal bool CanCreate => create.Enabled;
    internal bool PreviewAvailable => preview.Image != null;
    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    internal string ProjectName { get => name.Text; set => name.Text = value; }
    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    internal string ParentDirectory { get => parent.Text; set => parent.Text = value; }
    internal event Action? CreateRequested;
    bool engineReady;

    internal ProjectCreationPanel(ComboBox engines)
    {
        Dock = DockStyle.Fill;
        Controls.Add(layout);
        AddLabel("◈  Engine Version");
        engines.Height = 58;
        layout.Controls.Add(engines);
        layout.Controls.Add(engineStatus);
        AddLabel("▦  Templates");
        layout.Controls.Add(notice);
        layout.Controls.Add(cards);
        AddLabel("Selected Template");
        preview.Controls.Add(previewFallback);
        layout.Controls.Add(preview);
        layout.Controls.Add(description);
        AddLabel("Project Name");
        layout.Controls.Add(name);
        AddLabel("▱  Location");
        layout.Controls.Add(parent);
        var browse = new Button { Text = "▱  Browse…", AutoSize = true, MinimumSize = new(140, 36) };
        browse.Click += (_, _) =>
        {
            using var dialog = new FolderBrowserDialog();
            if (dialog.ShowDialog(this) == DialogResult.OK) parent.Text = dialog.SelectedPath;
        };
        layout.Controls.Add(browse);
        layout.Controls.Add(create);
        name.TextChanged += (_, _) => ValidateInput();
        parent.TextChanged += (_, _) => ValidateInput();
        create.Click += (_, _) => { if (CanCreate) CreateRequested?.Invoke(); };
        ValidateInput();
    }

    void AddLabel(string text) => layout.Controls.Add(new Label { Text = text, AutoSize = true, Margin = new(0, 18, 0, 8) });

    /** \brief Themeの共通設定後に専用コントロールの状態色を適用する */
    internal void ApplyAccents()
    {
        create.BackColor = LauncherTheme.Accent;
        create.ForeColor = Color.Black;
    }

    /** \brief Engine切替時は旧TemplateとPreviewを即座に無効化し、非同期応答を待つ */
    internal void Loading(Installed? engine)
    {
        SetTemplates([], false);
        engineStatus.Text = engine == null ? "利用可能なエンジンがありません。" : $"{engine.Configuration}  ·  検証中…\n{engine.Manifest}";
        notice.Text = engine == null ? "エンジンを登録・選択してください。" : "Templateを読み込んでいます…";
    }

    internal void SetTemplates(IEnumerable<ProjectTemplateDescriptor> templates, bool available, string? error = null)
    {
        engineReady = available;
        if (!available && error != null) engineStatus.Text = "Unavailable — Engine / Templateを確認してください。";
        Selected = null;
        foreach (var card in Cards) card.Dispose();
        cards.Controls.Clear();
        foreach (var template in templates)
        {
            var card = new TemplateCard(template);
            card.Click += (_, _) => SelectTemplate(template);
            cards.Controls.Add(card);
        }
        notice.Text = error ?? (cards.Controls.Count == 0 ? "このエンジンには対応Templateがありません。更新したSDKが必要です。" : "開始するLevelを選択してください。静的Previewは構成イメージです。");
        SelectTemplate(Cards.FirstOrDefault(c => c.Template.CanCreate)?.Template ?? Cards.FirstOrDefault()?.Template);
    }

    internal void SetEngineStatus(Installed engine, bool current) => engineStatus.Text =
        $"{(engine.Association == "installed" ? "Installed" : "Development")}  ·  {engine.Configuration}{(current ? "  ·  Current（既定）" : "")}\n{engine.Manifest}";

    internal void SelectTemplate(ProjectTemplateDescriptor? template)
    {
        Selected = template;
        foreach (var card in Cards) card.SetSelected(card.Template == template);
        var previous = preview.Image;
        preview.Image = template == null ? null : ProjectTemplatePreview.Load(template);
        previous?.Dispose();
        previewFallback.Visible = preview.Image == null;
        description.Text = template == null ? "Templateを選択してください。" : $"{template.Name}\n{template.Description}" +
            (template.CanCreate ? "" : $"\n作成できません: {template.Error}");
        ValidateInput();
    }

    void ValidateInput()
    {
        create.Enabled = false;
        if (!engineReady || Selected?.CanCreate != true) return;
        try { ProjectCreationService.Target(parent.Text, name.Text); create.Enabled = true; }
        catch (Exception ex) when (ex is IOException or ArgumentException or UnauthorizedAccessException or NotSupportedException) { }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) preview.Image?.Dispose();
        base.Dispose(disposing);
    }
}

/*-----------------------------------------------------------------------------------------
 * TemplateCard
 * - カード全体を一つのButtonとして扱い、Tab・Enter・Spaceでも選択できる
 * - Hoverは背景、Selectedは枠とチェックで区別する。画像はカードが所有する
 *---------------------------------------------------------------------------------------*/
internal sealed class TemplateCard : Button
{
    internal ProjectTemplateDescriptor Template { get; }
    readonly Image? preview;
    bool selected, hovered;
    internal TemplateCard(ProjectTemplateDescriptor template)
    {
        Template = template;
        preview = ProjectTemplatePreview.Load(template);
        Size = new(246, 204);
        Margin = new(0, 0, 12, 12);
        FlatStyle = FlatStyle.Flat;
        FlatAppearance.BorderSize = 0;
        Cursor = Cursors.Hand;
        AccessibleName = template.Name;
        AccessibleDescription = template.Description + template.Error;
        UseVisualStyleBackColor = false;
        MouseEnter += (_, _) => { hovered = true; Invalidate(); };
        MouseLeave += (_, _) => { hovered = false; Invalidate(); };
    }
    internal void SetSelected(bool value) { selected = value; Invalidate(); }
    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(hovered ? LauncherTheme.Input : LauncherTheme.Raised);
        var imageRect = new Rectangle(9, 9, Width - 18, 112);
        if (preview != null)
        {
            var ratio = Math.Min((float)imageRect.Width / preview.Width, (float)imageRect.Height / preview.Height);
            var size = new Size((int)(preview.Width * ratio), (int)(preview.Height * ratio));
            g.DrawImage(preview, new Rectangle(imageRect.X + (imageRect.Width - size.Width) / 2, imageRect.Y + (imageRect.Height - size.Height) / 2, size.Width, size.Height));
        }
        else TextRenderer.DrawText(g, "▧  Preview unavailable", Font, imageRect, LauncherTheme.Muted, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
        TextRenderer.DrawText(g, (selected ? "✓  " : "") + Template.Name, Font, new Rectangle(12, 127, Width - 24, 26), LauncherTheme.Text, TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
        TextRenderer.DrawText(g, Template.CanCreate ? Template.Description : "作成不可: " + Template.Error, Font,
            new Rectangle(12, 155, Width - 24, 40), LauncherTheme.Muted, TextFormatFlags.WordBreak | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
        using var border = new Pen(selected ? LauncherTheme.Accent : hovered ? LauncherTheme.Muted : LauncherTheme.Raised, selected ? 3 : 1);
        g.DrawRectangle(border, 2, 2, Width - 5, Height - 5);
        if (Focused) ControlPaint.DrawFocusRectangle(g, new Rectangle(5, 5, Width - 10, Height - 10));
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing) preview?.Dispose();
        base.Dispose(disposing);
    }
}
