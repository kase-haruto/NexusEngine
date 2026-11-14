using System.Numerics;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Nexus.Managed;
/*-----------------------------------------------------------------------------------------
 * ScriptMetadata
 * - Runtimeとビルド時Exporterで同じ公開条件・ID・値形式を使用する
 * - Reflection参照は所有Catalog内だけに閉じ込め、Native / EditorにはJSON値だけを渡す
 *---------------------------------------------------------------------------------------*/
internal sealed class ScriptMetadata
{
    internal readonly List<ScriptTypeMetadata> Types = []; //< セッションまたはExporterだけが所有するReflection参照
    /// <summary>正規IDと旧IDを同じ型へ解決し、曖昧な一致を許可しない。</summary>
    internal ScriptTypeMetadata Resolve(string id) => Types.SingleOrDefault(t => t.Names.Contains(id))
        ?? throw new InvalidOperationException($"Unknown Script Type ID: {id}");
    /// <summary>公開条件とIDの衝突を全型で検証する。Runtimeでは既定値用objectを追加生成しない。</summary>
    internal ScriptMetadata(Assembly assembly) : this(new[] { assembly }) { }
    /// <summary>全ModuleでType ID / Aliasの一意性を検証する。言語別Registryは作らない。</summary>
    internal ScriptMetadata(IEnumerable<Assembly> assemblies)
    {
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (var type in assemblies.SelectMany(assembly => assembly.GetTypes()).OrderBy(t => t.FullName, StringComparer.Ordinal))
        {
            if (type.IsAbstract || !typeof(ScriptBehaviour).IsAssignableFrom(type)) continue;
            if (!type.IsVisible || type.ContainsGenericParameters || type.GetConstructor(Type.EmptyTypes) == null)
                throw new InvalidOperationException($"Script requires a public concrete type and parameterless constructor: {type}");
            var item = new ScriptTypeMetadata(type);
            foreach (var name in item.Names) if (!names.Add(name)) throw new InvalidOperationException($"Duplicate Script ID / alias: {name}");
            Types.Add(item);
        }
        if (Types.Count > 256) throw new InvalidOperationException("Too many script types.");
    }
    /// <summary>独立したExporter processだけで既定値用objectを作る。Worldやlifecycleは起動しない。</summary>
    internal string Export()
    {
        var types = new JsonArray();
        foreach (var type in Types)
        {
            var instance = (ScriptBehaviour)Activator.CreateInstance(type.Type)!;
            var properties = new JsonArray();
            try {
                foreach (var property in type.Properties)
                    properties.Add(new JsonObject { ["id"] = property.Id, ["label"] = property.Label, ["aliases"] = Names(property.Aliases),
                        ["kind"] = property.Kind, ["default"] = property.Read(instance) });
            }
            finally { try { instance.BeginStop(); } finally { instance.Unbind(); } }
            types.Add(new JsonObject { ["id"] = type.Id, ["fullName"] = type.Type.FullName, ["version"] = type.Version,
                ["aliases"] = Names(type.Names.Where(n => n != type.Id)), ["properties"] = properties,
                ["sourceFiles"] = Names(SourceFiles(type.Type)) });
        }
        var text = new JsonObject { ["schemaVersion"] = 1, ["types"] = types }.ToJsonString(new JsonSerializerOptions { WriteIndented = true });
        if (Encoding.UTF8.GetByteCount(text) > 1024 * 1024) throw new InvalidOperationException("Metadata exceeds 1 MiB.");
        return text;
    }
    /// <summary>CLR参照を含まないAliasの値配列を作る。</summary>
    private static JsonArray Names(IEnumerable<string> names) => new(names.Select(n => (JsonNode?)JsonValue.Create(n)).ToArray());
    /// <summary>DLLと一致するPortable PDBの宣言文書を値へコピーする。Reader / Streamはこの呼出しで破棄する。</summary>
    private static string[] SourceFiles(Type type)
    {
        try {
            var assembly = type.Assembly.Location;
            using var image = File.OpenRead(assembly);
            using var pe = new PEReader(image);
            // 公式APIがDLLのdebug IDとPDBを照合する。同名の古いPDBで別の型をアタッチさせない。
            if(!pe.TryOpenAssociatedPortablePdb(assembly, path => File.Exists(path) ? File.OpenRead(path) : null,
                    out var provider, out _)) return [];
            using var symbols = provider!;
            var reader = symbols.GetMetadataReader();
            var files = new SortedSet<string>(StringComparer.Ordinal);
            const BindingFlags declared = BindingFlags.DeclaredOnly | BindingFlags.Public | BindingFlags.NonPublic |
                BindingFlags.Instance | BindingFlags.Static;
            var methods = type.GetMethods(declared).Cast<MethodBase>().Concat(type.GetConstructors(declared));
            foreach(var method in methods) {
                var row = method.MetadataToken & 0x00ffffff;
                var information = reader.GetMethodDebugInformation(MetadataTokens.MethodDebugInformationHandle(row));
                foreach(var point in information.GetSequencePoints()) {
                    var document = point.Document.IsNil ? information.Document : point.Document;
                    if(!document.IsNil && !point.IsHidden) files.Add(reader.GetString(reader.GetDocument(document).Name));
                }
            }
            return files.ToArray();
        }
        catch(Exception exception) when(exception is IOException or BadImageFormatException or UnauthorizedAccessException) {
            // PDBは開発用情報。欠損時もゲームの実行契約を維持し、Browserではアタッチ候補なしとする。
            return [];
        }
    }
    /// <summary>Nativeと共通のIDサイズ・制御文字制約を検証する。</summary>
    internal static string Id(string value)
    {
        if (string.IsNullOrEmpty(value) || Encoding.UTF8.GetByteCount(value) > 512 || value.Any(char.IsControl))
            throw new InvalidOperationException("Invalid Script metadata ID.");
        return value;
    }
    /// <summary>重複キーを含む曖昧な状態を両言語で拒否する。未知のPropertyは削除しない。</summary>
    internal static JsonObject State(string text)
    {
        if (text.Length == 0) return new JsonObject { ["schemaVersion"] = 1, ["types"] = new JsonObject() };
        if (Encoding.UTF8.GetByteCount(text) > 262144) throw new InvalidOperationException("Script state exceeds 256 KiB.");
        using var document = JsonDocument.Parse(text, new JsonDocumentOptions { MaxDepth = 16 });
        Validate(document.RootElement);
        var root = JsonNode.Parse(text) as JsonObject ?? throw new InvalidOperationException("Invalid Script state.");
        if (root.Count != 2 || root["schemaVersion"]?.GetValue<int>() != 1 || root["types"] is not JsonObject types)
            throw new InvalidOperationException("Unsupported Script state schema.");
        foreach (var entry in types)
        {
            Id(entry.Key);
            if (entry.Value is not JsonObject scope || scope.Count != 2 || scope["version"]?.GetValue<int>() is not > 0 || scope["values"] is not JsonObject values)
                throw new InvalidOperationException("Invalid Script type state.");
            foreach (var value in values)
            {
                Id(value.Key);
                if (value.Value is not JsonObject kinds) throw new InvalidOperationException("Invalid Property state.");
                foreach (var kind in kinds) { Id(kind.Key); }
            }
        }
        return root;
    }
    /// <summary>通常のJSON DOM生成では上書きされる重複キーを事前に拒否する。</summary>
    private static void Validate(JsonElement value)
    {
        if (value.ValueKind == JsonValueKind.Object)
        {
            var keys = new HashSet<string>(StringComparer.Ordinal);
            foreach (var field in value.EnumerateObject()) { if (!keys.Add(field.Name)) throw new InvalidOperationException("Duplicate JSON key."); Validate(field.Value); }
        }
        else if (value.ValueKind == JsonValueKind.Array) foreach (var item in value.EnumerateArray()) Validate(item);
    }
}
/*-----------------------------------------------------------------------------------------
 * ScriptTypeMetadata
 * - 一つのScript型の永続契約とPropertyInfoを所有する
 * - Stateは型ID別に保存し、型の切替や旧Propertyを理由にデータを消さない
 *---------------------------------------------------------------------------------------*/
internal sealed class ScriptTypeMetadata
{
    internal readonly Type Type; //< object生成用。Nativeへ公開しない
    internal readonly string Id; //< Level保存の型識別子
    internal readonly int Version; //< 任意migrationを暗黙には実行しない
    internal readonly string[] Names; //< 正規ID・FullName・Aliasの探索順序
    internal readonly List<ScriptPropertyMetadata> Properties = []; //< 検証済みの公開Propertyだけを所有する
    /// <summary>型・Propertyの永続契約を検証して固定する。</summary>
    internal ScriptTypeMetadata(Type type)
    {
        Type = type;
        var attribute = type.GetCustomAttribute<ScriptTypeAttribute>();
        Id = ScriptMetadata.Id(attribute?.Id ?? type.FullName!); Version = attribute?.Version ?? 1;
        if (Version <= 0) throw new InvalidOperationException("Script version must be positive.");
        Names = new[] { Id, ScriptMetadata.Id(type.FullName!) }.Concat(attribute?.Aliases ?? []).Select(ScriptMetadata.Id).Distinct(StringComparer.Ordinal).ToArray();
        if (Names.Length > 257) throw new InvalidOperationException("Too many Script aliases.");
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (var info in type.GetProperties(BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public).OrderBy(p => p.Name, StringComparer.Ordinal))
        {
            var exposed = Attribute.GetCustomAttribute(info, typeof(ExposeAttribute), true) as ExposeAttribute;
            if (exposed == null) continue;
            var property = new ScriptPropertyMetadata(info, exposed);
            foreach (var name in new[] { property.Id }.Concat(property.Aliases)) if (!names.Add(name)) throw new InvalidOperationException($"Duplicate Property ID / alias: {name}");
            Properties.Add(property);
        }
        if (Properties.Count > 256) throw new InvalidOperationException("Too many exposed properties.");
    }
    /// <summary>Bind / OnCreateより前に保存値を適用し、未知値・旧kindを残す。</summary>
    internal void Restore(object instance, string text)
    {
        var types = (JsonObject)ScriptMetadata.State(text)["types"]!;
        EnsureCompatible(types);
        foreach (var property in Properties)
        {
            // 新IDを優先する。同じIDの型が変わった場合、旧kindは保持して既定値を使用する。
            JsonNode? saved = null; var found = false;
            foreach (var name in Names)
            {
                if (types[name]?["values"] is not JsonObject values) continue;
                foreach (var key in new[] { property.Id }.Concat(property.Aliases))
                    if (values[key] is JsonObject kinds && kinds.TryGetPropertyValue(property.Kind, out saved)) { found = true; break; }
                if (found) break;
            }
            if (found) property.Info.SetValue(instance, property.Convert(saved ?? throw new InvalidOperationException("Null exposed value.")));
        }
    }
    /// <summary>既知IDとAliasを全て確認し、古いコードで新しい保存契約を暗黙にdowngradeしない。</summary>
    private void EnsureCompatible(JsonObject types)
    {
        foreach(var name in Names)
            if(types[name] is JsonObject scope && scope["version"]!.GetValue<int>() > Version)
                throw new InvalidOperationException($"Saved Script version is newer than the loaded metadata: {Id}. Rebuild scripts or use compatible code.");
    }
    /// <summary>全公開値が取得・検証できた場合だけ新しい状態文字列を返す。</summary>
    internal string Capture(object instance, string text)
    {
        var root = ScriptMetadata.State(text); var types = (JsonObject)root["types"]!;
        EnsureCompatible(types);
        if (types[Id] is not JsonObject) types[Id] = new JsonObject { ["version"] = Version, ["values"] = new JsonObject() };
        var scope = (JsonObject)types[Id]!; scope["version"] = Version;
        var values = (JsonObject)scope["values"]!;
        foreach (var property in Properties)
        {
            if (values[property.Id] is not JsonObject) values[property.Id] = new JsonObject();
            values[property.Id]![property.Kind] = property.Read(instance);
        }
        var result = root.ToJsonString();
        if (Encoding.UTF8.GetByteCount(result) > 262144) throw new InvalidOperationException("Script state exceeds 256 KiB.");
        return result;
    }
}
/*-----------------------------------------------------------------------------------------
 * ScriptPropertyMetadata
 * - PropertyValueへ損失なく渡せる型だけを公開し、例外・非有限値を保存境界で拒否する
 *---------------------------------------------------------------------------------------*/
internal sealed class ScriptPropertyMetadata
{
    internal readonly PropertyInfo Info; //< CLR内だけで使用するaccessor参照
    internal readonly string Id, Label, Kind; //< メンバ名とは独立した永続・表示契約
    internal readonly string[] Aliases; //< 旧Property IDの探索順序
    /// <summary>accessor・index・init制約と対応型を検証する。</summary>
    internal ScriptPropertyMetadata(PropertyInfo info, ExposeAttribute exposed)
    {
        Info = info; Id = ScriptMetadata.Id(exposed.Id ?? info.Name); Label = ScriptMetadata.Id(exposed.Label ?? info.Name);
        if(Id == "@enabled") throw new InvalidOperationException("The Script Property ID @enabled is reserved for attachment state.");
        Aliases = exposed.Aliases.Select(ScriptMetadata.Id).ToArray();
        if (Aliases.Length > 256) throw new InvalidOperationException("Too many Property aliases.");
        if (info.GetIndexParameters().Length != 0 || info.GetMethod is not { IsPublic: true, IsStatic: false } ||
            info.SetMethod is not { IsPublic: true, IsStatic: false } || info.SetMethod.ReturnParameter.GetRequiredCustomModifiers().Contains(typeof(System.Runtime.CompilerServices.IsExternalInit)))
            throw new InvalidOperationException($"Expose requires a public instance get/set property: {info.Name}");
        var t = info.PropertyType;
        Kind = t == typeof(bool) ? "Bool" : t == typeof(int) ? "Int" : t == typeof(float) ? "Float" : t == typeof(string) ? "String" :
            t == typeof(EntityReference) ? "EntityReference" : t == typeof(ComponentReference) ? "ComponentReference" :
            t == typeof(Vector2) ? "Vector2" : t == typeof(Vector3) ? "Vector3" : t == typeof(Vector4) ? "Vector4" : throw new InvalidOperationException($"Unsupported Expose type: {t}");
    }
    /// <summary>getterから値をコピーし、nullや非有限値を保存前に拒否する。</summary>
    internal JsonNode Read(object instance)
    {
        var value = Info.GetValue(instance) ?? throw new InvalidOperationException($"Null exposed value: {Id}");
        JsonNode node = value switch {
            bool b => JsonValue.Create(b)!, int i => JsonValue.Create(i)!, float f => JsonValue.Create(f)!, string s => JsonValue.Create(s)!,
            EntityReference reference => JsonValue.Create(reference.ToString())!, ComponentReference reference => JsonValue.Create(reference.ToString())!,
            Vector2 v => new JsonArray(v.X, v.Y), Vector3 v => new JsonArray(v.X, v.Y, v.Z), Vector4 v => new JsonArray(v.X, v.Y, v.Z, v.W),
            _ => throw new InvalidOperationException("Unsupported property value.") };
        _ = Convert(node); return node;
    }
    /// <summary>kindに一致する値だけをsetterへ渡し、暗黙の型変更を避ける。</summary>
    internal object Convert(JsonNode node)
    {
        float Number(JsonNode? item) { var value = item!.GetValue<float>(); if (float.IsNaN(value) || float.IsInfinity(value)) throw new InvalidOperationException("Nonfinite property value."); return value; }
        if (Kind == "EntityReference") return EntityReference.Parse(node.GetValue<string>());
        if (Kind == "ComponentReference") return ComponentReference.Parse(node.GetValue<string>());
        if (Kind == "Bool") return node.GetValue<bool>();
        if (Kind == "Int") return node.GetValue<int>();
        if (Kind == "Float") return Number(node);
        if (Kind == "String") { var text = node.GetValue<string>(); if (Encoding.UTF8.GetByteCount(text) > 16384 || text.Contains('\0')) throw new InvalidOperationException("Invalid String property."); return text; }
        var size = Kind == "Vector2" ? 2 : Kind == "Vector3" ? 3 : 4;
        if (node is not JsonArray array || array.Count != size) throw new InvalidOperationException("Invalid Vector property.");
        if (size == 2) return new Vector2(Number(array[0]), Number(array[1]));
        if (size == 3) return new Vector3(Number(array[0]), Number(array[1]), Number(array[2]));
        return new Vector4(Number(array[0]), Number(array[1]), Number(array[2]), Number(array[3]));
    }
}
