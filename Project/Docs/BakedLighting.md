# ベイクGIとライトプローブ

この資料は、静的背景のライトマップと動く物体のライトプローブを使う事前計算GIを説明する。
現在のEditorデモの既定は[DDGI](DDGI.md)。ベイク方式はDDGIを無効にして選択する。
ベイクはCPUで光輸送を計算する編集操作。描画中はラスタライズと保存済みテクスチャの参照で完結し、GI用のレイ追跡・時間蓄積・デノイズは行わない。
鏡・ガラスやRT Shadowは独立機能として引き続きDXRを利用できる。GIの読み込み・描画自体にはDXRが不要。

## 使用方法

1. Shadowsパネルの **Baked GI / Light Probes** でサンプル数、反射回数、Chart Resolution、Probe Countsを指定する。
2. **Bake and Save (CPU)** を押す。計算中は編集を停止する同期処理で、大きなシーンでは時間がかかる。
3. 成功後は次の描画でキャッシュをロードし、`Loaded: ... lightmaps / ... probes` を表示する。
4. Intensity、Enable、**Diffuse GI Only** / **Indirect Lighting Only** / **Final Lighting** で比較する。
5. 静的配置・材質・照明を変更したら再ベイクする。既存ファイルの差し替えには **Reload Baked GI** を使う。

Chart Resolution=4はメモリを抑えたプレビュー品質。大きい三角形の継ぎ目や照度の変化が目立つ場合は8／16と十分なサンプル数を使用する。
Inspectorの **Static Lighting (rebake required)** を無効にすると、その物体は次のベイクの遮蔽物から除外され、プローブで受光する。

デモの保存先は作業ディレクトリからの相対パス `Generated/Lighting/DemoLevel.nxgi`。
初回はこのファイルが存在しないため、まだベイクGIは動作せず、既存のIBL／一定環境光に戻る。明るい画面だけをGIの動作証拠にはしない。
ベイク方式の選択中はキャッシュの有無にかかわらずRTGIを起動しない。旧RTGIの比較にはBaked GIを無効にしてからRTGIを有効にする。
ノイズを避けるため、ゲーム配布時は作成済みキャッシュも配布し、起動後の自動ベイクは行わない。

Runtime側の例:

```cpp
renderScene.bakedLighting.enabled = true;
renderScene.bakedLighting.cachePath = "Content/Lighting/Room.nxgi";
renderScene.globalIllumination.enabled = false;
// 動く物体は静的遮蔽物に含めず、受光にはプローブを使う。
primitiveComponent.staticLighting = false;
```

ComponentのstaticLightingはLevel形式v4で保存する。v1～v3は従来どおり読み込み、静的扱いを既定とする。
永続Entity IDから描画用lightingIdを作り、Levelを再ロードしてECSの世代番号が変わっても照合する。
GraphicsだけでRenderSceneを組み立てる場合は安定したlightingIdを指定する。0の場合はsourceObjectを代用する。

## 責務と依存関係

| 要素 | 責務 |
|---|---|
| BakedLightingSettings / RenderScene | パス、品質、強度、プローブ配置の値snapshot。GPU資源を持たない |
| LightmapGeometry | 材質UVを保持したまま独立したライトマップUVとGPU用頂点を作る |
| LightBaker | CPUの静的BVH、材質評価、複数回の拡散反射、照度アトラスとSHプローブの生成 |
| BakedLightingAsset / Load・Save | CPU成果物、形式・寸法・checksum検証、ファイル保存 |
| BakedLighting | キャッシュの照合・同期転送・GPUテクスチャとDescriptorの所有 |
| PrimitiveRenderer | 既存描画への接続とインスタンスごとの照合。ベイク算法は持たない |
| BakedLighting.hlsli | ライトマップ／プローブの評価と受光材質への適用。描画方式に依存しない |
| EditorApplication | 明示ベイク操作と設定UI。GraphicsへImGui依存を追加しない |

```text
CPU ModelAsset + ModelInstance + RenderScene
    → 明示的なLightBaker::Bake
        ├ 静的三角形 → BVH → 複数回の拡散光輸送
        ├ 三角形ごとの照度 → 余白付きライトマップ
        └ 規則格子 → 9係数SH + 有効性 + 遮蔽距離
    → SaveBakedLighting → .nxgi

RenderSceneのパス／revision変更
    → LoadBakedLighting → Scene署名照合 → 共有FactoryでGPU転送
    → Raster / Mesh共通PS、DXRで交差した通常材質
        ├ 直接光と影: 既存経路
        ├ 間接拡散: ベイク照度 × 受光材質（既存ambientを置換）
        ├ 鏡面: 既存IBL × 空の可視率近似
        └ Emissive: 既存の線形HDR加算 → Bloom / Tone Mapping
```

SurfaceData、BRDF、DirectLighting、IndirectLightingの分離は[PBR / IBL設計](IBL.md)のまま。
GIは新しい間接拡散の供給元になり、Directional LightやShadow方式をBRDFへ持ち込まない。
通常材質のIBLとDXRの理想反射・透過は従来のローブ重みで分配し、二重加算しない。
DXR交差面は専用のアトラスUVを要求せず共通プローブを参照する。

## ベイクと照度の規約

静的インスタンスだけをWorld空間の三角形へ展開し、不変BVHを構築する。
材質画像は既存WIC decoderを再利用してベイク前に一度だけ読み込み、sRGBのBase Color／Emissiveは線形化する。
SamplerのRepeat／Mirror／ClampとNearest／Linearを反映する。
受光面から余弦重点サンプルを飛ばし、交差した面の遮蔽付きDirectional Lightと発光、および次の拡散反射を積算する。
missだけがHDR環境を受け取る。HDR未指定時はambientIrradiance/πを遠方radianceとして扱う。
各反射のサンプルには別々のHalton次元を使い、同じ乱数対の反復を避ける。

ライトマップに保存するRGBは **E/π**。余弦PDFとLambertの1/πが相殺された平均入射光を保存する。
受光側のBase Color、金属度、積分Fresnel、AO、GI強度は描画時に一度だけ掛ける。
二次面の拡散輸送は非金属F0=0.04のエネルギー配分で近似し、金属・理想鏡・透過へ割り当てた成分を拡散として再放射しない。
鏡面輸送、コースティクス、ガラス越しのGIは今回計算しない。ベイクの交差判定では透過物も不透明な遮蔽物になる。
真の無限反射解ではなく、指定回数で打ち切った拡散GIである。

モデルには専用UVが無かったため、既存材質UVと独立した三角形chartを生成する方式を採用した。
各chartの中央半分を三角形に使い、外周1/4を端の複製で埋める。辺上のベイク点は僅かに内側へ戻し、隣接面のt=0判定から外部光が漏れることを防ぐ。
ノード・メッシュ・インスタンスごとにアトラス領域を割り当て、同じGPUメッシュのインスタンスでも別々に受光できる。
GPUへ渡すコピーだけを展開し、Scene所有のCPUモデルと材質UVは変更しない。
ModelVertexは104bytes（接線offset80、追加UV offset96）、PrimitiveDrawDataは4720bytes。

## ライトプローブ

規則格子の各点で球全体の入射radianceを積分し、2次までの実SH、9係数へ射影する。
E/πの余弦畳み込み係数はバンド順に1、2/3、1/4。描画時に法線で評価するため、プローブ受光ではNormal Mappingも反映する。
位置の8近傍を三線形補間し、SHの負の値は0へ制限する。
各点の6面8×8距離と裏面率もベイクし、壁に埋まった点や受光点までの間に遮蔽物がある点を除外する。
距離参照は近傍4点の最小値で保守的に判定する。角付近を見逃さないよう、有効性は軸6本だけでなく距離用全方向で判定する。
グリッド内に有効な点がない場合は黒とする。未遮蔽の環境光へ戻すと壁越しに明るくなるためである。
グリッド外は既存環境光へ戻る。制作時は動く物体の活動領域をグリッド内へ含める。

静的と指定された物体も、描画時のWorld行列が保存値と異なればプローブへ切り替える。
スキニング物体はベイクの遮蔽物から除外し、常にプローブで受光する。
動的物体の移動によって、既に保存した背景照度や遮蔽が更新されるわけではない。
ライトマップは非指向性の照度なので、受光側のNormal Mappingによる細かい間接光変化を復元できない。

## GPU資源とLifetime

| 資源 | 形式・配置 |
|---|---|
| ライトマップ | RGBA32_FLOAT、最大4096²、RGB=E/π、A=空の可視率 |
| プローブ係数 | RGBA32_FLOAT、10×点数、9係数＋有効性1要素 |
| 遮蔽距離 | RGBA32_FLOAT、96×点数、6面8²距離を4要素ずつ格納 |
| Sampler | 線形・Clamp、1個を共用 |

DescriptorはBakedLightingがSRV3個とSampler1個だけ所有する。材質ごとの複製は作らない。
画像転送・Barrierは既存TextureUploaderを利用し、COPY_DESTからALL_SHADER_RESOURCEへ遷移する。
キャッシュの読み込み・署名計算・転送はパスまたはrevision変更時だけ。失敗した要求も記録し毎フレーム再試行しない。
新しいテクスチャを転送し終わるまで旧資源を保持し、共有Queueの完了を確認してから置換する。
Descriptorは既存FactoryのFence retire経路へ返す。Resizeはキャッシュ資源に影響しない。
ShutdownはGraphicsSystemのGPU idle後。ロード後はCPU側の照度・係数配列を解放し、照合メタデータだけを保持する。
描画時のBinding照合は読み込み時に作成したhash表を使い、全ライトマップを毎Draw検索しない。

キャッシュはマジック／バージョン、期待ファイル長、寸法上限、有限値、メタデータ込みchecksumを検証する。
モデル、材質画像／Sampler、静的配置、照明、HDR内容を署名で照合する。
保存は一時ファイルを最後まで書いた後に置換し、失敗した読み込み・再読み込みでは直前の有効なGPU成果物を保持する。
現形式はWindows/x64の固定サイズ構造とlittle endianを前提とする。Import時Bakeへ移す際は形式を版上げする。

## 方式の比較と選択理由

| 代替案 | 判断 |
|---|---|
| 毎フレームのRTGI | 動く照明に対応できるが、少数サンプルの粒状ノイズとデノイズ・履歴管理が必要。今回はゲーム画面の安定性を優先 |
| SSGI | 画面外からの光を失い、カメラ移動で変化する。静的な室内GIの基本解には選ばない |
| ライトマップだけ | 背景は安定するが動く物体が受光できないため、プローブと併用 |
| プローブだけ | 頂点UVを追加しなくてよいが、壁付近の補間と薄い遮蔽物の漏れが増える。背景にはライトマップを使用 |
| GPUベイク | 大規模シーン向き。最初の実装ではDXR非対応PCでも作成・使用でき、既存Assetを直接使えるCPU BVHを採用 |
| 外部unwrapライブラリ | 面積に応じた効率の良いアトラスに有利。新規依存を増やさずまず独立chartで動作を成立させた |

BVHとサンプル列はベイク時だけ使用し、最大8 CPUワーカーで点／chartを並列処理する。
実行時の静的背景はライトマップ1サンプル。動的物体は8点のSHと遮蔽距離の参照になる。
RGBA32_FLOATを選んだのはSHの負の係数とHDRを既存2D upload経路で保持するため。
最大アトラス単体で256MiBとなるため、本番の面積ベースunwrap、圧縮・half化、ストリーミングは次の最適化対象。
頂点共有を失う自動chartもメモリと頂点処理を増やす。現段階で本番性能を保証するものではない。

## 制約と次フェーズ

- 低サンプルのベイクには固定したムラが残り得る。毎フレームの粒状ノイズは無いが、成果物の画質確認と十分な品質設定は必要。
- 均等な三角形chartは面積あたり解像度が不均一。低密度の大きな床や細かい影には解像度不足がある。
- 単一アトラス／単一規則格子。最大4096プローブ。疎な配置、領域別の再ベイク、プローブ可視化、手動再配置は未実装。
- 遮蔽距離と裏面率は近似。薄い壁、細い穴、複雑な内外判定では光漏れや暗い領域を完全には防げない。
- 静的照明の変更後は明示再ベイクが必要。ベイク品質や配置設定の変更も再ベイクし、単なるReloadでは成果物は変わらない。
- 動的遮蔽、動的光源からの間接光、動く発光体、金属の多重反射、透過GI、指向性ライトマップは未実装。
- 環境鏡面への遮蔽は空の余弦重み付き可視率による近似。粗さ別の局所反射やReflection Probeではない。
- 大規模Sponza全体の高品質ベイク所要時間・GPUフレーム時間・制作上の最終画質は未測定。

次は面積ベースのUV展開とdenoise、非同期／領域別ベイク、プローブ配置・デバッグ可視化、GPUベイクを優先する。
Reflection Probeは共通IndirectLightingへ別の鏡面供給元を追加し、GI／BRDF本体を作り直さない。
将来の動的GIも今回の間接拡散入力を置き換え、直接光・Shadow・材質評価を分離したまま接続する。

## 参考と独自設計

- [Ramamoorthi / Hanrahan: An Efficient Representation for Irradiance Environment Maps](https://www.cs.columbia.edu/cg/pdfs/42_lighting.pdf): SHによる低周波照度表現。
- [PBRT: Working with Radiometric Integrals](https://www.pbr-book.org/3ed-2018/Color_and_Radiometry/Working_with_Radiometric_Integrals): 放射量と積分の規約。
- [Unity: Lightmap generation process](https://docs.unity.com/en-us/engine/6000.7/manual/lighting-overview/direct-and-indirect-lighting/lightmapping/lighting-gi-uvs/lightmap-generation-process): 独立UVとchart余白の背景。

CPU BVH、三角形chartの具体的配置、96幅への距離pack、既存Bindlessへの接続、失敗時の旧成果物保持はNexusEngine固有の判断。

## 変更対象と検証

新規実装はGraphics/LightingのBakedLightingData、LightmapGeometry、LightBaker、BakedLightingとキャッシュIO。
接続変更はRenderScene、PrimitiveRenderer、PrimitiveDrawData、ModelVertex、MeshletBuilder、TextureResource／Uploader／Format変換、GraphicsResourceFactory、OpticalSystem／Backend、Raster／Mesh／Optical Shader。
Scene側はPrimitiveRenderComponent、RenderSceneExtractor、LevelSerializer。Editor側は設定UI。プロジェクト登録と既存資料のサイズ・既定動作も更新する。

`NexusGraphicsTests.exe --baked` はCPUの閉空間遮蔽・色移り・独立UV・保存／破損検証に加え、GPUでライトマップ、プローブ、フレーム間一致、Mesh一致、Shadow共存、DXR非対応設定、再ロード、失敗時保持、古い署名の拒否を検証する。
鏡／ガラスを含む描画も同じテスト経路で確認する。画像はGenerated/BakedGI*Before.ppm、After.ppmへ出力する。
実施結果は作業完了報告を参照。フレーム間一致は固定シーンの確認で、全ゲームの無アーティファクト保証とは区別する。
`--baked-quality` は同じ室内をChart Resolution=16でも検証する。プレビュー解像度と画質設定を混同しない。
閲覧用の比較画像は[Generated/BakedGIComparison.png](../../Generated/BakedGIComparison.png)。診断PPMをsRGB表示へ変換したもので、実ゲームの高解像度最終画質とは区別する。

2026-10-16の検証環境はWindows/x64、RTX 3060 Laptop（DXR 1.1、Mesh Shader Tier 1）。
本体Debugビルドは警告0・エラー0。Graphics／ECSテストのビルドとECS実行も成功。
GPU Validationを有効にした`--baked`、既存全Graphics、`--ibl`、`--fog`、比較用の`--gi`はすべて終了コード0。
追加の`--baked-quality`も終了コード0。Chart Resolution=16で同じUV生成規則・プローブ・再読み込みが成立することを確認した。
閉じた未照明室のライトマップはRGB=0、画面外の赤い発光面ではRのみが増え、白い発光面＋赤壁では追加反射によるRの増加を確認した。
プローブ受光もフレーム間一致を確認し、DXR非対応設定でもベイク済みGIを利用できた。
意図的な欠損／古いキャッシュと既存AS失敗テストのError、非対応設定のfallback Warningは想定したログ。
Editor UIの手動操作、Sponza全体の高品質ベイク、GPUフレーム時間、長時間のリーク計測は未実施。
