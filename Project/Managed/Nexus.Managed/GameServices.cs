using System.Text;
using Nexus.Managed.Interop;
namespace Nexus.Managed;
/*-----------------------------------------------------------------------------------------
 * GameEvent
 * - 共通Eventの値コピー。Managed / Native callbackを保持せず、破棄済みSourceもHandleで判定する
 *---------------------------------------------------------------------------------------*/
public sealed record GameEvent(ulong Sequence, string Type, uint Version, Entity? Source, byte[] Payload);
/*-----------------------------------------------------------------------------------------
 * GameWorld (services)
 * - 同じWorldの明示的なPublic APIと値Eventに接続する。別ECSやManaged event busを作らない
 * - clientは世代寿命。Reloadで旧wrapperとqueueを失効させ、旧delegateをrootさせない
 *---------------------------------------------------------------------------------------*/
public sealed unsafe partial class GameWorld
{
    private NativeGameServicesApi services; //< 安定したEngine関数表だけをコピーする
    private ulong servicesClient; //< 世代ごとの数値capability。Native登録権限は持たない
    /// <summary>一回のInteropでNative Moduleの明示登録APIを実行する。応答は4096 byte以下。</summary>
    public byte[] InvokeNative(string id, uint version, Entity? target, ReadOnlySpan<byte> request)
    {
        WriteAccess(); if(servicesClient == 0) throw new InvalidOperationException("Game services are unavailable.");
        if(target != null) Access(target.Handle);
        if(request.Length > 4096) throw new ArgumentOutOfRangeException(nameof(request));
        var name = Encoding.UTF8.GetBytes(id); var response = new byte[4096]; uint length = 0;
        fixed(byte* key = name) fixed(byte* input = request) fixed(byte* output = response)
            Check(services.Invoke(servicesClient, new NativeString { Data = key, Length = checked((uint)name.Length) }, version, target?.Handle ?? default,
                new NativeBytes { Data = input, Length = checked((uint)request.Length) }, output, 4096, &length));
        if(length > 4096) throw new InvalidOperationException("Invalid Native response size.");
        return response.AsSpan(0, checked((int)length)).ToArray();
    }
    /// <summary>同じWorldの他clientへ有界queueで配送する。schemaはType IDとVersionで明示する。</summary>
    public void PublishEvent(string type, uint version, Entity? source, ReadOnlySpan<byte> payload)
    {
        WriteAccess(); if(servicesClient == 0) throw new InvalidOperationException("Game services are unavailable.");
        if(source != null) Access(source.Handle);
        if(payload.Length > 4096) throw new ArgumentOutOfRangeException(nameof(payload));
        var name = Encoding.UTF8.GetBytes(type);
        fixed(byte* key = name) fixed(byte* input = payload)
            Check(services.Publish(servicesClient, new NativeString { Data = key, Length = checked((uint)name.Length) }, version, source?.Handle ?? default,
                new NativeBytes { Data = input, Length = checked((uint)payload.Length) }));
    }
    /// <summary>Engine queueから一つコピーする。Runtime所有threadだけで使用する。</summary>
    public bool TryPollEvent(out GameEvent? value)
    {
        WriteAccess(); value = null; if(servicesClient == 0) throw new InvalidOperationException("Game services are unavailable.");
        var bytes = new byte[4224]; NativeGameEvent header = default; uint length = 0;
        fixed(byte* buffer = bytes) Check(services.Poll(servicesClient, &header, buffer, 4224, &length));
        if(length == 0) return false;
        if(header.TypeLength > 128 || header.PayloadLength > 4096 || length != header.TypeLength + header.PayloadLength) throw new InvalidOperationException("Invalid Event buffer.");
        var type = new UTF8Encoding(false, true).GetString(bytes, 0, checked((int)header.TypeLength));
        value = new GameEvent(header.Sequence, type, header.Version, header.Source.World == 0 ? null : Wrap(header.Source), bytes.AsSpan(checked((int)header.TypeLength), checked((int)header.PayloadLength)).ToArray());
        return true;
    }
}
