# PBR / IBL 実装設計

後続実装として[拡散RTGI](GlobalIllumination.md)を追加しています。GI有効時は本書の未遮蔽の拡散IBLを置換し、鏡面環境の遮蔽も評価します。

## 調査結果と変更方針

対象は feature/pbr。開始時の作業ツリーはclean。
GraphicsSystemはDevice、Queue、Frame fence、Descriptor、HDR target、PostProcessを所有し、GraphicsContextを通じてRendererを呼ぶ。
Scene → RenderScene（値snapshot）→ PrimitiveRenderer → GraphicsResourceFactory → DX12 Resourceという依存方向を維持する。
RenderExtensionは外部描画用であり、IBLのために拡張しない。ShaderCompiler/Reflectorは初期化時のみ使用し、既存Pipeline layoutとCBサイズ検証を維持する。
DescriptorAllocatorはslot、BindlessDescriptorTableはgeneration/fence retireを管理し、TextureResourceは単一MipのRGBA8画像を管理する。

現状の重複はRaster/DXRのMR・法線復元規則と直接光/ambient合成。PBR.hlsliはBRDFとNdotLによる寄与を一関数に混在している。
SurfaceData、Material評価、BRDF、直接光、間接光のHLSLを分け、stage固有の接線基底生成だけは各入口に残す。
Mesh Shaderは既存通りRaster PSを共有。OpticalMaterialの鏡・屈折は通常材質のIBLとは独立した経路重みを維持する。

変更対象はCommon HLSL、Primitive/DXR shaderと転送構造、Model/glTF、ResourceFactory、PrimitiveRenderer、Editor、VS project、Tests、Docs。
新規CPU概念はEnvironmentLightingData（設定）、HdrImage（CPU decode結果）、EnvironmentLighting（GPU所有のAPI境界）、EnvironmentMapProcessor（DX12 compute前処理）。
既存TextureResourceを多用途化せず、環境用array/mip/UAVだけをBackend内に閉じ込める。

Lambertを標準とし、Half-Lambertは明示的な互換設定にするため既存sceneの横向き面は暗くなる。環境未指定時は既存ambientをfallbackとして維持し、IBL指定時は置き換える。
AOは間接光のみ、Emissiveは線形HDRで加算する。Shadow/Optics/PostProcessのパス順序は維持する。

## Lifetime 方針

EnvironmentLightingがprocessorと生成された全texture/viewを一意所有する。Factoryから非所有Device/Queue/Tableを接続する。
初回ロード・reloadのみ専用Direct command listで前処理しFenceを待つ。旧環境はQueue idle確認後に置換する。通常frameの同期や前処理は増やさない。
失敗した提出のstaging/list/resourceもownerに保持し、idle後のShutdownまで破棄しない。
Resizeで環境textureは作り直さない。BRDF LUTはprocessorにつき一個、環境reloadでも再利用する。
Sceneはpath、enable、intensity、yaw、reload revisionのみを保持し、Descriptorを保持しない。

## 数式と参考資料

LambertはbaseColor / π、directのNdotLはLighting側で一度だけ乗算。GGX/height-correlated Smith/Schlickは描画方式に依存しない。
Diffuse cubeはcosine-weighted samplingの平均 × π、すなわちirradiance Eを格納し、照明評価でのみ1/πを掛ける。
SpecularはGGX importance samplingで生成し、PDFからsample solid angleを計算して入力cubeのMipを選ぶ。LUTは同じSmith visibilityを積分するsplit-sumのA/B係数。

理論の参考: [Filament](https://google.github.io/filament/Filament.html)、材質の規則: [glTF 2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)、同期: [Microsoft resource barriers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)。
これらの資料は理論・APIの根拠。クラス配置、同期ロード、fallback、互換モードは本エンジンに合わせた設計判断。

## PBRデータフロー

```text
glTF → ModelMaterialAssetData → GPU texture upload + ModelMaterial
                                  ↓
RenderScene → PrimitiveRenderer → Raster / Mesh PS / DXR closest hit
                                  ↓ texel + stage固有の接線基底
                             Material.hlsli
                                  ↓
                             SurfaceData
                                  ↓
                     PBR.hlsli (合成・表示切替)
                     ├ DirectLighting → BRDF
                     ├ IndirectLighting → IBL / fallback
                     └ Emissive
                                  ↓
                  線形HDR → Fog → Bloom → Exposure → Tone Mapping → sRGB
```

SurfaceDataはbaseColor、metallic、roughness、world normal、AO、emissiveのみを持つ。
テクスチャ、UV、光源、影、パイプラインを知らないため、将来別の材質入力やGI評価を追加してもBRDFを書き換えない。
Material.hlsliはglTFのG=roughness/B=metallic、AOのRとstrength、normal scaleの規則を共有する。
Rasterは暗黙微分によるSample、DXRはSampleLevel(0)を使う。接線がなければRasterは微分、DXRは三角形の辺から基底を求める。
通常の2Dテクスチャはupload時に1×1までのmip chainをCPU生成し、SRVから全段を公開する。
RasterのLODは画面上のUV微分から自動選択する。sRGBのRGBは線形化して面積平均し、alphaとdata/HDRは線形で平均する。
SamplerDescの線形filterは既定で16倍の異方性filterを使い、maxAnisotropy=1でtrilinearになる。Nearestはpointのまま。
minLod=0、maxLod=最大float、mipLodBias=0が既定。TextureDesc::generateMipmaps=falseで単一mipにできる。
DXRの二次rayは引き続き明示LOD 0であり、ray cone/differentialによるLOD推定は未対応。
stage制約が異なるため、この基底生成まで無理に同一関数へ押し込まない。従来どおりMikkTSpace生成は未対応。

BRDF.hlsliのGGX D、Smith visibility、Schlick F、Lambert、SpecularBRDFはいずれも光量や遮蔽を含まない。
DirectLightingだけがNdotL、incident radiance、visibilityを掛ける。影は0～1の値でありRT/Shadow Mapの実装を知らない。
Lambert標準への変更で旧sceneの側面は暗くなる。`legacyHalfLambert`を有効にすれば旧拡散の角度依存を比較できる。
metallicによる拡散抑制、非金属F0=.04、roughnessの下限.05は維持する。

Emissive画像はsRGB、factorは線形。`KHR_materials_emissive_strength`もfactorに反映し、1を超える放射をHDR targetへ加算する。
Occlusion画像はLinearのRを使い、`lerp(1, R, strength)`を間接光だけに適用する。
Specular AOは` saturate(pow(NdotV + AO, exp2(-16*roughness-1)) - 1 + AO) `という近似を採用した。
これは幾何的な遮蔽の厳密解ではない。GI/Probeからの遮蔽を導入する場合はIndirectLighting内で置き換えられる。

## IBL生成と評価

| 生成物 | 寸法 / format | 内容 |
|---|---|---|
| 入力panorama | 最大16384×8192、総画素数33554432以下 / RGBA32_FLOAT | CPU RGBEを線形floatに展開してupload。Bake後に破棄 |
| radiance cube | 256×256×6、9 Mips / RGBA16_FLOAT | equirectangular変換、通常Mip chain。DXR missにも使用 |
| irradiance cube | 32×32×6 / RGBA16_FLOAT | 512 samples/texelのcosine-weighted積分によるE |
| prefilter cube | 256×256×6、9 Mips / RGBA16_FLOAT | 各Mipはroughness=mip/8。512 GGX samples/texel |
| BRDF LUT | 128×128 / RG16_FLOAT | 1024 samples/texel、NdotV/roughnessからA/B係数 |

入力はRadiance `FORMAT=32-bit_rle_rgbe`、`-Y height +X width`の`.hdr`に対応する。
非圧縮RGBEとscanline RLEを受け付け、旧run marker形式、XYZE、EXR、別orientationはエラーにする。
画素を線形Rec.709/D65として扱う。ICC変換やHDRヘッダーのEXPOSURE/COLORCORR適用は行わないため、事前に目的の線形RGBへ変換した素材を使用する。
RGBEの絶対校正は入力素材に依存する。入力RGBとirradianceは半精度の上限65504へ制限する。
内部HDRとHDRディスプレイ出力は別であり、出力は従来どおりSDRである。

座標はworld Y up、D3D cube face順序+X,-X,+Y,-Y,+Z,-Z。
panoramaのu=.5は+X、u=.75は+Z、上端は+Y。横はwrap、縦はclampする。
正のyawは環境を+Y軸の周囲に回す。lookupで方向へ逆回転を適用するので、回転のための再Bakeは不要。
Intensityもlookupで線形乗算する。既存のExposure EVは照明合成後に作用するため、入力HDRへ事前に露出を焼き込まない。

通常Mipは前Mipの4 texel相当を平均し、Cube samplingで面境界をまたぐ。
DiffuseはHammersley系列をcosine PDFで半球へ分布する。`E = π * average(L)`を保存し、評価は`E * baseColor / π`。
metallicとsplit-sum Fresnelによる拡散エネルギー抑制、AOをこの評価時に掛ける。

PrefilterはV=N=Rを仮定し、GGXでhalf vector Hを選びLを反射方向から求める。
`pdf = D(NdotH) * NdotH / (4 * VdotH)`、sampleの立体角は`1 / (sampleCount * pdf)`。
入力texelの平均立体角`4π / (6 * resolution²)`との比のlog2/2を入力LODに使い、明るい小領域のaliasingを抑える。
出力はNdotLで重み付けして正規化する。Mip0だけは元環境の鋭い反射を直接コピーする。

LUTは同じheight-correlated Smithを使い、GGX PDFと反射変数変換を含めてSchlick Fresnelの定数項AとBを積分する。
実行時は`prefilter(reflect(-V,N), roughness*8) * (F0*A+B)`。
この積分済み係数にdirect用BRDFやNdotLを再乗算しない。roughness方向と視線角方向を持つLUTは材質・環境画像に依存しないためreloadで再利用する。
単一散乱split-sumであり、GGXの多重散乱補償や完全な白色炉エネルギー保存は今回の範囲外。

## 所有権・同期・失敗時の動作

| 所有者 | 所有物 / 寿命 |
|---|---|
| Scene / Application | path、intensity、yaw、enabled、revision、表示設定。GPU Handleなし |
| PrimitiveRenderer | EnvironmentLighting、通常材質texture、frame別draw CB |
| EnvironmentLighting | processorのPimpl、最後に試したpath/revision。失敗要求も記録し毎frameの再試行を防ぐ |
| EnvironmentMapProcessor | active/pending cube、共通LUT、sampler、SRV/UAV、PSO/root、uploadと専用list |
| GraphicsSystem | Device/Queue/Table。すべてのRendererより長生きし、Shutdown前にGPU idleを保証 |

Factoryは既存のDevice/Queue/Bindless接続点だけを提供するfriend境界。Native型はBackend cpp/headerに閉じる。
HDR decodeはResource/HdrImage、computeはBackend/DirectX12/Environmentに配置し、既存RGBA8専用TextureResourceへ環境固有処理を追加しない。
余分なEnvironmentMapクラスや仮想Managerは作らず、processor内の小さなMap所有構造で十分と判断した。

初回Prepareは描画への環境参照記録より前に呼ぶ。変更がなければpath/revision比較だけで終わる。
reloadは同じDirect Queueをidleにしてから専用listを実行し、完了確認後だけactiveを交換する。
MipごとのUAV書き込みは対象subresourceのみSRVへ遷移し、Downsampleは前Mip一つだけのSRVを使ってUAV範囲との重複を避ける。
Irradiance/Prefilter/LUTは書き込み後にALL_SHADER_RESOURCEへ遷移するため、RasterとDXRの両方が読める。
一時viewはFence完了後、旧cube viewはidle確認後、既存TableのRetire/CollectGarbageで返す。

decode・allocation・compile失敗では前のactiveを維持し、revisionを増やすまで同じ要求を再試行しない。
Execute/Signal/Wait失敗ではpending、upload、list、allocatorを保持してprocessorを使用停止し、idle後のShutdownまで破棄しない。
GPU完了を確認できない異常時にframeごとに再提出しない。
Resizeは環境解像度と無関係なので再生成しない。enabled=falseや空pathでは既存ambient fallbackを使い、生成済み環境は次回有効化に備え最大一世代保持する。
Levelへの環境設定シリアライズは追加していない。Editor設定は既存ambient設定同様、その実行中だけ保持する。

## DXRとの合成

通常PBR材質の環境反射はIBLのGGX反射。DXR有効時の滑らかな通常金属には、[Lighting](Lighting.md)に記載した1回の局所反射を使用する。粗さ0.2～0.5でIBLへ移行し、0.5以上はprefiltered IBLを使用する。両者の重みの合計を1とし、二重加算しない。OpticalMaterialの鏡・ガラスは既存の理想反射/屈折経路を使う。
交差面の通常PBR寄与は`(1-reflectivity)*(1-transmission)`で重み付けし、残りを既存の反射・屈折へ分配する。
したがって同じoptical経路のエネルギーをIBLとDXRで二重加算しない。
Emissiveはこの通常ローブの重みとは独立して加算する。DXR missは有効な環境cubeを参照し、未指定時は一定環境照度からL=E/πを使用する。
最終optical pixelは従来どおりRasterの結果を置換する。診断モードではOptical passを省略し、一次面の同一SurfaceDataを可視化する。
DXRのVS-only skinning制限は従来どおり。通常金属の局所反射はGGXをサンプリングし、深度・法線・材質境界を保つ空間デノイズを使用する。反射先からの追加の光学経路と時間蓄積には対応していない。

## 代替案と採用理由・性能

- CPUで全積分: 実装は容易だが大量のtexel samplingがCPUを占有する。今回はComputeを選び、CPUはdecode/uploadのみとした。
- 毎frame compute: 動的空向きだが静的HDRには不要。今回path/revision変更時だけ処理する。
- 非同期Bake+世代ごとのFence所有: streamingに適するが、現状の同期import基盤には複雑すぎる。同期reloadを選び、streamingは将来別途設計する。
- SH diffuse: より少ないメモリで評価できるが、cubeと同じ方向規約・デバッグ手法で検証できるirradiance cubeを初期実装に採用した。
- 各材質へLUT/環境を保持: 重複が大きいため不採用。rendererの環境インスタンスごとに一つだけ生成する。
- TextureResource全体のarray/mip/UAV化: 今回の用途に不要な既存API変更を避け、Backend内だけで実装する。

通常frameで増える主な費用はirradiance cube 1回、prefilter cube 1回、LUT 1回のsampling。
通常の3D描画pass数は増えず、GPU→CPU転送も通常運用では増えない。viewやSamplerは毎frame生成しない。
cube二つの全Mipは約8MiB、irradiance約48KiB、LUT約64KiB（配置alignmentを除く）。reload時には旧/新世代と入力float画像の一時メモリが必要。
材質デバッグは照明samplingを省略し、Direct OnlyではIBLを省略する。通常材質の空マップは既存同様uniform branchで除外する。
CPU/GPU時間の実測ベンチマーク、HDRの高輝度極小光源に対するサンプル数最適化は未実施。

## 使用方法・比較・検証

EditorのShadows → Environment / PBRで`.hdr`パスを入力し、Load / Reload HDRを押す。
IBL Enabled、IBL Intensity、Environment Rotationを調整できる。Legacy Half-Lambertは旧拡散との比較用。
Lighting ViewはFinal、Direct、Indirect、Diffuse IBL、Specular IBL、Base Color、Metallic、Roughness、Normal、AO、Emissiveに加え、GI実装後はDiffuse GI Onlyも利用できます。
材質表示もHDRポスト処理を通るため、数値比較ではExposure=0、Bloomなし、色調整を中立にする。

```cpp
scene.environment.imagePath = L"Resources/Assets/Environment/studio.hdr";
scene.environment.intensity = 1.0f;
scene.environment.rotation = 0.5f; // radians
++scene.environment.revision; // 同じファイルを再ロードする場合
```

テスト（repository rootで実行）:

```powershell
Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe --ibl
Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe
Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe --fog
Generated/Outputs/EcsTests/NexusEcsTests.exe
```

`--ibl`はネットワーク不要のRGBE raw/RLE画像とglTF/BMP材質をGeneratedへ生成する。
GPU読み戻しで一定radiance保持、E/π、LUT範囲、Lambert/互換モード、AO/Emissive、全prefilter Mip、yaw/intensityを検証する。
さらに通常Raster/Meshの一致、AO/Emissiveテクスチャの色空間、光源なし描画、診断11モード、同一HDR reload、Sponzaの鏡・ガラスを検証する。
metallic/roughness/normal mapを変えた実描画の差と、IBLのみの照明がShadow Map/RT Shadowで暗くならないことも検証する。
同一設定で再Bakeされないこと、LUT再利用、繰り返しreloadのDescriptor indexの有界性、失敗reloadがactiveを維持することも検証する。
GPU Validationのwarning/errorは失敗扱い。全テストの異常系による意図的なASエラーログは既存どおりである。

`Generated/IBLRasterBefore.ppm` / `After.ppm`、`IBLSponzaBefore.ppm` / `After.ppm`は同一camera・直接光0でのIBL無効/有効比較。
これは旧commitの撮影ではなく機能のon/off比較。PPMは線形HDR読み戻しを0～1へクランプした診断出力で、tone mapping済みスクリーンショットではない。
`Generated/IBLTests.log`、`IBLRegressionFinal.log`、`IBLFogTests.log`、`IBLEcsTests.log`が実行結果。

## 制約と次の拡張点

今回の環境はグローバル一個、固定解像度、静的HDR。skybox、Local Probe、LTC、ClearCoat、Anisotropy、SSS、Volumetric lightingは追加していない。拡散GIは後続のGlobalIllumination.mdに記載する。
実景HDRのアート品質評価、長時間のVRAM/COM leak profiling、TDR/DeviceLostの実機故障注入、別GPUでの性能比較は未実施。
DXRの二次rayの材質マップは従来どおりLOD0で、ray coneなどによる適切なLOD選択は今後の課題。
AO/Emissiveの追加UV/Texture Transformは既存MR/Normalと同じエラー規則。材質間の同一画像dedupは既存設計のままで、global asset cacheは作っていない。

次にimport時のBake/cache、非同期reloadとfence単位の世代回収を導入すると、起動/reload待ちを減らせる。
Reflection ProbeはEnvironment lookupの位置依存選択・blend・parallax補正をIndirectLightingへ追加し、SurfaceData/BRDFは再利用する。
DDGI/RTGIはdiffuse irradianceの供給元、RT reflectionはspecular radianceの供給元として追加し、同じ寄与を重複加算しないよう既存IBLとの置換・blendを定義する。
Area Light/LTCはDirectLightingの追加評価器、ClearCoat等はSurfaceDataと対応ローブを拡張する。未使用interfaceは先に用意しない。

## 実装・検証報告（2026-10-16）

| 変更ファイル（Project/基準） | 変更内容 |
|---|---|
| Shaders/Common/SurfaceData.hlsli、BRDF.hlsli、Material.hlsli、DirectLighting.hlsli、IndirectLighting.hlsli、PBR.hlsli | stage非依存の材質規則、BRDF、照明、合成・debug |
| Shaders/Primitive/Primitive.PS.hlsl、PrimitiveDrawData.hlsli | Emissive/AO/環境の解決と共通照明の呼出し。Mesh PSは既存include経路で利用 |
| Shaders/RayTracing/Optical.hlsl | 同じ照明関数、GI追加後は100byte payload、144byte材質record、環境miss |
| Shaders/Environment/EnvironmentMap.CS.hlsl | 変換・通常Mip・irradiance・GGX prefilter・LUTの5 entry |
| Graphics/Environment/EnvironmentLightingData.h、EnvironmentLighting.h/.cpp | 設定snapshot、変更検出、GPU所有のAPI境界 |
| Graphics/Backend/DirectX12/Environment/EnvironmentMapProcessor.h/.cpp | Computeと環境GPU資源・viewの寿命 |
| Graphics/Resource/HdrImage.h/.cpp | GPUと独立したRGBE raw/RLE decode |
| Graphics/Model/ModelAssetData.h、ModelResource.h、GltfModelLoader.cpp | Emissive/AOのCPU材質・glTF import |
| Graphics/Material/PrimitiveDrawData.h | HLSLと一致する4720byte転送値 |
| Graphics/Renderer/GraphicsResourceFactory.h/.cpp | 環境の内部Backend接続、材質係数検証と転送 |
| Graphics/Renderer/PrimitiveRenderer.h/.cpp、RenderScene.h | 環境所有・設定・材質upload・描画統合・失敗時descriptor回収 |
| Graphics/RayTracing/OpticalSystem.h/.cpp、Graphics/Backend/DirectX12/RayTracing/OpticalBackend.h/.cpp | 環境転送、Emissive/AO record、CB・payload契約更新 |
| Editor/Core/EditorApplication.h/.cpp | HDR path/reload、enable/intensity/yaw、互換モード、11表示 |
| Tests/GraphicsShadowTests.cpp、EnvironmentLightingTests.inl、EnvironmentLightingTest.hlsl | 数値・材質import・GPU・複数描画経路・reload検証、比較PPM |
| NexusEngine.vcxproj、NexusEngine.vcxproj.filters、Tests/NexusGraphicsTests.vcxproj | 新規source/header/shader/docの登録 |
| Docs/IBL.md、Lighting.md、MeshShader.md、GraphicsREADME.md、../README.md | 設計・使用方法・制約と転送サイズの更新 |

Debug/x64本体: ビルド成功、警告0・エラー0。Graphics/ECSテストのビルドも成功。
RTX 3060 Laptop GPU（DXR 1.1、Mesh Shader Tier 1）でGPU Validationを有効にし、`--ibl`、既存全Graphics、`--fog`、ECSはすべて終了コード0。
既存全Graphicsには色管理・HDR/Bloom/Tone Mapping、PBR係数/マップ、Shadow方式切替、RT非対応fallback、鏡/ガラス、Sponza、Resize、in-flight geometry寿命の検証を含む。
故意に壊したASによる既存fallbackテストのErrorログは想定どおり。新しいIBLのGPU Validation警告は発生していない。

描画品質の改善は、環境の方向とroughnessに応じた通常材質の反射、法線マップを反映した間接光、光源0での照明、発光材質のHDR寄与。
フォトリアル制作全体の完成を意味しない。前記の未実施項目とReflection Probe/高度材質は次フェーズに残る。後続の拡散GIは[BakedLighting.md](BakedLighting.md)、比較用RTGIはGlobalIllumination.mdを参照する。
比較PNGは`Generated/IBLComparison.png`。診断PPMを閲覧用にsRGB符号化して左右に並べたもので、実景HDRの画質承認や旧commit比較とは区別する。
