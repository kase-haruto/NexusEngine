using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using Nexus.Managed;

namespace Nexus.Managed.Bootstrap;
/*-----------------------------------------------------------------------------------------
 * GameScriptCatalog
 * - 一世代のGame Assemblyとcollectible contextだけを所有する。static cacheを作らない
 * - 成果物を専用shadowへコピーし、実行中のDLLと再コンパイル先を分離する
 *---------------------------------------------------------------------------------------*/
internal sealed class GameScriptCatalog
{
    private GameLoadContext? context; //< Releaseで強参照を解除する
    private Assembly? assembly; //< Reflection参照も破棄しなければUnloadは完了しない
    private Assembly[]? assemblies; //< 複数Managed Moduleも同じcollectible世代へ閉じ込める
    private string[] files = []; //< shadow内の依存順root。CLR objectを含まない
    internal string File { get; }
    internal string DirectoryPath => Path.GetDirectoryName(File)!;
    // 所有者へCatalogを渡してからGameコードをロードする。ロード失敗時もReleaseで監査できる。
    internal Assembly Assembly => assembly ??= (context ?? throw new ObjectDisposedException(nameof(GameScriptCatalog))).LoadFromAssemblyPath(File);
    internal IReadOnlyList<Assembly> Assemblies => assemblies ??= files.Select(path => (context ?? throw new ObjectDisposedException(nameof(GameScriptCatalog))).LoadFromAssemblyPath(path)).ToArray();
    private static readonly string ShadowRoot = Path.Combine(Path.GetTempPath(), "NexusEngine.Managed", Environment.ProcessId.ToString());
    /// <summary>同じ世代の依存物もコピーし、安定APIの型同一性だけを共有する。</summary>
    internal GameScriptCatalog(string file)
    {
        if (!Path.IsPathFullyQualified(file) || !System.IO.File.Exists(file)) throw new FileNotFoundException("GameScripts requires an existing absolute path.", file);
        var source = Path.GetDirectoryName(Path.GetFullPath(file))!;
        var destination = Path.Combine(ShadowRoot, Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(destination); File = Path.Combine(destination, Path.GetFileName(file));
        long bytes = 0; var count = 0;
        try
        {
            foreach (var entry in new DirectoryInfo(source).EnumerateFiles("*", new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint }))
            {
                if (entry.Name.StartsWith("Nexus.Managed", StringComparison.OrdinalIgnoreCase)) continue;
                bytes = checked(bytes + entry.Length);
                if (++count > 4096 || bytes > 512L * 1024 * 1024) throw new IOException("Managed deployment exceeds shadow copy limits.");
                var output = Path.Combine(destination, Path.GetRelativePath(source, entry.FullName));
                Directory.CreateDirectory(Path.GetDirectoryName(output)!); entry.CopyTo(output);
            }
            var roots = new List<string> { File };
            var roster = Path.ChangeExtension(File, ".modules.json");
            if (System.IO.File.Exists(roster)) {
                var entries = JsonSerializer.Deserialize<string[]>(System.IO.File.ReadAllText(roster)) ?? throw new IOException("Invalid Managed module roster.");
                if (entries.Length is 0 or > 256) throw new IOException("Invalid Managed module count.");
                roots.Clear(); var unique = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (var entry in entries) {
                    if (Path.GetFileName(entry) != entry || !entry.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) || entry.StartsWith("Nexus.Managed", StringComparison.OrdinalIgnoreCase) || !unique.Add(entry)) throw new IOException("Invalid Managed module file.");
                    var path = Path.Combine(destination, entry);
                    if (!System.IO.File.Exists(path)) throw new FileNotFoundException("Missing Managed module.", path);
                    roots.Add(path);
                }
                if (!roots.Contains(File, StringComparer.OrdinalIgnoreCase)) throw new IOException("Managed roster omits primary assembly.");
            }
            files = roots.ToArray();
            // rootをロードする前に実際のAssembly identityを読む。filenameの別名で同じModuleを二重ロードしない。
            // GetAssemblyNameはmetadata読取だけを行い、constructor / ModuleInitializerを実行しない。
            var identities = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach(var path in files) {
                var identity = AssemblyName.GetAssemblyName(path);
                var name = identity.Name ?? throw new FileLoadException("Managed root has no Assembly name.");
                if(name.StartsWith("Nexus.Managed", StringComparison.OrdinalIgnoreCase) || !identities.Add(name + '\0' + identity.CultureName))
                    throw new FileLoadException($"Duplicate or reserved Managed root Assembly identity: {name}");
            }
            context = new GameLoadContext(files);
        }
        catch {
            assembly = null;
            try { context?.Unload(); } finally { context = null; }
            // このconstructorではGame Assemblyをまだロードしていない。失敗した自分のshadowだけを片付ける。
            try { DeleteShadow(destination); } catch(Exception exception) when(exception is IOException or UnauthorizedAccessException) { }
            throw;
        }
    }
    /// <summary>JITのlocal参照を呼出元のGC確認へ残さない。弱参照とパスだけを返す。</summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    internal RetiredGame Release()
    {
        var old = context ?? throw new ObjectDisposedException(nameof(GameScriptCatalog));
        var weak = new WeakReference(old, trackResurrection: true);
        assembly = null; assemblies = null; files = []; context = null;
        string? failure = null;
        try { old.Unload(); }
        catch (Exception exception) { failure = $"Unload callback failed: {exception.GetType().Name}: {exception.Message}"; }
        return new RetiredGame(weak, DirectoryPath, failure);
    }
    /// <summary>自分で生成したGUID直下だけを削除する。解放確認前には呼ばない。</summary>
    internal static void DeleteShadow(string directory)
    {
        var path = Path.GetFullPath(directory);
        if (!string.Equals(Path.GetDirectoryName(path), Path.GetFullPath(ShadowRoot), StringComparison.OrdinalIgnoreCase) ||
            !Guid.TryParseExact(Path.GetFileName(path), "N", out _)) throw new IOException("Invalid shadow ownership.");
        if (Directory.Exists(path)) Directory.Delete(path, recursive: true);
    }
    /*-----------------------------------------------------------------------------------------
     * GameLoadContext
     * - 公式Resolverで依存を同じcollectible世代へ閉じ込める。安定APIだけを共有する
     * - Bootstrap参照は禁止し、ゲームからsession内部を保持させない
     *---------------------------------------------------------------------------------------*/
    private sealed class GameLoadContext(string[] files) : AssemblyLoadContext($"Nexus.GameScripts:{files[0]}", isCollectible: true)
    {
        private readonly AssemblyDependencyResolver[] resolvers = files.Select(file => new AssemblyDependencyResolver(file)).ToArray(); //< 全Moduleを同じ世代で解決する
        private readonly string shadowPrefix = Path.GetDirectoryName(Path.GetFullPath(files[0]))!.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar; //< この世代が所有する依存物の範囲
        /// <summary>全Moduleの有効な解決候補を比較する。同名・異内容を登録順で選ばず、同一内容だけを共有する。</summary>
        private string? ResolveSharedDependency(IEnumerable<string?> candidates, string name)
        {
            string? selected = null;
            foreach(var candidate in candidates) {
                if(candidate == null) continue;
                var path = Path.GetFullPath(candidate);
                if(!path.StartsWith(shadowPrefix, StringComparison.OrdinalIgnoreCase)) throw new FileLoadException($"Dependency escapes the Managed generation: {name}");
                if(selected == null) { selected = path; continue; }
                if(string.Equals(selected, path, StringComparison.OrdinalIgnoreCase)) continue;
                // hash比較は解決先が複数ある場合だけ行い、Managed / Native libraryとも曖昧な世代混在を拒否する。
                using var first = System.IO.File.OpenRead(selected);
                using var second = System.IO.File.OpenRead(path);
                if(!SHA256.HashData(first).AsSpan().SequenceEqual(SHA256.HashData(second)))
                    throw new FileLoadException($"Conflicting dependency across Managed Modules: {name}");
            }
            return selected;
        }
        protected override Assembly? Load(AssemblyName name)
        {
            var api = typeof(ScriptBehaviour).Assembly;
            if (string.Equals(name.Name, api.GetName().Name, StringComparison.OrdinalIgnoreCase))
            {
                if (name.Version != api.GetName().Version) throw new FileLoadException("GameScripts references another Nexus.Managed version.");
                return api;
            }
            if (name.Name?.StartsWith("Nexus.Managed", StringComparison.OrdinalIgnoreCase) == true) throw new FileLoadException("GameScripts may reference only the stable Nexus.Managed API, not Bootstrap or Metadata internals.");
            var dependency = ResolveSharedDependency(resolvers.Select(resolver => resolver.ResolveAssemblyToPath(name)), name.FullName ?? name.Name ?? "<unknown>");
            return dependency == null ? null : LoadFromAssemblyPath(dependency);
        }
        protected override nint LoadUnmanagedDll(string name)
        {
            var dependency = ResolveSharedDependency(resolvers.Select(resolver => resolver.ResolveUnmanagedDllToPath(name)), name);
            return dependency == null ? 0 : LoadUnmanagedDllFromPath(dependency);
        }
    }
}
/*-----------------------------------------------------------------------------------------
 * RetiredGame
 * - 弱参照と所有shadowパスだけを保持する。Assembly / Type / 例外を保持しない
 *---------------------------------------------------------------------------------------*/
internal sealed record RetiredGame(WeakReference Context, string DirectoryPath, string? UnloadError = null)
{
    /// <summary>Unload要求と完了を区別する。有限回のGC後も生存していれば失敗とする。</summary>
    internal bool Confirm()
    {
        if(UnloadError != null) return false; // cleanup契約違反時に解放成功と誤認しない。例外自体はrootしない。
        for (var attempt = 0; Context.IsAlive && attempt < 10; ++attempt)
        {
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        }
        return !Context.IsAlive;
    }
}
