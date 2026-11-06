namespace NexusLauncher;

internal sealed class ResizableNotesBox : UserControl
{
    readonly Panel grip = new() { Dock = DockStyle.Bottom, Height = 18, Cursor = Cursors.SizeNS };
    int dragY, dragHeight;

    internal ResizableNotesBox(TextBox text)
    {
        Height = 260; MinimumSize = new(0, 120);
        text.Dock = DockStyle.Fill;
        Controls.Add(text); Controls.Add(grip);
        grip.AccessibleName = "変更点の表示領域を上下にドラッグしてサイズ変更";
        grip.Paint += (_, e) =>
        {
            using var pen = new Pen(LauncherTheme.Muted);
            int center = grip.ClientSize.Width / 2;
            e.Graphics.DrawLine(pen, center - 18, 7, center + 18, 7);
            e.Graphics.DrawLine(pen, center - 18, 10, center + 18, 10);
        };
        grip.MouseDown += (_, e) =>
        {
            if (e.Button != MouseButtons.Left) return;
            dragY = MousePosition.Y; dragHeight = Height; grip.Capture = true;
        };
        grip.MouseMove += (_, _) =>
        {
            if (grip.Capture) Height = Math.Clamp(dragHeight + MousePosition.Y - dragY, 120 * DeviceDpi / 96, 600 * DeviceDpi / 96);
        };
        grip.MouseUp += (_, _) => grip.Capture = false;
        grip.SizeChanged += (_, _) => grip.Invalidate();
    }
}
