namespace Nexus.Managed;
public sealed unsafe partial class GameWorld
{
    /// <summary>Project Root内のasset fileの存在を確認する。model/textureの読込は共通Componentの参照から行う。</summary>
    public bool AssetExists(string reference)
    {
        var response=WorldControlCall(2,reference);
        if(response.Length!=4) throw new InvalidOperationException("Invalid asset lookup response.");
        var value=BitConverter.ToUInt32(response,0);
        if(value>1) throw new InvalidOperationException("Invalid asset existence value.");
        return value!=0;
    }
    /// <summary>次frame境界でのLevel遷移を要求する。要求受理後の読込失敗はConsoleへ通知し、現行Worldを維持する。</summary>
    public void RequestLevel(string reference) {
        if(WorldControlCall(1,reference).Length!=0) throw new InvalidOperationException("Unexpected Level request response.");
    }
    private byte[] WorldControlCall(uint operation,string reference)
    {
        if(string.IsNullOrEmpty(reference)) throw new ArgumentException("A Project-relative reference is required.",nameof(reference));
        var path=ComponentEncoding.GetBytes(reference);
        if(path.Length>2048) throw new ArgumentOutOfRangeException(nameof(reference));
        var request=new byte[8+path.Length];
        fixed(byte* data=request) { *(uint*)data=operation; *(uint*)(data+4)=(uint)path.Length; }
        Buffer.BlockCopy(path,0,request,8,path.Length);
        return InvokeWorldCommand("nexus.engine.world",default,request,operation==1);
    }
}
