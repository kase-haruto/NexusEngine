using System.Runtime.InteropServices;
namespace Nexus.Managed;
/*-----------------------------------------------------------------------------------------
 * AudioVoice
 * - 共通Audio BackendのHandleと所有GameWorldだけを保持する
 * - Script Runtime終了時はservices clientの解放でも停止し、Native resourceを取り残さない
 *---------------------------------------------------------------------------------------*/
public sealed class AudioVoice : IDisposable
{
    private readonly GameWorld world; //< sessionとthreadを検証する所有接続
    private ulong handle; //< Processで再利用しないVoice token。Native pointerではない
    internal AudioVoice(GameWorld owner,ulong token) { world=owner; handle=token; }
    public bool IsPlaying => handle!=0 && world.AudioCall(3,handle)==1;
    public void SetVolume(float volume) {
        if(handle==0) throw new ObjectDisposedException(nameof(AudioVoice));
        world.AudioCall(4,handle,volume:volume);
    }
    public void Stop() {
        if(handle==0) return;
        world.AudioCall(2,handle); handle=0;
    }
    public void Dispose() {
        // 旧世代cleanupから新Worldへ書き込まない。client解放が旧世代の全voiceを確実に止める。
        if(!world.CanReleaseAudio) { handle=0; return; }
        Stop();
    }
}
/** 固定24 byteのheader。可変長UTF-8 pathだけを後ろへ連結する。 */
[StructLayout(LayoutKind.Sequential, Pack=8)]
internal struct AudioRequest
{
    public uint Operation, PathLength;
    public ulong Voice;
    public float Volume;
    public uint Loop;
}
public sealed unsafe partial class GameWorld
{
    internal bool CanReleaseAudio => token!=0 && !writesBlocked;
    /// <summary>Project相対のPCM/float WAVを再生する。初回だけdefault出力deviceを作る。</summary>
    public AudioVoice PlayAudio(string asset,float volume=1,bool loop=false)
    {
        if(string.IsNullOrEmpty(asset)) throw new ArgumentException("A WAV asset path is required.",nameof(asset));
        var handle=AudioCall(1,0,asset,volume,loop);
        if(handle==0) throw new InvalidOperationException("Audio returned an invalid Voice Handle.");
        return new AudioVoice(this,handle);
    }
    /// <summary>Reload cleanupの書込禁止とsession/thread検査を共通利用する。</summary>
    internal ulong AudioCall(uint operation,ulong voice,string asset="",float volume=0,bool loop=false)
    {
        if(sizeof(AudioRequest)!=24) throw new InvalidOperationException("Audio ABI mismatch.");
        var path=ComponentEncoding.GetBytes(asset);
        if(path.Length>2048) throw new ArgumentOutOfRangeException(nameof(asset));
        var request=new byte[24+path.Length];
        fixed(byte* bytes=request) *(AudioRequest*)bytes=new AudioRequest { Operation=operation, PathLength=(uint)path.Length, Voice=voice, Volume=volume, Loop=loop ? 1u : 0u };
        Buffer.BlockCopy(path,0,request,24,path.Length);
        var result=InvokeWorldCommand("nexus.engine.audio",default,request,operation!=3);
        if(operation is 1 or 3) {
            if(result.Length!=8) throw new InvalidOperationException("Invalid Audio response.");
            var value=BitConverter.ToUInt64(result,0);
            if(operation==3 && value>1) throw new InvalidOperationException("Invalid Audio playing state.");
            return value;
        }
        if(result.Length!=0) throw new InvalidOperationException("Unexpected Audio mutation response.");
        return 0;
    }
}
