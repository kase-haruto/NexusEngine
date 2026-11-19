# Animation再設計 — 調査・移行記録

## 1. 現状（2026-11-17）

`Runtime/Animation/AnimationDocument`は.nxanim v1/v2、.nxcontroller、.nxtimelineを扱う。`AnimationRuntime`がファイルキャッシュ、時刻更新、旧Animator/Timeline、Reflection binding、値の復元、イベント通知をまとめている。`AnimationPlayerComponent`はプロパティと骨格の両方を動かす。これらを削除せず、既存アセット用の互換経路として残す。

骨格側は`Graphics/Model/ModelInstance`がCPUのTRS/階層/skin paletteを評価し、Rendererが結果を利用する。GPU資源の所有者はModelInstanceではない。`ModelAssetLibrary`はモデル取込、共有source cache、クリップ取込、階層binding、描画用集約データを所有する。

## 2. 責務と重複

再生状態はEntity側だが、共通再生APIはなく、旧Animator/TimelineとAnimationPlayerに時刻更新が分散している。クリップ読込はAnimationRuntimeとModelAssetLibraryの双方にある。.nxanimには用途の明示タグがなく、sourceと`$node`で骨格を推測する。一般プロパティbindingはReflectionで型・setterを検査するが、アニメーション許可metadataはない。不正trackで全体を中断する現状は変更が必要。

## 3. 依存関係の問題

AnimationDocumentがGraphics/Model/ModelAnimationを参照するため、純粋なキー形式とモデル形式が結合している。新しいFloatCurveとPlaybackはこの依存を持たせない。旧AnimationRuntimeをさらに拡張するのではなく、asset、binding、clockを抽出する。RuntimeからEditor/ImGuiへ依存させない。

## 4. 再利用する仕組み

- 安定型ID・property ID: RuntimeのTypeRegistry。表示名からbindingを作らない。
- 保存: nlohmann JSONとPlatform/FileSystem/AtomicFile。新しい汎用serializer frameworkは作らない。
- Undo: EditorHistory。ドラッグをbegin/update/endの1操作にまとめる。
- プレビュー: EditorSessionの複製Level/World。編集Worldを変更せず、未保存データはoverrideで渡す。
- アセット参照: Assets相対パス。AssetCatalog自身がAssetDatabase未導入と明記しており、GUID registryがあるとは扱わない。EntityのPersistentIdはasset GUIDではない。
- Docking: IEditorPanel/EditorWorkspace、Details: PropertyEditService/PropertyHandle/PropertyPages。
- 既存IPCはViewportSession/RuntimeProtocolに存在するが、Curve更新packetは未定義。現在のローカルpreview経路を優先する。IPCのCurve対応ができたとは報告しない。

## 5. 変更候補と理由

`Runtime/Curves`（新規）: 用途非依存のFloatCurve、検証、評価、積分、.curve保存。
`Runtime/Animation`：共通Playback、clip kind、共有不変asset、プロパティbindingの分離。
`Runtime/Scene/Components`：Keyframe/Skeletalの独立component。旧AnimationPlayerは維持。
`Graphics/Model`：既存pose評価の再利用とskeleton互換性診断。
`Editor/Services`：Curve編集とpreview通知。EditorHistoryを再利用。
`Editor/Gui/Panels`：独立Curve Editorと階層keyframe timeline。
`Editor/Gui/Property*`：用途別Details、asset picker、preview context。
`Tests`とbuild定義：Runtime単体とEditor統合の検証。

## 6. 互換方針

.nxanim拡張子とv1/v2読込を保持。新しいkindは新versionで明示し、legacyのmixed documentは黙って変換しない。旧controller/timeline/node UIは互換機能として残し、新State Machine/Blend Treeを作らない。既存プロジェクトの関連付け、配布済みSDK、サンプルを変更しない。

## 7. 推奨構成・所有権

Shared immutable clip/curve asset → Entity所有Playback state → property evaluatorまたはskeleton pose evaluator → scene/renderer。Editor draftは保存済みassetと別所有とし、immutable snapshotをpreviewに渡す。Curve EditorはAnimation componentを所有せず、変更callbackとpreview contextを借用する。assetには時刻や選択状態を置かない。継承による巨大AnimationManagerは作らない。

## 8. 段階移行

1. この調査とCurve正式仕様の確認。
2. FloatCurve・共通Playbackを独立Runtimeとして検証し、新components/asset経路へ接続。
3. Skeletalの二clip blend/crossfadeと互換検査。
4. Keyframe用hierarchical timeline/Details/binding診断。
5. Curve Editor P0/P1・未保存preview・Undo。
6. 統合回帰、実機UI確認、旧経路と新経路のREADME更新。

各段階は新経路の利用を確認してから既存呼出を移す。追加した型だけで「利用可能」とは扱わない。

## 9. CurveとPlaybackの決定事項

正式仕様: `Project/Docs/NexusEngine_CurveEditor_Spec.md`（提供版）を全文確認済み。例示JSON/APIは固定仕様とせず既存保存処理に合わせる。

CurveはCubic Hermite、接線はdy/dx。時間方向のBezier weightは持たせない。keysは厳密昇順、重複X/非有限値は拒否。0 keysは1、1 keyは一定値、範囲外は端点値保持。区間は左keyの補間。Constant/Linear/Cubic、Free/Aligned/Broken/Auto/ClampedAutoを表現する。汎用curveは負値を保持し、用途のclampを保存しない。

Playbackは独立した経過秒を持ち、正規化X=elapsed/curveDuration、baseSpeed×curve値を積分する。初期版は負速度も扱う。正規化1以降は端点速度を保持。loopはclip位置のwrapでありcurve入力を巻き戻さない。Seekは累積deltaではなく積分から再計算し、curveの変更後も同じelapsedで再評価する。極端な有限値への上限はPlayback側で明示する必要があり、Curveへは置かない。

競合は検出・警告し決定的な優先順位を定める。root motion、layering、retargetingは今回実装しない。現状の同名階層path matchingだけではrest/bind poseや軸の互換性は保証できない。将来のRetargetProfileはsource/target skeleton identity、mapping、rest/bind/軸補正をclipと別所有にする。

## 未検証・未完了

この文書は実装完了報告ではない。新component接続、Curve UI、旧asset全組合せの互換性、IPC再接続、実機編集操作は段階ごとに検証・記録する。現在の対応範囲をテスト実行結果で更新する。
