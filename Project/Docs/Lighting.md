# ライティングとPBR

## 実行負荷と材質確認

前の構成では、金属1pixelにつき最大32本の局所反射を評価し、それぞれで影レイと材質・DDGI参照を行っていた。
さらに最大7×7のフィルタと、Debug/DevelopでのHLSL最適化無効が重なっていた。GPU Based Validationはエンジンの既定で無効なので、通常実行の原因とは区別する。

現在の局所反射は既定4本。粗さ0.08未満は1本の安定した反射を使う。
粗さ0.2～0.5では局所DXR反射からprefiltered IBLへ滑らかに移行し、0.5以上はIBLのみを使用する。
これは粗い反射に対するゲーム向けの近似で、粗い面に動的物体のぼけた鏡像が必要な場合は局所reflection probe等が別途必要。
使用中の材質だけを検査して不要な画面RT／フィルタを省き、非反射面の一次レイでは詳細な法線・色・AO評価も省く。GIプローブと二次レイの属性収集は維持する。
フィルタは最大5×5。HLSLはDebug/DevelopでもO3で最適化し、デバッグ情報は保持する。必要なshaderだけ `ShaderCompileDesc::optimize=false` で最適化を解除できる。

Microsoft [DirectXTK PBRCommon](https://github.com/microsoft/DirectXTK/blob/main/Src/Shaders/PBRCommon.fxh)と
[DirectXTK12 PBREffect](https://github.com/microsoft/DirectXTK12/wiki/PBREffect)を参照し、直接光の拡散項へBurleyの角度応答を追加した。
GGX・粗さの二乗・非金属F0=0.04・金属の色付きF0・prefiltered IBLの構成は維持する。本エンジンの照度単位に合わせLambertの1/πを保持し、公式サンプルの定数を単純にコピーして光量を変えない。
PBRTestLevelは一定色の空から、複数の広いソフトボックスを持つ `PBRStudio.hdr` へ変更した。粗さと曲率を読み取れる環境反射を使う。
HDRは `Tools/GeneratePBRStudio.ps1` で再生成可能。Level形式v6で環境画像・強度・回転を保存し、v1～v5も引き続き読み込む。同じHDRのLevelへ戻るだけではIBLを再ベイクしない。

Renderer Diagnostics → Ray Traced Optics の **GPU (ms)** は、AS、DDGI、画面反射／光学パス、フィルタの直近の完了GPU時間。
Frame slotの既存Fence待機後にtimestampを読み、計測のための追加GPU待機はしない。記録を破棄したframeの値は公開しない。
CPU処理、シェーダー初回コンパイル、IBL初回ベイク、Raster、ShadowSystem、Post Process、UIはこの4項目に含まれない。
`NexusGraphicsTests.exe --pbr-performance` は1280×720でウォームアップ後24frameのGPU平均を測定する。GPU Based Validationは計測時のみ無効にし、画質・API検証の `--metal` では有効を維持する。
この比較は改修後の4本／32本の違いで、旧版全体とのFPS比較ではない。クロックや環境により結果は変わる。
RTX 3060 Tiでの測定では、反射パスの24frame平均は32本設定で約2.94～3.41ms、4本設定で約0.99ms。
これは上記の検証用シーンのパス単体の時間で、ゲーム全体のFPS改善率を示すものではない。

## 反射デノイズとSponzaの影側

通常描画はラスタライズで、DXRは影・局所反射・DDGIプローブの更新に使用するハイブリッド方式。
画面全体を毎pixel多重反射で積分するパストレーサーは使用しない。比較用pixel Diffuse RTGIは拡散経路追跡の実装だが、既定で無効。SponzaLevelでもDDGIを使用する。

反射は粗さ付きの鏡面成分を専用textureへ分離し、深度・法線・粗さ・形状／材質レコードIDを参照する空間フィルタで処理する。
局所反射の粗さ0.15未満はフィルタせず、それ以外は5×5の近傍を使用する。別の形状・材質、段差、法線の違う面からは混ぜない。
直接光・拡散GI・emissive・理想鏡・ガラスは平滑化しない。時間履歴を使わないため、カメラ移動やLevel切り替えによる残像は生じない。
Renderer Diagnostics → Ray Traced Optics → **Reflection Denoising** で比較可能。完全なノイズ除去ではなく、粗い反射の細部と滑らかさの交換条件がある。

**Level → SponzaLevel** を追加し、起動LevelもSponzaLevelにした。従来のDemoLevelは互換用に残す。
前回の金属反射フォールバック修正とは別に、今回はSponzaの直射光が当たらない領域の空の光・間接光を調整した。
Sponzaの空照度は `{4,4.8,6.4}`、DDGI表示倍率は1.8。直射光の強度と遮蔽判定は維持し、空・壁・床を経由した明かりを補う。
プローブは床から0.25離し、壁際の補間を細かくする16×6×8格子を使用する。
各点64本、合計49,152本／更新で、従来390点×128本＝49,920本と同程度のレイ予算にする。
表示倍率はプローブの多重反射フィードバックへ戻さず、照明の発散を防ぐ。

Level形式v5の任意の `LIGHTING` レコードで、空照度・GI表示倍率・プローブ原点／間隔／個数／各点レイ数を保存する。
v1～v4のLevelも読み込み可能。照明指定のないPBRTestLevelへ切り替えると従来の中立設定へ戻る。
Renderer Diagnosticsの照明編集は実行時の比較用で、Levelファイルのプロファイルを直接書き換えない。

追加検証: `NexusGraphicsTests.exe --metal` は4サンプルの生反射／デノイズ反射を64サンプル基準と比較し、被覆とDXR非対応時の一致も確認する。
`NexusGraphicsTests.exe --sponza-shade` はSponzaの間接光のみを同じ視点・直射光で比較し、pixel RTGIを起動せず影側の光量が増えることを検証する。

## 今回の照明見直しと金属反射

Microsoft [DirectXTK12 PBREffect](https://github.com/microsoft/DirectXTK12/wiki/PBREffect)と
[MiniEngine DefaultPS](https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/MiniEngine/Model/Shaders/DefaultPS.hlsl)を参照し、
金属のBase ColorをF0へ使うこと、粗さ付き環境鏡面反射と拡散環境光を別に供給することを確認した。
既存GGX・Smith・線形HDRの経路は維持する。IBLのDiffuse cubeはE、DDGIはE/πを格納しており、この正規化は変更しない。

HDR未指定時には拡散環境光だけが供給され、金属は拡散項が0になるため、直接光のハイライト以外が黒くなっていた。
一定環境の照度 `ambientIrradiance` を放射輝度 `L=E/π` に変換し、split-sumの近似AB係数で粗さ・Fresnel・AOを適用する鏡面フォールバックを追加した。
理想鏡のレイmissも同じ放射輝度を使うため、通常PBRと光学材質の空の強さが一致する。
HDR環境がある場合は従来のprefilter cube＋BRDF LUTを使用する。

DXR有効時の滑らかな通常金属材質（metallic>0、OpticalMaterialの反射・透過が0）は、上記の粗さ範囲で近傍の床や壁を反射する。
直接光・拡散GI・emissiveは維持し、間接鏡面項を置き換える。IBLへ反射光を加算しない。
反射レイのmissはHDR環境または一定環境を参照する。反射先の材質・直接光・DDGI／ベイクGIを評価する。
現段階では1回の局所反射で、反射先の鏡・ガラスへの追加の光学経路は追跡しない。
時間蓄積は使用せず、固定サンプルと上記の空間デノイズで粗い面の粒状感を抑える。
Renderer Diagnostics → Ray Traced Optics の **PBR Metallic Reflections** と **Metal Reflection Samples**（既定4、1～64）で切り替えられる。
DXR無効時は粗さ付き環境鏡面反射へ戻る。Lighting ViewのSpecular IBLは環境項だけの診断表示で、局所反射はFinal LightingまたはIndirect Lighting Onlyで確認する。

## 検証用Level

エディタ上部の **Level / レベル** メニューから `PBRTestLevel` を選択する。
`Resources/Assets/Levels` のLevelファイルを一覧表示し、現在のLevelにチェックを付ける。未保存の変更がある間は切り替えを無効にする。
既存のFile → Saveで保存すると切り替えられる。Levelを開けなかった場合は現在のDocumentを保持する。
切り替え時に選択・Undo履歴とDDGI履歴をリセットし、別Levelのベイク結果を流用しない。ベイク保存先の既定値も `Generated/Lighting/<Levelファイル名>.nxgi` へ切り替わる。

Sponzaは `SponzaLevel`（従来のDemoLevelも残す）、材質比較は `PBRTestLevel` に分離した。
比較シーンは手前に金色の金属5球、奥に灰色の非金属5球を並べ、左から粗さ0.05 / 0.275 / 0.50 / 0.725 / 0.95。
中立色の床と赤・青の壁で反射色と色移りを確認できる。カメラとDirectional LightはLevelに保存してある。
現行Rendererの単一Model入力に合わせ、CPU起動時に比較用の独立rootを共通アセットへ追加し、Levelが描画nodeを選択する。
同梱Sponzaのnode0、光学球のnode1～2、比較シーンのnode3～15という対応。PBRTestLevelはSponzaのnodeを描画しない。

検証コマンド（リポジトリルートから）:

```powershell
Generated/Outputs/EcsTests/NexusEcsTests.exe
Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe --metal
Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe --ibl
Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe --ddgi
```

`--metal` は環境前処理の数値検証に加え、HDRなしの金属の色・粗さ・AO・空強度0を検証し、実際のGPU描画でDXR on/offと反射先の壁色変更を比較する。
非対応capabilityの強制時に環境反射へ戻ることも確認する。画像は `Generated/PBRTestFallback.ppm` と `Generated/PBRTestReflections.ppm` に出力する。

シーンに `DirectionalLightComponent` を追加すると、Transform のワールド行列で変換した方向を照明と影の両方に使用します。方向は表面から光源へ向かうベクトルです。色は線形RGB、intensityは直接光の倍率です。Editor の Shadows パネルで色・方向・強度・有効状態を変更できます。ライトと静的GIフラグはLevel形式v4で保存され、v1～v3も読み込みできます。有効なDirectional Lightは1個までです。無効時・未配置時の直接光は0です。

鏡面反射はGGX分布・height-correlated Smith visibility・Schlick FresnelによるMetallic/Roughness PBRで計算します。直接拡散はLambertの1/πにBurleyの角度応答を適用し、照明側でNdotLを一度だけ乗算します。旧Half-LambertはRenderScene::environment.legacyHalfLambertで比較できます。カメラ位置から視線方向を求めるため、鏡面ハイライトは視点に応じて移動します。金属は拡散反射が0になり、Base Colorが鏡面反射色になります。非金属のF0は0.04です。粗さは数値安定性とサブピクセルのハイライト対策として描画時に0.05以上へ制限します。

glTFから次の値を読み込みます。

- Base Colorと頂点色（既存）。画像はsRGBで読み込み、係数は線形RGB。
- metallicFactor / roughnessFactor。Metallic/Roughness画像のGが粗さ、Bが金属度。
- normalTexture / scale。法線画像は線形データとして読み込み、接線空間のXYZを復元し、XYにscaleを乗算。
- Emissive Factor / Texture / KHR_materials_emissive_strength。画像はsRGB、出力は線形HDRで加算。
- Occlusion Texture / Strength。Linear画像のRから復元し間接光へ適用。
- TANGENT。Z反転と接線のhandedness変換でglTFから左手系へ変換。

Metallic/Roughnessと法線画像にはそれぞれ独立したSamplerを使用します。対応UVはTEXCOORD_0です。追加UVセットとtexture transformは明示的に読み込みエラーにします。接線が省略された場合はRasterの位置・UV微分（RTでは三角形の辺・UV）から基底を求めます。これはMikkTSpaceによる接線生成ではありません。ベイク時と一致させるには接線付きglTFを使用してください。退化したUVでは幾何法線に戻します。

従来VSとMesh Shaderは同じPBR Pixel Shaderを使用します。Skinned Meshは位置・法線・接線へJoint Paletteを適用します。非一様スケールでは法線に逆転置行列を使い、接線は直交化し、負スケールでhandednessを補正します。PrimitiveDrawDataは4720bytes、ModelVertexは104bytesです。

レイトレーシングの鏡・透過パスで交差した物体にも同じPBR係数とマップを適用し、直接光の遮蔽を評価します。既存の鏡面反射・屈折・吸収はOpticalMaterialで別途指定します。RTのVS-only skinningには引き続き対応していません。

`RenderScene::ambientIrradiance` は簡易な一定環境拡散光で、RasterとRTで共有します。Sponzaデモの既定値は `{1.5, 1.8, 2.4}` です。EditorのShadowsパネルにある **Fallback Sky Intensity** で環境光の強さを調整できます（既定3、範囲0～10）。環境光を強めると影の中の拡散反射が明るくなります。この調整値は実行中のみ保持し、Levelには保存しません。HDR環境を指定した場合は、この一定環境光をDiffuse/Specular IBLで置き換えます。Editorデモは[DDGI](DDGI.md)を選択し、DXRでプローブを動的に更新します。ベイクGIとpixel RTGIは比較用に選択できます。DDGIの収束には数フレームが必要です。GIの設定・検証・制約は[拡散GI設計](GlobalIllumination.md)を参照してください。通常材質の粗さ付き環境反射とOpticalMaterial/DXRによる鏡映りの合成規則は[PBR / IBL設計](IBL.md)を参照してください。

参照: [Khronos glTF PBR](https://www.khronos.org/gltf/pbr)、[glTF 2.0仕様](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)。

検証: Debug/x64ビルド、ECSテスト、DXCコンパイル、GPU Validation付き描画テスト。GPU読み戻しで粗さ・金属度・法線マップの応答、フラット法線の維持、Metallic/Roughnessチャンネル、接線あり/なしの一致、VS/Mesh Shaderの一致を確認しています。影のRT/Shadow Map切替、鏡・ガラスの反射/屈折/吸収、Sponzaのマップ読み込み、ResizeとDescriptor解放も確認しています。
