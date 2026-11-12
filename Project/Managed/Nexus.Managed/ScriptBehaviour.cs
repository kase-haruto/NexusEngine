namespace Nexus.Managed;
/*-----------------------------------------------------------------------------------------
 * ScriptBehaviour
 * - 一つの既存ECS Entityに関連付けるゲームロジックのlifecycle契約
 * - Entityを所有せず、Engine Main threadのcallbackからだけGame APIを使用する
 *---------------------------------------------------------------------------------------*/
public abstract class ScriptBehaviour
{
    private Entity? entity; //< Nativeポインタを持たないwrapper。Destroy後に関連付けを解除する
    private ulong attachment; //< 自身の関連付け。Entityの主Scriptを誤って有効化しない
    private readonly CancellationTokenSource lifetime = new(); //< 世代終了をworkerへ通知する。OnDestroyで完了を待つ
    private readonly List<IDisposable> resources = []; //< event購読・callback登録等の明示的な解除対象
    private bool stopping; //< cleanupから新たな購読を増やして旧世代をrootさせない
    private readonly int thread = Environment.CurrentManagedThreadId; //< resource登録もlifecycleと同じ所有threadへ限定する
    /// <summary>停止・Reload時にcancelされる。Task / Threadの終了はOnDestroyで確認する。</summary>
    protected CancellationToken Lifetime => lifetime.Token;
    /// <summary>外部event等の購読解除を登録する。Disposeでは待機中workerも終了させる。</summary>
    protected T TrackResource<T>(T resource) where T : IDisposable {
        if(thread != Environment.CurrentManagedThreadId) throw new InvalidOperationException("Script resources require the owner thread.");
        if(stopping) { resource.Dispose(); throw new InvalidOperationException("Script cleanup already started."); }
        resources.Add(resource); return resource;
    }
    public Entity Entity => entity ?? throw new InvalidOperationException("ScriptBehaviour is not attached.");
    public GameWorld World => Entity.World;
    public bool Enabled {
        get => World.TryScript(Entity.Handle, attachment, out var state) ? state.Enabled != 0 : throw new InvalidOperationException("Script attachment is no longer valid.");
        set { if(stopping) throw new InvalidOperationException("Script cleanup already started."); World.EnableScript(Entity.Handle, attachment, value); }
    }
    /// <summary>関連付けごとに一度実行する。無効状態でも初期化は行う。</summary>
    protected virtual void OnCreate() { }
    /// <summary>有効化時に呼ぶ。OnCreate成功より前には呼ばない。</summary>
    protected virtual void OnEnable() { }
    /// <summary>有効なframeだけ更新する。Engine APIはこのthreadから使用する。</summary>
    protected virtual void OnUpdate(float deltaTime) { }
    /// <summary>無効化・解除・停止時に、有効だったbehaviourだけを停止する。</summary>
    protected virtual void OnDisable() { }
    /// <summary>関連付けを解除する。自前のeventやworkerもここで停止する。破棄済みEntityにはアクセスしない。</summary>
    protected virtual void OnDestroy() { }
    internal void Bind(GameWorld world, Interop.EntityHandle handle, ulong token) { entity = world.Wrap(handle); attachment = token; }
    // 旧Bootstrapの主Script呼出しを維持する。複数Scriptは新Bootstrapのfeature確認が必要。
    internal void Bind(GameWorld world, Interop.EntityHandle handle) => Bind(world, handle, world.Script(handle).Attachment);
    internal void BeginStop() { stopping = true; lifetime.Cancel(); }
    internal void Unbind()
    {
        entity = null; attachment = 0; List<Exception>? failures = null;
        // 一つの解除失敗で残りのdelegate / event参照を残さない。
        for (var index = resources.Count - 1; index >= 0; --index)
            try { resources[index].Dispose(); } catch (Exception exception) { (failures ??= []).Add(exception); }
        resources.Clear(); lifetime.Dispose();
        if (failures != null) throw new AggregateException(failures);
    }
    internal void Create() => OnCreate();
    internal void Enable() => OnEnable();
    internal void Update(float deltaTime) => OnUpdate(deltaTime);
    internal void Disable() => OnDisable();
    internal void Destroy() => OnDestroy();
}
