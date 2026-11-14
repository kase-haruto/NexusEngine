using System.Numerics;

namespace Nexus.Managed;

/// <summary>System.Numericsの数学型を使うゲーム用補助API。左手系、+Y上/+Z前、row-vector、S*R*T。</summary>
public static class GameMath
{
    public const float Pi = (float)Math.PI;
    public const float Deg2Rad = Pi / 180;
    public const float Rad2Deg = 180 / Pi;
    /// <summary>三角関数の角度はラジアン。度からの変換はDeg2Radを使う。</summary>
    public static float Sin(float radians) { Require(IsFinite(radians), nameof(radians)); return (float)Math.Sin(radians); }
    public static float Cos(float radians) { Require(IsFinite(radians), nameof(radians)); return (float)Math.Cos(radians); }
    public static float Tan(float radians) { Require(IsFinite(radians), nameof(radians)); return (float)Math.Tan(radians); }
    public static float Atan2(float y, float x) { Require(IsFinite(x) && IsFinite(y), nameof(x)); return (float)Math.Atan2(y, x); }
    public static float Asin(float value) { Require(IsFinite(value) && value >= -1 && value <= 1, nameof(value)); return (float)Math.Asin(value); }
    public static float Acos(float value) { Require(IsFinite(value) && value >= -1 && value <= 1, nameof(value)); return (float)Math.Acos(value); }
    public static float Sqrt(float value) { Require(IsFinite(value) && value >= 0, nameof(value)); return (float)Math.Sqrt(value); }
    public static bool IsFinite(float value) => !float.IsNaN(value) && !float.IsInfinity(value);
    public static bool IsFinite(Vector2 value) => IsFinite(value.X) && IsFinite(value.Y);
    public static bool IsFinite(Vector3 value) => IsFinite(value.X) && IsFinite(value.Y) && IsFinite(value.Z);
    public static bool IsFinite(Vector4 value) => IsFinite(value.X) && IsFinite(value.Y) && IsFinite(value.Z) && IsFinite(value.W);
    public static bool IsFinite(Quaternion value) => IsFinite(new Vector4(value.X, value.Y, value.Z, value.W));
    public static bool IsFinite(Matrix4x4 value) => IsFinite(new Vector4(value.M11, value.M12, value.M13, value.M14)) &&
        IsFinite(new Vector4(value.M21, value.M22, value.M23, value.M24)) &&
        IsFinite(new Vector4(value.M31, value.M32, value.M33, value.M34)) &&
        IsFinite(new Vector4(value.M41, value.M42, value.M43, value.M44));
    internal static void Require(bool valid, string name)
    {
        if (!valid) throw new ArgumentException("A finite, valid value is required.", name);
    }
    public static float Clamp(float value, float minimum, float maximum)
    {
        Require(IsFinite(value) && IsFinite(minimum) && IsFinite(maximum) && minimum <= maximum, nameof(value));
        return Math.Min(maximum, Math.Max(minimum, value));
    }
    public static float Clamp01(float value) => Clamp(value, 0, 1);
    public static float Lerp(float from, float to, float t)
    {
        Require(IsFinite(from) && IsFinite(to), nameof(from));
        var weight = Clamp01(t);
        return from * (1 - weight) + to * weight;
    }
    public static Vector3 Lerp(Vector3 from, Vector3 to, float t)
    {
        Require(IsFinite(from) && IsFinite(to), nameof(from));
        var weight = Clamp01(t);
        return from * (1 - weight) + to * weight;
    }
    public static float SmoothStep(float from, float to, float t)
    {
        t = Clamp01(t);
        return Lerp(from, to, t * t * (3 - 2 * t));
    }
    public static float MoveTowards(float current, float target, float maximumDelta)
    {
        Require(IsFinite(current) && IsFinite(target) && IsFinite(maximumDelta) && maximumDelta >= 0, nameof(maximumDelta));
        var delta = (double)target - current;
        return Math.Abs(delta) <= maximumDelta ? target : (float)(current + Math.Sign(delta) * (double)maximumDelta);
    }
    public static Vector3 MoveTowards(Vector3 current, Vector3 target, float maximumDelta)
    {
        Require(IsFinite(current) && IsFinite(target) && IsFinite(maximumDelta) && maximumDelta >= 0, nameof(maximumDelta));
        var x = (double)target.X - current.X;
        var y = (double)target.Y - current.Y;
        var z = (double)target.Z - current.Z;
        var distance = Math.Sqrt(x * x + y * y + z * z);
        if (distance <= maximumDelta || distance == 0) return target;
        var t = maximumDelta / distance;
        return new Vector3((float)(current.X + x * t), (float)(current.Y + y * t), (float)(current.Z + z * t));
    }
    /// <summary>ゼロに近い入力にはfallbackを返す。巨大な有限値も長さ計算でoverflowさせない。</summary>
    public static Vector3 NormalizeSafe(Vector3 value, Vector3 fallback = default)
    {
        Require(IsFinite(value) && IsFinite(fallback), nameof(value));
        var largest = Math.Max(Math.Abs(value.X), Math.Max(Math.Abs(value.Y), Math.Abs(value.Z)));
        if (largest < 1e-12f) return fallback;
        return Vector3.Normalize(value / largest);
    }
    public static Quaternion NormalizeRotation(Quaternion value)
    {
        Require(IsFinite(value), nameof(value));
        var largest = Math.Max(Math.Max(Math.Abs(value.X), Math.Abs(value.Y)), Math.Max(Math.Abs(value.Z), Math.Abs(value.W)));
        Require(largest >= 1e-12f, nameof(value));
        return Quaternion.Normalize(new Quaternion(value.X / largest, value.Y / largest, value.Z / largest, value.W / largest));
    }
    public static Quaternion AngleAxis(float degrees, Vector3 axis)
    {
        Require(IsFinite(degrees), nameof(degrees));
        axis = NormalizeSafe(axis);
        Require(axis != Vector3.Zero, nameof(axis));
        return Quaternion.CreateFromAxisAngle(axis, degrees * Deg2Rad);
    }
    /// <summary>度単位の(X pitch, Y yaw, Z roll)。System.NumericsのYawPitchRoll規則で生成する。</summary>
    public static Quaternion EulerDegrees(Vector3 degrees)
    {
        Require(IsFinite(degrees), nameof(degrees));
        return Quaternion.CreateFromYawPitchRoll(degrees.Y * Deg2Rad, degrees.X * Deg2Rad, degrees.Z * Deg2Rad);
    }
    /// <summary>ローカル+Zをforwardへ向ける。ゼロ方向や平行なupは曖昧なので拒否する。</summary>
    public static Quaternion LookRotation(Vector3 forward, Vector3 up)
    {
        forward = NormalizeSafe(forward);
        up = NormalizeSafe(up);
        var right = NormalizeSafe(Vector3.Cross(up, forward));
        Require(forward != Vector3.Zero && right != Vector3.Zero, nameof(forward));
        up = Vector3.Cross(forward, right);
        return NormalizeRotation(Quaternion.CreateFromRotationMatrix(new Matrix4x4(
            right.X, right.Y, right.Z, 0, up.X, up.Y, up.Z, 0,
            forward.X, forward.Y, forward.Z, 0, 0, 0, 0, 1)));
    }
    public static Quaternion LookRotation(Vector3 forward) => LookRotation(forward, Vector3.UnitY);
    public static Quaternion Slerp(Quaternion from, Quaternion to, float t) =>
        NormalizeRotation(Quaternion.Slerp(NormalizeRotation(from), NormalizeRotation(to), Clamp01(t)));
    public static Matrix4x4 TRS(Vector3 translation, Quaternion rotation, Vector3 scale)
    {
        Require(IsFinite(translation) && IsFinite(scale), nameof(translation));
        var matrix = Matrix4x4.CreateScale(scale) * Matrix4x4.CreateFromQuaternion(NormalizeRotation(rotation)) * Matrix4x4.CreateTranslation(translation);
        Require(IsFinite(matrix), nameof(scale));
        return matrix;
    }
    public static Matrix4x4 Inverse(Matrix4x4 matrix)
    {
        Require(IsFinite(matrix), nameof(matrix));
        if (!Matrix4x4.Invert(matrix, out var inverse) || !IsFinite(inverse))
            throw new ArgumentException("Matrix is singular or its inverse is not finite.", nameof(matrix));
        return inverse;
    }
    public static Vector3 TransformPoint(Matrix4x4 matrix, Vector3 point)
    {
        Require(IsFinite(matrix) && IsFinite(point), nameof(point));
        var value = Vector4.Transform(new Vector4(point, 1), matrix);
        Require(IsFinite(value) && Math.Abs(value.W) >= 1e-12f, nameof(matrix));
        var result = new Vector3(value.X, value.Y, value.Z) / value.W;
        Require(IsFinite(result), nameof(matrix));
        return result;
    }
    /// <summary>平行移動を含めず、拡縮を含む線形部分でベクトルを変換する。</summary>
    public static Vector3 TransformVector(Matrix4x4 matrix, Vector3 vector)
    {
        Require(IsFinite(matrix) && IsFinite(vector), nameof(vector));
        var result = Vector3.TransformNormal(vector, matrix);
        Require(IsFinite(result), nameof(matrix));
        return result;
    }
    /// <summary>逆転置で法線を変換して正規化する。非一様拡縮にも対応する。</summary>
    public static Vector3 TransformNormal(Matrix4x4 matrix, Vector3 normal) =>
        NormalizeSafe(TransformVector(Matrix4x4.Transpose(Inverse(matrix)), normal));
}
