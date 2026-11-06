namespace NexusLauncher;

// Project/Tools/UI/ImGuiStyle.h のEditorStylePaletteに配色を合わせる。
internal static class LauncherTheme
{
    internal static readonly Color Background = Color.FromArgb(37, 37, 37);
    internal static readonly Color Panel = Color.FromArgb(29, 29, 29);
    internal static readonly Color Raised = Color.FromArgb(47, 47, 47);
    internal static readonly Color Input = Color.FromArgb(52, 52, 52);
    internal static readonly Color Text = Color.FromArgb(230, 230, 230);
    internal static readonly Color Muted = Color.FromArgb(150, 150, 150);
    internal static readonly Color Accent = Color.FromArgb(242, 125, 46);
    internal static Func<object, (string Title, string Detail)> FormatItem = item => (item.ToString() ?? "", "");

    internal static void Apply(Control root)
    {
        root.BackColor = Background; root.ForeColor = Text;
        foreach (Control child in root.Controls) Apply(child);
        switch (root)
        {
            case TextBox text:
                text.BackColor = Input; text.BorderStyle = BorderStyle.FixedSingle;
                break;
            case ComboBox combo:
                combo.BackColor = Input; combo.FlatStyle = FlatStyle.Flat;
                combo.DrawMode = DrawMode.OwnerDrawFixed;
                combo.DrawItem += (_, e) =>
                {
                    using var fill = new SolidBrush(Input); e.Graphics.FillRectangle(fill, e.Bounds);
                    if (e.Index >= 0) TextRenderer.DrawText(e.Graphics, combo.GetItemText(combo.Items[e.Index]), e.Font, e.Bounds, Text, TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix);
                    e.DrawFocusRectangle();
                };
                break;
            case ListBox list:
                list.BackColor = Panel; list.BorderStyle = BorderStyle.None;
                list.DrawMode = DrawMode.OwnerDrawFixed; list.ItemHeight = 72;
                list.DrawItem += (_, e) => DrawItem(list, e);
                break;
            case Button button:
                button.FlatStyle = FlatStyle.Flat; button.FlatAppearance.BorderSize = 0;
                button.BackColor = Raised; button.Cursor = Cursors.Hand;
                button.FlatAppearance.MouseOverBackColor = Color.FromArgb(61, 61, 61);
                button.FlatAppearance.MouseDownBackColor = Color.FromArgb(74, 74, 74);
                break;
        }
    }

    static void DrawItem(ListBox list, DrawItemEventArgs e)
    {
        if (e.Index < 0) return;
        bool selected = (e.State & DrawItemState.Selected) != 0;
        using var brush = new SolidBrush(selected ? Raised : Panel);
        e.Graphics.FillRectangle(brush, e.Bounds);
        if (selected) { using var accent = new SolidBrush(Accent); e.Graphics.FillRectangle(accent, e.Bounds.X, e.Bounds.Y, 3, e.Bounds.Height); }
        var item = list.Items[e.Index];
        var (title, detail) = FormatItem(item);
        var flags = TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix;
        using var bold = new Font(e.Font!, FontStyle.Bold);
        TextRenderer.DrawText(e.Graphics, title, bold, new Rectangle(e.Bounds.X + 18, e.Bounds.Y + 12, e.Bounds.Width - 36, 25), Text, flags);
        TextRenderer.DrawText(e.Graphics, detail, e.Font, new Rectangle(e.Bounds.X + 18, e.Bounds.Y + 40, e.Bounds.Width - 36, 24), Muted, flags);
        e.DrawFocusRectangle();
    }
}
