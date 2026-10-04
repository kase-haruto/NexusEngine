# Ray Traced Reflection / Transmission

Directional Shadow基盤に、周囲のMaterial色を追跡する鏡面反射と、屈折を伴うガラス透過を追加した。起動時はSponzaを読み込み、その中央通路にMirror Sphere（左）とGlass Sphere（右）を追加している。既存のSponza Mesh/Node/Materialを保持する。

## 確認方法

Repository rootからビルドし、実行ファイルのディレクトリをworking directoryにして起動する。

```powershell
./Project/Tests/Invoke-GraphicsBuild.ps1 -Configuration Debug
$app = (Resolve-Path ./Generated/Outputs/Debug/NexusEngine.exe).Path
Start-Process -FilePath $app -WorkingDirectory (Split-Path $app)
```

左Sphereは反射率1のMirrorで、Sponzaの柱・布・床などを反射する。右Sphereは透過率1、IOR=1.5のGlassで、背後のSponzaが屈折して見える。RGB吸収は(0.35, 0.08, 0.02)/World unitで、赤が厚みに応じて強く減衰する。

Sphere半径は0.7、配置はMirror=(-3,0.75,0.9)、Glass=(-3,0.75,-0.9)。Cameraは(-7.5,1.7,0)から+X方向へ向ける。Sphereは独立Root Nodeなので、Sponzaのimport scale=0.008を継承しない。Material slotは読込Material末尾から取得し、Editorに固定slot番号を使わない。

Editorの **Ray Traced Optics** パネルには以下がある。

- Reflection / Transmission：PassのON/OFF。OFFでは通常の不透明Raster Materialになる。
- Max Optical Bounces：入射/出射/内部反射を含む1〜12回の交差上限。初期値6。1では二次Rayが届かず鏡/ガラスは暗くなる。
- Optical Ray Bias：面の両側に応じたOrigin offsetとTMin。初期値0.001。
- Reflectivity / Transmission：各Sphereの反射率/透過率。
- IOR：Glassを1.0にすると屈折がなくなり、1.5と比べて背景パターンの変形が変わる。
- RGB Absorption：0へ変更するとBeer-Lambert吸収を無効化する。

Cameraの位置/回転は既存DemoLevelDebugWindowで変更できる。Shadowの方式/設定窓も維持した。設定は現在のRenderer寿命内の値で、Sceneファイルへの保存はまだ行わない。

## 構造と処理

```text
ModelLoader (Sponza CPU Asset) + Samples/OpticalDemoScene (Sphere/Material/Node追加)
    -> Scene / ModelPose / RenderScene
    -> PrimitiveRenderer (同じGeometry snapshotとMaterial参照)
    -> OpticalSystem (API非依存の設定/入力/結果)
    -> DirectX12 OpticalBackend
       -> RayTracingScene (既存BLAS/TLAS更新基盤)
       -> Optical StateObject / Frame metadata / ShaderTable
       -> Reflection + Refraction Rays
       -> RGBA16F Radiance texture
    -> Forward PS (光学pixelだけ合成)
```

CameraのNear/Farをinverse ViewProjectionでWorldへ戻し、一次Rayを生成する。通常の不透明pixelは出力alpha=0でForward Lightingへ委譲する。光学pixelは交差から法線・UV・Vertex色・Material色/Textureを評価し、alpha=1の最終Radianceを書き込む。

Reflectionはreflect方向へのRayで交差先の色を得る。GlassはSchlick Fresnelで反射/透過を分配し、refractでSnellの法則に従う方向を生成する。入射面と出射面を別々に追跡し、IORを空気/Glass間で変更する。全反射では透過分を反射へ移す。内部の実移動距離にexp(-absorption × distance)を乗算する。CameraがGlass内部から開始した場合も最初の裏面から媒質を判定する。

Hit Shaderは属性だけをPayloadへ格納し、Raygenの明示Path stackが二次Rayを繰り返す。StateObject recursion depthは1、Payloadは64byte。各PathはBounce上限とThroughput閾値0.002で終了し、stackは16件に制限している。確率的なサンプリングを使わないため、今回はDenoiserを必要としない。

## DXR / Material参照

既存DispatchRays/RayTracingSceneを利用する。複数GeometryのMaterialはInstanceContributionToHitGroupIndexとGeometry別Local Root b1から特定する。Tier1.1固有のGeometryIndex()は使用しない。採用理由は[Microsoft DXR仕様](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)の対応範囲による。実測環境はTier1.1であり、物理Tier1.0 GPUは未検証。

VB/IBを既存共有Bindless HeapのRaw SRVで読み、TriangleのIndexとBarycentricから属性を補間する。UInt16/UInt32とSubmesh firstIndex/baseVertexを扱う。UInt16の最終Halfwordを32bit loadしても範囲外にならないようIB storageを4byteへpadし、IAの論理Index数は維持する。

Shaderは既存Shader::Initialize/Compiler/Reflectionを使用する。ByteAddressBuffer reflectionを追加し、b0 Global constants、b1 Local geometry index、t0 TLAS、t1 Frame metadataの契約を検証する。二次RayのTextureは明示LOD0でSampleLevelする。Texture未指定Materialのfallbackを白へ変更し、従来の青いcheckerが反射色へ混ざらないようにした。

## 所有権・同期・更新

OpticalSystemがPImplを通してBackendを所有し、Renderer/Scene/ECSへDXR型を公開しない。OpticalBackendはStateObject/Root/Table/Radiance、Frame別CB/Metadata/HitTable、Raw viewとInput BufferのCOM参照を所有する。

既存Frame Fence完了後のslotだけMetadata/HitTableを更新する。Raw viewはMesh geometry generationでcacheし、全in-flight Frameから参照されなくなった世代だけ共有Allocatorへretireする。Radiance Resize/ShutdownはQueue idle後に行う。UAV→PS SRV遷移でForward読取を順序付ける。Command Listを未投入で破棄するとCPU Texture stateとAS built stateをrollbackする。

GraphicsContextの記録完了通知を複数Passに対応させ、OpticsとShadowの両方へ通知する。現在は最大4件、重複Ownerの登録は省く。Materialの反射率/IOR変更はFrame metadataだけを更新し、BLASを再Buildしない。

ShadowとOpticsは同じRayTracingScene実装を利用するが、現在は別AS owner/cacheを持つため、双方がRTを使用するとBLAS/TLASのGPU storageも二組になる。共通Geometry cache/AS serviceへの統合は今後のmemory最適化候補であり、現時点で物理AS共有を実装したとは扱わない。

## 実装ファイル

- Graphics/Material/OpticalMaterial.h：反射/透過/IOR/吸収と検証。
- Graphics/RayTracing/OpticalSystem.h/.cpp：公開入力、品質設定、結果。
- Graphics/Backend/DirectX12/RayTracing/OpticalBackend.h/.cpp：Native Optical Pass。
- Shaders/RayTracing/Optical.hlsl：属性取得とReflection/Transmission評価。
- Graphics/Model/PrimitiveMeshBuilder.h/.cpp：CPU UV Sphere/Box生成。
- Samples/OpticalDemoScene.h/.cpp：SponzaへのSphere追加。独立した色Reference StageはGPU回帰テスト用に維持。

ModelAssetData/ModelResourceへ光学係数を追加した。PrimitiveRenderer/DrawData/PSへRadiance合成とMaterial設定API、GraphicsContextへ複数通知、RayTracingSceneへ任意HitGroup offset、IndexBufferUploaderへRaw load用paddingを追加した。Editorとmainで設定窓を接続している。

## 検証

Tests/NexusGraphicsTestsはWindowなしで実GPUを使用し、GPU Validation/InfoQueueのwarning/errorを失敗扱いにする。最終Forward Colorのreadbackで両Sphereを検証し、Generated/OpticalDemo.bmpへ実描画を保存する。

Sponza追加ケースも同じRendererで検証し、Generated/SponzaOpticalDemo.bmpへ保存する。Sponzaの既存Mesh/Node数を維持し2件ずつ追加すること、両SphereのON/OFF差分、IOR/吸収への応答、Descriptor全解放を確認した。SponzaでのIOR差分は2035pixel、吸収差分は1107pixel（384×216のreadback）。ログはGenerated/SponzaOpticalTests.log。

2026-10-04、RTX 3060 Ti/Tier1.1：

- Debug/Develop/Release本体Build成功。Debug/Release GPUテストは/W4 /WXでBuild/実行成功。
- Mirror/GlassのON/OFF差分、Mirrorの赤/緑色、Glassの複数交差が背景色を運ぶことを確認。
- IOR 1.5→1.0でGlass領域1303pixel、吸収無効化で896pixelの応答を確認。
- Frame slot 0/1、未投入List後の復帰、Output Resize、複数SubmeshのLocal Root tableを確認。
- Output/Raw view/Material/Transient Descriptorを含む全利用可能slotの再確保成功。
- 非対応Capabilityは不透明Raster fallbackで、光学ON/OFFのColorが同じことを確認。実際の非DXR物理GPUではない。
- 既存ShadowのGPU回帰テストとECSのCPU回帰テストを維持。公開ヘッダーのNative include拒否テストも成功。
- Debug Editorは同じStageで起動・継続実行し、本体WindowへのWM_CLOSEで終了コード0、Framework shutdown completedを確認。

EditorのPrintWindow取得はDX12画面を黒く返したため見た目の証拠に使っていない。画像評価にはGPU readbackのOpticalDemo.bmpを使用した。Editorの全Sliderを手動操作したとは扱わない。

```powershell
./Project/Tests/Invoke-GraphicsBuild.ps1 -Project Project/Tests/NexusGraphicsTests.vcxproj -Configuration Debug
./Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe
./Project/Tests/Invoke-GraphicsBuild.ps1 -Project Project/Tests/NexusGraphicsTests.vcxproj -Configuration Release
./Generated/Outputs/GraphicsTests/Release/NexusGraphicsTests.exe
```

ログはGenerated/OpticalTests-Debug.log、OpticalTests-Release.log、OpticalEditorClean.out.log。AS失敗FallbackテストのEngine Errorログは意図した結果で、OpticsエラーやGPU Validation warning/errorはない。

## 制約と拡張

今回は滑らかな鏡面と閉じた均一Glass、空気との境界を対象とする。Roughness/Glossy、分散、入れ子の異なる媒質、Caustics、半透明Alpha texture、Skinned変形Buffer、glTFの光学extension importは未実装。光学Rayが到達した通常Materialは既存のAmbient+Directional Diffuseと明示LOD0で評価し、二次交差先でShadow Maskを再利用しない。Glassの既存ShadowはOpaque遮蔽で、色付き透過Shadowは今後の別Passとして扱う。

非DXRではShadow Mapを維持し、光学Materialは不透明Raster fallbackとなる。真のRT反射/屈折を非対応GPUで提供するとは主張しない。

追加負荷はフル解像度の一次Rayと光学pixelの二次Ray、Hit属性/Texture load、二組のAS storage。Bounce上限/Throughputで制御し、光学Materialなし/Pass OFFではDispatchを省略する。GPU timingによる定量性能測定は未実施。
