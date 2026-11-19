# NexusEngine 1.4.0

2026-11-17 / Windows x64 / Release。アニメーションを共有アセットとEntityごとの再生状態へ分け、汎用Curve Editorを追加しました。

## アニメーションとCurve

- glTF/GLBの複数クリップを個別アセットとして扱います。モーフchannelを含むモデルでも、対応する骨格/TRSクリップの取り込みを維持します。
- KeyframeAnimationComponentとSkeletalAnimationComponentを分離しました。Transform TRS、RGBA、Camera FOVのproperty bindingは安定Reflection IDと許可metadataで検証し、不正trackだけをskipして警告します。
- 共通PlaybackでPlay / Pause / Stop / Seek、独立経過時間、速度、loop、完了を扱います。Skeletonは二clipの独立速度、TRS/Quaternion blend、Crossfadeを使用します。
- `.nxanim`のv1/v2を維持し、新規v3はKeyframe / Skeletalのkindを明示します。共有assetへEntityの再生時刻を保存しません。
- `.curve`を用途非依存のFloatCurve assetとして追加。Constant / Linear / Cubic Hermite、手動・Auto・Clamped Auto接線、境界保持、検証、原子的保存を提供します。
- Curve Editorはdock可能です。キー/接線の直接drag、複数選択、右クリックメニュー、数値入力、zoom/pan/frame、Undo/Redo、保存・別名保存・再読込を使用できます。
- Speed Curveは独立経過秒から解析積分します。更新履歴に依存しないSeekと、未保存Curve変更後の停止位置再評価に対応します。正規化区間の後はX=1の速度を保持します。
- Detailsにclip/curve選択、再生操作、速度・loop、Curve新規作成/編集/mini preview、blend設定をまとめました。Keyframe timelineをComponent / Property階層へ整理しました。
- 編集Worldと保存済みassetを変更せず、複製Worldへ未保存draftを反映します。Curveの変更・Undo・Seek・preview切断を検証しました。

## ほかの未配布変更

- 名前付き衝突レイヤーとProjectの衝突マトリクス、使用レイヤーを絞った編集画面、名前変更・削除時の所属移行とUndo/Redoを追加します。Raycastの検索maskは維持します。
- PostProcessと既存アニメーション画面で共有するImNodes wrapperのcontext・選択・view状態を分離します。
- 新規プロジェクト用Empty / Basic Levelテンプレートを、公開済みLauncher 1.3.0の作成画面に合わせます。

## 導入と互換性

Launcherでエンジン一覧を更新し、1.4.0を別の保存先へインストールしてください。旧SDKは保持します。既存プロジェクトの関連付けは自動変更しません。移行時は対応SDKでHost / Game DLL / C#を再ビルドしてください。C++内部型・SDK ABIとLevelの衝突設定が変更されています。

旧AnimationPlayer / Controller / Timelineは互換経路として維持します。新しいState Machine / Blend Treeの拡張は今回の対象ではありません。新規開発はKeyframe / Skeletalの独立componentを使用してください。v3 clipは旧SDKで読み込めません。

衝突相手はProjectのマトリクスで設定します。従来のColliderごとの接触maskに依存するプロジェクトは、移行後にマトリクスを確認してください。[衝突レイヤー](CollisionLayersREADME.md)に詳細があります。

開発環境はWindows x64、DirectX 12対応GPU、Visual Studio C++ v145、Windows SDK、.NET SDK、C#には.NET Framework 4.8参照アセンブリです。Mono Runtimeは引き続きSDKへ同梱します。Launcher本体は公開済み1.3.0を使用します。

## 制約

- Retargeting、root motion、animation layering、Bezierの時間方向weight、Curve P2（copy/paste、範囲選択、snap等）は未実装です。
- 骨格互換性は階層と、glTF由来のrest/inverse bindを検査します。古いauthored $node clipにはrest/bind metadataがなく、階層検査までです。skinは最大64 joints、モーフ姿勢描画は未対応です。
- durationを省略した旧v1 clipは新Skeletal clockでは既定1秒になります。モデルから正しいdurationを持つclipを再取り込みするか、旧再生経路を維持してください。
- Crossfade割込は重みの大きいclipを次のsourceとする規則です。snapshotによる完全連続な割込は行いません。
- 新componentのイベントは前進結果の区間で通知し、Seek/停止/逆方向では通知しません。複数loopを飛び越した同じeventは最新1回にまとめます。
- Curveの未保存previewは同processの複製Worldに対応します。別processへCurveを送るIPCは未対応です。
- 全GPU・DPI・解像度・任意のユーザープロジェクトは未検証です。

設計と操作は[Animation System](AnimationSystemREADME.md)、実施済み検証と公開結果は[1.4.0検証記録](Release-1.4.0-Verification.md)を参照してください。
