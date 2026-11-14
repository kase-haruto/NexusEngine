using System.Numerics;

namespace Nexus.Managed;

public enum GameLogLevel { Info, Warning, Error }

/// <summary>ワールド空間の診断線。Colorは0～1の線形RGBA。</summary>
public readonly struct DebugLine
{
    public Vector3 Start { get; }
    public Vector3 End { get; }
    public Vector4 Color { get; }
    public bool DepthTest { get; }
    public DebugLine(Vector3 start, Vector3 end, Vector4 color, bool depthTest = true)
    {
        GameMath.Require(GameMath.IsFinite(start) && GameMath.IsFinite(end), nameof(start));
        GameMath.Require(GameMath.IsFinite(color) && color.X >= 0 && color.Y >= 0 && color.Z >= 0 && color.W >= 0 &&
            color.X <= 1 && color.Y <= 1 && color.Z <= 1 && color.W <= 1, nameof(color));
        Start = start; End = end; Color = color; DepthTest = depthTest;
    }
}

/// <summary>World所有threadで使う診断API。Stop/Reload後の参照は失効する。呼出しはRelease構成でも有効。</summary>
public sealed class GameDebug
{
    private readonly GameWorld world;
    internal GameDebug(GameWorld world) { this.world = world; }
    public void Log(string message) => WriteLog(GameLogLevel.Info, message);
    public void LogWarning(string message) => WriteLog(GameLogLevel.Warning, message);
    public void LogError(string message) => WriteLog(GameLogLevel.Error, message);
    /// <summary>型名・message・stack trace・inner exceptionをEngineのErrorログへ送る。</summary>
    public void LogException(Exception exception)
    {
        if (exception == null) throw new ArgumentNullException(nameof(exception));
        LogError(exception.ToString());
    }
    /// <summary>falseならErrorログへ記録してfalseを返す。実行の停止や例外送出は行わない。</summary>
    public bool Assert(bool condition, string message = "Assertion failed.")
    {
        if (!condition) LogError("Assertion failed: " + message);
        return condition;
    }
    public void WriteLog(GameLogLevel level, string message)
    {
        if (message == null) throw new ArgumentNullException(nameof(message));
        if (level < GameLogLevel.Info || level > GameLogLevel.Error) throw new ArgumentOutOfRangeException(nameof(level));
        var bytes = GameWorld.ComponentEncoding.GetBytes(message);
        // ABI上限を越える例外のstack traceもUnicodeを壊さず末尾省略して通知する。
        if (bytes.Length > 4080)
        {
            var count = 4064;
            while ((bytes[count] & 0xc0) == 0x80) --count;
            var suffix = GameWorld.ComponentEncoding.GetBytes("\n[truncated]");
            var truncated = new byte[count + suffix.Length];
            Buffer.BlockCopy(bytes, 0, truncated, 0, count);
            Buffer.BlockCopy(suffix, 0, truncated, count, suffix.Length);
            bytes = truncated;
        }
        world.DebugCall(1, (uint)level, 0, bytes, false);
    }
    /// <summary>1要求64線、Scene全体1024線。duration=0は抽出された次のゲーム更新まで表示する。</summary>
    public void DrawLines(IReadOnlyList<DebugLine> lines, float duration = 0)
    {
        if (lines == null) throw new ArgumentNullException(nameof(lines));
        if (lines.Count > 64) throw new ArgumentOutOfRangeException(nameof(lines));
        GameMath.Require(GameMath.IsFinite(duration) && duration >= 0, nameof(duration));
        var data = new byte[lines.Count * 44];
        for (var index = 0; index < lines.Count; ++index)
        {
            var line = lines[index];
            // default(DebugLine)も同じ検証を通し、ABI境界へNaN等を送らない。
            _ = new DebugLine(line.Start, line.End, line.Color, line.DepthTest);
            var values = new[] { line.Start.X, line.Start.Y, line.Start.Z, line.End.X, line.End.Y, line.End.Z,
                line.Color.X, line.Color.Y, line.Color.Z, line.Color.W };
            for (var field = 0; field < values.Length; ++field)
                Buffer.BlockCopy(BitConverter.GetBytes(values[field]), 0, data, index * 44 + field * 4, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(line.DepthTest ? 1u : 0u), 0, data, index * 44 + 40, 4);
        }
        world.DebugCall(2, (uint)lines.Count, duration, data, true);
    }
    public void DrawLine(Vector3 start, Vector3 end, Vector4 color, float duration = 0, bool depthTest = true) =>
        DrawLines(new[] { new DebugLine(start, end, color, depthTest) }, duration);
    public void DrawRay(Vector3 origin, Vector3 direction, Vector4 color, float duration = 0, bool depthTest = true) =>
        DrawLine(origin, origin + direction, color, duration, depthTest);
    /// <summary>Boxのsizeは全幅。center/rotationはワールド空間で指定する。</summary>
    public void DrawBox(Vector3 center, Vector3 size, Quaternion rotation, Vector4 color, float duration = 0, bool depthTest = true)
    {
        GameMath.Require(GameMath.IsFinite(size) && size.X >= 0 && size.Y >= 0 && size.Z >= 0, nameof(size));
        var matrix = GameMath.TRS(center, rotation, size * .5f);
        var corners = new Vector3[8];
        for (var i = 0; i < 8; ++i)
            corners[i] = GameMath.TransformPoint(matrix, new Vector3((i & 1) == 0 ? -1 : 1, (i & 2) == 0 ? -1 : 1, (i & 4) == 0 ? -1 : 1));
        var lines = new List<DebugLine>(12);
        for (var i = 0; i < 8; ++i)
            for (var axis = 1; axis <= 4; axis *= 2)
                if ((i & axis) == 0) lines.Add(new DebugLine(corners[i], corners[i | axis], color, depthTest));
        DrawLines(lines, duration);
    }
    /// <summary>3つの大円で球を描く。segmentsは1円あたり3～21。</summary>
    public void DrawSphere(Vector3 center, float radius, Vector4 color, float duration = 0, bool depthTest = true, int segments = 16)
    {
        GameMath.Require(GameMath.IsFinite(center) && GameMath.IsFinite(radius) && radius >= 0, nameof(radius));
        if (segments < 3 || segments > 21) throw new ArgumentOutOfRangeException(nameof(segments));
        var lines = new List<DebugLine>(segments * 3);
        for (var plane = 0; plane < 3; ++plane)
        {
            Vector3 Point(int index)
            {
                var angle = index * Math.PI * 2 / segments;
                var x = radius * (float)Math.Cos(angle); var y = radius * (float)Math.Sin(angle);
                return center + (plane == 0 ? new Vector3(x, y, 0) : plane == 1 ? new Vector3(x, 0, y) : new Vector3(0, x, y));
            }
            for (var index = 0; index < segments; ++index)
                lines.Add(new DebugLine(Point(index), Point(index + 1), color, depthTest));
        }
        DrawLines(lines, duration);
    }
    public void DrawAxes(Matrix4x4 matrix, float length = 1, float duration = 0, bool depthTest = true)
    {
        GameMath.Require(GameMath.IsFinite(length) && length >= 0, nameof(length));
        var origin = GameMath.TransformPoint(matrix, Vector3.Zero);
        DrawLines(new[] {
            new DebugLine(origin, GameMath.TransformPoint(matrix, Vector3.UnitX * length), new Vector4(1, 0, 0, 1), depthTest),
            new DebugLine(origin, GameMath.TransformPoint(matrix, Vector3.UnitY * length), new Vector4(0, 1, 0, 1), depthTest),
            new DebugLine(origin, GameMath.TransformPoint(matrix, Vector3.UnitZ * length), new Vector4(0, 0, 1, 1), depthTest) }, duration);
    }
    /// <summary>同じWorld内でも、このManaged sessionから登録した線だけを消す。</summary>
    public void Clear() => world.DebugCall(3, 0, 0, Array.Empty<byte>(), true);
}

public sealed unsafe partial class GameWorld
{
    private GameDebug? debug;
    public GameDebug Debug { get { Access(); return debug ??= new GameDebug(this); } }
    internal byte[] DebugCall(uint operation, uint count, float duration, byte[] payload, bool write, Entity? target = null)
    {
        if (target != null) Access(target.Handle);
        var request = new byte[16 + payload.Length];
        fixed (byte* data = request)
        {
            *(uint*)data = operation; *(uint*)(data + 4) = count; *(float*)(data + 8) = duration;
        }
        Buffer.BlockCopy(payload, 0, request, 16, payload.Length);
        return InvokeWorldCommand("nexus.engine.debug", target?.Handle ?? default, request, write);
    }
    internal Matrix4x4 ReadWorldMatrix(Entity entity)
    {
        var response = DebugCall(4, 0, 0, Array.Empty<byte>(), false, entity);
        if (response.Length != 64) throw new InvalidOperationException("Invalid world matrix response.");
        fixed (byte* data = response) return *(Matrix4x4*)data;
    }
}
