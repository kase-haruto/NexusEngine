using System.Reflection;
using System.Text.Json;

namespace Nexus.Managed.Bootstrap;
/*-----------------------------------------------------------------------------------------
 * GameScriptCatalog (Mono)
 * - Native hostが所有する一つのMono Domain内へGame Assemblyを読み込む
 * - Assemblyの解放はNative側のDomain unloadで確認する。Disposeは購読とManaged参照だけを解除する
 *---------------------------------------------------------------------------------------*/
internal sealed class GameScriptCatalog : IDisposable
{
    internal static string ShadowRoot = ""; //< Native hostがDomainごとに設定する所有directory
    private Assembly[]? assemblies; //< このDomain内だけで保持するGame型
    private readonly string[] files; //< 複数Moduleの依存順root
    private bool disposed; //< AssemblyResolveの購読を一度だけ解除する
    internal string File { get; }
    internal string DirectoryPath => Path.GetDirectoryName(File)!;
    internal Assembly Assembly => Assemblies[0];
    internal IReadOnlyList<Assembly> Assemblies => assemblies ??= files.Select(System.Reflection.Assembly.LoadFrom).ToArray();

    /// <summary>現在のDomain専用shadowへcopyし、更新中の配置と稼働Assemblyを分離する。</summary>
    internal GameScriptCatalog(string file)
    {
        if(!Path.IsPathRooted(file) || !System.IO.File.Exists(file)) throw new FileNotFoundException("Missing Game Assembly.", file);
        var root = ShadowRoot.Length == 0 ? Path.Combine(Path.GetTempPath(), "NexusEngine.MonoMetadata", Guid.NewGuid().ToString("N")) : ShadowRoot;
        var destination = Path.Combine(root, Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(destination);
        File = Path.Combine(destination, Path.GetFileName(file));
        var source = new DirectoryInfo(Path.GetDirectoryName(file)!);
        var directories = new Stack<DirectoryInfo>(); directories.Push(source);
        long bytes = 0; var count = 0;
        while(directories.Count != 0) {
            var directory = directories.Pop();
            if((directory.Attributes & FileAttributes.ReparsePoint) != 0) throw new IOException("Managed shadow cannot traverse a reparse point.");
            foreach(var child in directory.GetDirectories()) directories.Push(child);
            foreach(var entry in directory.GetFiles()) {
                if((entry.Attributes & FileAttributes.ReparsePoint) != 0) throw new IOException("Managed shadow cannot copy a reparse point.");
                if(entry.Name.StartsWith("Nexus.Managed", StringComparison.OrdinalIgnoreCase)) continue;
                bytes = checked(bytes + entry.Length);
                if(++count > 4096 || bytes > 512L * 1024 * 1024) throw new IOException("Managed deployment exceeds shadow copy limits.");
                var relative = entry.FullName.Substring(source.FullName.TrimEnd(Path.DirectorySeparatorChar).Length + 1);
                var output = Path.Combine(destination, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(output)!); entry.CopyTo(output);
            }
        }
        var roots = new List<string> { File };
        var roster = Path.ChangeExtension(File, ".modules.json");
        if(System.IO.File.Exists(roster)) {
            var entries = JsonSerializer.Deserialize<string[]>(System.IO.File.ReadAllText(roster)) ?? throw new IOException("Invalid Managed module roster.");
            if(entries.Length is 0 or > 256) throw new IOException("Invalid Managed module count.");
            roots.Clear(); var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach(var entry in entries) {
                if(Path.GetFileName(entry) != entry || !entry.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) ||
                    entry.StartsWith("Nexus.Managed", StringComparison.OrdinalIgnoreCase) || !names.Add(entry)) throw new IOException("Invalid Managed module filename.");
                var path = Path.Combine(destination, entry);
                if(!System.IO.File.Exists(path)) throw new FileNotFoundException("Missing Managed module.", path);
                roots.Add(path);
            }
        }
        files = roots.ToArray();
        AppDomain.CurrentDomain.AssemblyResolve += Resolve;
    }
    /// <summary>安定APIは同じDomainの既存Assemblyへ解決し、ゲームの依存は所有shadow内から読む。</summary>
    private Assembly? Resolve(object? sender, ResolveEventArgs args)
    {
        if(disposed) return null;
        var name = new AssemblyName(args.Name);
        var api = typeof(ScriptBehaviour).Assembly;
        if(string.Equals(name.Name, api.GetName().Name, StringComparison.OrdinalIgnoreCase)) {
            if(name.Version != api.GetName().Version) throw new FileLoadException("GameScripts references another Nexus.Managed version.");
            return api;
        }
        if(name.Name?.StartsWith("Nexus.Managed", StringComparison.OrdinalIgnoreCase) == true) throw new FileLoadException("GameScripts may reference only Nexus.Managed.");
        if(string.IsNullOrEmpty(name.Name) || Path.GetFileName(name.Name) != name.Name) return null;
        var path = Path.Combine(DirectoryPath, name.Name + ".dll");
        return System.IO.File.Exists(path) ? System.Reflection.Assembly.LoadFrom(path) : null;
    }
    /// <summary>参照と購読を解除する。Domain unload完了とは区別する。</summary>
    public void Dispose()
    {
        if(disposed) return;
        disposed = true;
        AppDomain.CurrentDomain.AssemblyResolve -= Resolve;
        assemblies = null;
    }
}
