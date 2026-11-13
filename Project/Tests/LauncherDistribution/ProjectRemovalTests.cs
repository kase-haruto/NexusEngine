using NexusLauncher;

internal static class ProjectRemovalTests
{
    public static int Run(string sandbox)
    {
        var root = Path.Combine(sandbox, "RecycleValidation"); Directory.CreateDirectory(root);
        var file = Path.Combine(root, "Game.nxproject"); File.WriteAllText(file, "{\"engine\":{}}");
        var project = new ProjectEntry(file, "Game", "1.1.1", "Release");
        if (ProjectRemoval.Validate(project, [project], []) != root) throw new Exception("Valid project rejected");
        int passed = 1;
        void Reject(Action action) { try { action(); } catch (InvalidDataException) { passed++; return; } throw new Exception("Unsafe project removal accepted"); }
        Reject(() => ProjectRemoval.Validate(project, [project], [root]));
        Reject(() => ProjectRemoval.Validate(project, [project], [sandbox]));
        Reject(() => ProjectRemoval.Validate(project, [project, project with { File = Path.Combine(root, "Other.nxproject") }], []));
        var other = Path.Combine(root, "Other.nxproject"); File.WriteAllText(other, "{}");
        Reject(() => ProjectRemoval.Validate(project, [project], [])); File.Delete(other);
        var manifest = Path.Combine(root, "NexusEngine.nxengine"); File.WriteAllText(manifest, "{}");
        Reject(() => ProjectRemoval.Validate(project, [project], [])); File.Delete(manifest);
        File.WriteAllText(file, "{}"); Reject(() => ProjectRemoval.Validate(project, [project], []));
        File.Delete(file); Reject(() => ProjectRemoval.Validate(project, [project], []));
        return passed;
    }
}
