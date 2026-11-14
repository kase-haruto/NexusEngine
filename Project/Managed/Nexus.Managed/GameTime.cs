using System.Runtime.InteropServices;
namespace Nexus.Managed;

/// <summary>Worldの時計の値コピー。秒単位。固定callback内ではDeltaTimeは固定間隔。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct GameTime
{
    public double Elapsed, UnscaledElapsed, FixedElapsed;
    public float DeltaTime, UnscaledDeltaTime, FixedDeltaTime, TimeScale;
    public ulong Frame, FixedTick;
    private uint inFixedStep, reserved;
    public double DroppedFixedTime;
    public bool InFixedUpdate => inFixedStep != 0;
}
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct TimeRequest { internal uint Operation, Reserved; internal float Value; internal uint Reserved2; }

/// <summary>Disposeで取消すWorld所有のタイマー。callbackはゲーム更新threadで実行する。</summary>
public sealed class GameTimer : IDisposable
{
    internal Action? Callback;
    internal readonly GameWorld World;
    internal double Deadline;
    internal readonly double Interval;
    internal readonly bool Unscaled;
    internal readonly ulong EarliestFrame;
    internal GameTimer(GameWorld world, Action callback, double deadline, double interval, bool unscaled, ulong frame)
    { World = world; Callback = callback; Deadline = deadline; Interval = interval; Unscaled = unscaled; EarliestFrame = frame; }
    public bool IsActive => Callback != null;
    public void Dispose() { World.CancelTimer(this); }
}
public sealed unsafe partial class GameWorld
{
    private readonly List<GameTimer> timers = new();
    public GameTime Time
    {
        get {
            TimeRequest input = new() { Operation = 1 };
            var bytes = new byte[sizeof(TimeRequest)];
            fixed(byte* data = bytes) *(TimeRequest*)data = input;
            var result = InvokeWorldCommand("nexus.engine.time", default, bytes, false);
            if(result.Length != sizeof(GameTime) || sizeof(GameTime) != 72) throw new InvalidOperationException("Invalid GameTime response.");
            fixed(byte* data = result) return *(GameTime*)data;
        }
    }
    /// <summary>0でゲーム時間を停止。0～100。変更は次frameの時間積算から反映。</summary>
    public float TimeScale { get => Time.TimeScale; set => SetClock(2, value); }
    /// <summary>物理とOnFixedUpdateの間隔(.001～1秒)。変更は次frameから反映。</summary>
    public float FixedDeltaTime { get => Time.FixedDeltaTime; set => SetClock(3, value); }
    private void SetClock(uint operation, float value)
    {
        TimeRequest input = new() { Operation = operation, Value = value };
        var bytes = new byte[sizeof(TimeRequest)];
        fixed(byte* data = bytes) *(TimeRequest*)data = input;
        _ = InvokeWorldCommand("nexus.engine.time", default, bytes, true);
    }
    /// <summary>指定秒後に一回実行。0秒も次frame以降。Pause中はunscaled指定でも進まない。</summary>
    public GameTimer After(double seconds, Action callback, bool unscaled = false) => Schedule(seconds, 0, callback, unscaled);
    /// <summary>指定間隔で実行。遅延時は一frame一回にまとめ、未実行回数を無制限に追跡しない。</summary>
    public GameTimer Every(double seconds, Action callback, bool unscaled = false)
    {
        if(seconds <= 0) throw new ArgumentOutOfRangeException(nameof(seconds));
        return Schedule(seconds, seconds, callback, unscaled);
    }
    private GameTimer Schedule(double delay, double interval, Action callback, bool unscaled)
    {
        WriteAccess();
        if(callback == null) throw new ArgumentNullException(nameof(callback));
        if(double.IsNaN(delay) || double.IsInfinity(delay) || delay < 0) throw new ArgumentOutOfRangeException(nameof(delay));
        if(timers.Count >= 4096) throw new InvalidOperationException("World timer limit exceeded.");
        var time = Time;
        var timer = new GameTimer(this, callback, (unscaled ? time.UnscaledElapsed : time.Elapsed) + delay, interval, unscaled, checked(time.Frame + 1));
        timers.Add(timer); return timer;
    }
    internal void CancelTimer(GameTimer timer)
    {
        // World終了後にもDispose可能。ただしcallbackの寿命操作は所有threadに限定する。
        if(thread != Environment.CurrentManagedThreadId) throw new InvalidOperationException("Timer requires its owner thread.");
        timer.Callback = null; timers.Remove(timer);
    }
    internal void TickTimers()
    {
        Access();
        if(timers.Count == 0) return;
        var time = Time;
        // callback中の追加・取消で反復を壊さず、新規timerは次frameまで待つ。
        foreach(var timer in timers.ToArray()) {
            var callback = timer.Callback;
            var now = timer.Unscaled ? time.UnscaledElapsed : time.Elapsed;
            if(callback == null || time.Frame < timer.EarliestFrame || now < timer.Deadline) continue;
            if(timer.Interval == 0) { timer.Callback = null; timers.Remove(timer); }
            else timer.Deadline += (Math.Floor((now - timer.Deadline) / timer.Interval) + 1) * timer.Interval;
            try { callback(); }
            catch(Exception exception) {
                timer.Callback = null; timers.Remove(timer);
                Debug.LogException(exception);
            }
        }
    }
    private void ClearTimers()
    {
        foreach(var timer in timers) timer.Callback = null;
        timers.Clear();
    }
}
