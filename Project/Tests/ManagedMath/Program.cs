using Nexus.Managed;
using System.Numerics;

int checks = 0;
void Require(bool value, string message) { if (!value) throw new Exception(message); ++checks; }
void Near(Vector3 value, Vector3 expected) => Require(Vector3.Distance(value, expected) < 1e-4f, $"{value} != {expected}");
void Reject(Action action)
{
    try { action(); } catch (ArgumentException) { ++checks; return; }
    throw new Exception("Invalid mathematical input accepted.");
}
Near(GameMath.NormalizeSafe(Vector3.Zero), Vector3.Zero);
Require(Math.Abs(GameMath.Sin(90 * GameMath.Deg2Rad) - 1) < 1e-5f, "Trigonometric angle convention failed.");
Require(Math.Abs(GameMath.Atan2(1, 0) * GameMath.Rad2Deg - 90) < 1e-5f && GameMath.Sqrt(4) == 2, "Scalar math failed.");
Reject(() => GameMath.Sqrt(-1));
Reject(() => GameMath.Acos(2));
Near(GameMath.NormalizeSafe(new Vector3(float.MaxValue, 0, 0)), Vector3.UnitX);
Near(Vector3.Transform(Vector3.UnitZ, GameMath.AngleAxis(90, Vector3.UnitY)), Vector3.UnitX);
Near(Vector3.Transform(Vector3.UnitZ, GameMath.EulerDegrees(new Vector3(0, 90, 0))), Vector3.UnitX);
Near(Vector3.Transform(Vector3.UnitZ, GameMath.LookRotation(Vector3.UnitX)), Vector3.UnitX);
Near(Vector3.Transform(Vector3.UnitY, GameMath.LookRotation(Vector3.UnitX)), Vector3.UnitY);
var matrix = GameMath.TRS(new Vector3(10, 20, 30), GameMath.AngleAxis(90, Vector3.UnitY), new Vector3(2, 3, 4));
var point = new Vector3(1, 2, 3);
Near(GameMath.TransformPoint(GameMath.Inverse(matrix), GameMath.TransformPoint(matrix, point)), point);
Near(GameMath.TransformVector(matrix, Vector3.UnitZ), new Vector3(4, 0, 0));
Near(GameMath.TransformNormal(matrix, Vector3.UnitZ), Vector3.UnitX);
Near(GameMath.MoveTowards(Vector3.Zero, new Vector3(3, 4, 0), 2), new Vector3(1.2f, 1.6f, 0));
Require(GameMath.MoveTowards(1, 2, 10) == 2 && GameMath.MoveTowards(1, -1, .5f) == .5f, "MoveTowards overshot.");
Require(GameMath.Lerp(0, 10, 2) == 10 && GameMath.SmoothStep(0, 1, .5f) == .5f, "Interpolation failed.");
Require(GameMath.IsFinite(GameMath.Lerp(float.MaxValue, -float.MaxValue, .5f)), "Lerp overflowed.");
Near(Vector3.Transform(Vector3.UnitZ, GameMath.Slerp(Quaternion.Identity, GameMath.AngleAxis(90, Vector3.UnitY), 1)), Vector3.UnitX);
Reject(() => GameMath.Inverse(Matrix4x4.CreateScale(0)));
Reject(() => GameMath.AngleAxis(90, Vector3.Zero));
Reject(() => GameMath.LookRotation(Vector3.UnitY, Vector3.UnitY));
Reject(() => GameMath.NormalizeRotation(default));
Reject(() => GameMath.MoveTowards(Vector3.Zero, Vector3.One, -1));
Reject(() => GameMath.Clamp(0, 1, -1));
Reject(() => GameMath.Lerp(0, 1, float.NaN));
Reject(() => new DebugLine(Vector3.Zero, Vector3.One, new Vector4(2, 0, 0, 1)));
Reject(() => new DebugLine(new Vector3(float.NaN, 0, 0), Vector3.One, Vector4.One));
Console.WriteLine($"PASS: {checks} managed mathematical convention, inverse, interpolation and invalid-value checks.");
