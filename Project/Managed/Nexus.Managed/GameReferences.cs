using System.Globalization;
using System.Runtime.InteropServices;
using Nexus.Managed.Interop;

namespace Nexus.Managed;

/// <summary>Levelに保存できるEntityの永続参照。未設定はdefault。WorldやNative pointerを保持しない。</summary>
public readonly struct EntityReference : IEquatable<EntityReference>
{
    internal readonly ulong High, Low;
    internal EntityReference(ulong high, ulong low) { High = high; Low = low; }
    public bool IsEmpty => (High | Low) == 0;
    public Entity? Resolve(GameWorld world) => world.Resolve(this);
    public bool TryResolve(GameWorld world, out Entity? entity) { entity = Resolve(world); return entity != null; }
    public override string ToString() => IsEmpty ? "" : High.ToString("x16", CultureInfo.InvariantCulture) + Low.ToString("x16", CultureInfo.InvariantCulture);
    public static EntityReference Parse(string text)
    {
        if(text == "") return default;
        if(text == null || text.Length != 32 || !ulong.TryParse(text.Substring(0, 16), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var high) ||
            !ulong.TryParse(text.Substring(16), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var low) || (high | low) == 0)
            throw new FormatException("EntityReference requires a nonzero 32-digit hexadecimal persistent ID.");
        return new(high, low);
    }
    public bool Equals(EntityReference other) => High == other.High && Low == other.Low;
    public override bool Equals(object? value) => value is EntityReference other && Equals(other);
    public override int GetHashCode() => High.GetHashCode() ^ Low.GetHashCode();
    public static bool operator ==(EntityReference a, EntityReference b) => a.Equals(b);
    public static bool operator !=(EntityReference a, EntityReference b) => !a.Equals(b);
}

/// <summary>Entityの永続IDとComponentの安定Type IDを保存する。同じ型の現在のComponentを解決する。</summary>
public readonly struct ComponentReference : IEquatable<ComponentReference>
{
    public EntityReference Entity { get; }
    private readonly string? typeId;
    public string TypeId => typeId ?? "";
    public bool IsEmpty => Entity.IsEmpty;
    public ComponentReference(EntityReference entity, string typeId)
    {
        if(entity.IsEmpty || string.IsNullOrEmpty(typeId) || System.Text.Encoding.UTF8.GetByteCount(typeId) > 512 || typeId.Any(c => c < 32 || c == 127))
            throw new ArgumentException("ComponentReference requires an Entity and a stable Component Type ID.");
        Entity = entity; this.typeId = typeId;
    }
    public GameComponent? Resolve(GameWorld world)
    {
        var entity = Entity.Resolve(world);
        return entity != null && entity.TryGetComponent(TypeId, out var component) ? component : null;
    }
    public bool TryResolve(GameWorld world, out GameComponent? component) { component = Resolve(world); return component != null; }
    public override string ToString() => IsEmpty ? "" : Entity + "|" + TypeId;
    public static ComponentReference Parse(string text)
    {
        if(text == "") return default;
        if(text == null || text.Length < 34 || text[32] != '|') throw new FormatException("Invalid ComponentReference.");
        return new(EntityReference.Parse(text.Substring(0, 32)), text.Substring(33));
    }
    public bool Equals(ComponentReference other) => Entity == other.Entity && TypeId == other.TypeId;
    public override bool Equals(object? value) => value is ComponentReference other && Equals(other);
    public override int GetHashCode() => Entity.GetHashCode() ^ TypeId.GetHashCode();
    public static bool operator ==(ComponentReference a, ComponentReference b) => a.Equals(b);
    public static bool operator !=(ComponentReference a, ComponentReference b) => !a.Equals(b);
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct ReferenceRequest { internal uint Operation, Reserved; internal ulong High, Low; }
public sealed unsafe partial class GameWorld
{
    /// <summary>生成元Worldの永続IDを取得する。別のWorldのEntityは拒否する。</summary>
    public EntityReference Reference(Entity entity)
    {
        if(entity == null || entity.World != this) throw new ArgumentException("Entity belongs to another World.", nameof(entity));
        ReferenceRequest input = new() { Operation = 1 };
        var bytes = new byte[sizeof(ReferenceRequest)];
        fixed(byte* data = bytes) *(ReferenceRequest*)data = input;
        var result = InvokeWorldCommand("nexus.engine.references", entity.Handle, bytes, false);
        if(result.Length != 16) throw new InvalidOperationException("Invalid persistent ID response.");
        return new(BitConverter.ToUInt64(result, 0), BitConverter.ToUInt64(result, 8));
    }
    public ComponentReference Reference(GameComponent component)
    {
        if(component == null) throw new ArgumentNullException(nameof(component));
        return new(Reference(component.Entity), component.TypeId);
    }
    /// <summary>現在のWorldだけを検索する。未設定・削除済み・別Levelの参照はnull。</summary>
    public Entity? Resolve(EntityReference reference)
    {
        Access();
        if(reference.IsEmpty) return null;
        ReferenceRequest input = new() { Operation = 2, High = reference.High, Low = reference.Low };
        var bytes = new byte[sizeof(ReferenceRequest)];
        fixed(byte* data = bytes) *(ReferenceRequest*)data = input;
        var result = InvokeWorldCommand("nexus.engine.references", default, bytes, false);
        if(result.Length != sizeof(EntityHandle)) throw new InvalidOperationException("Invalid reference response.");
        fixed(byte* data = result) { var handle = *(EntityHandle*)data; return handle.World == 0 ? null : Wrap(handle); }
    }
}
