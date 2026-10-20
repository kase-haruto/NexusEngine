# DDGI — 動的照度フィールド

ゲーム描画向けの既定GIをDDGI（Dynamic Diffuse Global Illumination）へ変更した。
静的なライトマップ／SHプローブのベイクとは別の方式で、実行中にDXRでプローブを更新する。
画面の各pixelから拡散パスを追跡せず、ラスタライズした表面でプローブの照度を補間する。
本実装は原論文の照度フィールドと距離モーメントを基にした独自実装。NVIDIA RTXGI SDKを組み込んだものではない。

## 使用方法

EditorのShadowsパネルに **DDGI / Dynamic Irradiance Field** を追加した。

- **DDGI Enabled**：動的GIを有効にする。デモでは既定で有効。
- **DDGI Origin / Spacing / Counts**：最小格子点、XYZ間隔、点数。設定変更で履歴をリセットする。
- **DDGI Rays x32**：1点あたり32～256本。既定128本。
- **DDGI Hysteresis**：履歴の重み。既定0.97。大きいほど安定するが、照明変更への追従が遅くなる。
- **DDGI Intensity**：表示用倍率。フィードバックの物理量へは掛けない。
- **DDGI Normal / View Bias**：受光位置の自己遮蔽回避。大き過ぎると薄い壁を越えるため、シーン尺度に合わせる。
- **DDGI Ray Distance**：最大追跡距離。体積内の必要な遮蔽物を含む値にする。
- **DDGI Relocation**：形状内や面付近のプローブをセルの範囲内で移動する。
- **DDGI Multi-bounce Feedback**：前フレームの照度を二次面へ戻し、反射を時間方向へ伝播する。
- **DDGI Rotate Rays**：球面サンプルの方位を更新する。既定は無効。
- **Freeze DDGI / Reset DDGI History**：照度更新の停止と履歴リセット。
- **Diffuse GI Only / Indirect Lighting Only**：現在選択されたGIだけ、または環境鏡面反射との合成を表示する。

```cpp
renderScene.ddgi.enabled = true;
renderScene.ddgi.origin = {-4, 0.2f, -4};
renderScene.ddgi.spacing = {1, 1, 1};
renderScene.ddgi.counts = {9, 5, 9};
renderScene.ddgi.raysPerProbe = 128;
renderScene.ddgi.hysteresis = 0.97f;
// 明示的に照度履歴を破棄する。
++renderScene.ddgi.revision;
```

補間領域は各端の格子点から半セルまで。領域外は従来の環境照明へ戻る。
動く物体も背景も同じ照度フィールドを参照する。staticLightingフラグはベイク専用で、DDGIの動的な遮蔽物を除外しない。
選択優先度はDXR対応時の **DDGI → ベイクGI → pixel RTGI**。
DDGIを選択すると旧pixel RTGIを停止する。DDGI対応時はベイクのロード／評価を停止し、拡散項を二重加算しない。
DXR非対応時はDDGIを生成せず、選択済みのベイクGIまたはIBL／一定環境光へ戻る。
DDGIの設定は現在RenderSceneの値snapshotで、Levelファイルへの保存は追加していない。

## 調査と責務分離

既存OpticalBackendには、インスタンス対応のTLAS、Geometry世代別のVB／IB参照、glTFの材質マップ評価、光学パス用Shader Tableがあった。
これらを再利用してプローブraygenを追加した。新しいAS所有者・材質ローダー・Scene走査・Managerは追加しない。
従来のベイク成果物はSH係数と最小距離を保存していたが、DDGIには時間的に更新する方向別照度と距離モーメントを別途用意する。

| 要素 | 責務・変更点 |
|---|---|
| DDGISettings / RenderScene | 配置・品質・有効状態だけを渡す。Native型やGPU資源を保持しない |
| DDGIProcessor | GPUの2世代の照度・距離・配置状態、レイ作業領域、Compute更新、履歴とBarrierの管理 |
| OpticalBackend | 既存AS・材質・Hit Shaderを共有し、プローブのDispatchRaysを記録する |
| OpticalSystem | Rendererへ有効なDDGI描画値と更新回数を渡す。Editor用には有効状態だけを公開する |
| DDGI.hlsli | 八面体写像、照度と距離可視性の補間、受光位置補正 |
| DDGIProbe.hlsli | 既存の交差情報を使い、入射radianceと符号付き距離を書き出す |
| DDGIUpdate.hlsl | 照度・距離モーメント・時間フィルタ・分類・再配置・境界を更新する |
| PrimitiveRenderer / 共通PS | 手法の優先度と描画への接続。Raster／Mesh Shaderで同じ評価関数を使用する |
| EditorApplication | 設定・履歴リセット・状態表示。GraphicsへImGui依存を追加しない |

## データフロー

```text
RenderScene.ddgi + 抽出済みのGeometry / Material / Light
  → OpticalBackend（既存TLAS・材質record・closesthit）
  → DDGIProbeRaygen：N本 × P点
       ├ miss：HDRまたは遠方ambientのradiance
       ├ front hit：発光＋遮蔽付き直接光＋前フレーム照度による間接反射
       └ back hit：負距離、radiance=0
  → レイ作業テクスチャ
  → Compute：次の照度／距離モーメント／配置状態へ時間蓄積
  → Raster / Mesh Shader / 鏡・ガラスのDXR交差面
       └ 可視性付き8近傍補間 → 共通の材質応答 → 間接拡散
```

プローブの追跡は一交差と直接光用の遮蔽rayで構成する。
多重反射は同じフレーム内で多数の経路を追跡せず、前の照度フィールドを交差面で評価して次のフィールドへ伝播する。
照度生成と鏡・ガラスの画面描画は別raygenで、既存miss／closesthit／Shader Tableを共有する。
各raygen用CBは分離し、同じFrame sliceの上書きでプローブ追跡が新旧を取り違えないようにした。

## 照度と距離の計算

各点から球面Fibonacci列でサンプルする。既定では方向を固定し、時間方向にサンプル模様を動かさない。
任意で方位を回転し、履歴と合わせて積分する。画面pixelごとの乱数は使わない。

照度は法線方向ごとの八面体マップへ保存する。保存値は **E/π** で、球面一様PDF=1/(4π)に対し `4/N × Σ(L × max(NdotL,0))` を使う。
受光材質のBase Color・Metallic・積分Fresnel・AOは共通の`ApplyDiffuseIrradiance`で一度だけ適用する。
同じ関数をベイクGIでも使用し、π係数と材質評価の重複を防ぐ。
直接光には既存BRDFを使用し、DDGIのAOを直接光のShadowへ掛けない。

距離には方向ごとの重み付き平均`μ`と2次モーメント`m2`を保存する。
分散`max(m2 - μ², ε)`からChebyshev型の可視率を計算し、三乗と極小重みの抑制で壁越しの混入を減らす。
受光位置の周囲8点を、三線形重み・法線の向き・可視率で補間する。
有効な点がない領域内の結果は黒とし、遮蔽を無視した環境光で埋めない。

時間蓄積は指数移動平均。照明の大きい変化では履歴重みを下げて追従を速める。
新規点・再配置した点・履歴リセット直後は古い照度を混ぜない。
裏面率が高い点は無効にし、近い裏面または表面の距離から移動量を求める。
移動を最小格子間隔の0.45倍以内に制限し、移動した点は次の追跡で有効性を確かめるまで補間へ使わない。
面上にある格子点も検出するためプローブrayのTMinは0。交差面の遮蔽rayには既存の幾何法線offsetを適用する。

## GPU資源・Lifetime・性能

| 資源 | 配置 |
|---|---|
| 入射レイ | RGBA32_FLOAT、レイ数×点数。RGB=radiance、A=符号付き距離 |
| 照度履歴×2 | RGBA16_FLOAT、1点あたり8²＋1 texelの境界。RGB=E/π、A=空の可視率 |
| 距離履歴×2 | RG32_FLOAT、同じ方向別配置。距離の1次・2次モーメント |
| 配置状態×2 | RGBA32_FLOAT、点数×1。XYZ=格子点からのoffset、W=有効性 |

1点のタイルは10×10。XY格子を横へ並べ、Zを縦へ置く。
八面体の境界は接続先の方向を同じCompute Dispatch内で積分し、別の境界コピーpassを追加しない。
描画側はbilinearで照度と距離を参照する。照度と距離の解像度は現在どちらも8²に固定した。

2世代の履歴をping-pongし、追跡は旧世代を読み、Computeは次世代を書き、その後の描画は次世代を読む。
処理は既存Direct Queue内で順序付ける。async computeは使用しない。
レイUAV→Compute SRV、履歴SRV→UAV→全Shader SRVのBarrierを記録する。
未提出のContextは履歴番号・更新番号・資源状態を復元する。公開getterも復元後の履歴だけを返す。
Frame別の定数／材質recordは既存Fence完了済みsliceを使う。

資源とDescriptorはDDGIProcessorが所有する。SRV/UAV計14個と共通Sampler1個で、毎フレームは作り直さない。
配置・追跡条件変更時は論理的に履歴をリセットする。点数・レイ数変更時だけQueue完了後にテクスチャを再生成する。
 viewportのResizeはプローブ資源を再生成しない。ShutdownはOpticalBackendのGPU idle後に行う。
DDGIだけが有効な通常材質のシーンでは、旧pixel GIと光学描画の画面サイズ出力・DispatchRaysを追加しない。
鏡・ガラスが有効なら、その既存画面パスへ更新済みのフィールドを渡す。

上限は2048点、各256レイ。既定390点×128レイで49,920本／更新（直接光の遮蔽rayは別）。
最大設定の作業領域と履歴はおよそ15MiB、既定は約2MiB。AS・材質・CB・Allocatorの使用量は別。
全点を毎更新で処理するため、密度・レイ数・距離がGPU負荷へ影響する。
CPU↔GPUの照度転送やreadbackは通常描画に追加していない。

## 互換性と制約

- ラスタによる受光は維持するが、動的プローブ更新にはDXRが必要。DXR非対応設定ではDDGIを有効と報告しない。
- 剛体の移動、光源変更、発光面の影響に対応する。VSだけで変形するSkinned MeshはASに含めず、受光だけを行う。
- 拡散GIであり、透過GI、コースティクス、局所の粗い鏡面反射を解く機能ではない。透過物もプローブ追跡では不透明として扱う。
- 通常材質の鏡面IBLは空の可視率で遮蔽する近似。鏡・ガラスの専用DXR経路は維持する。
- 固定rayでちらつきを減らす一方、有限サンプルの偏りや小さな発光体の取りこぼしが残る。完全な無ノイズ・無光漏れを保証しない。
- 時間蓄積には収束待ちと照明変化への遅延がある。起動直後やリセット直後の明るさは最終収束値ではない。
- 薄い壁、粗い距離マップ、配置・bias次第では光漏れや暗い箇所が残る。再配置だけで不適切な体積を自動修正するものではない。
- 単一の固定格子。Cascade、scrolling、更新予算による分割、重要度サンプリング、プローブ可視化は未実装。
- Sponzaの本番フレーム時間、長時間のリーク計測、Editor UIの手動操作は未検証。ゲームの対象GPUに合わせた調整が必要。

## 代替案と今回の判断

ベイクは実行時負荷と画面の安定性に有利だが、静的な照明・遮蔽へ固定される。
pixel RTGIはプローブ補間の偏りを避けられるが、少数rayでは画面に粒状ノイズが出るため、今回は照度フィールドへ更新を集約する。
SHプローブの動的更新だけでは方向別可視性が不足するため、DDGIでは方向別照度と距離モーメントを採用した。
SDKの導入も候補だが、既存のAS／Shader／Descriptorをそのまま共有し、外部依存と広いインターフェース追加を避けるため基本算法を実装した。
照度／距離を同解像度にしたのは初期実装の所有・境界処理を簡潔にするため。必要なら距離だけ高解像度化する。

### SDFDDGIとの比較

[Hu et al., Signed Distance Fields Dynamic Diffuse Global Illumination (2020)](https://arxiv.org/html/2007.14394) は、解析的SDFプリミティブとクラスタによる交差判定、SDF勾配によるプローブ再配置、SDFによる受光点とプローブ間の可視性判定を提案している。DXRを必要としない点は、将来の非DXR環境への展開に有用である。ただし、任意のglTFメッシュをそのままSDFへ置き換えられるわけではなく、対応するシーン表現と材質参照を別途用意する必要がある。

今回の実装は既存メッシュのDXR交差判定と距離モーメントによる可視性判定を使う。SDFクラスタ、SDF勾配、Contact GI、画面縮小による可視性検査は実装していない。論文の光漏れや性能の評価を、この実装の保証として扱わない。

論文3.3節のサンプル再利用と同様に、照度更新は1プローブにつき8×8スレッドのグループを割り当て、入射レイと方向をgroupsharedに読み込む。境界を含む100 texelで共有することで、グローバルメモリの反復読み込みと球面方向の再計算を減らす。履歴フィードバックと移動時の履歴破棄も採用しているが、交差判定や再配置の数式はSDF方式とは異なる。

## 変更対象と検証

新規: DDGISettings.h、Backend/DirectX12/Lighting/DDGIProcessor.h/.cpp、Common/DDGI.hlsli、Lighting/DDGIUpdate.hlsl、RayTracing/DDGIProbe.hlsli、Tests/DDGITests.inl。
接続変更: RenderScene、PrimitiveDrawData、PrimitiveRenderer、OpticalSystem／Backend、PrimitiveDrawData.hlsli、共通PS／IndirectLighting／BakedLighting、Optical.hlsl、EditorApplication、プロジェクト登録と関連Docs。
PrimitiveDrawDataは4720byte、Optical定数は400byte。従来の材質・Vertexの配置は変更しない。

`NexusGraphicsTests.exe --ddgi` は、閉空間、床面上のプローブ、画面外発光、反射フィードバック、天井・光源変更、Freeze、履歴リセット、格子再生成、未提出記録、Mesh一致、Shadow共存、DXR非対応設定、鏡／ガラスの最終合成を検証する。
診断画像はGenerated/DDGI*.ppm、ログはGenerated/DDGITests.log。
最終の実行結果は完了報告と本資料末尾に記録する。

## 参考と独自設計

- [Majercik et al., Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields (2019)](https://jcgt.org/published/0008/02/01/): 照度フィールド、距離モーメント、可視性付き補間。
- [NVIDIA RTXGI DDGI Shader API](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/ShaderAPI.md): 追跡・照度／距離更新・分類・再配置の責務の参考。
- [Hu et al., Signed Distance Fields Dynamic Diffuse Global Illumination (2020)](https://arxiv.org/html/2007.14394): SDFを使う代替方式との比較、プローブ単位の共有メモリ利用と履歴管理の参考。

既存OpticalBackendの共有、同一Queueのping-pong、単一Computeでの境界生成、固定ray既定、既存PBRへ渡すE/πの規約はNexusEngine固有の設計判断。
次は更新の分割とGPU時間計測、距離解像度・再配置の改善、プローブ可視化、Skinned MeshのGPU変形AS対応を優先する。

## 検証記録（2026-10-16）

NVIDIA GeForce RTX 3060 Laptop GPUで、DX12 Debug LayerとGPU-Based Validationを有効にしたGraphicsTestsを実行した。テストのInfoQueue検査は警告・エラーを失敗として扱う。

| 検証 | 結果 |
|---|---|
| NexusEngine Debug/x64ビルド | 成功、警告0・エラー0 |
| NexusGraphicsTests Debug/x64ビルド | 成功 |
| NexusEcsTests | 終了コード0 |
| GraphicsTests 通常実行 | 終了コード0 |
| GraphicsTests --ddgi（共有メモリ最適化前） | 終了コード0 |
| GraphicsTests --ddgi（共有メモリ最適化後） | 終了コード0、全7ケース通過 |
| GraphicsTests --baked | 終了コード0 |
| GraphicsTests --ibl | 終了コード0 |
| GraphicsTests --fog | 終了コード0 |
| GraphicsTests --gi | 終了コード0 |
| DDGIUpdate cs_6_6 / Optical lib_6_6のDXCコンパイル | 成功 |

回帰ログはGenerated/DDGI*Regression.log、共有メモリ最適化後のDDGI再検証ログはGenerated/DDGIFinalTests.logに保存した。最適化前後の全診断ケースのRGB合計値は一致した。Generated/DDGIComparison.pngは閉空間・画面外の赤い発光面・赤い壁の多重反射を並べたDiffuse GI Onlyの確認画像。テストは小規模な診断シーンを含む機能検証であり、製品シーンでのフレーム時間や長時間稼働の保証ではない。
