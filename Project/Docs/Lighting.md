# ライティングとPBR

シーンに `DirectionalLightComponent` を追加すると、Transform のワールド行列で変換した方向を照明と影の両方に使用します。方向は表面から光源へ向かうベクトルです。色は線形RGB、intensityは直接光の倍率です。Editor の Shadows パネルで色・方向・強度・有効状態を変更できます。ライトはLevel形式v3で保存され、v1/v2も読み込みできます。有効なDirectional Lightは1個までです。無効時・未配置時の直接光は0です。

鏡面反射はGGX分布・height-correlated Smith visibility・Schlick FresnelによるMetallic/Roughness PBRで計算します。拡散反射はユーザー指定のHalf-Lambertで、角度による重みを `saturate(N・L * 0.5 + 0.5)^2` とします。横向きの面は0.25、光源を向く面は1、完全に背を向ける面は0です。これは見た目を調整する拡散照明であり、物理的なLambert BRDFとは異なります。鏡面反射の角度重みと影の遮蔽は従来どおりです。カメラ位置から視線方向を求めるため、鏡面ハイライトは視点に応じて移動します。金属は拡散反射が0になり、Base Colorが鏡面反射色になります。非金属のF0は0.04です。粗さは数値安定性とサブピクセルのハイライト対策として描画時に0.05以上へ制限します。

glTFから次の値を読み込みます。

- Base Colorと頂点色（既存）。画像はsRGBで読み込み、係数は線形RGB。
- metallicFactor / roughnessFactor。Metallic/Roughness画像のGが粗さ、Bが金属度。
- normalTexture / scale。法線画像は線形データとして読み込み、接線空間のXYZを復元し、XYにscaleを乗算。
- TANGENT。Z反転と接線のhandedness変換でglTFから左手系へ変換。

Metallic/Roughnessと法線画像にはそれぞれ独立したSamplerを使用します。対応UVはTEXCOORD_0です。追加UVセットとtexture transformは明示的に読み込みエラーにします。接線が省略された場合はRasterの位置・UV微分（RTでは三角形の辺・UV）から基底を求めます。これはMikkTSpaceによる接線生成ではありません。ベイク時と一致させるには接線付きglTFを使用してください。退化したUVでは幾何法線に戻します。

従来VSとMesh Shaderは同じPBR Pixel Shaderを使用します。Skinned Meshは位置・法線・接線へJoint Paletteを適用します。非一様スケールでは法線に逆転置行列を使い、接線は直交化し、負スケールでhandednessを補正します。PrimitiveDrawDataは4480bytes、ModelVertexは96bytesです。

レイトレーシングの鏡・透過パスで交差した物体にも同じPBR係数とマップを適用し、直接光の遮蔽を評価します。既存の鏡面反射・屈折・吸収はOpticalMaterialで別途指定します。RTのVS-only skinningには引き続き対応していません。

`RenderScene::ambientIrradiance` は簡易な一定環境拡散光で、RasterとRTで共有します。Sponzaデモの既定値は `{1.5, 1.8, 2.4}` です。EditorのShadowsパネルにある **Ambient Light Intensity** で環境光の強さを調整できます（既定3、範囲0～10）。環境光を強めると影の中の拡散反射が明るくなります。この調整値は実行中のみ保持し、Levelには保存しません。HDR環境マップによるIBLと粗さに応じた環境鏡面反射はこの実装には含みません。通常のPBR材質は直接光の鏡面ハイライトを表示し、シーンの鏡映りは既存OpticalMaterial/DXR経路で扱います。

参照: [Khronos glTF PBR](https://www.khronos.org/gltf/pbr)、[glTF 2.0仕様](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)。

検証: Debug/x64ビルド、ECSテスト、DXCコンパイル、GPU Validation付き描画テスト。GPU読み戻しで粗さ・金属度・法線マップの応答、フラット法線の維持、Metallic/Roughnessチャンネル、接線あり/なしの一致、VS/Mesh Shaderの一致を確認しています。影のRT/Shadow Map切替、鏡・ガラスの反射/屈折/吸収、Sponzaのマップ読み込み、ResizeとDescriptor解放も確認しています。
