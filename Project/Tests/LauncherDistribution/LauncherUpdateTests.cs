using NexusLauncher;
using System.Net;
using System.Security.Cryptography;

static class LauncherUpdateTests
{
    public static async Task<int> Run(string root)
    {
        if (LauncherUpdates.CatalogUrl(new("https://example.com/releases.json", "tool")) != "https://example.com/launcher-releases.json")
            throw new Exception("Launcher catalog was not derived from the engine catalog.");
        var bytes = new byte[] { 77, 90, 1, 2, 3 };
        var release = new Release("nexus-launcher", "1.1.0", "Release", "更新", "https://example.com/setup.exe", Convert.ToHexString(SHA256.HashData(bytes)), bytes.Length);
        using var client = new HttpClient(new Handler(bytes));
        var parent = Path.Combine(root, "LauncherUpdates");
        var installer = await LauncherUpdates.Download(release, parent, new Progress<int>(), default, client);
        if (!File.ReadAllBytes(installer).SequenceEqual(bytes)) throw new Exception("Setup contents changed.");
        foreach (var invalid in new[] { release with { Sha256 = new string('0', 64) }, release with { Bytes = bytes.Length + 1 }, release with { Bytes = bytes.Length - 1 }, release with { Id = "nexus" } })
        {
            try { await LauncherUpdates.Download(invalid, parent, new Progress<int>(), default, client); throw new Exception("Invalid Setup accepted."); }
            catch (InvalidDataException) { }
        }
        try { await LauncherUpdates.Download(release, parent, new Progress<int>(), new CancellationToken(true), client); throw new Exception("Setup cancellation ignored."); }
        catch (OperationCanceledException) { }
        if (Directory.GetDirectories(parent).Length != 1) throw new Exception("Failed or cancelled Setup left partial files.");
        return 7;
    }
    sealed class Handler(byte[] bytes) : HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
        {
            token.ThrowIfCancellationRequested();
            return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = new ByteArrayContent(bytes) });
        }
    }
}
