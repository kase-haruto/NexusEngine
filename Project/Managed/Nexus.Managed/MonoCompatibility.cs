// net48のBCLにも同じ永続データ型を生成できるよう、compiler専用のmarkerを定義する。
namespace System.Runtime.CompilerServices
{
    /*-----------------------------------------------------------------------------------------
     * IsExternalInit
     * - Mono profileでrecord / initをコンパイルするためのmarker
     * - 実行時のEngine / Script状態を保持しない
     *---------------------------------------------------------------------------------------*/
    public static class IsExternalInit { }
}
