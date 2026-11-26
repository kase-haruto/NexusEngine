namespace NexusLauncher;

/*-----------------------------------------------------------------------------------------
 * ProjectCard
 * - 16:9のプレビューと識別情報を描画し、選択・ホバー・キーボードフォーカスを示す
 *---------------------------------------------------------------------------------------*/
internal sealed class ProjectCard : Control
{
    internal const int CardWidth = 192;
    readonly ProjectThumbnailCache thumbnails;
    readonly string? bundled;
    bool hovered, selected;
    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    internal ProjectEntry Project { get; set; }
    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    internal bool Selected
    {
        set { selected = value; Invalidate(); }
    }

    internal ProjectCard(ProjectEntry project, ProjectThumbnailCache thumbnails)
    {
        Project = project;
        this.thumbnails = thumbnails;
        bundled = ProjectPreviewMetadata.BundledImage(project);
        SetStyle(ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.AllPaintingInWmPaint |
            ControlStyles.Selectable | ControlStyles.StandardClick | ControlStyles.StandardDoubleClick, true);
        Width = CardWidth;
        Height = 176;
        Margin = new(0, 0, 12, 12);
        TabStop = true;
        Cursor = Cursors.Hand;
        AccessibleName = project.Name;
        AccessibleRole = AccessibleRole.PushButton;
    }

    protected override bool IsInputKey(Keys keyData) =>
        (keyData & Keys.KeyCode) is Keys.Enter or Keys.Left or Keys.Right or Keys.Up or Keys.Down ||
        base.IsInputKey(keyData);

    protected override void OnMouseEnter(EventArgs e) { hovered = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hovered = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnGotFocus(EventArgs e) { Invalidate(); base.OnGotFocus(e); }
    protected override void OnLostFocus(EventArgs e) { Invalidate(); base.OnLostFocus(e); }
    protected override void OnMouseDown(MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left)
            Focus();
        base.OnMouseDown(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var graphics = e.Graphics;
        graphics.Clear(selected || hovered ? LauncherTheme.Raised : LauncherTheme.Panel);
        var imageBounds = new Rectangle(8, 8, Width - 16, (Width - 16) * 9 / 16);
        using var backdrop = new SolidBrush(LauncherTheme.Background);
        graphics.FillRectangle(backdrop, imageBounds);
        var image = thumbnails.Find(Project, bundled);
        if (image != null)
            DrawContained(graphics, image, imageBounds);
        else if (thumbnails.Icon is Image icon)
            DrawContained(graphics, icon, new(imageBounds.X + (imageBounds.Width - 36) / 2, imageBounds.Y + (imageBounds.Height - 36) / 2, 36, 36));
        else
            graphics.DrawIcon(SystemIcons.Application, new(imageBounds.X + (imageBounds.Width - 32) / 2, imageBounds.Y + (imageBounds.Height - 32) / 2, 32, 32));

        bool available = File.Exists(Project.File);
        var engineVersion = string.IsNullOrEmpty(Project.Version) ? Project.ResolvedEngineVersion : Project.Version;
        var version = string.IsNullOrEmpty(engineVersion) ? "Development" : $"v{engineVersion}";
        var launch = Project.LastLaunch?.ToLocalTime().ToString("yyyy/MM/dd HH:mm") ?? "未記録";
        using var bold = new Font(Font, FontStyle.Bold);
        DrawLine(Project.Name, bold, available ? LauncherTheme.Text : LauncherTheme.Muted, 112);
        DrawLine(string.IsNullOrEmpty(Project.Languages) ? version : $"{version}  ·  {Project.Languages}", Font, LauncherTheme.Muted, 132);
        DrawLine(available ? $"起動  {launch}" : "プロジェクトが見つかりません", Font, LauncherTheme.Muted, 151);

        using var border = new Pen(selected ? LauncherTheme.Accent : hovered ? LauncherTheme.Muted : LauncherTheme.Input, selected ? 2 : 1);
        graphics.DrawRectangle(border, 1, 1, Width - 3, Height - 3);
        if (Focused && ShowFocusCues)
            ControlPaint.DrawFocusRectangle(graphics, new Rectangle(4, 4, Width - 8, Height - 8));

        void DrawLine(string value, Font font, Color color, int y) =>
            TextRenderer.DrawText(graphics, value, font, new Rectangle(8, y, Width - 16, 19), color,
                TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
    }

    static void DrawContained(Graphics graphics, Image image, Rectangle bounds)
    {
        var scale = Math.Min((float)bounds.Width / image.Width, (float)bounds.Height / image.Height);
        var width = (int)Math.Round(image.Width * scale);
        var height = (int)Math.Round(image.Height * scale);
        graphics.DrawImage(image, new Rectangle(bounds.X + (bounds.Width - width) / 2, bounds.Y + (bounds.Height - height) / 2, width, height));
    }
}
