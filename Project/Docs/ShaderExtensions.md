# シェーダーの追加と利用

ノードで編集する場合は[描画パイプライングラフ](RenderPipelineGraph.md)を参照してください。

| 用途 | 公開API | HLSLの配置 |
|---|---|---|
| エンジン共通の画面エフェクト | `GraphicsSystemDesc::screenEffects` / `GraphicsSystem::SetScreenEffects` | Shader Root相対、または絶対パス |
| ゲーム固有の画面エフェクト | `Level::ScreenEffects()` | ゲームProject Root相対、または絶対パス |
| 独自のシーン描画 | `IGraphicsRenderer`、`Shader`、`GraphicsResourceFactory`、`GraphicsContext` | 任意のHLSLファイル |
| Grid | `RenderScene::grid` / `GridRenderer` | `Shaders/Grid/Grid.hlsl` |

HLSLはDXCで実行時にコンパイルします。Visual StudioのFxCompileへの登録は不要です。C++実装を追加する場合は使用するvcxprojへ登録します。SDK配布ではエンジンのShadersもコピーされます。ゲーム固有のHLSLとincludeはゲーム資源として配布してください。

## ゲーム側の画面エフェクト

`GameLevelSetup`（サンプルの `SetupSampleGame(Level&)`）から登録します。

```cpp
ScreenEffect vignette;
vignette.shaderPath = L"Assets/Shaders/MyVignette.hlsl";
vignette.parameters[0] = 0.7f;
level.ScreenEffects().push_back(vignette);
```

`Assets/Shaders/MyVignette.hlsl`:

```hlsl
#include "PostProcess/ScreenEffect.hlsli"
float4 PSMain(ScreenVertex v) : SV_TARGET {
    float4 c = sourceColor.SampleLevel(linearClamp, v.uv, 0);
    float2 p = (v.uv - 0.5) * 2;
    c.rgb *= 1 - saturate(parameters[0].x) * saturate(dot(p, p) * 0.5);
    return c;
}
```

`GameApplication`とEditorのPlayはLevelの設定を描画snapshotへ渡します。相対パスは `ApplicationPaths::projectRoot` が基準です。Project Root未設定の独自ホストでは絶対パスを使用してください。独自Rendererでは `IGraphicsRenderer::GetScreenEffects()` からCPU設定のspanを返せます。

実行中は `Level::ScreenEffects()[index].parameters` / `enabled` の変更が次フレームに反映されます。数値や有効・無効の更新では再Compile・GPU idle待機を行いません。これは実行時設定で、Levelファイルへの保存対象ではありません。ロード・Unloadでクリアされ、Play開始時のSetupで再登録します。

include検索先にはエンジンのShader Rootが入ります。独自の `Shader::Initialize` / `ShaderCompiler::Compile` では `ShaderCompileDesc::includeDirectories` で検索先を追加できます。検索先もCompileキャッシュのキーに含まれます。

## エンジン側の画面エフェクト

```cpp
GraphicsSystemDesc desc;
ScreenEffect vignette;
vignette.shaderPath = L"PostProcess/Vignette.hlsl"; // desc.shaderDirectory相対
vignette.parameters[0] = 0.5f;
desc.screenEffects.push_back(vignette);
// GraphicsSystem::Initialize(surface, desc) または FrameworkDesc::graphics へ渡す。
```

初期化後は `SetScreenEffects(span)` でエンジン側の列を交換し、空リストで解除します。`UpdateScreenEffect(index, parameters, enabled)` はエンジン側の項目を更新します。これらは描画スレッドのフレーム境界で呼びます。自動ファイル監視はありません。HLSL編集後は `ReloadScreenEffects()` で明示的に再読込します。全Compile・PSO生成が成功してからGPU idleを待ち交換するため、失敗時は旧Pipelineが保持されます。ゲーム設定の同期に失敗したフレームはErrorを返すため、ホスト側で診断・復旧を扱います。

```text
HDRシーン（Gridを含む） → Bloom / 色調整 / Tone mapping / sRGB符号化
  → エンジンの画面エフェクト（登録順）
  → ゲームの画面エフェクト（登録順） → FXAA → Editor UI
```

画面エフェクトの契約は `t0 = 直前の画像`、`s0 = Linear Clamp`、`b0 = float4 parameters[4]` です。共通の全画面三角形VSを使い、PSの既定entryは `PSMain`（`entryPoint` で変更可）です。独自Texture/UAV/Depth入力は含みません。入力はsRGB符号化済みRGBA8 UNORMで、追加のsRGB符号化は不要です。線形光で計算したい処理では明示的にdecode・encodeしてください。HDR Bloomや深度依存処理用の挿入点ではありません。

内部画像2枚を交互に使い、同一画像を同時にSRV/RTVへ設定しません。FXAA双方、奇数・偶数パス、無効項目、Resizeに対応します。カメラプレビューも同じ列を使い、画像資源は独立しています。

## Grid・独自シーン描画

`RenderScene::grid.enabled = true` でXZ平面のGridを表示します。`spacing`、`height`、`fadeDistance`、線形RGBAの `color` を設定できます。Editorの設定 → レベルエディタ → グリッドを表示で切り替えます。Editor Gridは編集中のシーンビューに表示し、Playとカメラプレビューには表示しません。

`GridRenderer.cpp` がシェーダーパス追加の実装例です。

1. `Shader::Initialize` でVS/PSをCompile・Reflectionする。
2. `GraphicsPipelineDesc` で頂点形式、HDR出力、Depth、Blendを指定する。
3. FactoryでPipeline・ConstantBufferを生成する。
4. 現在Frame indexの定数を更新し、ContextでPipeline・定数をBindしてDrawする。
5. GPU idle後、Deviceより先にShutdownする。

Gridは全画面三角形から床面との交点と深度を計算します。X軸は赤、Z軸は緑で、ズームに応じて目盛りを10倍刻みで滑らかに切り替えます。HDR出力、alpha blend、深度テスト有効・深度書込無効で、物体の後・選択枠の前に描画します。独自Rendererでも `GridRenderer::Initialize` と `Render(context, viewProjection, cameraPosition, settings)` で再利用できます。

通常のシーンPSOは `hdrRenderTarget = true`、Depth付きの場合は `depthStencilFormat = D32Float` を指定します。ReflectionからRootSignatureを作るため、Root parameter番号を直接管理する必要はありません。

## 検証

`NexusGraphicsTests.exe --color` は既存の色空間・Bloom・Tone mapping検証に加え、独自エフェクト1～3段の順序、FXAA双方、無効項目、Resize、無効入力、失敗時の旧構成保持、Grid描画・深度遮蔽をGPU Validation付きで確認します。
