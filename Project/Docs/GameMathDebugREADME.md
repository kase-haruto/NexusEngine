# C#の数学・デバッグAPI

数学型は`System.Numerics.Vector2 / Vector3 / Vector4 / Quaternion / Matrix4x4`を使用します。独自の互換Vector型は必要ありません。補助処理は`Nexus.Managed.GameMath`、診断は`World.Debug`または`ScriptBehaviour.Debug`から利用します。

## 座標と数学

エンジンと同じ左手系、+X右、+Y上、+Z前です。行列はrow-vectorで、TRSは`Scale * Rotation * Translation`、親子は`Local * ParentWorld`です。`AngleAxis`と`EulerDegrees`の角度は度単位です。`EulerDegrees`は(X pitch, Y yaw, Z roll)をSystem.NumericsのYawPitchRoll規則へ渡します。Y軸+90度は+Zを+Xへ向けます。`LookRotation`はローカル+Zをforwardへ向け、upとの直交基底を作ります。

| API | 動作 |
|---|---|
| `Pi / Deg2Rad / Rad2Deg` | 角度の定数・換算係数 |
| `Sin / Cos / Tan / Asin / Acos / Atan2 / Sqrt` | float向け基本数学関数。三角関数の角度はラジアン |
| `Clamp / Clamp01` | 有限値の範囲制限 |
| `Lerp(float/Vector3)` | tを0～1に制限した補間 |
| `SmoothStep` | tを0～1に制限した滑らかな補間 |
| `MoveTowards(float/Vector3)` | 非負の最大移動量で目標へ近付ける。通り過ぎない |
| `NormalizeSafe` | Vector3の正規化。ほぼゼロの場合は指定fallback。既定はゼロ |
| `NormalizeRotation / AngleAxis / EulerDegrees / LookRotation / Slerp` | 回転生成・正規化・補間 |
| `TRS / Inverse` | TRS行列と逆行列。特異行列のInverseは例外 |
| `TransformPoint / TransformVector / TransformNormal` | 点、拡縮を含むベクトル、逆転置での法線変換 |
| `IsFinite` | scalar、各Vector、Quaternion、MatrixのNaN/Infinity検査 |

これ以外の内積・外積・長さ・各Vectorの演算・行列の積などはSystem.NumericsのAPIをそのまま使えます。`GameMath`の入力は有限値が前提です。ゼロQuaternion、ゼロ回転軸、平行なforward/up、不正な範囲などはArgumentException系で拒否します。

## Entityの行列と座標変換

- `Entity.LocalMatrix`：現在のLocal Transformから作る行列。
- `Entity.WorldMatrix / WorldPosition`：現在のLocal値と親階層を評価した読み取り専用のコピー。TransformSystemのcache更新前でも書き換え直後の値を使えます。
- `Entity.TransformPoint / InverseTransformPoint`：ローカルとワールド間の点の変換。
- `Entity.TransformVector / TransformNormal`：ワールドへのベクトル・法線変換。前者は拡縮を含み、後者は非一様拡縮に対応して正規化します。

Local Transformの変更は従来どおりコピーを取得して`Entity.Transform`へ書き戻します。WorldMatrixを変更してもEntityは変更されません。WorldMatrixの取得は親階層を辿るInteropなので、大量の点を変換するときは一度取得した行列を再利用してください。

## デバッグ

| API | 動作 |
|---|---|
| `Debug.Log / LogWarning / LogError` | 既存のEngine LoggerへInfo / Warning / Errorとして配送 |
| `Debug.LogException` | 例外の型・本文・stack trace・inner exceptionをErrorとして記録 |
| `Debug.Assert` | falseならErrorを記録してfalseを返す。実行の停止や例外送出は行わない |
| `Debug.DrawLine / DrawRay` | ワールド空間の線。Rayのdirectionは長さを含む |
| `Debug.DrawBox` | center、全幅size、回転で指定する12辺のBox |
| `Debug.DrawSphere` | 3つの大円で球を描く。segmentsは1円あたり3～21、既定16 |
| `Debug.DrawAxes` | 行列のX/Y/Z軸を赤/緑/青で描画。行列の拡縮を含む |
| `Debug.DrawLines` | 最大64線をまとめて追加。全件検証してから反映 |
| `Debug.Clear` | 同じManaged sessionから登録した線を消す。他のclientの線は維持 |

線の色は0～1の線形RGBA、太さは1 pixelです。HDR Sceneへ描くため、最終的な見え方にはポストプロセスが適用されます。`depthTest=true`でScene深度を使い、falseで遮蔽物の手前へ重ねます。線自体は深度を書き込みません。

`duration`は非負のゲーム秒です。0は描画snapshotへ抽出された次のゲーム更新まで表示します。複数ViewportやCamera previewの抽出で消費されません。Pause中は時間を進めません。Reload・Stopでclientが閉じると、そのclientの線は破棄します。シーンファイルには保存しません。表示には描画対象のCameraが必要です。

Scene全体の上限は1024線です。上限を越える要求は部分反映せず失敗します。ログは1件4080 UTF-8 byte以内へUnicode境界で省略し、`[truncated]`を付けます。診断APIはRelease構成でも有効です。所有thread以外や停止後のWorldへのアクセスは拒否します。デバッガの接続やブレークポイント操作、画面上の文字描画はこのAPIに含みません。

ScriptのLifecycle例外も新しいHostではErrorとstack traceで記録します。追加サービスがない旧Hostでは従来のLogへfallbackします。新APIを使うにはEngine / Nexus.Managed / Bootstrapを揃えてビルドし、ホストを再起動してください。

## 使用例

```csharp
using Nexus.Managed;
using System.Numerics;

public sealed class Player : ScriptBehaviour
{
    protected override void OnCreate()
    {
        Debug.Log("Player created.");
        Debug.Assert(Entity.HasTransform, "Player needs a Transform.");
    }

    protected override void OnUpdate(float deltaTime)
    {
        var transform = Entity.Transform;
        transform.Rotation = GameMath.EulerDegrees(new Vector3(0, 45, 0));
        Entity.Transform = transform;

        var matrix = Entity.WorldMatrix;
        var origin = GameMath.TransformPoint(matrix, Vector3.Zero);
        Debug.DrawRay(origin, Vector3.UnitZ * 3, new Vector4(1, 1, 0, 1));
        Debug.DrawSphere(origin, .5f, new Vector4(0, 1, 1, 1));
    }
}
```

## 検証

- `dotnet run --project Project/Tests/ManagedMath/ManagedMathTests.csproj`：角度・座標規約、基本数学関数、逆行列、補間、不正値の27項目。
- `NexusEditorTests`：実Bridgeで重要度、要求の検証、容量、複数描画先、所有者の消去、現在の階層行列、thread拒否を検証。
- `NexusGraphicsTests --debug-draw`：実GPUのreadbackで可視線、深度遮蔽、深度なしoverlay、次の空frameを検証。
- `NexusMathDebugTests <nethost.dll> <Bootstrap.dll> <MathDebugScripts.dll>`：実CoreCLRでC#からの行列・全shape・ログ・例外stack・Unicode省略・thread拒否・Reloadを検証。fixtureは`Tests/Fixtures/MathDebug`。
- Managed APIとBootstrapはMono向けnet48でもビルド確認。今回のMono上の実行検証は未実施。
