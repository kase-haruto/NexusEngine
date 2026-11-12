using System.Text;
using System.Runtime.CompilerServices;
using Nexus.Managed;
using Nexus.Managed.Bootstrap;

/*-----------------------------------------------------------------------------------------
 * Program
 * - ビルド時だけ動く別processでC# Reflectionを値Metadataへ変換する
 * - EditorはこのJSONだけを読む。Scriptのconstructor / getterはWorld非依存である必要がある
 *---------------------------------------------------------------------------------------*/
internal static class Program
{
    /// <summary>Game Type参照を別stackへ隔離し、Exporter自身のUnload確認を妨げない。</summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
#if NEXUS_MONO
    private static string Export(string file)
    {
        // 親側が一意なshadowを所有し、子Domainの解放確認後だけ削除する。
        var shadow = Path.Combine(Path.GetTempPath(), "NexusEngine.MonoMetadata", Guid.NewGuid().ToString("N"));
        var domain = AppDomain.CreateDomain("Nexus.Metadata", null, new AppDomainSetup {
            ApplicationBase = Path.GetDirectoryName(typeof(Program).Assembly.Location),
            ConfigurationFile = AppDomain.CurrentDomain.SetupInformation.ConfigurationFile
        });
        try {
            var worker = (MonoMetadataWorker)domain.CreateInstanceAndUnwrap(typeof(Program).Assembly.FullName, typeof(MonoMetadataWorker).FullName);
            return worker.Export(file, shadow);
        }
        finally {
            AppDomain.Unload(domain);
            // Unload例外ではここへ到達させず、参照が残る可能性のある配置を保持する。
            if(Directory.Exists(shadow)) Directory.Delete(shadow, true);
        }
    }
#else
    private static string Export(string file, out RetiredGame retired)
    {
        var catalog = new GameScriptCatalog(file);
        try { return new ScriptMetadata(catalog.Assemblies).Export(); }
        finally { retired = catalog.Release(); }
    }
#endif
    /// <summary>全型の検証が成功してからsidecarを置換する。失敗時はBuildを失敗させる。</summary>
    private static int Main(string[] args)
    {
        string? temporary = null;
        try
        {
            if (args.Length != 2) throw new ArgumentException("Usage: Nexus.Managed.Metadata <GameScripts.dll> <output.json>");
#if NEXUS_MONO
            var text = Export(Path.GetFullPath(args[0]));
#else
            var text = Export(Path.GetFullPath(args[0]), out var retired);
            if(!retired.Confirm()) throw new InvalidOperationException("Metadata constructor / getter retained a collectible context.");
            GameScriptCatalog.DeleteShadow(retired.DirectoryPath);
#endif
            var output = Path.GetFullPath(args[1]);
            temporary = output + "." + Guid.NewGuid().ToString("N") + ".tmp";
            File.WriteAllText(temporary, text, new UTF8Encoding(false));
            if(File.Exists(output)) File.Replace(temporary, output, null);
            else File.Move(temporary, output);
            temporary = null;
            return 0;
        }
        catch (Exception exception) { Console.Error.WriteLine($"Managed metadata: {exception}"); return 1; }
        finally { if (temporary != null && File.Exists(temporary)) File.Delete(temporary); }
    }
}
#if NEXUS_MONO
/*-----------------------------------------------------------------------------------------
 * MonoMetadataWorker
 * - 別Domain内だけでGame型を所有し、Exporterの呼出元へJSON文字列を返す
 * - Domain unloadが成功してから出力を確定し、GameのThread / delegateをToolへ残さない
 *---------------------------------------------------------------------------------------*/
public sealed class MonoMetadataWorker : MarshalByRefObject
{
    public string Export(string file, string shadow) {
        GameScriptCatalog.ShadowRoot = shadow;
        using var catalog = new GameScriptCatalog(file);
        return new ScriptMetadata(catalog.Assemblies).Export();
    }
    public override object InitializeLifetimeService() => null!;
}
#endif
