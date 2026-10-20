# Mesh Shader / Meshlet Rendering

## Mesh Shader とは

従来描画は IA が Vertex / Index Buffer を読み、VS が頂点を変換して Rasterizer に渡す。
Mesh Shader 経路は AS が可視 Meshlet を選び、MS が頂点と三角形の両方を生成する。
IA / VS は使用せず、Rasterizer と PS、Color / Depth Target は従来と共有する。

実装の API 制限は [Microsoft Mesh Shader specification](https://microsoft.github.io/DirectX-Specs/d3d/MeshShader.html)
に基づく。SM6.6 は既存エンジンの Bindless Heap Indexing の必須条件であり、今回だけの新しい GPU 要件ではない。

## Meshlet とは

Meshlet は独立して描画・Cull できる小さな三角形集合。本実装は最大64頂点、126三角形。
局所頂点から元頂点への remap と、局所頂点番号で構成した三角形を保持する。
大きな Mesh 全体を描く前に不可視の集合を除外できる。

## 既存構造と Engine Architecture

```text
ModelLoader -> ModelAssetData / MeshAssetData (CPU bytes / Submesh / Skin)
                         |
             GraphicsResourceFactory
                         |
              ModelResource -> MeshResource (Vertex / Index Buffer)

MeshAssetData -> MeshletBuilder -> MeshletData
                                    |
                        GraphicsResourceFactory
                                    |
                          MeshletResource (Default Heap)

Editor -> MeshletDebugSettings <- PrimitiveRenderer
                                    |
                             GraphicsContext
                                    |
                       DX12 MeshShaderPipeline Backend
```

`GraphicsSystem` は Device、Queue、FrameContext、Descriptor Heap、Frame Fence を所有する。
`ModelResource` は複数の `MeshResource`、Material metadata と hierarchy を所有する。
`ModelInstance` は CPU 側の姿勢を提供する。Camera は `RenderScene::camera` に抽出済みの値を使用する。
Runtime / Graphics に Scene の CameraComponent や ImGui の依存を追加していない。

| ファイル | 責務 |
|---|---|
| Graphics/Meshlet/MeshletData.h | API 非依存 metadata、remap、packed primitives、Sphere、Submesh range |
| Graphics/Meshlet/MeshletBuilder.h / .cpp | CPU 分割、入力検証、Bounds 生成 |
| Graphics/Meshlet/MeshletResource.h | GPU 所有者の非 Native インターフェース |
| Graphics/Backend/DirectX12/Meshlet/MeshletResource.cpp | 4 Buffer の同期 Upload と寿命管理 |
| Graphics/Meshlet/MeshShaderPipeline.h | AS/MS/PS Pipeline と描画向け MeshCullingView |
| Graphics/Backend/DirectX12/Meshlet/MeshShaderPipeline.cpp | DX12 PSO stream、Root Signature、DispatchMesh |
| Graphics/Meshlet/MeshletDebugSettings.h | Editor が操作する設定値 |
| Shaders/Mesh/MeshletConfig.h | C++ / HLSL 共通の上限定義 |
| Graphics/Meshlet/MeshletConfig.h | C++ 側から共通設定への include |
| Shaders/Mesh/MeshletCommon.hlsli | Buffer layout、binding、Payload、Sphere 判定 |
| Shaders/Mesh/MeshletAS.hlsl | Frustum 判定、可視 ID の group 内集約 |
| Shaders/Mesh/MeshletMS.hlsl | remap / primitive 展開、頂点変換、三角形出力 |
| Shaders/Mesh/MeshletPS.hlsl | 既存 Pixel 処理共有、Debug 色表示 |

既存変更は Device capabilities、ShaderStage、Factory の生成 API、Context の Dispatch API、
PrimitiveRenderer の経路選択、Editor UI とプロジェクトのファイル登録に限定した。
Graphics Tests プロジェクトも同じ Factory / Context / Renderer を link するため新規 cpp を登録した。
既存 ShaderCompiler は任意 target profile を DXC へ渡せるので変更不要。
既存 ShaderReflector は AS/MS の resource reflection に利用でき、Vertex input 解析は VS のみなので変更不要。

## Meshlet 生成

DirectXMesh は導入されていないため、新規外部依存を追加せず入力三角形の順序を維持する greedy 分割を採用した。
Submesh ごとに新しい集合を開始し、頂点重複を除去して、頂点数または三角形数の上限で flush する。
UInt16 / UInt32 と signed base vertex に対応。退化三角形も元のまま保持する。
Material 境界、triangle winding、元頂点の意味を維持する。

各 Meshlet は `vertexOffset / vertexCount / primitiveOffset / primitiveCount` の16bytes。
Primitive は3つの局所 index を uint32 の下位24bitへ pack する。
Sphere は AABB 中心と全参照頂点までの最大距離から生成し、半径を上向きに丸める。
有限位置、index 範囲、triangle list、32bit offset を検証し、不正入力は GPU へ送らない。

生成はモデル GPU 初期化時だけ。CPU の結果は Upload 後に破棄でき、毎Frame の再構築はない。
空の全 Mesh、独自 Vertex layout、Skinning Mesh は Meshlet 対象外として従来描画へ戻る。
入力順分割は品質最適化を目的としたものではない。将来 DirectXMesh / meshoptimizer、
Import 時生成やキャッシュへ置換しても、出力を `MeshletData` に変換すれば描画側は維持できる。
ライブラリ固有型を Resource / Renderer に露出させる必要はない。

## Rendering Flow

```text
Mesh Resource + Meshlet Resource
             |
GraphicsContext::DispatchMeshlets (Submesh 単位)
             |
ID3D12GraphicsCommandList6::DispatchMesh
             |
Amplification Shader (32 candidates / group)
             |
Sphere + Frustum Culling
             |
Visible Meshlet Payload
             |
Mesh Shader (1 visible Meshlet / group)
             |
Rasterizer -> Pixel Shader -> Color / Depth
```

AS は32候補を読み、groupshared counter の atomic increment により可視 ID を compact する。
Culling 無効時の Normal / Meshlet Color では Bounds 読込と平面判定を省略する。
Group barrier の後に全 thread が一様に `DispatchMesh` を1回呼ぶ。
可視数0では MS を起動しない。末尾の余剰 thread は Buffer を読まない。
Wave32 / Wave64 の違いに依存する Wave prefix sum は使用しない。
Payload は ID と可視判定を各32個、合計256bytes。上限16KBより小さい。
atomic による slot 順序は保証しないが、Debug 色は元 ID を使用するので固定される。

MS は Payload slot を元 Meshlet ID に戻し、`SetMeshOutputCounts` を
出力配列への書き込み前に一様な制御フローで1回呼ぶ。
remap から元頂点を読み、最大64頂点を変換する。最大126三角形は64 thread の stride loop で出力する。
各三角形の `uint3` を一括で書き、局所 index と元 index を混同しない。
巨大 Mesh は CPU が AS Dispatch の X 上限65535に合わせてバッチを分ける。可視性選択は全て GPU 側。

## Culling

既存 RenderCamera の ViewProjection と描画 Object / Node の World から LocalToClip を得る。
row-vector 規約に従い行列の列から以下の clip 半空間を抽出する。

```text
left/right: w + x >= 0 / w - x >= 0
bottom/top: w + y >= 0 / w - y >= 0
near/far:   z >= 0     / w - z >= 0    (D3D depth 0..1)
```

これを local 空間の正規化平面として AS に渡す。
AS は `dot(plane, float4(center,1)) < -radius - tolerance` なら外側と判断する。
平面を local に引き戻すため、Sphere の world 半径を scale から近似しない。
非一様 scale / shear でも保守的な Sphere 判定になる。不正な平面は Cull しない。
CPU は6平面を Draw ごとに準備するだけで、Meshlet ごとの可視性を走査しない。
Normal Cone は今回生成・判定しない。

## GPU Resource / Binding

| register | 種別 / stride | 所有者 |
|---|---|---|
| b0 | Root CBV、PrimitiveDrawData、4720bytes | 既存 Renderer の Draw ごとの frame-sliced ConstantBuffer |
| b1 | Root constants、6 float4 planes + 4 uint、112bytes / 28 DWORD | Command 内にコピー |
| t0 | Root SRV、StructuredBuffer MeshVertex、80bytes | 元 MeshResource の既存 VertexBuffer |
| t1 | Root SRV、StructuredBuffer Meshlet、16bytes | MeshletResource |
| t2 | Root SRV、StructuredBuffer uint、4bytes | MeshletResource、vertex remap |
| t3 | Root SRV、StructuredBuffer uint、4bytes | MeshletResource、packed primitives |
| t4 | Root SRV、StructuredBuffer MeshletBounds、16bytes | MeshletResource |
| Material Texture / Sampler | ResourceDescriptorHeap / SamplerDescriptorHeap | 既存 BindlessDescriptorTable / Renderer |

Geometry は Root SRV のため Descriptor allocation が不要。Root parameter 番号は DX12 Backend 内の enum に集約する。
Debug mode の番号も MeshletConfig.h を C++ / HLSL で共有し、別々の literal に依存しない。
Root cost は40 DWORD、64 DWORD 制限以内。Mesh Pipeline の Root Signature は IA flag を使用しない。
起動時 Reflection は register space / register 範囲 / Resource 型 / CB size を検証する。
C++ 側の static_assert で Meshlet、Sphere、ModelVertex のサイズと offset、CullingConstants のサイズを固定する。
行列は HLSL `row_major` と Engine の row-vector 規約を一致させる。
PS は既存 Primitive.PS.hlsl の Material / Light / Shadow / Optical 処理を呼び、Debug 時だけ色へ置き換える。

## Resource Lifetime

- `GraphicsResourceFactory` が Backend Device / Queue を使って生成し、PrimitiveRenderer が Pipeline と MeshletResource を unique ownership で保持する。
- 元 Vertex / Index Buffer は ModelResource 内の MeshResource が所有する。MeshletResource は頂点 Buffer を重複生成しない。
- 4 Buffer は Default Heap。1つの初期化 Upload List で転送し、NON_PIXEL_SHADER_RESOURCE に遷移する。
- 成功時は Fence 完了後に staging / Upload List / Allocator を解放する。
- Upload の Signal / Wait が失敗した場合は描画用世代を0にして、staging / destination / List / Allocator を Shutdown まで保持する。
- Factory の Upload エラーで Resource が内部保留状態を持つことがある。呼び出し側はエラーでも生成先を GPU idle 後まで生存させる。本 Renderer は失敗した Resource も保持する。
- Draw 定数は既存の Object / Mesh / Submesh ごとの Frame slice を使用する。Root constants は Command 自体にコピーされる。
- Geometry generation が元 Mesh と一致しないと Meshlet 描画を拒否する。再 Upload 時は GPU idle を保証し、Meshlet も再構築する。
- CommandList6 の QueryInterface は Context ごとに最大1回。参照は Context 破棄時に Release し、Native 型を公開しない。
- GraphicsSystem は GPU idle 後に Renderer::Shutdown を呼ぶ。Meshlet を元 Vertex Buffer より先に破棄する。
- 公開 Shutdown / destructor は GPU 同期を代行しない。独立利用時も呼び出し側が GPU idle と Device の生存を保証する。
- Texture / Sampler Descriptor の所有・retire は既存の Bindless 規約を継続する。

## Debug Visualization

Editor の `Mesh Shader` ウィンドウから操作する。既定は `Use Mesh Shader = false`。

| 設定 | 表示 |
|---|---|
| Use Mesh Shader | 対象静的 Mesh の AS/MS 経路を有効にする |
| Enable Meshlet Culling | 通常描画で GPU Frustum Culling を有効にする |
| Normal | 既存 Material / Lighting を共有 |
| Meshlet Color | 元 Meshlet ID の整数 hash から固定 RGB を生成 |
| Culling Result | 全候補を送り、可視 Sphere は緑、外側 Sphere は赤 |

色は frame や可視 payload の slot 順序に依存しない。ID は各 Mesh Resource 内で一意。
Culling Result は判定の確認用に Cull をバイパスする特別な Debug モード。
Rasterizer の clipping は継続するので、完全に画面外の Sphere の Meshlet は赤くても見えない。
大きな Sphere による保守的な false positive は許容する。表示は GPU の判定結果であり Readback はしない。

既存の汎用 Sphere / Line Debug Renderer がないため、Bounds の線画表示は未実装。
新しい Debug 描画基盤は作っていない。将来その基盤を追加した時は Builder の Sphere を用いて拡張できる。
Editor は設定値を書き換えるだけで、Runtime / Graphics は ImGui に依存しない。

## Existing Pipeline / Unsupported GPU

従来 GraphicsPipeline / RootSignature / IA / VS / DrawIndexed を保持する。
Mesh Shader Tier は GraphicsDevice 内の OPTIONS7 で照会し、Device2 も確認する。
上位には `MeshShaderSupport::Unsupported / Tier1` と `SupportsMeshShaders()` だけを公開する。
非対応 GPU では AS/MS を compile せず、Meshlet Upload / PSO 作成を行わず、従来描画を継続する。
対応 GPU でも Shader / PSO 作成失敗はログへ出し、従来描画を継続する。
Editor は Pipeline 利用不可なら操作を無効化する。
Device 全体の SM6.6 必須条件は従来から存在し、今回変更していない。

Skinning、独自 Vertex format、不正 Meshlet、再生成による世代不一致、CommandList6 非対応、
Mesh Shader 無効時も従来描画へ戻る。従来描画前に元 PSO / Root Signature を再設定する。
Shadow / Optical の別 Pass は従来方式を維持し、Main Color の静的 Mesh のみ新経路を使用する。

## 静的レビューと手動確認

実施した確認は include 解決、forward declaration、namespace、宣言 / 定義、
追加 cpp のプロジェクト登録、Root register / 型 / space、CPU/HLSL layout、
dispatch / payload / output 上限、GPU lifetime、従来経路復元の静的確認。
初回実装時は指示に従い静的確認のみ実施した。その後、ビルドとエラー修正の依頼を受け、
Debug / x64 の Engine ビルドと AS / MS / PS の DXC コンパイルを実施した。
PSO 作成、GPU 実行、画面比較は未確認。

ユーザーによる確認手順:

1. Tier 1 GPU で起動してログと UI の Pipeline ready を確認する。
2. Normal で Mesh Shader を切り替え、従来 Material / Depth / Shadow / Optical 表示と比較する。
3. Meshlet Color で subdivision と移動中の色の安定性を確認する。
4. Culling を on/off して Camera を動かし、可視 Geometry の欠落がないことを確認する。
5. Culling Result で境界付近を確認する。完全に Clip された Meshlet は表示されない。
6. 非一様 Scale / Shear と Skinning Mesh でそれぞれ Culling と従来経路を確認する。
7. 非対応 GPU で操作が無効になり、従来描画が継続することを確認する。
8. D3D12 Debug Layer / GPU validation で Buffer 境界、Root binding、Resource state を確認する。

### ビルド・Shader コンパイル確認（2026-10-09）

- `NexusEngine.vcxproj`、Debug / x64、Build 成功：警告0件、エラー0件。
- MeshletAS / MeshletMS / MeshletPS は Windows SDK 10.0.26100.0 の DXC で個別コンパイル成功。
- Shader profile は as_6_6 / ms_6_6 / ps_6_6、実行時 Debug 設定と同じ `-HV 2021 -Werror -Zi -Qembed_debug -Od`。
- MSVC C4324 が原因だった PSO stream subobject の pointer alignment は、自然 alignment を持つ union で表現するよう修正した。
  警告抑制は使用せず、alignment と size の static_assert も追加した。
- 個別 Shader 検証の生成物は Generated/MeshShaderValidation に配置し、配布 Shader や Source Asset は変更していない。
- 実機での Pipeline 作成、Culling、Debug 表示は引き続き上記の手動確認対象。

## 完了条件の静的監査

以下はソースとファイル登録を根拠にした実装確認であり、実機動作を確認した結果ではない。

| 完了条件 | 実装上の根拠 |
|---|---|
| Mesh Shader 対応 GPU 検出 | GraphicsDevice の OPTIONS7 / Device2 照会、GraphicsCapabilities の API 非依存 Tier |
| Mesh Shader Pipeline 作成基盤 | 専用 PSO stream の AS / MS / PS subobject、専用 Root Signature、HRESULT 検査 |
| 通常 Mesh から Meshlet 生成 | MeshletBuilder の Submesh ごとの分割、remap / packed triangle 生成 |
| GPU 転送 | MeshletResource の4 Default Buffer、初期化時の CopyBufferRegion と Fence 待機 |
| Meshlet 単位の描画 | GraphicsContext::DispatchMeshlets、Payload ID ごとの MS group |
| Amplification Shader | MeshletAS.hlsl の visibility selection と DispatchMesh |
| Meshlet ごとの Bounds | MeshletData の parallel bounds 配列、Builder の Sphere 生成 |
| GPU Frustum Culling | AS の Sphere / local-space Frustum 半空間テスト |
| 可視 Meshlet のみ送信 | 通常 Culling 有効時の条件付き Payload 集約と visibleCount Dispatch |
| 固定色 Debug | 元 Meshlet ID の hash、共通 Debug mode 定義 |
| Bounds 線画表示 | 任意項目。既存汎用 Debug Renderer がなく、今回は未実装 |
| Debug / Editor 分離 | MeshletDebugSettings の値、EditorApplication のみが ImGui を操作 |
| 従来描画との共存 | opt-in、Skinning 維持、Mesh -> Traditional 切替時の PSO / Root 復元 |
| 非対応 GPU の挙動 | AS/MS compile と Upload を省略、従来 DrawIndexed を継続、UI 操作無効 |
| Native API の境界 | OPTIONS7 は Device 実装、CommandList6 は Context / DX12 Backend 内のみ |
| Resource / Descriptor 所有 | unique_ptr / ComPtr、元頂点の再利用、Root SRV、既存 Bindless Material 所有 |
| Upload 完了前の寿命 | staging / destination / Allocator 保持、失敗 Resource 保留、公開 API の lifetime 契約 |
| README / コメント | 本書、クラス責務、header の関数契約、AS / Payload / MS / Culling の処理コメント |
| link 対象登録 | Engine と GraphicsTests の両 vcxproj に新規3 cpp を登録 |
| binding / layout | b0 / b1 / t0..t4、Reflection 検査、sizeof / offsetof static_assert |
| scope 維持 | Hi-Z / Occlusion / LOD / Skinning MS / Indirect を追加していない |

静的スクリプトで quoted include の解決、プロジェクト XML、新規 cpp の重複・登録漏れ、
共通設定の Thread / Payload / Output / Root cost 上限、HLSL register map を確認した。
既存 Camera と同じ row-vector 規約で、Frustum 平面の引き戻しが clip 条件に一致することを参照計算でも確認した。
インストール済み Windows SDK の d3d12.h で、使用する AS/MS subobject、Root constants、PSO stream、Tier 型の宣言を照合した。
C++ Build / HLSL compile は上記のとおり成功を確認した。
PSO 作成 / GPU 実行は、ビルド後の手動確認事項として上記手順に残している。

## Future Work

今回の実装範囲は Meshlet Rendering、AS Frustum Culling、Debug 表示まで。
Normal / Backface Cone は CPU Bounds と AS 判定を拡張する。
Hi-Z / Occlusion は AS 入力と独立 Pass を追加する。
LOD / Instance Culling は可視選択と Payload を拡張する。
Offline Import / Cache / Meshlet 品質改善は Builder の入出力境界で置き換える。
GPU Driven Rendering / ExecuteIndirect は別の描画スケジューリング層として検討する。
Mesh Shader Skinning、Animation、Clustered Rendering、Virtual Geometry は今回実装していない。
