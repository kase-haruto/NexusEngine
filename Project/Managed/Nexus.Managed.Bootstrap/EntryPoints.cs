using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Nexus.Managed;
using Nexus.Managed.Interop;

namespace Nexus.Managed.Bootstrap;

/*-----------------------------------------------------------------------------------------
 * BootstrapContext
 * - NexusManagedBootstrapContextV1と対応する起動時Interop値
 * - Worldは数値token。LogはEngine常駐関数で、ECSのNativeアドレスを保持しない
 *---------------------------------------------------------------------------------------*/
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct BootstrapContext
{
    public uint AbiVersion; //< Native側と一致するBootstrap ABI
    public uint StructSize; //< Layoutを呼出前に照合するbyte数
    public ulong World; //< Stop時に失効する共通World token
#if NEXUS_MONO
    private IntPtr log;
    public NativeLogDelegate? Log {
        get => log == IntPtr.Zero ? null : Marshal.GetDelegateForFunctionPointer<NativeLogDelegate>(log);
        set => log = value == null ? IntPtr.Zero : Marshal.GetFunctionPointerForDelegate(value);
    }
#else
    public delegate* unmanaged[Cdecl]<ulong, NativeString, int> Log; //< セッション停止時に保持参照を解除する
#endif
}

/*-----------------------------------------------------------------------------------------
 * EntryPoints
 * - 安定BootstrapのManagedセッション開始・終了と境界例外処理を担当する
 * - GameScripts、ECS、Editor、Thread / Taskを所有しない。集約するのはセッションの寿命だけ
 *---------------------------------------------------------------------------------------*/
public static unsafe partial class EntryPoints
{
    private const int Ok = 0, InvalidArgument = 1, InvalidHandle = 2, WrongThread = 3, Failed = 5, IncompatibleAbi = 6;
    /*-----------------------------------------------------------------------------------------
     * SessionRegistry
     * - Managedセッションだけを所有する内部表。ECSやGame Worldの別Storageを作らない
     * - 明示的static constructorにより、初回確保もentry内のtryで捕捉する
     *---------------------------------------------------------------------------------------*/
    private static class SessionRegistry
    {
        public static readonly object Gate = new(); //< 複数Runtimeからの開始を直列化する
        public static readonly Dictionary<ulong, Session> Sessions = new(); //< 停止時に必ず参照を解除する
        public static ulong Next = 1; //< 再利用しない数値ID。GCHandleではない
        static SessionRegistry() { }
    }

    /*-----------------------------------------------------------------------------------------
     * Session
     * - 一つのBootstrap接続のthread、World token、Logger参照を保持する
     * - Stopで全参照を明示解除し、Process寿命のBootstrap staticへWorldを残さない
     *---------------------------------------------------------------------------------------*/
    private sealed class Session
    {
        public readonly int Thread = Environment.CurrentManagedThreadId; //< NativeのStart threadと対応する
        private ulong world; //< Engine発行の数値World token
#if NEXUS_MONO
        private NativeLogDelegate? log; //< Engine関数へのdelegate。Domain内だけで保持し、Stopで解除する
#else
        private delegate* unmanaged[Cdecl]<ulong, NativeString, int> log; //< Engine資源の所有権は持たない
#endif
        public GameWorld? World; //< Gameplayセッションだけが持つ、失効可能なManaged capability
        public ScriptRuntime? Gameplay; //< behaviour所有。Native Worldは所有しない
        public bool Busy; //< callback中のTick / Stop再入を拒否する
        public GameScriptCatalog? Catalog; //< 現在世代のcollectible context。Runtime参照の解除後にUnloadする
#if !NEXUS_MONO
        public readonly List<RetiredGame> Retired = []; //< 解放を監査する弱参照だけを保持する
#endif
        public NativeGameApi GameApi; //< 世代ごとのWorld capabilityを作る共通Engine関数表
        public NativeScriptApi ScriptApi; //< Component関連付けAPI。Game objectは含まない
        public NativeScriptPropertyApi PropertyApi; //< 値copy API。呼出側bufferを保持しない
        public NativeGameServicesApi ServicesApi; //< 世代ごとのManaged clientを作るEngine常駐表
        public NativeBindingApi BindingApi; //< Game DLL pointerを含まないEngine dispatch。世代Worldへ値コピーする
        public string PendingAssembly = ""; //< Preflight済みの不変shadowパス。借用文字列は保持しない
        public ulong NextInstance = 1; //< 同じsessionのReloadを跨いで再利用しないID
        public bool Suspended = false; //< Unload / 初期化失敗時もNative Worldを破棄しない
        public bool ReloadCommitting; //< Prepare取消と旧世代停止後のAbortを区別し、現行Behaviourを不要に破棄しない
        public bool CleanupFailed; //< Script / World capabilityの解除失敗。GCで解放できても同sessionの再開を許さない

        public Session(BootstrapContext context) { world = context.World; log = context.Log; }
        public GameWorld CreateWorld() => new(world, GameApi, ScriptApi, PropertyApi, ServicesApi, BindingApi);

        /// <summary>UTF-8 bufferを一時的にpinし、共通Game Loggerへ同期配送する。</summary>
        public int Write(string message)
        {
            if (log == null) return InvalidHandle;
            // 任意のGame例外messageでもNative LoggerのUTF-8上限・制御文字規約を破らない。
            if(message.Length > 2048) message = message.Substring(0, 2048);
            message = new string(message.Select(ch => ch < 32 && ch != '\n' && ch != '\r' && ch != '\t' ? '?' : ch).ToArray());
            var bytes = Encoding.UTF8.GetBytes(message);
            fixed (byte* data = bytes) return log(world, new NativeString { Data = data, Length = checked((uint)bytes.Length) });
        }

        /// <summary>停止後にEngine callbackや旧World tokenを保持しない。</summary>
        public bool Clear(bool reportStop = false)
        {
            var clean = false;
            try { RetireGame(this); PendingAssembly = ""; clean = ConfirmRetired(this); if(clean && reportStop) clean = Write("Managed bootstrap stopped.") == Ok; }
            catch (Exception exception) { Report(this, exception); }
            finally { Gameplay = null; World?.Invalidate(); World = null; log = null; world = 0; GameApi = default; ScriptApi = default; PropertyApi = default; ServicesApi = default; BindingApi = default; }
            return clean;
        }
    }

    /// <summary>Managed bootstrapを開始する。contextのポインタは保存せず、全例外をstatusへ閉じ込める。</summary>
#if !NEXUS_MONO
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
#endif
    public static int Start(BootstrapContext* context, ulong* sessionId)
    {
        if (sessionId == null) return InvalidArgument;
        *sessionId = 0;
        if (context == null || context->AbiVersion != 1 || context->StructSize != sizeof(BootstrapContext) ||
            sizeof(BootstrapContext) != 24 || sizeof(NativeString) != 16 || context->World == 0 || context->Log == null) return IncompatibleAbi;
        Session? session = null;
        ulong id = 0;
        try
        {
            session = new Session(*context);
            var status = session.Write("Managed bootstrap started.");
            if (status != Ok) { session.Clear(); return status; }
            lock (SessionRegistry.Gate)
            {
                if (SessionRegistry.Next == 0) { session.Clear(); return Failed; }
                id = SessionRegistry.Next;
                SessionRegistry.Next = unchecked(SessionRegistry.Next + 1);
                SessionRegistry.Sessions.Add(id, session);
            }
            *sessionId = id;
            return Ok;
        }
        catch (Exception exception)
        {
            try { if (id != 0) { lock (SessionRegistry.Gate) SessionRegistry.Sessions.Remove(id); } }
            catch { /* cleanupの失敗でもABIの外へ例外を出さない。Native側へFailedを返す。 */ }
            Report(session, exception);
            session?.Clear();
            return Failed;
        }
    }

    /// <summary>同じthreadでセッションを破棄する。CLRやBootstrap assemblyをUnloadしたとは扱わない。</summary>
#if !NEXUS_MONO
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
#endif
    public static int Stop(ulong sessionId)
    {
        Session? session = null;
        try
        {
            lock (SessionRegistry.Gate)
            {
                if (!SessionRegistry.Sessions.TryGetValue(sessionId, out session)) return InvalidHandle;
                if (session.Thread != Environment.CurrentManagedThreadId) return WrongThread;
                if (session.Busy) return Failed;
                SessionRegistry.Sessions.Remove(sessionId);
            }
            var stopped = session.Clear(reportStop: true);
            return stopped ? Ok : Failed;
        }
        catch (Exception exception) { Report(session, exception); session?.Clear(); return Failed; }
    }

    /// <summary>例外の要点を境界内で記録する。診断失敗によって二つ目の例外をNativeへ出さない。</summary>
    private static void Report(Session? session, Exception exception)
    {
        try
        {
            var message = $"Managed bootstrap: {exception.GetType().Name}: {exception.Message}";
            if (message.Length > 1024) message = message.Substring(0, 1024);
            _ = session?.Write(message);
        }
        catch { /* Native側も失敗statusを記録するため、ここから例外を再送しない。 */ }
    }
}
