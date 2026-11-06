namespace NexusLauncher;

internal sealed class ResponsiveFlowPanel : FlowLayoutPanel
{
    bool sizing;
    protected override void OnLayout(LayoutEventArgs e)
    {
        if (!sizing)
        {
            sizing = true;
            try
            {
                int width = Math.Max(180, ClientSize.Width - Padding.Horizontal - SystemInformation.VerticalScrollBarWidth - 8);
                foreach (Control control in Controls)
                {
                    if (control is Button) continue;
                    if (control is Label label && label.AutoSize) label.MaximumSize = new(width, 0);
                    else control.Width = width;
                }
            }
            finally { sizing = false; }
        }
        base.OnLayout(e);
    }
}
