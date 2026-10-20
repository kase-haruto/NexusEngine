# NexusEngine ECS / Level Architecture

## 目的

NexusEngineのECSは、Component単位の連続走査、世代付きHandleによる安全な参照、Level保存とEditor操作の両立を目的とする。既存の`GameObject`階層を一括置換する仕組みではなく、データ指向処理が有効な領域から段階的に移行するRuntime基盤である。

参考資料「SOL-AVESの高性能なランタイムを構成するアーキテクチャ」は、64KB Segmentを使うSoA Archetype、即時APIとEntity Command Buffer、RO/RW宣言から依存関係を作るUpdaterGraph、構造変更時のredirect tableを説明している。本実装はアクセス宣言とRenderer抽出境界の思想を採用したが、Archetypeと並行redirectは採用していない。NexusEngineにはまだJob Systemがなく、Editorによる単体Component編集とLevel復元の頻度が高いためである。

## Architecture

```text
Application / future Scene transition
                 │
                 ▼
               Level ───────── LevelSerializer
                 │                   │
                 ├── SystemScheduler │ PersistentId mapping
                 │      │            │
                 ▼      ▼            │
             Scene (ECS World) ◄─────┘
                 ├── Entity slots (index + generation)
                 ├── Sparse ComponentStorage<T>
                 └── allocation-free ForEach query
                          │
                          ▼
                RenderSceneExtractor
                          │ value snapshot
                          ▼
                    RenderScene
                          ▼
                       Renderer / DX12
```

`Scene`は歴史的な名前を維持したECS Worldである。`Level`はWorldのロード状態、永続ID発行、System実行を所有する。Scene遷移や複数Levelの切替は、将来Application側の専用Serviceが担当する。

## 採用方式

ストレージ方式は型別Sparse Setを採用する。Level/System/永続IDは独立層として組み合わせる。ArchetypeとSparse Setを併用するストレージHybridは、現段階では実装していない。

| 方式 | Iteration / cache | Add / Remove | Query | Serialize / Editor | 複雑度・並列化 |
|---|---|---|---|---|---|
| Sparse Set | 各Componentは連続。複数型ではSparse照合が必要 | 平均O(1)、型内swap-and-pop | 最小Storageを走査 | 単体編集・任意構成に強い | 低～中。Storage単位の分割が可能 |
| Archetype | 同じ構成のEntity群では非常に良い | Archetype間relocationが必要 | 対象Archetypeをcache可能 | 頻繁な構成変更では移動が増える | 高。Chunk並列化に向く |
| Component Pool | 実装次第。安定addressを優先すると分断されやすい | Pool allocationは安価 | 型横断の索引が別途必要 | Object指向移行に向く | 中 |
| Registry wrapper | 利便性はbackend依存 | backend依存 | backend依存 | 導入は容易 | 性能契約とSerialize方針が不透明になりやすい |
| 今回のSparse Set | Hot Componentを型別dense配列で走査 | 平均O(1) | 最小dense配列 + O(1) membership | PersistentIdとRuntime IDを分離 | 現在は逐次、RO/RW metadataを将来利用 |

ArchetypeはComponent構成が安定し、同一Queryが大規模に繰り返され、Chunk並列Job Systemが存在するときに再検討する。現状で導入すると、Component追加削除とLevelロードのたびに複数列のrelocationが起き、実装・デバッグ・Editor統合コストが先行する。実測でSparse照合が主要bottleneckになった場合は、Transform/Physicsなど限定領域をArchetype ChunkにするHybrid拡張が候補となる。

Entity生成はどの方式でもslot確保が必要で、今回のCreateEntity測定にはName/Transform追加も含まれる。Sparse Setの破棄は登録Storage数をKとしてO(K)のmembership照合と実Componentのswap-and-pop、Archetypeの破棄はそのEntityの所属列の移動・破棄となる。今回の追加削除は他型の移動を伴わず、Editorからの単体変更とLevelの逐次復元で挙動を追いやすい。Archetypeは一括load時に構成別bulk生成すればrelocationを抑えられる。

メモリはSparse Setでは型ごとにentity index高水位までSparse配列を持つため、希少Componentが多いと不利。dense配列にはEntity indexと本体が必要で、capacity余剰もある。ArchetypeはSparse配列を共有するEntity location表へ集約できる反面、組合せ数と未充填Chunkが増えると無駄が増える。固定Poolはcapacity上限と空slot、pointer型Poolはallocationとfragmentationが課題。Registryは管理APIの概念であり、それ自体はStorage方式や性能を決めない。

数十万EntityではSparse Setも連続反復できるが、複数Componentのdense順序が異なるとランダムアクセスとbranchが増える。Archetypeは構成一致した列を同順で反復できる点で有利。並列化はSparse Setでもdense範囲分割が可能だが、構造変更はbarrierが必要。ArchetypeはChunk分割が自然で、いずれも宣言されたread/write依存の検証が必要となる。Debug、Serialize、Editorの要件を満たす初期実装として今回はSparse Setを選び、性能優劣は同条件比較Benchmarkの追加後に判断する。

## Memory Layout

```text
EntitySlot[index] = { generation, alive }
freeEntityIndices = [destroyed index ...]

ComponentStorage<T>
  sparseIndices[entityIndex] ─────► denseIndex
  denseEntities[denseIndex]  = entityIndex
  denseComponents[denseIndex]= T T T T ... (contiguous)
```

追加はdense末尾へ構築し、削除は末尾要素を穴へ移すswap-and-popである。これにより穴と断片化を残さず、単一Component走査は連続メモリアクセスになる。複数Component Queryは最小Storageだけを線形走査し、残りをsparse indexで照合する。Storageの型解決はQuery開始時に一度だけであり、Entityごとの`unordered_map`検索やheap allocationはない。

Component参照はStorageの再確保およびswap-and-popで無効化される。`ForEach`中のEntity生成・破棄・Component追加削除は拒否される。Systemから構造変更が必要になった時点で、専用Entity Command Bufferを安全点に追加する。

## Entity Lifecycle

```text
Create ─► Alive ─► Destroy ─► generation++ ─► FreeList ─► index reuse
              │                       │
              └─ IsAlive(index,generation) ─ stale handle rejected
```

`EntityId`は32 bit indexと32 bit generationの8 byte値である。`Entity`はそれに非所有`Scene*`を加えた操作Handleで、Componentを所有しない。generation 0とinvalid indexは予約値である。最大世代まで使ったslotは退役し、wrapによる古いHandleの復活を防ぐ。EntityIdはWorld内だけの識別子であり、異なるWorldの同じindex/generationを区別しない。Worldをまたぐ操作にはSceneを伴うEntityを使う。

LevelのLoad/LoadEmpty/Unloadは旧Worldを破棄するため、旧Entity HandleとComponent参照はすべて失効する。Sceneへの非所有pointerを持つEntityは旧World破棄後に呼び出してはいけない。Editorの選択・undo情報はPersistentIdで保持し、操作時に現在Worldで解決する。

## Query / System

`Scene::ForEach<Components...>`は内部Storageを利用側へ公開せず、callbackへ`Entity`とComponent参照を渡す。毎frameのQuery object生成や型vector allocationはない。

`SystemScheduler`は`ISceneSystem`をLevel単位で所有し、Simulation→Transformのphase順、同phaseは登録順に逐次実行する。後からMovement Systemを登録してもMatrix評価はその後になる。各Systemはread/write Component型を宣言する。これは現時点では診断metadataだが、将来は次の競合規則でDAGを構築できる。宣言は現段階でアクセス権を強制しないため、このmetadataだけで並列実行してはいけない。System実行中にSystemを追加してはいけない。

```text
Read(A)  + Read(A)  = parallel candidate
Write(A) + Read(A)  = dependency
Write(A) + Write(A) = dependency
```

`TransformUpdateSystem`は既存`TransformSystem`をSchedulerへ接続するAdapterである。System同士は参照しない。

## Levelとの関係

- `Scene`: Entity slot、Component Storage、Queryだけを管理するECS World。
- `Level`: 1ゲーム空間としてScene、System、Load/Unload状態、永続ID発行を管理する。
- `LevelSerializer`: ファイルI/OとRuntime Entity参照の再構築を管理する。
- 将来のScene/Level Manager: Active Level切替、非同期ロード、transitionを管理する。

`Unload`はEntityを1件ずつ破棄せずWorldを一括解放する。Level loadは一時Worldへ全データを読み、形式と参照を検証できた場合だけ現在Worldと置換するため、失敗時に既存Levelを壊さない。

## Rendererとの関係

ComponentはDirectX 12型やGPU Resourceを所有しない。`PrimitiveRenderComponent`と`CameraComponent`はRuntime設定だけを保持し、`RenderSceneExtractor`が1 frame分の値snapshotへ変換する。RendererはEntity/Storageのaddressとlifetimeを知らない。

```text
Level / Scene → Render Extraction → RenderScene / Queue → Renderer → DirectX 12
```

## Serialization

Runtime `EntityId`はslot再利用とロード順で変化するため保存しない。`PersistentIdComponent`の128 bit IDをEntityと親子参照の保存鍵にする。ロードは全Entityを生成後、PersistentIdから新しいEntityIdへのmapを使ってHierarchyを再構築する。

version 1のテキスト形式はName、Transform、Hierarchy、Camera、Primitiveを明示的に扱う。GPU Resource、System object、計算済みMatrixは保存しない。新しいComponentを永続化する場合はversion migrationとserializer登録機構を追加する必要がある。

未対応の非空Component Storage、必須Component不足、無効/重複PersistentIdは保存開始前に拒否する。ロードは循環Hierarchy、参照先不足、非有限Transform、未知version、不正な終端を拒否して旧Worldを維持する。ファイル保存は現段階で直接書込みであり、書込み中のI/O失敗から既存ファイルを守るatomic replacementは今後の課題である。PersistentIdは乱数64 bit名前空間と連番で生成し、暗号学的なUUID保証は提供しない。

## Editor境界

ロードをまたぐ選択状態はPersistentIdを保持し、`Level::FindEntity`で現在Worldへ解決できる。このcold pathは線形走査であり、高頻度の参照解決が必要になればLevel側のID indexへ拡張する。

RuntimeはImGuiやEditorへ依存しない。Hierarchy/Inspectorは`Level::GetScene()`、Entity/Component API、PersistentIdを利用するEditor側Panelとして追加する。Component名、生成関数、property情報が必要になった場合はRuntime UI処理ではなくReflection/Metadata registryとして提供する。

## Performance

- Component本体は型ごとの`std::vector`に連続配置し、単一型走査時のcache line利用率を上げる。
- Entity lookupとComponent membershipは配列indexで平均O(1)。Object graphのpointer chasingを避ける。
- free listでEntity slotを再利用し、steady stateのslot allocationを抑える。
- swap-and-popで削除後の穴を残さず、fragmentationを抑える。
- Query中は型Storageを一度だけ解決し、毎EntityのRTTI/hash lookupを避ける。
- Queryはheap allocationを行わない。Transformのvisit配列とRenderScene抽出先はcapacityを保持し、高水位を超えたときだけ再確保する。Transform階層は現段階で再帰評価のため、非常に深い階層には非再帰化が必要である。
- virtual callはSystem単位とEntity破棄時のComponent型単位に限定され、Component反復内にはない。

Benchmarkは`Benchmarks/NexusEcsBenchmark.vcxproj`に分離し、10,000 / 100,000 / 500,000 Entityについて生成、Component追加、2-Component Query、削除、破棄を計測する。結果はHardware、Build設定、常駐processの影響を受けるため、同条件で複数回測定し中央値を比較する。

2026-10-03の開発機（Intel64 Family 6 Model 165、Windows x64、MSVC 14.51、Release /O2 /GL）で最終コードを3回実行した各列の中央値（単位ms）は次の通り。比較baselineではなく、規模に対する増加傾向を確認する測定である。単一Queryはtranslation.x加算、複数QueryはVelocityをTransformへ加算する。checksumは20k/200k/1Mで一致した。

| Entity | Create | Add | Single Query | 2-Component Query | Remove | Destroy |
|---:|---:|---:|---:|---:|---:|---:|
| 10,000 | 2.434 | 0.908 | 0.040 | 0.038 | 0.632 | 0.226 |
| 100,000 | 22.604 | 9.191 | 1.059 | 1.334 | 6.400 | 2.921 |
| 500,000 | 104.718 | 43.298 | 5.230 | 7.103 | 32.126 | 15.447 |

初回測定ではComponent追加が異常に長く、`reserve(size + 1)`がvectorの幾何増加を無効化してO(N²)になっていることを発見した。容量不足時だけ約2倍へ増やす修正後の値が上表である。

## Alternatives

- 完全Archetype: Physics/particleなど構成が安定した数十万EntityのQueryが支配的になれば有力。現状は構造変更と実装複雑度が過剰。
- SOL-AVES型redirect table: 即時かつ並行な構造変更には有効だが、atomic metadata、旧データ追跡、回収epochが必要。Job System導入前には採用しない。
- EntityLogic: boss/playerの複雑な個体ロジックには有効。まずLegacy objectとの共存で需要を測り、ECS Coreへ入れない。
- 外部Registry library: 成熟実装を利用できる一方、性能契約、Serialize形式、デバッグ方法が依存先に拘束される。現時点のCore規模では採用しない。

## Future Work

1. Entity Command Bufferと安全点での一括Playback
2. Read/Write宣言の検証と依存DAG、Job System連携
3. Query cacheおよびChunk/Archetypeを限定採用する実測比較
4. Component Reflection、Serializer registration、version migration
5. Prefab、Hierarchy index、Editor undo/redo
6. Streaming Levelと非同期Asset解決
7. Render extractionのparallel化とframe allocator
8. 非再帰階層評価とHierarchy順序cache

## 現在のEngine調査と変更範囲

ProjectはCore/Framework、Platform/Window、Foundation（Math/Error/Logging）、Graphics、Runtime/Scene、Runtime/Rendering、Editor、ThirdParty、Resourcesに分かれる。Actor/GameObject継承階層、Asset Manager、Scene Manager、Level Serializer、Job Systemは実コードに存在しなかった。Sceneは既にSparse Set ECSであり、今回そのAPIと名前を維持した。新しいComponentStorageはSceneから独立したheaderとなった。

FrameworkはWindowとGraphicsSystemを値所有し、Application所有Renderer/Editorを非所有参照で接続する。Graphicsのnative resourceはunique_ptr/PimplとComPtr、共有所有は必要なError cause等に限定される。Resultはstd::expectedを使い、プロジェクトは既にC++23設定である。ECSアルゴリズムはC++20で表現できるが、今回のLevel/Frameworkは既存Resultに従うためC++23 STLが必要。C++20へ戻す場合はFoundation Resultの互換実装が必要になる。

現行名規則はNexusEngine namespace、PascalCase型/関数、末尾underscoreのmember、Resultで外部エラー伝播、内部構造操作はpointer/boolまたはlogic_error。IDはEntityIdとGraphics側opaque Handleが既存。今回永続IDを別途導入した。

Frameworkに非所有UpdateClientを追加し、EditorApplication→Level.Update→System Scheduler、続いてPrepareRender→再利用RenderSceneを抽出する。PrimitiveRendererはSceneやEntity型を参照せず、そのsnapshotの全Primitiveのworld/tintとCamera行列を描画へ反映する。複数Drawが同じConstantBufferの最後の値を読む問題を避けるため、objectごとのframe安全なGPU bufferを高水位まで保持する。これは最小描画統合であり、大量描画向けinstancingやframe arenaは今後のRenderer側課題である。

デモのモデル選択・CPUロード・Clip選択・回転・アニメーション更新はEditorApplicationのScene構築／Update側に置く。ModelInstanceはCPU ModelAssetDataから姿勢を評価し、GPU ModelResourceには依存しない。RendererはScene指定AssetのGPUアップロードとGPU所有、更新済み姿勢の読み取り、描画だけを担当する。GPU fence、Descriptor、Asset importer、Editor UI基盤は既存責務を維持した。

コードでのScene編集箇所は`Editor/Core/EditorApplication.cpp`の`CreateScene`（モデルパス、Clip、Entity、Transform、Camera）と`Update`（回転）、同ヘッダーの`rotationSpeed_`（rad/s）。`main`はGPU初期化より先にCPU Sceneを構築して非所有入力を接続する。AssetはModelInstanceとRendererより長く保持し、更新と描画は現状同一スレッドで実行する。

この接続は単一共有モデルを使うデモ用。モデルのパス・Clip・角速度は現在のLevelファイルには保存されない。異なるモデル／Entity別Clip、実行中のAsset差し替え、並列描画にはAsset Handleとcache、Entity別姿勢snapshot、追加Serializationが必要。`CreateScene`は起動前だけに呼び、Renderer稼働中のモデル変更には使用しない。

## PDFの記述と採用判断

| 項目 | PDFの記述 | NexusEngineでの判断 |
|---|---|---|
| Entity | 単なるID（p57） | 既存index/generationとScene操作Handleを維持 |
| Storage/Layout | Component構成別Archetype、型別64KB Segment、N=64KB/max(sizeof列)（p58–60） | 型別dense配列を採用。64KB最適値は測定が必要 |
| Access/Query/Iterator | RO/RW EntityAccessor、EntityStream、ForEach/ParallelForEach（p70–73） | typed callbackと最小pool駆動。現段階は逐次 |
| System | RO/RW依存Graphを起動/構成変更時に構築（p65–68） | access metadataとphaseを実装。DAG/強制Accessorは将来 |
| Create/Destroy/Add/Remove | 即時Directと遅延ECB、Archetype間物理移動（p61–64） | Sparse Set構造操作はiteration外のみ。ECBは将来 |
| ID再利用/Generation | bit幅、free-list規則、wrap対策の詳細は資料に記述されていない | Engine独自に世代検証・free-list・最大世代退役を設計 |
| Fragmentation/回収 | Segment配置とredirect案を示すが、回収/圧縮の完全な手順はない | dense swap-and-popで穴を残さない |
| 並行構造変更 | redirect table、作成/破棄sequence、atomic metadata（p88–90）、検証中（p93） | Job System/epoch回収の基盤が未導入のため採用しない |

資料が述べないGenerationや回収方式を推測で実装例として扱わない。今回の性能値はPDFの5000体60fpsデモとはワークロードが異なり、比較できない。

## Test / Benchmark実行

Visual Studio 18のDeveloper Shellで実行する。

```powershell
msbuild Project/Tests/NexusEcsTests.vcxproj /p:Configuration=Debug /p:Platform=x64
./Generated/Outputs/EcsTests/NexusEcsTests.exe
msbuild Project/Benchmarks/NexusEcsBenchmark.vcxproj /p:Configuration=Release /p:Platform=x64
./Generated/Outputs/EcsBenchmark/NexusEcsBenchmark.exe
```

テストはEntity再利用/stale handle、swap-and-pop、iteration中の構造変更拒否、永続ID参照復元、破損ロードでの旧World保持、未対応Componentの保存拒否、System phase順、Render抽出capacity再利用を検証する。
