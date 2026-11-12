namespace Nexus.Managed;
/*-----------------------------------------------------------------------------------------
 * ExposeAttribute
 * - 共通DetailsとLevel保存へ公開するPropertyを明示する
 * - Idを維持すればC#名変更後も値と編集Handleを引き継げる
 *---------------------------------------------------------------------------------------*/
[AttributeUsage(AttributeTargets.Property, Inherited = true)]
public sealed class ExposeAttribute : Attribute
{
    public string? Id { get; set; } //< 未指定時はProperty名。公開後の変更にはAliasesを指定する
    public string? Label { get; set; } //< UI表示。IDとは独立する
    public string[] Aliases { get; set; } = []; //< 過去のProperty ID。暗黙の型変換は行わない
}
/*-----------------------------------------------------------------------------------------
 * ScriptTypeAttribute
 * - ソース上のFullNameと独立した永続型ID・Version・旧IDを宣言する
 * - FullNameも互換名として扱い、Phase 4のLevelを作り直さずに使用する
 *---------------------------------------------------------------------------------------*/
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class ScriptTypeAttribute(string id) : Attribute
{
    public string Id { get; } = id;
    public int Version { get; set; } = 1;
    public string[] Aliases { get; set; } = [];
}
