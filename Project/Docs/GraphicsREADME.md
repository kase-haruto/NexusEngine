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
  └─ NexusFramework
       ├─ ProjectWindow
       │    └─ Win32
       └─ GraphicsSystem
            └─ DirectX 12実装（PImpl）
                 ├─ GraphicsDevice
                 │    ├─ GraphicsDebugConfigurator
                 │    └─ GraphicsAdapterSelector
                 ├─ CommandQueue
                 ├─ SwapChain
                 ├─ FrameContext × BackBuffer数
                 ├─ BindlessDescriptorTable
                 │    └─ DescriptorAllocator × Heap種別
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
- BackBuffer専用RTV Heap

RTV Heapは現段階ではSwapChain専用です。RTVを汎用Descriptor Systemへ統合しても用途が増えないため、共通Allocatorは導入していません。

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

### BindlessDescriptorTable

`BindlessDescriptorTable`はBindless Resource参照に必要な二つのShader-visible Heapを管理します。

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
PrimitiveRenderer::Draw
  ├─ RootSignature / PSO Bind
  ├─ VertexBuffer Bind
  ├─ Triangle List設定
  └─ DrawInstanced
  ↓
BackBuffer: RENDER_TARGET → PRESENT
  ↓
CommandList Close / Execute
  ↓
Present
  ↓
Fence Signal
  ↓
FrameContextへFence値を保存
```

ResourceBarrierはDirectX 12 Backend内部に留まり、Frameworkや将来のScene Rendererへ公開しません。

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

CPU側の`ShaderResourceRef.index`をConstantBuffer、StructuredBuffer、Root Constantなどを通してShaderへ渡します。どの転送方式を標準とするかは、MaterialとDraw Dataの設計時に決定します。

現在はTexture LoaderとMaterial Bufferが未実装なので、Bindless indexの生成・安全な寿命・Shaderアクセス規約までが実装範囲です。

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
CommandQueue / Fence
  ↓
GraphicsDevice
  ↓
ProjectWindow
```

COM Objectは`Microsoft::WRL::ComPtr`で所有し、raw pointerは非所有参照としてだけ利用します。

## 12. PrimitiveRendererの現在位置

現在、`GraphicsSystem::Impl`は基盤検証用の`PrimitiveRenderer`を所有しています。

これはClear、Shader、Pipeline、Vertex Buffer、Draw、Presentの経路を検証する第一段階の構造です。正式なScene Renderer構造として、今後`ModelRenderer`や`SpriteRenderer`をすべて`GraphicsSystem`へ追加することは想定していません。

将来の目標構造は次のとおりです。

```text
NexusFramework（Composition Root）
  ├─ GraphicsSystem
  └─ Renderer
       ├─ PrimitiveRenderer
       ├─ ModelRenderer
       └─ SpriteRenderer
            ↓
       DX非依存GraphicsContext
            ↓
       GraphicsSystem / DirectX12 Backend
```

FrameworkがRendererを所有する場合も、Frameworkの責務は`Initialize / Render / Shutdown`のライフサイクル管理に限定します。Shader Compile、Material Bind、Draw詳細をFrameworkへ追加してはいけません。

RendererをGraphicsSystemから分離するには、`ID3D12GraphicsCommandList`を直接渡すのではなく、DX非依存の`GraphicsContext`またはCommand Encoder境界が必要です。この境界が確定する前に大規模なRHI Interfaceを先行実装しない方針です。

## 13. 現在実装しないもの

次の機能は責務と利用方法が確定してから追加します。

- Material System完成版
- Texture Loader
- Model Loader
- Default Heapへの非同期Upload
- Transient Descriptor Arena
- Bindless Heap自動拡張
- Shader Hot Reload
- Shader/Pipeline Cache
- RenderGraph
- Vulkan Backend
- GPU Driven Rendering

## 14. 今後の推奨実装順序

1. DX非依存のGraphicsContext境界を定義する
2. RendererをGraphicsSystemの所有から分離する
3. ConstantBufferと共通Alignment Utilityを追加する
4. Texture ResourceとUpload処理を追加する
5. Texture/Samplerから`ShaderResourceRef`を生成する
6. Material Draw DataへBindless indexを格納する
7. Default HeapとUpload stagingへVertex/Texture転送を移行する
8. Persistent DescriptorとTransient Descriptorを明確に分離する

この順序により、現在のDirectX 12 Backend境界を保ちながら、Material、Texture、Model Rendererへ段階的に拡張できます。
