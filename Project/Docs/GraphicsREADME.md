# NexusEngine Graphics Architecture

## 1. この文書の目的

この文書は、NexusEngineにおけるDirectX 12 Graphics基盤の構造、描画フロー、リソースの所有権、依存方向、および現在の設計を採用した理由をまとめたものです。

現在のGraphics基盤は、次の機能までを対象としています。

- DirectX 12 DeviceとGPU Adapterの生成
- Command Queue、Fence、SwapChain、BackBuffer、RTVの管理
- BackBufferごとのFrameContext
- DXCによるHLSL Compile
- DXIL Reflectionと独自Shader Metadata
- Reflectionに基づくPipeline LayoutとRootSignature生成
- Graphics Pipeline State生成
- Vertex BufferとPrimitive描画
- Shader Model 6.6 Direct Heap Indexing
- Bindless Descriptorの確保、参照、遅延解放

Material、Texture Loader、Model Renderer、RenderGraphなどは、現在の責務境界を利用して今後追加する機能です。

## 2. 設計原則

Graphics基盤では、動作することだけでなく、次の原則を優先しています。

1. 依存方向を上位から下位への一方向にする
2. DirectX 12固有型をFrameworkやRuntimeへ公開しない
3. WindowへGraphicsの責務を持たせない
4. Device、Queue、Presentation、Shader、Pipeline、Descriptorの責務を分離する
5. GPU ResourceとDescriptorの所有者および破棄時期を明確にする
6. Shader追加時にRoot parameter番号を手作業で管理しない
7. 毎フレームの文字列検索やShader Reflectionを行わない
8. 将来の機能だけを理由に過剰な抽象化を導入しない

## 3. 現在の依存構造

```text
Application
  ├─ ImGuiRenderer
  │    └─ Dear ImGui Win32 / DX12 backend
  └─ NexusFramework
       ├─ ProjectWindow
       │    └─ Win32 + 上位Message Handler接続点
       └─ GraphicsSystem
            └─ DirectX 12実装（PImpl）
                 ├─ GraphicsDevice
                 │    ├─ GraphicsDebugConfigurator
                 │    └─ GraphicsAdapterSelector
                 ├─ CommandQueue
                 ├─ DescriptorManager
                 │    └─ DescriptorAllocator × 4 Heap種別
                 ├─ SwapChain
                 ├─ FrameContext × BackBuffer数
                 ├─ BindlessDescriptorTable
                 │    └─ DescriptorManagerのResource/Samplerを参照
                 └─ PrimitiveRenderer
                      ├─ Shader
                      │    ├─ ShaderCompiler
                      │    └─ ShaderReflector
                      ├─ GraphicsPipeline
                      │    ├─ PipelineLayout
                      │    └─ RootSignature
                      └─ VertexBuffer
```

`GraphicsSystem`の公開ヘッダーはPImplを利用しています。これにより、`NexusFramework`は次の型を認識しません。

- `ID3D12Device`
- `ID3D12GraphicsCommandList`
- `IDXGISwapChain`
- `D3D12_RESOURCE_STATES`
- CPU/GPU Descriptor Handle
- Root parameter番号

小さなDirectX 12 Backendクラスには一律にPImplを導入せず、実際のレイヤー境界である`GraphicsSystem`に限定しています。

## 4. 各クラスの責務

### GraphicsDevice

`GraphicsDevice`はGraphics Backendの起点です。

担当するもの:

- Debug LayerとDREDのDevice生成前設定
- DXGI Factory生成
- GPU Adapter選択
- D3D12 Device生成
- Shader Model 6.6対応確認
- Info Queue設定

担当しないもの:

- SwapChain
- Command Queue
- Descriptor
- Shader
- Pipeline
- 描画命令

Adapterは`IDXGIFactory6::EnumAdapterByGpuPreference`を利用し、高性能GPUを優先します。通常選択ではSoftware Adapterを除外し、WARPは設定で明示された場合だけ利用します。

Bindless Direct Heap IndexingにはShader Model 6.6が必要です。非対応環境ではShader CompileやPSO生成まで進まず、Device初期化エラーとして終了します。

### CommandQueue

`CommandQueue`はDirect Command QueueとFenceを所有します。

- Command Listの実行
- Fence Signal
- 指定Fence値の待機
- Queue全体のIdle待機
- 完了Fence値の取得

CommandAllocatorとCommandListは所有しません。これらはBackBufferごとの`FrameContext`が所有します。

### SwapChain

`SwapChain`はPresentationだけを担当します。

- DXGI SwapChain
- BackBuffer
- 現在のBackBuffer index
- Present
- Resize
- DescriptorManagerから確保したBackBuffer RTV Handle

SwapChainはRTV Heapを生成しません。BackBufferごとの連続RTV領域だけを所有し、Resize時は同じ領域へViewを再生成します。

### FrameContext

各BackBufferに対応する`FrameContext`は次を所有します。

```text
FrameContext
  ├─ CommandAllocator
  ├─ GraphicsCommandList
  └─ FenceValue
```

CPUは同じFrameContextを再利用する直前だけ、そのContextのFence値を待機します。毎フレームQueue全体を待機する方式ではないため、CPUとGPUが複数フレームを並行処理できます。

### ShaderCompiler

`ShaderCompiler`の責務はHLSLからDXILへの変換だけです。

Debug/Developでは次を使用します。

- Debug情報の埋め込み
- 最適化無効
- 警告のエラー化

Releaseでは最適化を有効にします。Compile診断はLoggerと共通Errorへ変換します。

### ShaderReflector

`ShaderReflector`はDXIL Reflection APIを使用し、結果をDirectX非依存のMetadataへ変換します。

取得する情報:

- Resource名
- CBV、SRV、UAV、Sampler種別
- Register番号
- Register space
- Bind count
- Shader Stage
- ConstantBuffer size
- Vertex input semantic

Reflection COM InterfaceやD3D12のReflection構造体は上位へ公開しません。

### Shader

`Shader`は次を所有します。

```text
Shader
  ├─ Compile済みDXIL
  ├─ ShaderMetadata
  └─ ShaderStage
```

CompileとReflectionは`Shader::Initialize`中に一度だけ実行します。フレーム描画中にHLSL CompileやReflectionは行いません。

### PipelineLayout

`PipelineLayout`はVertex ShaderとPixel ShaderのBinding情報をPipeline単位へ統合します。

```text
Vertex Shader
  b0 Transform

Pixel Shader
  b0 Transform
  t0 AlbedoTexture

        ↓

PipelineLayout
  b0 Transform      : VS | PS
  t0 AlbedoTexture : PS
```

次の不整合はPipeline初期化時にエラーになります。

- 同名Resourceの型がStage間で異なる
- 同名Resourceのregisterまたはspaceが異なる
- 異なる名前が同じregister範囲へ重なる

Resource名は初期化時に`ShaderBindingHandle`へ解決できます。Draw中は文字列検索せず、軽量なindexを利用する方針です。

### RootSignature

`RootSignature`は`PipelineLayout`をDirectX 12 RootSignatureへ変換します。

従来型Reflection Bindingは次の最大2 Tableへ集約します。

```text
Root Table 0: CBV / SRV / UAV
Root Table 1: Sampler
```

ResourceごとにRoot parameterを一つ作る方式は採用していません。Root parameter番号は`RootSignature`内部だけが保持し、Rendererへ公開しません。

Bindless用にはRootSignature 1.1の次のフラグを有効にします。

```text
D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED
```

これによりSM 6.6 Shaderは`ResourceDescriptorHeap`と`SamplerDescriptorHeap`を直接indexできます。

### GraphicsPipeline

`GraphicsPipeline`は一つの描画Pipelineとして次を所有します。

- Vertex Shader
- Pixel Shader
- PipelineLayout
- RootSignature
- Graphics Pipeline State

Vertex input semanticはReflectionと照合します。ただしCPU Vertex構造のstride、offset、Buffer slot、Instance分類はReflectionだけでは安全に決められないため、`VertexAttribute`として明示します。

### DescriptorAllocator

`DescriptorAllocator`は単一Heap種別について次を担当します。

- Descriptor Heap生成
- 連続slot確保
- slot解放
- CPU/GPU Handle計算
- 容量管理

Texture、Material、Pipeline、GPU Resource本体は管理しません。

### DescriptorManager

`DescriptorManager`はGraphics backend全体で共有するCBV/SRV/UAV、RTV、DSV、Sampler Heapを所有します。Resource/Samplerはshader-visible、RTV/DSVはCPU-onlyです。ゲーム描画とImGuiは同じCBV/SRV/UAV Heapを共有します。

### BindlessDescriptorTable

`BindlessDescriptorTable`はDescriptorManagerの二つのshader-visible Heap上で、Bindless参照の世代とFence retireを管理します。

```text
BindlessDescriptorTable
  ├─ CBV / SRV / UAV Heap
  ├─ Sampler Heap
  ├─ Slot generation
  └─ Fence retire queue
```

index 0はnull Descriptorとして予約しています。未設定ResourceのShader indexには0を利用できます。

CPU側は`ShaderResourceRef`を保持します。

```cpp
struct ShaderResourceRef {
    uint32_t index;
    uint32_t generation;
    ShaderResourceClass resourceClass;
};
```

- `index`: Shaderへ渡すDescriptor Heap index
- `generation`: CPU側で古い参照を検出する世代番号
- `resourceClass`: Resource HeapかSampler Heapかを識別

CPU/GPU Descriptor HandleをMaterialやShaderデータへ直接保存しません。

## 5. 初期化フロー

```text
Applicationが任意のGraphicsRenderExtensionをFrameworkDescへ設定
  ↓
NexusFramework::Initialize
  ↓
ProjectWindow::Initialize
  ↓
GraphicsSystem::Initialize
  ↓
GraphicsDevice
  ├─ Debug Layer
  ├─ DRED
  ├─ DXGI Factory
  ├─ Adapter
  ├─ Device
  └─ Shader Model 6.6確認
  ↓
CommandQueue + Fence
  ↓
DescriptorManager × 4 Heap
  ↓
SwapChain + BackBuffer + RTV
  ↓
FrameContext × BackBuffer数
  ↓
BindlessDescriptorTable
  ├─ Resource Heap
  ├─ Sampler Heap
  └─ null slot
  ↓
PrimitiveRenderer
  ├─ Shader Compile
  ├─ Shader Reflection
  ├─ PipelineLayout
  ├─ RootSignature 1.1
  ├─ PSO
  └─ VertexBuffer
  ↓
任意のGraphicsRenderExtensionを接続
```

全要素が成功するまで`GraphicsSystem::Impl`はローカル所有されます。途中で失敗した場合はRAIIによって部分初期化済みResourceを破棄し、不完全なGraphicsSystemを公開しません。

## 6. 1フレームの描画フロー

```text
現在のBackBuffer indexを取得
  ↓
対応FrameContextのFenceを待機
  ↓
完了FenceまでのBindless Descriptorを回収
  ↓
CommandAllocator / CommandListをReset
  ↓
BackBuffer: PRESENT → RENDER_TARGET
  ↓
RTV設定・Clear
  ↓
Bindless Resource/Sampler Heapを設定
  ↓
Viewport / Scissor設定
  ↓
IGraphicsRenderer::Render(GraphicsContext&)
  ├─ RootSignature / PSO Bind
  ├─ VertexBuffer Bind
  ├─ Triangle List設定
  └─ DrawInstanced
  ↓
任意のGraphicsRenderExtension::BeginFrame / Record
  ├─ ImGui_ImplDX12_NewFrame
  ├─ ImGui_ImplWin32_NewFrame
  ├─ DockSpace / DemoWindow
  └─ ImGui_ImplDX12_RenderDrawData
  ↓
BackBuffer: RENDER_TARGET → PRESENT
  ↓
CommandList Close / Execute
  ↓
Fence Signal / FrameContextへFence値を保存
  ↓
Present
```

ResourceBarrierはDirectX 12 Backend内部に留まり、Frameworkや将来のScene Rendererへ公開しません。

`ProjectWindow`はDear ImGuiをincludeせず、汎用Message Handler経由でWin32 backendへイベントを渡します。InputSystemはまだ存在しないため、`io.WantCaptureMouse`と`io.WantCaptureKeyboard`によるゲーム入力抑止はInputSystem導入時の接続事項です。

## 7. Bindless Descriptorの寿命

DescriptorはGPUが使用中に同じindexへ別Resourceを割り当ててはいけません。そのため、解放要求を受けても即時再利用しません。

```text
ShaderResourceRefを使用
  ↓
最後のCommandをQueueへ投入
  ↓
Fence値を取得
  ↓
Retire(reference, fenceValue)
  ↓
CPU側では直ちに参照を無効化
  ↓
GPU Fence完了を待つ
  ↓
CollectGarbage(completedFenceValue)
  ↓
DescriptorAllocatorへslotを返却
  ↓
generationを更新
  ↓
同じindexを再利用可能
```

同じindexが再利用されてもgenerationが異なるため、古いCPU参照は`IsValid`で拒否されます。Shaderへgenerationは渡さず、CPU側の整合性確認に限定します。

## 8. Bindless Shaderの書き方

Bindless helperは`Shaders/Common/Bindless.hlsli`にあります。

```hlsl
#include "../Common/Bindless.hlsli"

float4 SampleMaterialTexture(
    uint textureIndex,
    uint samplerIndex,
    float2 texcoord)
{
    Texture2D<float4> texture =
        NexusGetTexture2D(textureIndex);

    SamplerState textureSampler =
        NexusGetSampler(samplerIndex);

    return texture.Sample(textureSampler, texcoord);
}
```

CPU側の`ShaderResourceRef.index`は現在、frame slice付きMaterial Constant Bufferを通してShaderへ渡します。
大量のObject/Materialを描画する段階では、Draw数と更新頻度を計測してStructured BufferまたはRoot Constantとの役割分担を決定します。

現在は1x1検証TextureとMaterial Draw Dataまで接続済みです。画像ファイルをdecodeするTexture Loader、
複数MaterialのAsset管理、Model Loaderは未実装です。

## 9. Shader追加フロー

Resourceを使用しないShaderは次の手順で追加できます。

1. `Shaders/`以下へHLSLを追加する
2. entry pointと`vs_6_6`または`ps_6_6`を指定する
3. 対応RendererでShaderを初期化する
4. CPU Vertex構造に対応する`VertexAttribute`を定義する
5. `GraphicsPipeline`を生成する

Resource付きShaderでは追加で次を行います。

1. Texture、Buffer、Samplerから`ShaderResourceRef`を取得する
2. Resource名を必要とする従来Bindingは初期化時に`ShaderBindingHandle`へ解決する
3. Bindless Resourceは`ShaderResourceRef.index`をDraw Dataへ格納する
4. Shaderから`Bindless.hlsli`を通して参照する

Shader追加ごとにRoot parameter番号、Descriptor Range、Renderer内のRoot indexを手作業で追加する必要はありません。

## 10. Resizeフロー

WindowはGraphicsを参照しません。

```text
WM_SIZE
  ↓
ProjectWindowがWindowResizeEventを保存
  ↓
NexusFrameworkがイベントを取得
  ↓
GraphicsSystem::Resize
  ↓
CommandQueue::WaitForIdle
  ↓
BackBuffer参照を解放
  ↓
ResizeBuffers
  ↓
BackBufferとRTVを再生成
```

最小化時の0サイズはSwapChainへ渡しません。

## 11. 終了と破棄順序

終了時は最初にGPU完了を保証します。

```text
CommandQueue::WaitForIdle
  ↓
Frame CommandList / Allocator
  ↓
PrimitiveRenderer
  ├─ VertexBuffer
  └─ Pipeline / RootSignature
  ↓
BindlessDescriptorTable
  ↓
SwapChain / BackBuffer / RTV
  ↓
DescriptorManager
  ↓
CommandQueue / Fence
  ↓
GraphicsDevice
  ↓
ProjectWindow
```

COM Objectは`Microsoft::WRL::ComPtr`で所有し、raw pointerは非所有参照としてだけ利用します。

## 12. PrimitiveRendererの現在位置

基盤検証用の`PrimitiveRenderer`は`Application`が所有し、`IGraphicsRenderer`として
`GraphicsSystem`へ非所有接続します。`GraphicsSystem`はRendererの初期化、Frame記録、終了順序だけを
制御し、具象Rendererや形状データを所有しません。

これはClear、Shader、Pipeline、Vertex Buffer、Draw、Presentの経路を検証する第一段階の構造です。
正式なScene Renderer構造として、今後`ModelRenderer`や`SpriteRenderer`を
`GraphicsSystem::Impl`へ追加することは想定していません。

RendererのFrame記録にはDX非依存の`GraphicsContext`を渡します。現在の最小契約は次の操作です。

- Graphics Pipeline設定
- Vertex Buffer設定
- Primitive topology設定
- 非Index Draw

GPU Resource生成は`GraphicsResourceFactory`を経由します。これによりRenderer初期化時にも
`ID3D12Device`を渡しません。既存の`GraphicsPipeline`と`VertexBuffer`内部はDX12実装ですが、
その生成に必要なNative DeviceはFactory実装内へ留めています。

Dear ImGuiは公式DX12 backendがNative Device、Queue、Command Listを必要とするため、
通常Rendererとは別の`IGraphicsRenderExtension`経路へ限定します。このnative拡張境界を
Scene RendererやMaterial Rendererへ使用してはいけません。

将来の目標構造は次のとおりです。

```text
Application（Composition Root）
  ├─ NexusFramework
  │    └─ GraphicsSystem
  └─ Renderer / GraphicsRenderExtension
       ├─ PrimitiveRenderer
       ├─ ModelRenderer
       └─ SpriteRenderer
            ↓
       DX非依存GraphicsContext
            ↓
       GraphicsSystem / DirectX12 Backend
```

FrameworkがRendererを接続する場合も、Frameworkの責務は`Initialize / Render / Shutdown`の
ライフサイクル管理に限定します。所有権はApplicationに残し、Shader Compile、Material Bind、
Draw詳細をFrameworkへ追加してはいけません。

`GraphicsContext`は現在必要な命令だけを持つ小さなCommand Encoder境界として導入しています。
Texture、Index Buffer、Constant Bufferなどは実際の利用側を実装するときに追加し、
将来利用を理由に巨大なRHI Interfaceを先行実装しない方針です。

## 13. 現在実装しないもの

次の機能は責務と利用方法が確定してから追加します。

- Material System完成版
- Texture Loader
- Model Loader
- Default Heapへの非同期Upload
- Bindless Heap自動拡張
- Shader Hot Reload
- Shader/Pipeline Cache
- RenderGraph
- Vulkan Backend
- GPU Driven Rendering

## 14. 今後の推奨実装順序

1. ~~DX非依存のGraphicsContext境界を定義する~~（完了）
2. ~~RendererをGraphicsSystemの所有から分離する~~（完了）
3. ~~ConstantBufferと共通Alignment Utilityを追加する~~（完了）
4. ~~Texture ResourceとUpload処理を追加する~~（完了）
5. ~~Texture/Samplerから`ShaderResourceRef`を生成する~~（完了）
6. ~~Material Draw DataへBindless indexを格納する~~（完了）
7. ~~Default HeapとUpload stagingへVertex/Texture転送を移行する~~（完了）
8. ~~Persistent DescriptorとTransient Descriptorを明確に分離する~~（完了）

この順序により、現在のDirectX 12 Backend境界を保ちながら、Material、Texture、Model Rendererへ段階的に拡張できます。

### Constant Bufferの現在の規約

`ConstantBuffer`はCPU更新用Upload Resourceを所有し、BackBufferごとに独立したsliceを確保します。
各sliceはDirectX 12のCBV要件に合わせて256byte境界へ整列します。

```text
ConstantBuffer Upload Resource
  ├─ Frame 0 slice（256byte alignment）
  └─ Frame 1 slice（256byte alignment）
```

CPUはGPUが利用を終えた現在のFrameContextに対応するsliceだけを更新します。これにより、
GPUが前Frameの定数を読み取っている間に同じmemoryを上書きしません。

共通のoverflow検査付き切り上げ処理は`Foundation/Memory/Alignment.h`の`TryAlignUp`を使用します。
Rendererは`GraphicsResourceFactory::CreateConstantBuffer`から生成するため、Native Deviceと
FrameContext数を知る必要がありません。

現在の`ConstantBuffer`はGPU Resourceとmappingだけを所有します。描画用CBVは
`TransientDescriptorArena`がFrame中に生成し、対応Fence完了後にまとめて再利用します。
Descriptor寿命をBuffer本体へ暗黙に混在させない方針です。

### Texture Uploadの現在の規約

`TextureResource`はupload完了済みのDefault Heap Textureだけを所有します。ファイル形式のdecode、
Upload staging、Descriptor、Sampler、Materialは所有しません。公開記述には`TextureDesc`と
`TextureFormat`を使用し、DXGI formatや`ID3D12Resource`を上位へ公開しません。

```text
Asset Loader（将来）
  ↓ decoded tightly-packed RGBA pixels
GraphicsResourceFactory::CreateTexture2D
  ↓
TextureUploader
  ├─ Default Heap Texture（COPY_DEST）
  ├─ Upload Buffer + copy footprint
  ├─ 一時CommandAllocator / CommandList
  ├─ CopyTextureRegion
  ├─ PIXEL_SHADER_RESOURCEへBarrier
  └─ Queue Signal / Fence Wait
       ↓
TextureResourceへ完成Resourceをcommit
```

現在はRenderer初期化時の同期uploadです。一時Upload ResourceとCommand資源をFence完了まで保持するため、
GPU参照中にstaging memoryを破棄しません。非同期Asset Streamingが必要になった時点で、
`TextureUploader`の完了待機をupload request／retire queueへ拡張します。

第一段階は1 mip、1 array slice、RGBA8 UNORMまたはsRGBを対象とします。Mip生成や圧縮formatを
利用側が存在する前に抽象化へ追加しない方針です。

### Texture/Sampler Bindless参照の規約

完成済み`TextureResource`は`GraphicsResourceFactory::CreatePersistentTextureShaderResource`によって
CBV/SRV/UAV Heap上のSRVへ変換します。SamplerはBackend非依存の`SamplerDesc`から
Sampler Heap上へ生成します。どちらも結果は同じ`ShaderResourceRef`ですが、
`resourceClass`によってHeap分類を保持します。

```text
TextureResource ─ CreatePersistentTextureShaderResource ─┐
                                               ├─ ShaderResourceRef
SamplerDesc ───── CreatePersistentSampler ────────────────┘
                                                      │
                              index      → Shaderへ渡す
                              generation → CPUで古い参照を検出
                              class      → Resource/Sampler Heapを識別
```

TextureやMaterialへCPU/GPU Descriptor Handleを保存してはいけません。Shaderへ渡すのは
`ShaderResourceRef.index`だけです。CPU側で利用する前にはFactoryの
`IsPersistentShaderResourceValid`でgenerationを含めて検証できます。

解放は`RetirePersistentShaderResource`を使用します。この関数はDirect Queue末尾へFenceをSignalし、
参照を直ちにCPU側で無効化します。Descriptor slotはFence完了後の`CollectGarbage`まで
Allocatorへ返さないため、GPUが古いindexを参照中に別Resourceへ再割当されません。

Retireを呼ぶ前に、MaterialやDraw Dataから対象参照を除去し、以降のCommandへ記録されないことを
呼び出し側が保証します。Texture Resource本体も、参照を使用したGPU処理の完了前に破棄してはいけません。

### Material Draw Dataの現在の規約

`MaterialDrawData`はGPU Constant Bufferへ転送する32byteの値型です。TextureとSamplerは
`ShaderResourceRef.index`だけを保持し、generationやDescriptor HandleをShaderへ渡しません。

```text
MaterialDrawData（CPU / HLSL共通layout）
  ├─ uint textureIndex
  ├─ uint samplerIndex
  ├─ float2 padding
  └─ float4 tint
```

`PrimitiveRenderer`は1x1 white Textureを実際にDefault Heapへuploadし、Texture SRV、Sampler、
frame別Material Constant Bufferを生成します。描画時は現在の`GraphicsContext::GetFrameIndex`に
対応するsliceだけを更新し、一時CBVをPipelineのresource descriptor tableへ設定します。

Pixel ShaderはCBVからTexture/Sampler indexを読み、`ResourceDescriptorHeap`と
`SamplerDescriptorHeap`を直接indexしてsampleします。これにより次の経路が実際の描画で接続されます。

```text
Texture upload
  ↓
ShaderResourceRef
  ↓ index
MaterialDrawData
  ↓ frame-local Constant Buffer / CBV
Pixel Shader
  ↓ Direct Heap Indexing
Texture sample
```

`GraphicsContext`はRoot parameter番号やGPU Descriptor Handleを公開せず、Pipelineと
世代検証済み`ShaderResourceRef`からdescriptor tableを設定します。

### Persistent / Transient Descriptorの規約

TextureやSamplerのように複数Frameをまたいで同じindexを参照するDescriptorは、
`BindlessDescriptorTable`がgenerationとretire Fenceを持つPersistent領域へ配置します。
一方、Material CBVのようにDraw時に現在Frameの内容を指せばよいDescriptorは、
`TransientDescriptorArena`のFrame専用領域へ順番に配置します。

```text
Shader-visible Resource Heap
  ├─ Persistent領域
  │    └─ Texture SRVなど（個別allocate / generation / Fence retire）
  └─ Transient Frame領域
       ├─ Frame 0 bump arena ─ Fence 0完了後に一括Reset
       └─ Frame 1 bump arena ─ Fence 1完了後に一括Reset
```

この分離により、Drawごとに変化するCBVが永続slotとgeneration管理を消費せず、個別Freeも不要になります。
また、FrameContext、ConstantBuffer slice、Descriptor Arenaを同じindexとFence寿命へ揃えるため、
GPU参照中のDescriptor上書きを構造的に防げます。Arenaの所有とResetは`GraphicsSystem`、
描画側への公開は`GraphicsContext::SetGraphicsConstantBufferTable`が担当し、Rendererへ
DirectX 12 HandleやFenceを公開しません。

現在は必要性が確認できたCBVのみをTransient生成対象としています。Transient SRV/UAVや連続table構築は、
Model/Material描画で実際のbinding要件が決まった段階でArenaへ追加します。

### Default Heapへ静的Resourceを配置する理由

静的VertexとTextureは、初期転送後にCPUから継続更新しないためDefault Heapへ配置します。
Upload HeapはCPU書き込みに最適化されたCPU可視memoryであり、GPUが毎Drawで頂点を読む
恒久的な保管先には適していません。

Default Heapへ移行するメリットは次のとおりです。

- GPUローカルmemoryに配置されやすく、継続的なvertex fetchとtexture sampleに適する
- 転送完了後にUpload stagingを破棄でき、CPU可視memoryを静的Assetごとに保持しない
- Resource本体からMap、CPU copy、Command記録、Fence同期の責務を除去できる
- TextureとVertexで同じUpload寿命規約を使用できる
- 将来のModel Loader、非同期Asset streaming、Copy Queue化へ拡張しやすい

一方、生成時にstaging Resource、copy command、Resource Barrier、Fence同期が必要になり、
小さなResourceの初期化コストは増えます。毎Frame CPU更新するConstant Bufferはこの方式へ移さず、
frame slice付きUpload Heapへ残します。Resourceの更新頻度に応じてHeapを選択する方針です。

`ImmediateUploadContext`は同期Upload用の一時CommandAllocator／CommandList生成、Close、Execute、
Signal、Waitだけを共通化します。copy内容とResource Barrierは`TextureUploader`または
`VertexBufferUploader`が決定し、Resource固有知識を共通Contextへ集めません。

```text
CPU Asset Data
  ↓ Upload staging（Upload Heap）
Copy command
  ↓
Static Resource（Default Heap）
  ↓ Resource固有の利用状態へBarrier
Fence Wait
  ↓
Upload stagingを破棄
```
