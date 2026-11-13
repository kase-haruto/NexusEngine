using NexusLauncher;
using System.IO.Compression;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

if (args.Length == 1 && args[0] == "--verify-public-download")
{
    var catalog = await Distribution.LoadCatalog("https://nexus-engine.nexus-engine-website.workers.dev/releases.json", default);
    if (catalog.Length != 3) throw new Exception("Public version catalog is incomplete.");
    var latest = catalog.Single(item => item.Version == "1.1.1");
    using var response = await Distribution.DownloadHttps(latest.Url, default);
    response.EnsureSuccessStatusCode();
    using var body = await response.Content.ReadAsStreamAsync();
    var sha256 = Convert.ToHexString(await SHA256.HashDataAsync(body));
    if (!sha256.Equals(latest.Sha256, StringComparison.OrdinalIgnoreCase)) throw new Exception("Public SDK checksum mismatch.");
    Console.WriteLine("PASS: Launcher loaded three public versions and downloaded verified SDK 1.1.1 through GitHub HTTPS redirects.");
    return;
}

// 子プロセス用の代役で引数の扱いを検証し、個人のRegistryには触れない。
if (args.Length == 3 && args[0] is "engine-register" or "engine-remove")
{
    if (args[0] == "engine-remove" && Path.GetFileName(args[2]) == "reject-remove.txt") { Environment.ExitCode = 1; return; }
    File.AppendAllText(args[2], args[1] + "\n"); return;
}

int passed = 0;
void Reject(Action action)
{
    try { action(); } catch (InvalidDataException) { passed++; return; }
    throw new Exception("Expected rejection");
}
// サンドボックス内でも.NETとネイティブ側が共有できる作業フォルダーにテストデータを置く。
var root = Path.Combine(AppContext.BaseDirectory, "TestRuns", Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);
try
{
    passed += ProjectRemovalTests.Run(root);
    Reject(() => Distribution.Https("http://example.com/releases.json"));
    Reject(() => Distribution.Https("https://user:secret@example.com/releases.json"));
    Reject(() => Distribution.Within(root, "../escape.txt"));
    Reject(() => Distribution.Within(root, "C:\\escape.txt"));
    Reject(() => Distribution.Within(root, "file:stream"));
    try { InstallationFiles.NoRunningExecutables(AppContext.BaseDirectory); throw new Exception("Running executable was not detected"); }
    catch (IOException) { passed++; }
    var release = new Release("nexus", "0.1.5", "Release", "修正", "https://example.com/engine.zip", new string('a', 64), 100);
    Distribution.Validate(release); passed++;
    Reject(() => Distribution.Validate(release with { Version = "../bad" }));
    Reject(() => Distribution.Validate(release with { Sha256 = "bad" }));
    Reject(() => Distribution.Validate(release with { Bytes = 0 }));
    var zip = Path.Combine(root, "escape.zip");
    using (var archive = ZipFile.Open(zip, ZipArchiveMode.Create)) { using var writer = new StreamWriter(archive.CreateEntry("../outside.txt").Open()); writer.Write("escape"); }
    Reject(() => Distribution.Extract(zip, Path.Combine(root, "escape"), 1000, default));
    var valid = Path.Combine(root, "valid.zip");
    using (var archive = ZipFile.Open(valid, ZipArchiveMode.Create)) { using var writer = new StreamWriter(archive.CreateEntry("SDK/test.txt").Open()); writer.Write("valid"); }
    Reject(() => Distribution.Extract(valid, Path.Combine(root, "limited"), 1, default));
    var extracted = Path.Combine(root, "extracted");
    Distribution.Extract(valid, extracted, 1000, default);
    if (File.ReadAllText(Path.Combine(extracted, "SDK/test.txt")) != "valid") throw new Exception("Extract failed"); passed++;
    var duplicate = Path.Combine(root, "duplicate.zip");
    using (var archive = ZipFile.Open(duplicate, ZipArchiveMode.Create)) { archive.CreateEntry("same.txt"); archive.CreateEntry("SAME.txt"); }
    try { Distribution.Extract(duplicate, Path.Combine(root, "duplicate"), 1000, default); throw new Exception("Duplicate accepted"); } catch (IOException) { passed++; }
    var cancelled = new CancellationToken(true);
    try { Distribution.Extract(valid, Path.Combine(root, "cancelled"), 1000, cancelled); throw new Exception("Cancellation ignored"); } catch (OperationCanceledException) { passed++; }
    async Task RejectAsync(Func<Task> action)
    {
        try { await action(); } catch (InvalidDataException) { passed++; return; }
        throw new Exception("Expected asynchronous rejection");
    }
    using (var redirects = new HttpClient(new RedirectFixtureHandler((request, count) => count == 1
        ? new HttpResponseMessage(HttpStatusCode.Found) { Headers = { Location = new Uri("https://release-assets.githubusercontent.com/signed.zip") } }
        : new HttpResponseMessage(HttpStatusCode.OK) { Content = new StringContent("package") })))
    {
        using var response = await Distribution.DownloadHttps("https://github.com/package.zip", default, redirects);
        if (await response.Content.ReadAsStringAsync() != "package") throw new Exception("HTTPS CDN redirect failed"); passed++;
    }
    foreach (var destination in new[] { "http://example.com/package.zip", "https://user:secret@example.com/package.zip", "https://example.com/loop" })
    {
        using var redirects = new HttpClient(new RedirectFixtureHandler((request, count) => new HttpResponseMessage(HttpStatusCode.Found) { Headers = { Location = new Uri(destination) } }));
        await RejectAsync(() => Distribution.DownloadHttps("https://github.com/package.zip", default, redirects));
    }
    using var handler = new FixtureHandler();
    using var client = new HttpClient(handler);
    var sdkZip = Path.Combine(root, "sdk.zip");
    byte[] EngineZip(string version)
    {
        using var memory = new MemoryStream();
        using (var package = new ZipArchive(memory, ZipArchiveMode.Create, true))
        using (var writer = new StreamWriter(package.CreateEntry("NexusEngine.nxengine").Open()))
            writer.Write(JsonSerializer.Serialize(new { association = "installed", id = "nexus", version, configuration = "Release" }));
        return memory.ToArray();
    }
    var engineBytes = EngineZip("0.1.5");
    var downloadable = release with { Bytes = engineBytes.Length, Sha256 = Convert.ToHexString(SHA256.HashData(engineBytes)) };
    handler.Content = engineBytes;
    var versions = Path.Combine(root, "versions");
    var registry = Path.Combine(root, "fixture-registry.txt");
    var reporter = new Progress<int>();
    var tool = Environment.ProcessPath!;
    var first = await Distribution.Install(downloadable, versions, tool, reporter, default, client, registry);
    if (!File.Exists(first) || !File.ReadAllText(registry).Contains(first)) throw new Exception("Install/register failed"); passed++;
    handler.Content = EngineZip("0.1.6");
    var second = downloadable with { Version = "0.1.6", Bytes = handler.Content.Length, Sha256 = Convert.ToHexString(SHA256.HashData(handler.Content)) };
    var secondManifest = await Distribution.Install(second, versions, tool, reporter, default, client, registry);
    if (!File.Exists(first) || !File.Exists(secondManifest)) throw new Exception("Side-by-side installation failed"); passed++;
    try { await Distribution.Install(second, versions, tool, reporter, default, client, registry); throw new Exception("Existing version overwritten"); } catch (IOException) { passed++; }
    await RejectAsync(() => Distribution.Install(downloadable with { Version = "bad-hash", Sha256 = new string('0',64) }, versions, tool, reporter, default, client, registry));
    await RejectAsync(() => Distribution.Install(downloadable with { Version = "bad-size", Bytes = 1 }, versions, tool, reporter, default, client, registry));
    await RejectAsync(() => Distribution.Install(second with { Version = "wrong-identity" }, versions, tool, reporter, default, client, registry));
    if (Directory.GetDirectories(versions, ".download-*").Length != 0 || Directory.GetDirectories(versions).Length != 2) throw new Exception("Failed installation leaked files"); passed++;
    handler.Content = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(new Catalog(1, [downloadable]), Distribution.Json));
    var loaded = await Distribution.LoadCatalog("https://example.com/releases.json", default, client);
    if (loaded.Length != 1 || loaded[0] != downloadable) throw new Exception("Catalog load failed"); passed++;
    handler.Content = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(new Catalog(1, [downloadable, downloadable]), Distribution.Json));
    await RejectAsync(() => Distribution.LoadCatalog("https://example.com/releases.json", default, client));
    var localFolder = Path.Combine(root, "local-packages"); Directory.CreateDirectory(localFolder);
    var localRelease = downloadable with { Url = "nexus.zip" };
    File.WriteAllBytes(Path.Combine(localFolder, localRelease.Url), engineBytes);
    var localFile = Path.Combine(localFolder, "releases.local.json");
    File.WriteAllText(localFile, JsonSerializer.Serialize(new Catalog(1, [localRelease]), Distribution.Json));
    var localList = await Distribution.LoadLocalCatalog(localFile, default);
    if (localList.Single() != localRelease) throw new Exception("Local catalog failed"); passed++;
    var localManifest = await Distribution.Install(localList[0], Path.Combine(root, "local-installed"), tool, reporter, default, engineRegistry: registry, localCatalogRoot: localFolder);
    if (!File.Exists(localManifest)) throw new Exception("Local installation failed"); passed++;
    var localEngine = new Installed(localManifest, "nexus", "0.1.5", "Release");
    var localRoot = Path.GetDirectoryName(localManifest)!;
    var localReceipt = Distribution.EngineReceipt(localManifest, registry);
    if (!File.Exists(localReceipt)) throw new Exception("Install ownership record missing"); passed++;
    async Task PreserveOnFailure(Func<Task> action)
    {
        var previousRegistry = File.ReadAllText(registry);
        try { await action(); throw new Exception("Unsafe uninstall accepted"); }
        catch (Exception ex) when (ex is IOException or InvalidDataException) { }
        if (!File.Exists(localManifest) || File.ReadAllText(registry) != previousRegistry) throw new Exception("Failed uninstall modified files/registry");
        passed++;
    }
    var extra = Path.Combine(localRoot, "UserProject.nxproject"); File.WriteAllText(extra, "user data");
    await PreserveOnFailure(() => Distribution.UninstallEngine(localEngine, tool, registry, default));
    if (File.ReadAllText(extra) != "user data") throw new Exception("User file lost"); File.Delete(extra);
    var manifestBytes = File.ReadAllBytes(localManifest); File.AppendAllText(localManifest, " ");
    await PreserveOnFailure(() => Distribution.UninstallEngine(localEngine, tool, registry, default));
    File.WriteAllBytes(localManifest, manifestBytes);
    using (var held = new FileStream(localManifest, FileMode.Open, FileAccess.Read, FileShare.Read))
        await PreserveOnFailure(() => Distribution.UninstallEngine(localEngine, tool, registry, default));
    var ownership = InstallationFiles.Load(localReceipt, localRoot, "engine");
    File.WriteAllText(localReceipt, JsonSerializer.Serialize(ownership with { Root = Path.GetDirectoryName(first)! }, Distribution.Json));
    await PreserveOnFailure(() => Distribution.UninstallEngine(localEngine, tool, registry, default));
    File.WriteAllText(localReceipt, JsonSerializer.Serialize(ownership, Distribution.Json));
    await PreserveOnFailure(() => Distribution.UninstallEngine(localEngine with { Association = "development" }, tool, registry, default));
    try { await Distribution.UninstallEngine(localEngine, tool, registry, cancelled); throw new Exception("Uninstall cancellation ignored"); }
    catch (OperationCanceledException) { passed++; }
    var rejectRegistry = Path.Combine(root, "reject-remove.txt");
    await PreserveOnFailure(() => Distribution.UninstallEngine(localEngine, tool, rejectRegistry, default));
    await Distribution.UninstallEngine(localEngine, tool, registry, default);
    if (Directory.Exists(localRoot) || File.Exists(localReceipt) || !File.Exists(first) || !File.Exists(secondManifest)) throw new Exception("Version-scoped uninstall failed"); passed++;
    Reject(() => Distribution.Validate(localRelease));
    Reject(() => Distribution.Validate(localRelease with { Url = "../outside.zip" }, localFolder));
    Reject(() => Distribution.Validate(downloadable, localFolder));
    handler.Content = Encoding.UTF8.GetBytes("{\"schemaVersion\":1,\"releases\":[null]}");
    await RejectAsync(() => Distribution.LoadCatalog("https://example.com/releases.json", default, client));
    if (args.Length >= 1 && args[0] != "--native-only")
    {
        var assembly = System.Reflection.Assembly.LoadFile(Path.GetFullPath(args[0]));
        using var payload = assembly.GetManifestResourceStream("payload.zip") ?? throw new Exception("Setup payload missing");
        using var package = new ZipArchive(payload);
        foreach (var required in new[] { "NexusLauncher.exe", "launcher.json", "Tools/NexusProjectTool.exe", "Tools/dxcompiler.dll", "Tools/dxil.dll", "coreclr.dll", "System.Windows.Forms.dll", "Remove-Launcher.ps1" })
        {
            if (!package.Entries.Any(e => e.FullName.Replace('\\', '/') == required)) throw new Exception("Missing payload file: " + required);
            passed++;
        }
        using var removalScript = package.GetEntry("Remove-Launcher.ps1")!.Open();
        var bom = new byte[3]; removalScript.ReadExactly(bom);
        if (!bom.SequenceEqual(new byte[] { 0xef, 0xbb, 0xbf })) throw new Exception("Windows PowerShell removal script requires a UTF-8 BOM"); passed++;
    }
    if (args.Length >= 2)
    {
        var native = Path.GetFullPath(args[1]);
        var newProject = Path.Combine(root, "日本語 project");
        await Distribution.Run(native, ["create", newProject, "日本語プロジェクト"], default);
        var projectFile = Directory.GetFiles(newProject, "*.nxproject").Single();
        var recent = Path.Combine(root, "RecentProjects.json");
        await Distribution.Run(native, ["register", projectFile, recent], default);
        await Distribution.Run(native, ["register", projectFile, recent], default);
        using (var registered = JsonDocument.Parse(File.ReadAllText(recent)))
            if (registered.RootElement.GetProperty("projects").GetArrayLength() != 1) throw new Exception("Native project registration failed");
        passed++;
        var inspected = await Distribution.Run(native, ["inspect", projectFile], default);
        if (!inspected.Contains("日本語プロジェクト")) throw new Exception("Native UTF-8 output corrupted"); passed++;
        var entry = new ProjectEntry(projectFile, "日本語プロジェクト", "", "Release");
        ProjectRemoval.Validate(entry, [entry], []); passed++;
        // ごみ箱移動後と同じくdescriptorが元の場所にない状態で、登録解除を検証する。
        var moved = Path.Combine(root, "MovedProject"); Directory.Move(newProject, moved);
        await Distribution.Run(native, ["remove", projectFile, recent], default);
        using (var removed = JsonDocument.Parse(File.ReadAllText(recent)))
            if (removed.RootElement.GetProperty("projects").GetArrayLength() != 0 || !File.Exists(Path.Combine(moved, Path.GetFileName(projectFile)))) throw new Exception("Project unregister after relocation failed");
        passed++;
        handler.Content = engineBytes;
        var nativeRegistry = Path.Combine(root, "NativeEngines.json");
        var invalidRoot = Path.Combine(root, "native-inventory");
        try
        {
            await Distribution.Install(downloadable, invalidRoot, native, reporter, default, client, nativeRegistry);
            throw new Exception("Incomplete native inventory accepted");
        }
        catch (IOException ex) when (ex.InnerException is IOException)
        {
            if (File.Exists(nativeRegistry) || !File.Exists(Path.Combine(invalidRoot, "nexus-0.1.5-Release", "NexusEngine.nxengine"))) throw new Exception("Inventory rejection recovery failed");
            passed++;
        }
    }
    if (args.Length == 3)
    {
        var native = Path.GetFullPath(args[1]);
        var localCatalog = Path.GetFullPath(args[2]);
        var sdkReleases = await Distribution.LoadLocalCatalog(localCatalog, default);
        var actual = sdkReleases.Single(r => r.Version == "1.0.0" && r.Configuration == "Release");
        var actualRegistry = Path.Combine(root, "ActualEngines.json");
        var actualManifest = await Distribution.Install(actual, Path.Combine(root, "actual-installed"), native, reporter, default, engineRegistry: actualRegistry, localCatalogRoot: Path.GetDirectoryName(localCatalog));
        await Distribution.Run(native, ["engine-list", actualRegistry], default); passed++;
        var game = Path.Combine(root, "ActualGame");
        var file = await Distribution.Run(native, ["create-template", game, "V1 package verification", actualManifest, "blank"], default);
        await Distribution.Run(native, ["register", file, Path.Combine(root, "ActualProjects.json")], default); passed++;
        await Distribution.Run(native, ["build-editor", file, actualRegistry, "Release"], default);
        await Distribution.Run(native, ["verify-editor", file, actualRegistry, "Release"], default); passed++;
        await Distribution.Run(Path.Combine(game, "Build/Bin/x64/Release/GameEditor.exe"), ["--game", "--project", file, "--engine", Path.Combine(Path.GetDirectoryName(actualManifest)!, "Data"), "--check-game"], default); passed++;
        // 所有記録のない旧ランチャー導入版は、版別フォルダーのinventory検証後に削除する。
        File.Delete(Distribution.EngineReceipt(actualManifest, actualRegistry));
        var legacyExtra = Path.Combine(Path.GetDirectoryName(actualManifest)!, "KeepMyProject.nxproject");
        File.WriteAllText(legacyExtra, "project data");
        try { await Distribution.UninstallEngine(new(actualManifest, actual.Id, actual.Version, actual.Configuration), native, actualRegistry, default); throw new Exception("Legacy uninstall accepted extra data"); }
        catch (IOException) { }
        if (!File.Exists(legacyExtra) || !File.Exists(actualManifest)) throw new Exception("Legacy uninstall lost user data"); passed++;
        File.Delete(legacyExtra);
        await Distribution.UninstallEngine(new(actualManifest, actual.Id, actual.Version, actual.Configuration), native, actualRegistry, default);
        using var afterUninstall = JsonDocument.Parse(File.ReadAllText(actualRegistry));
        if (Directory.Exists(Path.GetDirectoryName(actualManifest)) || afterUninstall.RootElement.GetProperty("engines").GetArrayLength() != 0 || !File.Exists(file)) throw new Exception("Native uninstall removed a project or left an engine registered"); passed++;
    }
    passed += await LifecycleUninstallTests.Run(root);
    Console.WriteLine($"PASS: {passed} distribution checks");
}
catch (Exception ex) { Console.Error.WriteLine(ex); Environment.ExitCode = 1; }
finally { Directory.Delete(root, true); }

sealed class FixtureHandler : HttpMessageHandler
{
    public byte[] Content { get; set; } = [];
    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
        => Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = new ByteArrayContent(Content) });
}

sealed class RedirectFixtureHandler(Func<HttpRequestMessage, int, HttpResponseMessage> respond) : HttpMessageHandler
{
    int calls;
    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
        => Task.FromResult(respond(request, ++calls));
}
