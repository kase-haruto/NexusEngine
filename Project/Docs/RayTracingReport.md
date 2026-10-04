# DXR Directional Shadow 実装報告

2026-10-04。設計・操作方法・所有権表は[Ray Tracing System](RayTracing.md)を参照。

## 1. 調査した既存構造

既存はPrimitiveRendererのForward描画、RenderSceneExtractorによるScene/ECSからの値抽出、GraphicsSystemのDirect Queue/二重Frame Context、GraphicsContextによる描画意図APIで構成される。ModelAsset/ModelPoseとMeshResource、Material、Camera/Transform、DepthBuffer、ShaderCompiler/Reflection、Pipeline/Layout/RootSignature、DescriptorManager/Bindless/Transient arena、Texture/Resource uploader、Editor/Runtime境界を確認した。

独立RenderGraph/RenderPass、既存Shadow/Lightシステムは存在しなかった。SkinningはVertex Shader方式。PipelineヘッダーからNative RootSignature型が上位へ伝播していたためPImplで閉じた。既存のFrame Fence、Descriptor Heap、Shader管理、Scene抽出とAsset/GPU所有分離を維持した。

## 2. 採用した設計

PrimitiveRenderer → ShadowSystem → DirectX12 ShadowBackend → IShadowTechnique（RayTracedShadowTechnique / ShadowMapTechnique）。SceneはDirectionalLightDataと描画値を提供し、BackendはScene型を参照しない。

両方式は共通R32_FLOAT Visibility Maskを生成し、既存Forward PSがDirect Diffuseへ適用する。Ambientは維持する。DispatchRaysを採用しTier1.0も対象とした。Inline RayQueryとの比較・現行Forward構成との関係は設計文書に記載した。

## 3. 新規追加ファイル

- Graphics/Device/GraphicsCapabilities.h：API非依存のCapability。
- Graphics/Shadow/ShadowTypes.h、ShadowSystem.h/.cpp：設定、Light、非所有Draw入力、統計、公開API。
- Graphics/Backend/DirectX12/RayTracing/RayTracingScene.h/.cpp：共有BLAS、Frame別TLASと更新・寿命。
- Graphics/Backend/DirectX12/Shadow/ShadowBackend.h/.cpp、ShadowMapTechnique.cpp、RayTracedShadowTechnique.cpp：Native Pass、共通入力/Mask、方式切替。
- Shaders/Shadow/ShadowConstants.hlsli、Depth.VS.hlsl、Resolve.CS.hlsl：共通定数、Depth、Map resolve。
- Shaders/RayTracing/Shadow.hlsl：Raygen/Miss/Hit、Hard/Soft Shadow。
- Tests/GraphicsShadowTests.cpp、GraphicsPublicHeaderTests.cpp、NexusGraphicsTests.vcxproj、Invoke-GraphicsBuild.ps1：実GPU検証、DX include境界、ビルド環境補助。
- Docs/RayTracing.md、RayTracingReport.md：設計と本報告。

## 4. 変更ファイル

- Graphics/Device/GraphicsDevice.h/.cpp：OPTIONS5判定とログ。
- Graphics/Renderer/GraphicsSystem.h/.cpp：Capability、Factory接続、Frame記録完了通知。
- Graphics/Renderer/GraphicsContext.h/.cpp：主RenderTarget/Viewport復帰と記録破棄通知。
- Graphics/Renderer/GraphicsResourceFactory.h/.cpp：Shadow生成、Mesh入力検証、Backend接続。
- Graphics/Renderer/PrimitiveRenderer.h/.cpp、RenderScene.h：Shadow統合とDirectional Light入力。
- Graphics/Pipeline/GraphicsPipeline.h/.cpp：PImplによるNative型隔離。
- Graphics/Resource/MeshResource.h/.cpp、VertexBuffer.h/.cpp、IndexBuffer.h/.cpp：Geometry世代、Backend限定AS入力。
- Graphics/Resource/VertexBufferUploader.cpp、IndexBufferUploader.cpp：AS読取にも適合する状態遷移。
- Graphics/Shader/ShaderCompiler.cpp、ShaderReflector.cpp、ShaderTypes.h：Library/Compute/AS reflection、共通compile経路。
- Graphics/Material/PrimitiveDrawData.h、Shaders/Primitive/PrimitiveDrawData.hlsli、Primitive.PS.hlsl：MaskとDirectional Lighting。
- Graphics/Core/CommandQueue.cpp：Fence待機失敗とDevice removal sentinelを検出。
- Editor/Core/EditorApplication.h/.cpp、main.cpp：Editor設定/統計パネルの接続。
- NexusEngine.vcxproj、Docs/GraphicsREADME.md：ファイル登録と文書導線。

Foundation/Error/Error.hは作業開始時から存在するユーザー変更であり、この実装の変更に含めない。

## 5. Ray Tracing処理フロー

RenderScene/ModelPoseから描画snapshot → Camera Depth prepass → 世代対応BLAS取得/必要時Build → Frame別TLASの差分Build/Update → DispatchRays → Mask UAVからPS SRVへの遷移 → Forward Lighting。

DepthからWorld positionを再構成し、Light方向へOrigin BiasとTMin/TMaxを設定してTraceRayする。遮蔽0、非遮蔽1。Soft設定ではAngular RadiusのCone内をSample Count分評価する。

## 6. Shadow Mapフォールバックの処理フロー

Capability/Mode/Geometry適合性をBackendの一箇所で選択 → Camera Depth → Directional Lightの正射影Depth Map → ComputeでWorld positionをLight空間へ投影しBias付き3×3 PCF → 同じMask/Lighting。

明示RayTracingで非対応の場合も警告してMapを使う。RT初期化/AS生成失敗は原因を記録しMapへ切替え、そのBackend寿命では失敗を反復しない。Skinned入力は既存PaletteでMapをRasterizeする。

## 7. GPU Capability判定

GraphicsDevice内でD3D12_FEATURE_D3D12_OPTIONS5のRaytracingTierとDevice5可用性を確認。Unsupported/Tier1_0/Tier1_1を独自enumへ変換する。上位はSupportsRayTracing()/GraphicsCapabilitiesを利用しNative Feature Supportを操作しない。

## 8. BLAS/TLASの更新戦略

Mesh upload完了で単調増加Geometry generationを付け、同世代BLASを共有する。Static BLASを毎Frame再構築しない。TLASはFrame slot別snapshotで変化なしならBuildを省略、TransformのみならPERFORM_UPDATE、Geometry GPU address/Instance集合変更ならBuildする。

記録後にCommand Listを投入せず破棄した場合は、AS built状態とTexture状態をrollbackし、次Frameで未実行Buildを再記録する。

## 9. Resource Lifetime

RayTracingSceneのcacheがBLASのInput VB/IB、Result/ScratchをCOM所有し、Frame TLASは参照BLASを共有保持する。MeshのCPU所有を先に解放しても投入済みAS入力を保持する。Frame slotのFence完了後にのみInstance upload/TLAS/Scratchを上書き・再確保する。

ShadowBackendはSurface Depth/Mask/Root/Frame CBを、Map techniqueはLight Depth/Compute PSOを、RT techniqueはScene AS/StateObject/Shader Tableを所有する。Resize/ShutdownはQueue idle後。Descriptorは既存共有Allocatorから取得し、GPU完了後に解放する。専用Heapを追加していない。

## 10. DirectX依存の隔離

PrimitiveRenderer、RenderScene、Shadow公開API、Scene/ECS/LevelはNative型を扱わない。DXRのDevice5/List4/Resource/AS構造、StateObject、Shader Table、Native Descriptor操作はDirectX12 Backendに閉じた。公開ヘッダーの別TUテストで、Renderer/System/Shadow/Extractor/LevelからWindows/D3D12/DXGIのtransitive includeがないことを検証した。

既存GraphicsのDevice/Resource/Descriptor/Pipeline実装はNative境界として残す。Editorの既存ImGui DX12連携とPlatformのHWNDは維持した。全面的なRHI再構築は行っていない。

## 11. パフォーマンス上の注意点

BLAS buildは新Geometry世代のみ。TLASの変化なし/Update/Buildを区別し、Draw vector、Instance容量、Frame CBとDescriptorを再利用する。既存RenderSceneを利用しECSを追加走査しない。

追加Surface prepass、フル解像度Mask、Ray数=幅×高さ×Sample Countの負荷がある。Soft OFFは1 Ray。MapはResolution²のDepthとPCF負荷。Static BLAS scratchはcache寿命まで保持する。GPU timing/実Scene性能の定量計測は未実施で、高速化率は主張しない。

## 12. 今後の拡張ポイント

Reflection/AO/GIの独立PassがRayTracingSceneを共有できる。Skinnedは変形済みGeometry Bufferとversion、ProceduralはAABB/intersection export、Denoiser/Temporalは履歴Resource、Async Computeはcross-queue Fenceと所有/状態契約を追加する。BLAS compaction/Scratch回収、Mask低解像度化、Map cascades/安定化も候補。

## 13. 残っている課題

今回はDirectional Light一つ、Opaque Geometryを対象とする。SkinnedフレームはMapへFallback。変形BLAS、Alpha-test hit、Cascaded Map、Denoiser、Light/Shadow設定のScene serializationは未実装。

Tier1.0の物理GPUと物理DXR非対応GPUは未検証。WARPもTier1.1だったため非対応経路はCapabilityをUnsupportedに差し替えて確認した。既存のSM6.6/Bindless要件まで非対応なGPUへの対応は今回のDXR Fallbackとは別の既存制約。Editor UIの全項目の手動操作・画像による見た目の評価は未実施。

## 14. ビルド・動作確認結果

| 検証 | 結果 |
| --- | --- |
| 本体 x64 Debug / Develop / Release | 全てBuild成功、未解決シンボルなし |
| GPUテスト x64 Debug / Release | /W4 /WXでBuild成功、両方終了コード0 |
| Hardware RT/Map/Auto/明示RT/OFF、WARP | MaskをGPU readbackしてShadow/Lit範囲確認 |
| Unsupported capability | WARP/Hardware ForwardでMap、明示RTの警告Fallback確認 |
| PrimitiveRenderer統合 | 最終Color readbackで影の暗部とLit面を確認 |
| Static/TLAS/Geometry世代 | BLAS再利用、TLAS更新、Geometry再uploadでBLAS追加Build確認 |
| Soft/Resize/Map resolution | 実行成功、出力値有限・0〜1 |
| Descriptor | Shadow shutdown後に全使用可能slotの再確保成功 |
| 未投入List / in-flight Mesh解放 / Instance削除 | 次描画復帰、GPU使用中の入力保持、空TLAS/cache削除を確認 |
| AS失敗 | 合法Raster入力の負BaseVertexをBLAS側で拒否、原因ログとMap出力・再試行抑止を確認 |
| D3D12 GPU Validation / InfoQueue | 全GPUテストでwarning/errorなし。AS失敗ケースのEngine Errorログは期待結果 |
| ECS回帰 | Debug Build成功、テスト終了コード0 |
| Debug Editor実行 | RTX 3060 Ti/Tier1.1、初期化とRT選択、継続実行、WM_CLOSE後にprocess終了 |
| 差分レビュー | git diff --check成功、責務/Native依存/寿命/状態/同期/コメント/文書を確認 |

Release本体には変更していないThirdParty/imgui/backends/imgui_impl_dx12.cppのC4189が3件ある（1049/1221/1321、IM_ASSERTのみで使用するHRESULT変数）。該当コードはHEADと同じで、本変更のソースに新規警告はない。テストprojectはThirdPartyを含まず/WXで成功する。

再実行はRepository rootから以下を使用する。

```powershell
./Project/Tests/Invoke-GraphicsBuild.ps1 -Configuration Debug
./Project/Tests/Invoke-GraphicsBuild.ps1 -Configuration Develop
./Project/Tests/Invoke-GraphicsBuild.ps1 -Configuration Release
./Project/Tests/Invoke-GraphicsBuild.ps1 -Project Project/Tests/NexusGraphicsTests.vcxproj -Configuration Debug
./Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe
./Project/Tests/Invoke-GraphicsBuild.ps1 -Project Project/Tests/NexusGraphicsTests.vcxproj -Configuration Release
./Generated/Outputs/GraphicsTests/Release/NexusGraphicsTests.exe
./Project/Tests/Invoke-GraphicsBuild.ps1 -Project Project/Tests/NexusEcsTests.vcxproj -Configuration Debug
./Generated/Outputs/EcsTests/NexusEcsTests.exe
```

Build helperはtool hostが重複提供するPath/PATHを子環境で正規化する。再現ログはGenerated/GraphicsShadowTests-Debug.log、GraphicsShadowTests-Release.log、ShadowEditorSmoke.out.log/.err.logに出力した。Generatedは成果物としてGit登録しない。
