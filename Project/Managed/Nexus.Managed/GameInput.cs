using System.Numerics;

namespace Nexus.Managed;
/*-----------------------------------------------------------------------------------------
 * InputState
 * - 1frameの入力を保持する読み取り専用snapshot。キーごとのInteropを発生させない
 * - 同じsnapshotのPressedを複数回読んでも消費しない
 *---------------------------------------------------------------------------------------*/
public sealed class InputState
{
    private readonly byte[] data; //< Nativeからコピー済みの800 byte。外部へ可変配列を渡さない
    internal InputState(byte[] value) {
        if(value.Length!=800 || BitConverter.ToUInt32(value,788)>1 || BitConverter.ToUInt32(value,792)!=0 || BitConverter.ToUInt32(value,796)!=0)
            throw new InvalidOperationException("Input ABI mismatch.");
        data=value;
    }
    public bool Focused => BitConverter.ToUInt32(data,788)!=0;
    public Vector2 MousePosition => new(BitConverter.ToSingle(data,768),BitConverter.ToSingle(data,772));
    public Vector2 MouseDelta => new(BitConverter.ToSingle(data,776),BitConverter.ToSingle(data,780));
    public float Wheel => BitConverter.ToSingle(data,784);
    /// <summary>Windows Virtual-Key番号の保持状態。focusがない場合はfalse。</summary>
    public bool Down(int key) => Focused && Key(0,key);
    public bool Pressed(int key) => Focused && Key(256,key);
    /// <summary>focus喪失時の解除edgeも返す。</summary>
    public bool Released(int key) => Key(512,key);
    private bool Key(int offset,int key) {
        if(key<0 || key>255) throw new ArgumentOutOfRangeException(nameof(key));
        return data[offset+key]!=0;
    }
}
public sealed partial class GameWorld
{
    /// <summary>全System/Scriptが共有するこのframeの入力を一回のInteropでコピーする。</summary>
    public InputState ReadInput() => new(InvokeWorldCommand("nexus.engine.input",default,Array.Empty<byte>(),false));
    /// <summary>4台分の接続・button・analogを一括取得する。XInput user slotは0〜3。</summary>
    public GamepadInputState ReadGamepads() => new(InvokeWorldCommand("nexus.engine.input",default,BitConverter.GetBytes(1u),false));
}
/* 共通button bit。組合せは指定した全buttonのDown/Pressed/Releasedを検査する。 */
[Flags]
public enum GamepadButton : uint
{
    DPadUp=0x0001, DPadDown=0x0002, DPadLeft=0x0004, DPadRight=0x0008,
    Start=0x0010, Back=0x0020, LeftStick=0x0040, RightStick=0x0080,
    LeftShoulder=0x0100, RightShoulder=0x0200, A=0x1000, B=0x2000, X=0x4000, Y=0x8000
}
/*-----------------------------------------------------------------------------------------
 * GamepadState
 * - 1 slotの読み取り専用値。接続/切断とframe edgeを含み、Native pointerを保持しない
 * - 切断/非focusでもReleasedを返すため、解放処理をConnectedだけで遮断しない
 *---------------------------------------------------------------------------------------*/
public readonly struct GamepadState
{
    public bool Connected { get; }
    public bool ConnectedThisFrame { get; }
    public bool DisconnectedThisFrame { get; }
    public Vector2 LeftStick { get; }
    public Vector2 RightStick { get; }
    public float LeftTrigger { get; }
    public float RightTrigger { get; }
    private readonly uint down,pressed,released;
    internal GamepadState(byte[] data,int offset)
    {
        var connected=BitConverter.ToUInt32(data,offset); var added=BitConverter.ToUInt32(data,offset+4); var removed=BitConverter.ToUInt32(data,offset+8);
        down=BitConverter.ToUInt32(data,offset+12); pressed=BitConverter.ToUInt32(data,offset+16); released=BitConverter.ToUInt32(data,offset+20);
        LeftStick=new(BitConverter.ToSingle(data,offset+24),BitConverter.ToSingle(data,offset+28));
        RightStick=new(BitConverter.ToSingle(data,offset+32),BitConverter.ToSingle(data,offset+36));
        LeftTrigger=BitConverter.ToSingle(data,offset+40); RightTrigger=BitConverter.ToSingle(data,offset+44);
        if(connected>1 || added>1 || removed>1 || ((down|pressed|released)&~0xf3ffu)!=0 ||
            !StickValid(LeftStick) || !StickValid(RightStick) || !Unit(LeftTrigger) || !Unit(RightTrigger))
            throw new InvalidOperationException("Invalid Gamepad state.");
        for(int i=48;i<64;i+=4) if(BitConverter.ToUInt32(data,offset+i)!=0) throw new InvalidOperationException("Gamepad ABI mismatch.");
        Connected=connected!=0; ConnectedThisFrame=added!=0; DisconnectedThisFrame=removed!=0;
    }
    public bool Down(GamepadButton buttons) => Test(down,buttons);
    public bool Pressed(GamepadButton buttons) => Test(pressed,buttons);
    public bool Released(GamepadButton buttons) => Test(released,buttons);
    private static bool Test(uint state,GamepadButton buttons) {
        var mask=(uint)buttons;
        if(mask==0 || (mask&~0xf3ffu)!=0) throw new ArgumentOutOfRangeException(nameof(buttons));
        return (state&mask)==mask;
    }
    private static bool Unit(float value) => !float.IsNaN(value) && value>=0 && value<=1;
    private static bool StickValid(Vector2 value) => !float.IsNaN(value.X) && !float.IsNaN(value.Y) && value.LengthSquared()<=1.00001f;
}
/*-----------------------------------------------------------------------------------------
 * GamepadInputState
 * - 同じframeの4 slotを保持する不変snapshot。反復取得でedgeを消費しない
 * - 配列を外部へ渡さず、範囲外slotをNative呼出し前に拒否する
 *---------------------------------------------------------------------------------------*/
public sealed class GamepadInputState
{
    private readonly GamepadState[] pads; //< Nativeからコピー済みの値。device objectを所有しない
    public bool Focused { get; }
    public int Count => 4;
    internal GamepadInputState(byte[] data) {
        if(data.Length!=272 || BitConverter.ToUInt32(data,0)!=4 || BitConverter.ToUInt32(data,4)>1 ||
            BitConverter.ToUInt32(data,8)!=0 || BitConverter.ToUInt32(data,12)!=0) throw new InvalidOperationException("Gamepad ABI mismatch.");
        Focused=BitConverter.ToUInt32(data,4)!=0; pads=new GamepadState[4];
        for(int i=0;i<4;++i) pads[i]=new GamepadState(data,16+i*64);
    }
    public GamepadState this[int slot] => slot>=0 && slot<Count ? pads[slot] : throw new ArgumentOutOfRangeException(nameof(slot));
}

/// <summary>全操作の不変snapshot。同じframeの押下は読むたびに消費しない。</summary>
public sealed class InputActions
{
    private readonly Dictionary<string, ActionValue> actions = new(StringComparer.Ordinal);
    public string Context { get; }
    public bool Focused { get; }
    private readonly struct ActionValue
    {
        public readonly Vector2 Value;
        public readonly bool Down, Pressed, Released;
        public ActionValue(Vector2 value, bool down, bool pressed, bool released)
        {
            Value = value;
            Down = down;
            Pressed = pressed;
            Released = released;
        }
    }
    internal InputActions(byte[] data)
    {
        if(data.Length != 2888 || BitConverter.ToUInt32(data, 0) > 32 || BitConverter.ToUInt32(data, 4) > 1)
            throw new InvalidOperationException("Input Actions ABI mismatch.");
        Context = Name(data, 8);
        Focused = BitConverter.ToUInt32(data, 4) != 0;
        var count = BitConverter.ToUInt32(data, 0);
        for(var index = 0; index < count; ++index)
        {
            var offset = 72 + index * 88;
            var name = Name(data, offset);
            var value = new Vector2(BitConverter.ToSingle(data, offset + 68), BitConverter.ToSingle(data, offset + 72));
            if(BitConverter.ToUInt32(data, offset + 64) > 2 ||
                float.IsNaN(value.X) || float.IsInfinity(value.X) || float.IsNaN(value.Y) || float.IsInfinity(value.Y) ||
                BitConverter.ToUInt32(data, offset + 76) > 1 || BitConverter.ToUInt32(data, offset + 80) > 1 ||
                BitConverter.ToUInt32(data, offset + 84) > 1 || actions.ContainsKey(name))
                throw new InvalidOperationException("Invalid Input Action state.");
            actions.Add(name, new ActionValue(value, BitConverter.ToUInt32(data, offset + 76) != 0,
                BitConverter.ToUInt32(data, offset + 80) != 0, BitConverter.ToUInt32(data, offset + 84) != 0));
        }
    }
    private static string Name(byte[] data, int offset)
    {
        var length = Array.IndexOf(data, (byte)0, offset, 64) - offset;
        if(length < 0) throw new InvalidOperationException("Unterminated Input Action name.");
        return System.Text.Encoding.UTF8.GetString(data, offset, length);
    }
    public bool Contains(string name) => actions.ContainsKey(name);
    public bool Down(string name) => actions.TryGetValue(name, out var action) && action.Down;
    public bool Pressed(string name) => actions.TryGetValue(name, out var action) && action.Pressed;
    public bool Released(string name) => actions.TryGetValue(name, out var action) && action.Released;
    public float Value(string name) => actions.TryGetValue(name, out var action) ? action.Value.X : 0;
    public Vector2 Vector2(string name) => actions.TryGetValue(name, out var action) ? action.Value : default;
}
public sealed partial class GameWorld
{
    /// <summary>全Actionを1回のInteropで取得。未登録名の読み取りはfalse/zero。</summary>
    public InputActions ReadActions()
    {
        var request = new byte[68];
        BitConverter.GetBytes(2u).CopyTo(request, 0);
        return new InputActions(InvokeWorldCommand("nexus.engine.input", default, request, false));
    }
    /// <summary>次の入力frameからContextを切り替える。空Contextの定義は常に有効。</summary>
    public void SetInputContext(string context)
    {
        if(string.IsNullOrEmpty(context)) throw new ArgumentException("Context is required.", nameof(context));
        foreach(var character in context)
            if(character <= 32 || character == 127) throw new ArgumentException("Invalid Context name.", nameof(context));
        var bytes = System.Text.Encoding.UTF8.GetBytes(context);
        if(bytes.Length > 63) throw new ArgumentException("Context exceeds 63 UTF-8 bytes.", nameof(context));
        var request = new byte[68];
        BitConverter.GetBytes(3u).CopyTo(request, 0);
        bytes.CopyTo(request, 4);
        InvokeWorldCommand("nexus.engine.input", default, request, true);
    }
}
