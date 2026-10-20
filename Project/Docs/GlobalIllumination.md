# 拡散Ray Traced Global Illumination

現在のゲーム向け既定は[DDGI](DDGI.md)。[ベイクGI＋ライトプローブ](BakedLighting.md)と本資料のpixel RTGIは比較用の任意機能。DDGIまたはベイク方式の選択中はpixel RTGIを停止する。

## 既存描画の調査

Editorは初期状態でambientIrradiance={1.5,1.8,2.4}を設定する。HDR pathは空で、明るさだけではIBL/GIが動作していることを示さない。
既存IBLは遠方環境を積分するがシーンの遮蔽を含まない。室内でも同じirradianceを与えるため、壁越しに明るくなる。
これらは環境照明の近似であり、シーン内の相互反射を計算するGIではない。

## 実装方針

GIの設定はRenderSceneの値snapshotとし、DXRのTLAS/材質record/closesthitは既存OpticalBackendと共有する。
新しいAS所有者・Manager・Scene走査は追加しない。光輸送の実装は独立したGlobalIllumination.hlsliに置く。
専用のRGBA16_FLOAT拡散GI出力だけを追加し、Raster/Mesh Shaderの材質・直接光・Shadow Map/RT Shadow・IBL鏡面反射は維持する。
GI有効pixelでは一定ambient/Diffuse IBLをGIで置換し、加算しない。鏡・ガラスの経路で交差した面も同じGI関数を使用する。

cosine-weighted経路で実際の遮蔽を判定し、交差面の直接光と発光を積分する。追加bounceで色の照り返しと環境の反射を追跡する。
ray missだけにHDR環境を与え、HDR未指定時のambientはE/πの一様な遠方radianceとして扱う。
二次面へ未遮蔽のIBLやambientを足さない。閉じた箱の内部は光源/発光がなければ暗くなる。
Lambertの1/πとcosine PDFは相殺し、経路throughputには反射率とFresnelによる拡散エネルギー配分を掛ける。

参考: [PBRT cosine hemisphere sampling](https://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations)、[DXR specification](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)。
TLAS共有、別出力texture、既存GI未対応Sceneのopt-inは本エンジン固有の設計判断。

## 合成とデータフロー

```text
RenderScene.globalIllumination（値だけ）
  → PrimitiveRenderer（既存のGeometry/Material snapshot）
  → OpticalSystem → OpticalBackend（共有TLAS、Hit属性、Shader Table）
  → OpticalRaygen
       ├ GlobalIllumination.hlsli：間接拡散と鏡面環境の可視率 → GI出力
       └ 既存の鏡・屈折経路：交差面でも同じGI評価 → 光学出力
  → Raster / Mesh共通PS → PBR / IndirectLighting
       ├ 直接光：既存Shadow Map / RT Shadowのvisibility
       ├ 間接拡散：有効なGIでambient/Diffuse IBLを置換
       ├ 鏡面：既存Specular IBL × 環境への可視率
       └ 発光：線形HDRで加算
```

一次面をcosine重点でサンプルし、面の拡散BRDFによる反射率を経路重みに掛ける。
各交差点では遮蔽付きDirectional Lightの直接反射とEmissiveを積算し、次の拡散方向を選ぶ。
指定segment数で経路を打ち切るため、無限バウンスの解ではない。maxBounces=1でも最初に交差した面からの反射・発光は計算する。
maxBounces=2では、受光面→色付き壁→発光面のような経路も計算できる。
環境だけのシーンでも、rayが外へ到達した経路だけが環境を受け取る。
二次交差点へ一定ambientを注入しないことが、閉空間への光漏れ防止に必要である。

通常材質の鏡面IBLは残すが、GI有効時は反射方向の遮蔽rayも追跡する。
反射方向が壁で遮られる場合に遠方のHDRが映らないようにする。
これは粗さ付きBRDF全体の可視率を積分する処理ではなく、単一反射方向による近似である。
通常材質に近傍の物体の鏡像を生成するRT reflectionは今回追加していない。既存OpticalMaterialの鏡・ガラスはその専用経路を維持する。

GI出力のRGBは受光面の材質・AOまで適用済みの間接拡散radiance。
alpha=0は未計算、alpha=1+鏡面環境可視率（1または2）は有効な結果を表す。
有効時はIndirectLightingで古い拡散項を置き換え、Diffuse IBL用cubeの不要な参照と二重加算を避ける。
GIへの環境Intensityはmissで一度だけ適用し、PBR側では再乗算しない。
GI Intensityの物理基準は1で、0にすると間接拡散が0になる。有効化そのものを切ると既存IBL/ambientへ戻る。

## 所有権・同期

専用GI出力はRGBA16_FLOATで、光学出力と同じ画面寸法を持つ。OpticalBackendがtextureとSRV/UAVを所有する。
初回のDXR利用時だけ生成し、同じ寸法では有効・無効切替で再生成しない。光学描画だけを使う場合にもGI用textureは確保するが書き込みは省く。
Resizeは既存のQueue idle後に両出力を作り直す。通常frameではQueue待機や新しいDescriptor割り当てを増やさない。
同じDirect Queue内でUAV→SRV遷移によりRasterの読み込みとの順序を保証する。
未提出のCommand Listが破棄された場合は、既存CompleteRecording callbackで両出力の状態とAS記録を復元する。
Shutdownは既存Fence完了後にviewをRetireし、textureを解放する。
OpticalSystemは毎回Render前に公開GI参照を消し、無効・非対応・失敗時に前frameの結果を参照しない。
Scene/RuntimeへDescriptorやDirectX型は公開しない。PrimitiveDrawDataへGI入力とベイク転送値を追加し、現在は4720byte。
DXRの共通定数は224byte、交差ペイロードは幾何法線を追加して100byteとなった。

## 自己交差と光漏れ

法線マップの法線とは別に三角形の辺から幾何法線を復元し、ray始点の移動に使う。
移動済みの始点から追跡するGI/shadow rayではTMin=0とする。
始点移動に加えてTMinにも同じbiasを掛けると、角でごく近い隣接面を飛び越える場合がある。
閉空間テストでこの問題を検出し、二重の除外を修正した。
ray biasと最大距離は既存OpticalSettingsのrayBias/rayDistanceを共有する。
極端に小さい形状、薄い壁、rayDistance外の遮蔽物についてはシーンのスケールに応じた調整が必要。

## 設定と表示

EditorのShadowsパネルで以下を利用する。

- Diffuse RTGI Enabled：有効化。現在のEditorデモは初期状態で無効。ベイク方式を無効にした場合だけ選択できる。
- RTGI output (last frame)：直近の描画でGI出力が利用可能か表示。
- GI Samples Per Pixel：1～64、既定8。
- GI Diffuse Segments：1～4、既定2。
- GI Intensity：既定1。通常はこの値を維持して環境・直接光・露出を調整する。
- Diffuse GI Only：間接拡散だけを表示。
- Indirect Lighting Only：GIと環境鏡面反射を合成した結果。
- Diffuse IBL Only / Specular IBL Only：比較用の未遮蔽IBL。
- Fallback Sky Intensity：HDR未指定時の遠方の一様環境。GI有効時は遮蔽を通過した経路だけに作用。

一般のRenderSceneはglobalIllumination.enabled=falseを既定とし、既存Sceneの挙動を維持する。
RTGI設定は現時点では実行中のみ保持する。別途追加した静的ベイク対象フラグはLevel形式v4で保存する。
DXR非対応時は既存Raster/IBLへフォールバックする。その場合の明るさはGIの検証結果ではない。
VS-only skinningは共有ASが対応していないためGIも未対応で、エラーと既存描画へのフォールバックとなる。

## 性能と制約

鏡・ガラスのTLASと材質recordを共有し、GI用の別AS構築やScene再走査を増やさない。
鏡・ガラスがある場合は同じDispatchRaysを使用する。通常材質だけのシーンではGI有効化によりDXRパスが一つ加わる。
RT Shadow用のASは既存どおり独立しており、今回はAS管理全体の統合までは行っていない。
GIの主な追加費用は画素数×経路数×segment数のray追跡と、照らされた二次面でのshadow ray。
HDR使用時には一次面・光学経路の交差面で鏡面環境遮蔽rayも追加する。
Shadow rayは最初の交差で終了し、closesthitの材質サンプリングを省く。
GI用textureは約8byte/pixel、1920×1080で約15.8MiB（配置alignmentを除く）。Rasterでの参照は画素ごとに1回。
黒色・完全金属の間接拡散と、完全な鏡・透過ローブでは不要な拡散追跡を省く。
画面外の形状・発光も共有TLASへ含まれるため、SSGIと異なり画面外情報を取得できる。

実装は拡散光輸送の基礎であり、DDGI/Probe cacheや製品向けdenoiserではない。
履歴蓄積・再投影・空間フィルターは未実装。静止中の乱数は固定してちらつきを避けるが、低sample数では粒状ノイズが残り、camera移動時にはサンプル配置が変わる。
サンプル数増加で品質は上がるが負荷も増える。GPU時間の詳細計測・大規模シーンの性能最適化は未実施。
最大4segmentで打ち切るため、高反射率の室内では実際の多重反射より暗くなる。
発光面への重点サンプリング/MISは未実装で、小さい発光面の収束は遅い。
GI経路内の屈折・鏡面バウンス・causticsは未対応。GIの透過材質はopaqueな境界として扱うため、ガラス越しの間接照明は正確ではない。
通常材質の鏡面反射はIBLと単一rayの遮蔽近似であり、完全なspecular GIではない。
材質mapはDXRでLOD0、Alpha Mask非対応など共有DXR基盤の制約も引き継ぐ。

## 検証

```powershell
Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe --gi
Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe --ibl
Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe
```

GPU Validationを有効にした実描画・readbackで検証する。
variant1は外部の一様環境に照らされた閉じた部屋。旧ambientでは明るいがGIではRGBが全て0になることを確認する。
variant2は画面外の赤い発光面。白い受光面に赤だけの間接光が届くことを確認する。
variant3は白い発光面と赤い壁。1segmentでは中立色、2segmentでは赤い色の照り返しが増えることを確認する。
variant5は白いHDR環境の閉空間。Diffuse GI表示だけでなく最終描画もRGB=0となり、鏡面IBLの光漏れも抑えられることを確認する。
加えてRaster/Mesh一致、Shadow方式切替、強度0、静止frameの決定性、DXR非対応fallback、記録破棄後の復帰、鏡/ガラスとの共存を検証する。
比較画像はGenerated/GI1～5Before.ppm / After.ppm（4は鏡・ガラス）、ログはGenerated/GITests.log。
PPMは線形readbackを0～1へ制限した診断出力で、実景HDRの画質評価を代替しない。

## 変更箇所・実施結果

| ファイル（Project/基準） | 内容 |
|---|---|
| Graphics/Lighting/GlobalIlluminationSettings.h | API非依存のGI設定と範囲検証 |
| Graphics/Renderer/RenderScene.h、PrimitiveRenderer.h/.cpp | 設定snapshot、GI出力の受け渡し、Editor向け利用可能状態 |
| Graphics/Material/PrimitiveDrawData.h、Shaders/Primitive/PrimitiveDrawData.hlsli、Primitive.PS.hlsl | 既存paddingへのGI index転送、専用出力の参照 |
| Graphics/RayTracing/OpticalSystem.h/.cpp | GI要求の検証、失敗・無効時の参照リセット |
| Graphics/Backend/DirectX12/RayTracing/OpticalBackend.h/.cpp | TLAS/材質/Dispatch共有、追加texture、同期・Resize・破棄 |
| Shaders/RayTracing/GlobalIllumination.hlsli、Optical.hlsl | 拡散経路追跡、環境可視率、幾何法線、光学経路との共存 |
| Shaders/Common/PBR.hlsli、IndirectLighting.hlsli | GIによる拡散項の置換・鏡面環境の遮蔽 |
| Graphics/Environment/EnvironmentLightingData.h | Diffuse GI診断モード |
| Editor/Core/EditorApplication.cpp | デモのGI有効化、品質設定、状態表示、遠方環境のUI表記 |
| Tests/GlobalIlluminationTests.inl、GraphicsShadowTests.cpp | 閉空間・画面外発光・色の照り返し・共存の実描画テスト |
| NexusEngine.vcxproj / .filters、Tests/NexusGraphicsTests.vcxproj | 新しい設定・shader・テスト・資料の登録 |
| README、Docs/Lighting.md、GraphicsREADME.md、IBL.md | 現行GIとの関係・設定・制約の更新 |

2026-10-16、RTX 3060 Laptop GPU（DXR 1.1、Mesh Shader Tier 1）で検証。
Debug/x64本体は警告0・エラー0でビルド成功。Graphics/ECSのビルドも成功。
`--gi`、`--ibl`、既存全Graphics回帰、`--fog`、ECSは全て終了コード0。
GPU Validationの警告・エラーは失敗扱いで、新しいGIパスでは発生していない。
全Graphicsの故意に壊したASによるfallbackエラーログは既存テストの想定どおり。

閉空間テストは一定環境/HDRの両方でGI後のRGB合計が0。
赤い画面外発光のテストは受光面のreadback合計がR=253961、G=B=0。
白い発光面と赤い壁の2segmentテストはR=268339、G=B=237035で、1segment参照はR=Gを確認した。
比較用のGenerated/GIComparison.pngはPPMを閲覧用にsRGB符号化して並べた診断画像で、ポスト処理済みの完成画質ではない。

次段階はGPU時間計測、履歴蓄積・再投影・denoiser、発光面への重点サンプリング、GI用の半解像度化とエッジを保った拡大。
静的なProbe cacheは後続の[BakedLighting](BakedLighting.md)で実装した。今後は動的なDDGI、透過を含む光輸送、粗さに対応した局所鏡面反射へ進める。
