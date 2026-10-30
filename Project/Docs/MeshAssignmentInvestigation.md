# 新規Entityへのメッシュ適用が遅い原因

2026-10-30の修正前実装を調査し、通常のDebugエディターと同じくDebug Layer有効・GPU Based Validation無効で計測した。GPUはNVIDIA GeForce RTX 3060 Laptop GPU。下記の原因特定後に、末尾の「修正結果」に記載した変更を実施した。

## 計測結果

| 再現条件 | Renderer初期化 | 形状準備・転送 | テクスチャ | Meshlet系 |
| --- | ---: | ---: | ---: | ---: |
| Cubeのみ・初回 | 1,283 ms | 3.0 ms | 1.6 ms | 662 ms |
| Cubeのみ・シェーダーキャッシュ済み | 574 ms | 3.4 ms | 1.4 ms | 314 ms |
| 既存Sponza・デモ形状に同じCubeを追加 | 20,289 ms | 383 ms | 16,446 ms | 3,197 ms |

Cubeは24頂点、12三角形。最後の条件では、追加したCubeだけでなく119メッシュ・218,332頂点・42材質、圧縮画像約43.1 MiBを処理している。起動時のSponza読み込みは約175 ms、Cubeを統合するCPU処理は約22 msだった。

72件の遅い画像転送ログでは、CPUミップ生成の合計が14,723 ms、投入・GPU完了待機の合計が327 ms。シーン全体のテクスチャ再準備、とくにCPUミップ生成が支配的だった。GPU待機の合計は各画像で記録した値であり、その他の待機をすべて含む値ではない。

Cube単体のキャッシュ済み実行でも、通常Pipeline生成が232 ms、Mesh Shader Pipeline準備が309 msかかる。通常ShaderのCompile/Reflectionは約3.2 msで、DXILキャッシュがあってもPSO等の作り直しが残る。Meshlet描画は既定で無効だが、対応GPUではMeshlet Pipelineと形状を初期化している。

## 呼び出し経路と原因

1. `EditorApplication::PrepareAssets` は、Primitiveの要求集合が変わると要求番号を進める。空のメッシュ参照も比較対象に入っている。
2. `ModelAssetLibrary::Retain/Resolve` は、空のメッシュを無視する。未割当の描画Entityを追加しても、既存メッシュをそのEntityへ割り当てても、CPU snapshotとアセットRevisionが変わらない場合がある。`--mesh-assignment-cached` はこの条件を検証する。
3. 準備処理はsnapshotの同一性やアセットRevisionを確認せず、常に `new PrimitiveRenderer` と `GraphicsSystem::PrepareRenderer` を実行する。
4. `PrimitiveRenderer::Initialize` はsnapshot内の全Mesh・全Materialを処理する。既存テクスチャもデコード・全ミップ生成・転送をやり直し、通常描画・選択輪郭・Mesh ShaderのPipelineも初期化する。
5. 空Entityの追加による準備中にメッシュを選ぶと、先行ジョブが完了するまで次のジョブを開始しない。古い結果は破棄されるが、準備の処理時間は支払ったうえで最新要求を再実行する。

環境マップ・影・光学系の引き継ぎは、この新Rendererの初期化が完了した後に行われる。したがって前回の非同期化・引き継ぎは、待ち時間中の描画継続には有効でも、上記の再生成コストを取り除いていない。

既定の `Project.settings` は `SponzaLevel.json` を開く。再現ではその起動アセットと同じSponza・デモ用形状を使用した。ユーザーが操作した個別セッションのトレースではなく、現在のコードを使った再現計測である。

## 修正対象

- 未割当Entityの増減や既存メッシュの割り当てではGPUアセットを再構築しない。
- 変更のないPSO・GPUメッシュ・テクスチャを再利用し、追加・削除されたアセットだけを処理する。
- Meshlet描画が無効な場合の準備を遅延する。
- 選び直し時の先行準備を中断するか、不要なGPU準備へ進む前に最新要求を確認する。

## 再現方法

リポジトリルートでグラフィックステストをビルドし、次を実行する。`--mesh-assignment-profile` はRendererを毎回初期化する比較用の経路であり、修正後のエディターの適用経路ではない。

```powershell
& .\Generated\Outputs\GraphicsTests\Debug\NexusGraphicsTests.exe --mesh-assignment-cached
& .\Generated\Outputs\GraphicsTests\Debug\NexusGraphicsTests.exe --mesh-assignment-profile
```

計測ログは [Generated/MeshAssignmentProfile.DefaultDebug.log](../../Generated/MeshAssignmentProfile.DefaultDebug.log)。GPU検証を有効にした参考値は [Generated/MeshAssignmentProfile.log](../../Generated/MeshAssignmentProfile.log) に分離している。

`Assets.Profile` ログにRendererの段階別時間を記録し、25 ms以上かかった画像ではCPUミップ生成とGPU転送待機も分けて記録する。

## 修正結果

- 空の描画Entityを要求集合から除外し、すでに使用中のアセット集合と一致する場合は準備ジョブを起動しない。
- CPUの不変な形状・画像に識別子を持たせ、GPU資源を共有する。統合時に材質番号が変わっても、頂点・Index Bufferは共有し、Submeshの材質番号だけを更新する。
- 準備スレッドでは変更されたモデル資源だけを生成する。公開時は既存Rendererのモデルだけを交換し、通常描画・選択輪郭・Mesh Shader・影・光学・環境のPipelineや描画先を維持する。
- 共通画像・Samplerは最後の所有者がなくなった時点でDescriptorを退役する。プレビューもメインビューのGPUメッシュ・画像を共有する。
- 次snapshotで使用しないBLASや形状SRVを解放し、変更のない形状のBLASは残す。
- Meshletは有効化まで生成しない。有効化後の差分準備では既存Meshletも共有する。
- 選び直された準備は、CPU解決後およびメッシュ・画像の区切りでキャンセルを確認する。描画中の資源は変更しない。

同じSponza・デモ用形状に24頂点のCubeを追加する計測で、GPU資源の準備は **20,289 ms → 7.47 ms**。新規転送は1メッシュ、再利用は118メッシュ・74画像で、画像の新規生成は0件だった。この数値はGPU資源準備部分であり、最初のSponza読み込み、CPU snapshotの統合、公開時のGPU完了待ちは含まない。初回画像ロードのコスト自体は残る。

小規模テストでも1メッシュの追加を約2.5 msで準備した。実機・ソフトウェア描画で準備中の描画継続、既存BLASの保持、メインとプレビュー間の画像寿命、材質番号の圧縮、未使用形状の解放、破損画像・キャンセル時の保護を検証する。

```powershell
& .\Generated\Outputs\GraphicsTests\Debug\NexusGraphicsTests.exe --incremental-assets
& .\Generated\Outputs\GraphicsTests\Debug\NexusGraphicsTests.exe --incremental-sponza
& .\Generated\Outputs\GraphicsTests\Debug\NexusGraphicsTests.exe --background-assets
& .\Generated\Outputs\GraphicsTests\Debug\NexusGraphicsTests.exe --meshlet-lazy
```

修正後の計測ログは [Generated/IncrementalSponzaProfile.log](../../Generated/IncrementalSponzaProfile.log)。CPUアセットをコードから変更する場合は、頂点・Index・描画範囲の変更時に `MeshAssetData::resourceId`、画像変更時に対応する `*ImageId` を `NextModelResourceIdentity()` で更新する。材質係数や統合時の材質番号だけの変更では、GPU形状・画像の識別子を維持する。
