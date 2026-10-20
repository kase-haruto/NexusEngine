# Ray Tracing System

## Overview

Directional LightのDXR ShadowとShadow Map fallbackを既存Forward Rendererへ追加した。影は共通Visibility MaskとしてLightingへ渡す。BLAS/TLASは他のRT Passへ再利用できる独立責務であり、Reflection/AO/GIそのものは今回実装していない。

## 実装前の既存構造調査

| 領域 | 調査結果 |
| --- | --- |
| Device | GraphicsDeviceがAdapter/Device/SM6.6を確認。DXR照会はなかった |
| Renderer / Pass | Application所有PrimitiveRendererをIGraphicsRendererで接続。Forward indexed draw、RenderGraphなし |
| Queue / List / Frame | CommandQueueがDirect Queue/Fenceを所有。GraphicsSystem内部FrameContextがAllocator/List/Fenceを所有 |
| Descriptor | DescriptorManager共有4 Heap、連続Allocator、Bindless世代/Fence retire、一時CBV Arena |
| Resource / Texture | Factory同期Upload、VB/IB/Texture/Depth所有クラス、WIC decode |
| Shader / Reflection | ShaderCompilerのDXC、ShaderReflector、Shader初期化時compile。VS/PSのみ |
| PSO / Root | GraphicsPipeline/PipelineLayout/RootSignature、ReflectionとSM6.6 Direct Heap Indexing |
| Scene / ECS / Level | Scene/ComponentStorage/Level/SystemScheduler。Extractorが値snapshotを生成 |
| Camera / Transform | Primary選択、view逆行列/projectionはExtractor。階層はTransformSystem |
| Model / Mesh / Material | CPU Asset/GPU Resource分離、VB/IB/Submesh、Material slot、Texture/Sampler、node/skin姿勢 |
| Lighting / Shadow | PS内の固定Directional Lightのみ。Shadow Map/Passなし |
| Depth | GraphicsSystem所有D32Float Main Depth。Shadow用SRVなし |
| Editor / Runtime | ImGuiはEditorのRenderExtension。RuntimeにImGui依存なし |

主な問題はPipeline/RootSignature includeによるRendererヘッダーへのNative型漏出と、PS内の固定Light値。PipelineをPImpl化し、LightをRenderSceneの値入力へ移した。既存Frame Fence、共有Descriptor、Scene snapshot、CPU/GPU Asset分離、Application所有Rendererを維持した。全面的なRHI/Scene/RenderGraph置換は行っていない。

新しい責務はCapability snapshot、Shadow選択、Surface Prepass、共通Mask、Geometry世代/AS cache、Frame別TLAS/Instance、DXR library/StateObject/Shader Table、Editor診断。旧READMEの未実装一覧より現行コードを根拠に調査した。

Tier1.0だけの実機と物理DXR非対応GPUはこの環境にないため、その実機挙動は不明・確認が必要。

## Architecture

    Application / Game
      -> Runtime Scene / ECS / Level
      -> RenderSceneExtractor -> RenderScene (Camera / Primitive / Light値)
      -> PrimitiveRenderer
           -> GraphicsContext / GraphicsResourceFactory
           -> ShadowSystem (API非依存設定/統計/Mask境界、PImpl)
                -> Graphics/Backend/DirectX12/Shadow/ShadowBackend
                     -> IShadowTechnique
                          -> ShadowMapTechnique
                          -> RayTracedShadowTechnique
                               -> RayTracingScene (BLAS/TLAS)
                     -> 既存Device / Queue / Descriptor / Shader管理
      -> Forward Lighting (共通Maskだけを消費)

    EditorApplication -> ShadowSystem設定/統計API
    Editor ImGui DX12拡張 -> 既存の明示Native拡張境界

BackendはScene/Entity/Level/Game型を参照しない。ShadowDrawItemはMesh、World、非所有Palette、描画IDだけを持つ。RendererのDevice/List/Resource/DXGI/Descriptor型依存はない。GraphicsSystem/Context/Factory実装は既存Backend橋渡しの位置を維持する。ImGui公式DX12 backendのNative依存はEditor RenderExtensionに限定した。

## GPU Capability

GraphicsDeviceが起動時にOPTIONS5からTierを取得し、Device5 interfaceも確認する。照会失敗/Unsupportedはオプション機能として通常描画を継続。公開値は独自GraphicsCapabilities / RayTracingSupportであり、D3D12 enumの数値を転送しない。上位はGraphicsSystem::GetCapabilities().SupportsRayTracing()で問い合わせられる。Tierは診断用。

将来Mesh Shader/VRS/Sampler Feedback等は実際の照会を追加して独立fieldにする。DirectStorageは別責務。既存SM6.6必須条件は維持した。DXR非対応でもSM6.6対応ならMap経路を使える。SM6.6非対応GPUの対応は別のBindless互換経路が必要。

## Shadow Selection

    ShadowSettings settings;
    settings.mode = ShadowMode::Auto; // RayTracing / ShadowMapも指定可能
    settings.enabled = true;         // falseでOFF
    settings.rayOriginBias = 0.002f;  // World単位
    settings.rayTMin = 0.001f;
    settings.sampleCount = 1;
    auto result = renderer.GetShadows().SetSettings(settings);
    // resultを確認する。NaN、無効interval、範囲外sample/resolution等は拒否。

Autoは対応GPUでRT、非対応でMap。明示RayTracingの非対応時も警告を出してMapへ切り替える。ShadowMap指定ではRT library/ASを初期化しない。判断はShadowBackend内へ集約した。

OFF/空DrawではPrepass/Mask/AS更新を省き、無効Mask参照を返す。Lightingはvisibility=1。生成済み資源はOFF中も保持し、再ONの再確保を避ける。

EditorのShadowsパネルで対応/Tier/方式/BLAS/TLAS/Build数、Mode/OFF/Bias/Soft/sampleを表示・変更できる。Light方向/Angular Radius/強度も編集可能。ImGuiはEditor内のみ。設定保存とLight ComponentのLevel serializationは今回追加していない。

## DXR Architecture

Mesh Geometry -> BLAS、描画InstanceのWorldとBLAS GPU VA -> TLAS。Camera DepthからWorld位置を復元し、Light方向へRayを生成。最初のopaque hitは遮蔽、missは全可視。Payloadはuint visibility、再帰深度1。全Instanceで一つのHitGroup recordを共有する。HitからMaterial/UVを取得する機能は今回不要。

## Update Strategy

- Static Geometry: upload成功時のGeometry generationをkeyにBLASを共有。Instance数と独立。
- Dynamic Transform: 各Frame slotのsnapshotを比較。BLAS再利用、同じ集合はTLAS update。
- Geometry Changed: 同じMesh addressでも再生成は別generation。新BLASとTLAS build。
- Instance集合変更: TLAS build。変更なしはBuild/Updateとも省く。
- Empty Scene: slotのTLASを非公開、cacheを削除。他slotが読むBLASは共有所有で残す。

TLAS初回はALLOW_UPDATE、Transform変更はPERFORM_UPDATE。Frame slot数だけ初期Buildが必要。毎frame全BLAS再構築はない。VB/IBをCOM共有所有し、CPU Asset/Mesh解放後もGPU Build入力を保持する。

Index+vertex offsetはCPU upload前に検証する。現行AS契約はoffset 0のfloat3 positionとtriangle Submesh。負のvertex offset等、現行AS契約に合わないGeometryは原因付きエラーからMapへfallbackする。

## Resource Lifetime

| Resource | 生成・所有者 | 更新/破棄とGPU保証 |
| --- | --- | --- |
| BLAS result / scratch / Input参照 | RayTracingSceneの世代cache | cache退役後もTLAS Frameのshared ownershipで保持。最終参照slotのFence後に解放 |
| TLAS result / scratch / Instance upload | RayTracingScene::Frame | GraphicsSystemのslot Fence待機後に更新。容量不足時のみ再確保 |
| Surface Depth / SRV / DSV | ShadowBackend | Direct Queue順序で全Frame共有。Resize/終了はQueue idle後 |
| Mask / SRV / UAV | ShadowBackend | Direct Queueで生成->Lightingを順序実行。Resize/終了はidle後 |
| Light Depth / DSV / SRV | ShadowMapTechnique | 初回Map/解像度変更で生成。旧資源はidle後に解放 |
| Root / Depth PSO | ShadowBackend | GPU idle後。RendererからNative型を参照できない |
| RT StateObject / Shader Table | RayTracedShadowTechnique | 全Frame共有の不変資源。GPU idle後に解放 |
| Resolve Compute PSO | ShadowMapTechnique | 初期化時生成、GPU idle後に解放 |
| Draw/Frame CB | ShadowBackend、既存ConstantBuffer | 高水位まで保持。Frame sliceはslot Fence後だけ書込む |
| Descriptor Heap | 既存DescriptorManager | DXR専用Heapなし。BindlessのRetire/CollectGarbage/世代管理を維持 |

COMはComPtr、Backend本体はunique_ptr。BLAS cacheとin-flight Frameの実際の共有だけにshared_ptrを使う。raw pointerはQueue/Factory/入力snapshotへの非所有参照。ShadowSystem/Rendererは記録中Contextより長く生存し、記録中にShutdownしない。

AS resultはRAYTRACING_ACCELERATION_STRUCTUREを維持。ScratchはCOMMON->UAV->COMMON。BLAS後UAV BarrierでTLASへの依存、TLAS後UAV Barrierでtraceへの依存を保証する。Shader Tableは32byte identifier、64byte record間隔/開始位置で一度だけ書込む。

Depth Write->Non-Pixel SRV、Mask UAV->Pixel SRVは明示Barrier。未投入List破棄時はContext終了通知でTexture状態を戻し、記録したASを無効化する。Execute直後に投入確定を通知し、Present/Signal失敗で未投入扱いしない。Fenceのdevice-removal sentinelとOS Wait失敗もエラーとして返す。

## Shadow Rendering

    同じMesh/World/PaletteのDraw snapshot
      -> Camera Depth Prepass
      -> inverse ViewProjectionでSurface World位置復元
      -> RT: BLAS/TLAS -> DispatchRays -> Mask UAV
         Map: Light ViewProjection -> Depth Map -> Compute PCF -> Mask UAV
      -> Mask Pixel SRV
      -> Main Color/Depth/Viewport復帰
      -> 共通Forward Lighting

RT OriginはLight方向へ設定可能Biasだけずらす。TMin/TMaxも設定。硬い影は1 Ray。SoftはAngular Radius内を1～64 sampleで平均。背景は全可視。固定空間sampleでFrame間のちらつきを避けるが、Denoiserは未実装。

MapはCamera近傍を中心に設定可能な正射影volume、D32 Depth、比較Bias、3x3 PCF。volume外は全可視。Biasはvolume/解像度/傾斜に応じて調整する。Cascades、Receiver slope bias、Temporal filteringは未実装。

## Fallback

非対応Capability、RT library/StateObject/AS初期化・更新失敗では原因/native codeと警告を記録してMapへ切り替える。反復失敗はBackend寿命中にlatched fallbackとし、毎frame再試行しない。

既存SkinningはVS内だけで変形するため、Skinned Drawを含むFrameはMapで同じPaletteをRasterizeする。Bind Pose BLASの誤った影は使わない。Skinned RTは変形済みGeometry Bufferが必要でFuture Work。Static Meshとnode Transform animationはRT対応。

## Shader Management

Depth VS、Map CS、RT libraryは既存Shader::Initialize -> ShaderCompiler -> ShaderReflectorを使う。Compute/RayTracingLibrary stage、API非依存AccelerationStructure種別を追加した。LibraryはFunctionのBindingを統合する。

Backendのb0定数/t0 TLAS契約をReflectionで照合し、size/register/space/type不一致は初期化エラー。RT export identifierも検証する。Libraryは単一entryを持たないため-Eを省く。include、警告エラー化、Debug/Release最適化は既存と同じ。初期化ContextのshaderDirectoryを使い、Frame中にcompile/reflectionしない。共通定数/位置復元はShadowConstants.hlsli、RT固有処理はShaders/RayTracingへ分離。

## Design Decisions

Shadow抽象化は方式選択/GPU資源をLightingから分離するため。LightingはMask indexだけを受け取る。DirectX隔離によりScene/Renderer/Editor設定にDevice/AS/Descriptor構造を持ち込まない。

独立MaskはForwardにDepth Prepassを追加するが、同じLightingを維持し、将来Denoiser/解像度変更を影Passに閉じ込められる。GBuffer/Deferred全体は新設せず、上位Sceneを再走査せずDraw snapshotを再利用する。

## Alternatives Considered

| 方式 | 利点 | 欠点 / 判断 |
| --- | --- | --- |
| DispatchRays（採用） | Tier1.0対応、独立Pass、将来RT PassのAS再利用 | StateObject/Table/Surface入力が必要 |
| Inline RayQuery | Forward PSと相性がよくTable不要 | Tier1.1必須。Tier1.0でAuto RTという対応範囲を満たさない |
| Shadow Map（Fallback/強制使用） | DXR不要、負荷が予測しやすい、既存SkinningをRasterize | 解像度/volume/Bias/aliasing調整 |
| Ray Traced Shadow | Geometry交差、Angular Radius/sampleによるSoft | AS memory/build、Ray負荷、ノイズ、変形Buffer |

現行はVS/PS Forward、SM6.6 Bindless、Direct QueueなのでRayQueryをPSへ加える実装量は小さい。それでもTier1.0対応、独立Mask、将来の独立RT Passを優先しDispatchRaysを選んだ。

## Performance

BLASは世代変更時だけBuild。TLASはFrame別snapshotで、変更なしゼロBuild、Transform update、集合変更build。Input/Result/Scratchの容量、Instance/Draw vector、CBを高水位まで再利用する。

Static BLAS scratchはcache寿命まで保持するため、Fence後回収/共有scratch/Compactionはmemory最適化候補。Ray countはwidth×height×sample。Soft OFFは設定にかかわらず1 Ray。追加PrepassはDraw/Depth帯域、Maskは4byte/pixelの書込/読込を増やす。Mapはresolution²のDepthメモリ/Fillと3x3 PCF、Camera/Lightの2回Rasterを要する。

現在フル解像度Mask、Cascades/Async Computeなし。性能測定なしに高速化率を主張しない。影PassはNative Root CBVで追加Draw Descriptorを確保しない。

## Future Work

Reflection/AO/GIはAS基盤を共有する独立Pass。Denoiser/TemporalはSoft品質測定後に追加。SkinnedはRaster/AS共有の変形済みVB/versionと変形->AS同期。Procedural/ParticleはAABB種別とintersection export、更新通知。Instancingは既存BLAS共有/TLASを利用。Async Computeはcross-queue Fenceと所有/状態遷移を定義して導入。

Map Cascades/安定化/Slope/Normal bias、Scene serialization、BLAS compaction/Scratch回収、Pipeline cache、GPU timing、half-resolution Maskも拡張候補。

## Verification

Tests/NexusGraphicsTests.vcxprojはWindowなしで実Backendを動かす。GPU Validation/InfoQueueのwarning/errorを失敗扱いにし、Maskと最終Colorを読み戻す。PSO生成成功だけで動作とは扱わない。

Hardware/WARPのMap/RT/OFF、Auto/明示Fallback、Static再利用、TLAS update、Geometry再生成、Soft、Resize/Map解像度変更、Descriptor全解放、未投入List後の復帰、in-flight Mesh解放、Instance削除、PrimitiveRenderer Lightingを検証。公開ヘッダーをNative pre-includeなしの別TUにcompileし、transitive DX includeも拒否する。

    # PowerShell 7。子プロセスの重複Path/PATHを正規化するbuild helper。
    ./Project/Tests/Invoke-GraphicsBuild.ps1 -Project Project/Tests/NexusGraphicsTests.vcxproj -Configuration Debug
    ./Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe
    ./Project/Tests/Invoke-GraphicsBuild.ps1 -Configuration Release

test exeのworking directoryはRepository root。2026-10-04 RTX 3060 Ti/Tier1.1でDebugテスト成功。このWARPもTier1.1を報告した。非対応経路はCapabilityをUnsupportedにしたWARP/Hardware検証で実行しており、物理非対応GPUの実測ではない。

Debug/Develop/Releaseの本体ビルド、Debug/ReleaseのGPUテスト、ECS回帰テストが成功。Debug Editorの初期化、RT選択、継続実行、WM_CLOSEによる終了も確認した。AS入力エラーを意図的に発生させたテストでは原因ログとMapへのFallback、影/照明Maskの復帰を確認した。詳細と未検証項目は[実装報告](RayTracingReport.md)を参照。

## References

- [Microsoft DXR仕様 / AS更新と同期](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)
- [DXR 1.1 / Inline Raytracing](https://devblogs.microsoft.com/directx/dxr-1-1/)

