using NexusLauncher;
using System.Text.Json;

/*-----------------------------------------------------------------------------------------
 * ProjectCreationTests
 * - 実CLIによる最小Project生成と、画像保存を伴わないWinForms操作を検証する
 * - 個人Registryやインストール済みSDKを変更しない
 *---------------------------------------------------------------------------------------*/
static class ProjectCreationTests
{
    internal static async Task Run(string root, string manifest, string tool)
    {
        Directory.CreateDirectory(root);
        var engine = new Installed(manifest, "test", "test", "Debug", "development");
        var templates = await ProjectCreationService.Templates(tool, engine, default);
        Check(templates.Length == 2 && templates.All(t => t.CanCreate), "Expected two usable project templates.");
        Check(templates.Select(t => t.Name).Order().SequenceEqual(new[] { "Basic Level", "Empty" }), "Display names differ.");
        foreach (var template in templates)
        {
            // 表示名を変えても同じIDのTemplateが生成される。
            var file = await ProjectCreationService.Create(tool, engine, template with { Name = "renamed" }, root, "日本語 " + template.Id, default);
            using var project = JsonDocument.Parse(File.ReadAllText(file));
            Check(!project.RootElement.TryGetProperty("modules", out var modules) || modules.GetArrayLength() == 0, "Unexpected game module.");
            var directory = Path.GetDirectoryName(file)!;
            var level = File.ReadAllLines(Path.Combine(directory, "Levels/Startup.level"));
            var entities = level.Where(l => l.StartsWith("ENTITY ")).ToArray();
            Check(template.Id == "blank" ? entities.Length == 0 : entities.Length == 2 && entities[0].EndsWith("\"Camera\"") && entities[1].EndsWith("\"Directional Light\""), "Unexpected scene entity.");
            Check(!Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories).Any(p => Path.GetExtension(p) is ".cs" or ".cpp" or ".h" or ".vcxproj"), "Demo or source code was copied.");
            Check(Directory.Exists(Path.Combine(directory, "Assets")), "Missing Assets directory.");
            using var image = ProjectTemplatePreview.Load(template);
            Check(image is { Width: 960, Height: 540 }, "Preview asset cannot be decoded.");
            try { await ProjectCreationService.Create(tool, engine, template, root, "日本語 " + template.Id, default); throw new Exception("Existing project was overwritten."); }
            catch (IOException) { }
        }
        // Control構造・選択操作・FallbackをSTA上で確認し、画面captureは行わない。
        Exception? failure = null;
        var thread = new Thread(() =>
        {
            try
            {
                using var panel = new ProjectCreationPanel(new ComboBox());
                panel.Size = new(900, 680);
                panel.CreateControl();
                panel.ParentDirectory = root;
                panel.ProjectName = "UI Project";
                panel.SetTemplates(templates, true);
                Check(panel.CanCreate && panel.PreviewAvailable, "Valid selection did not enable Create or Preview.");
                var emptyCard = panel.Cards.Single(c => c.Template.Id == "blank");
                Check(emptyCard.Controls.Count == 0, "Card has child hit targets.");
                emptyCard.PerformClick();
                Check(panel.Selected?.Id == "blank", "Whole-card button did not select.");
                foreach (var width in new[] { 900, 620, 520 })
                {
                    panel.Width = width;
                    panel.PerformLayout();
                    foreach (var card in panel.Cards)
                        Check(card.Width <= card.Parent!.ClientSize.Width, "Card is wider than its container.");
                }
                var unavailable = templates[0] with { PreviewImage = Path.Combine(root, "missing.png") };
                panel.SetTemplates([unavailable], true);
                Check(!panel.PreviewAvailable && panel.CanCreate, "Missing preview blocked creation.");
                var corrupt = Path.Combine(root, "bad.png"); File.WriteAllText(corrupt, "not an image");
                panel.SetTemplates([unavailable with { PreviewImage = corrupt }], true);
                Check(!panel.PreviewAvailable && panel.CanCreate, "Corrupt preview did not fall back.");
                panel.SetTemplates([unavailable with { PreviewKind = "future-runtime" }], true);
                Check(!panel.PreviewAvailable && panel.CanCreate, "Unknown preview source did not fall back.");
                panel.SetTemplates([unavailable with { Error = "Broken template" }], true);
                Check(!panel.CanCreate, "Broken template enabled creation.");
                panel.SetTemplates(templates, true);
                panel.Loading(engine);
                Check(panel.Selected == null && !panel.CanCreate, "Engine change retained a stale template.");
            }
            catch (Exception ex) { failure = ex; }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start(); thread.Join();
        if (failure != null) throw new Exception("UI control test failed.", failure);
        Console.WriteLine("PASS: two minimal projects, Unicode paths, stable IDs, no demo source, previews, whole-card selection, resize, missing/corrupt/unknown preview fallback, disabled broken templates and stale engine selection.");
    }
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
}
