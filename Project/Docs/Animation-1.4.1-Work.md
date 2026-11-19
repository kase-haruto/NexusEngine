# Animation 1.4.1 実装作業記録

2026-11-17。利用者が以前の対象外機能も含めた実装を指定。版数は指定の1.4.1を使用する。旧1.4.0 SDKと既存作業ツリーを保持する。

## 対応範囲と進捗

- [ ] Curve P2: copy/paste、範囲選択、snap、軸制限、preset、重ね合わせ表示、他用途への接続
- [ ] legacy duration補完、asset参照正規化、長いWindows inventory path、共有draftの残留不具合
- [ ] 連続なcrossfade割込とイベント区間の扱い
- [ ] RetargetProfile asset、検証、rest/軸/体格補正、Editor作成と割当
- [ ] Root Motion抽出とEntityへの適用
- [ ] Animation Layerとmask/override/additive、独立再生
- [ ] 新componentと同じclip assetを使うState Machine/Blend Treeとノード編集
- [ ] モーフchannelと描画、骨格joint上限の見直し
- [ ] Curve別process IPC、切断/再接続と未保存preview
- [ ] アセットの安定識別・改名移動への追従
- [ ] 保存互換、単体/統合/GPU/UI検証
- [ ] 新Installed SDK、旧版保持、GitHub/Web/Launcher catalog公開と実取得照合

チェックは利用経路と検証が揃った時点で付ける。仕様書のP2や将来機能について、APIを置いた段階だけで完了としない。RuntimeからEditor/ImGuiへの依存を追加せず、clip/curveは不変共有、Entityの時計とposeはinstance側に保つ。既存のserializer/history/node wrapperとpose評価を再利用する。

## 段階検証（未配布）

- Phase A: Curve text serializerをclipboard/IPCと共用。copy/paste（同一X置換、1操作Undo、無効clipboardの原子拒否）、preset、marquee/snap/axis/overlay UIを追加。長いinventory pathのSHA-256をWin32拡張pathで修正。EditorTests成功。
- Phase B: cgltf実装をGltfData.cppへ分離、geometry非依存のduration取得。省略durationの旧clip補完・clip参照の正規化。RetargetProfile/署名・joint mapping・rest/軸/倍率補正、.nxanim v4のprofile参照と描画binding。Retarget+legacy durationのAnimationAssetTests成功。Crossfade割込を混合入力の固定へ変更、ECS回帰成功。
- Phase C: .nxlayers共有asset、独立clock/Speed/Weight Curve、mask、ordered Override/Additive。LayerのECS検証とCPU姿勢検証が成功。Rig/Layer Editorと共通Historyによる編集文書、未保存終了確認を追加。EditorTests成功（追加の専用文書テストは次回build対象）。
- Phase D: 新AnimationControllerPlayerを作成。.nxcontroller/既存ノード編集を再利用し、Keyframe/Skeletalの両方から不変clipを制御。共通clock、parameter/trigger、1D/2D Blend Tree、連続割込を実装。新controllerのECSテスト成功。Editor統合変更後の再buildはまだ。
- 最新のCPU bindingはRetargetProfileのBindCompatibleAnimationへ共有化中。ModelLoaderにgeometryを読まない経路を追加済みで、Root MotionのCPU資源準備に利用予定。これらの最後の変更は次回のbuildで検証する。

Root Motion、morph描画、joint palette拡張、別process IPC、安定Asset ID、汎用Curveの他用途binding、時間weight付きBezier、残るイベント区間分割はまだ作業が必要。1.4.1 SDK/ZIP/catalogはまだ生成・公開していない。

## 追加の段階検証（未配布）

- Phase E: Root MotionのCPU asset準備、rest相対抽出、loop/reverse/旋回cycle合成、Entity適用、描画poseからの成分除去。ECSとAnimationAssetTests成功。
- Phase F: 時間方向weight付きBezier、時間逆算、解析積分、形状を維持するkey分割、.curve v2。ECS curveテスト成功。
- Phase G: .nxmeta UUIDと.nexusassets.json registry、旧path alias、改名・移動の原子操作、開いているCurve/Rig/Studio文書の追従。EditorTests成功。authored clipのskeletonSourceと安定ID pickerも追加。
- Phase H: CurveBinding component。カメラFOV/色などanimatableな数値propertyの指定channelへCurve出力を接続。既存PropertyEvaluatorを共用して検証・元値復元。ECSでカメラ、pause、無効channel、component削除を検証済み。
- Phase I: NamedPipeChannelの実通信、同一WindowsユーザーACL・remote拒否、bounded queue、再接続、chunk転送、worker parseとmain-thread交換。CurvePreviewBridgeのseek/state/切断復帰を実際の二つのprocessで検証成功。Host/Standalone/Curve Editorへの配線後のEditor全体検証は残る。
- Phase J: 速度Curveの零点によるイベント区間分割、opt-in逆イベントと方向情報、controllerのeventとmotion区間の分離。ECS curveテスト成功。EditorTestsはbuild成功したが実行code 3で停止し、原因調査が必要（成功扱いしない）。
- Phase K作業中: morph delta/default weights/channelのglTF importとModelInstanceの独立weight blendを追加中。GPU buffer/Forward/Shadow/Selectionの変更と検証はこれから。

1.4.1はまだ生成・公開していない。現在の公開最新版は1.4.0のまま。

## 現在の到達点

Phase Oでtyped Timelineの優先制御・区間外復元・controller借用・Root Motion経路を追加。ECS curves、1 MiB超のchunk転送を含む実二process IPCの再接続、最終EditorTestsが成功。Phase Lの5 case GPU animationとPhase NのCamera Effect shader/colour chainも成功。残る必須工程はInstalled SDK/実Host起動・UI QA、配布物とLauncher同梱Toolの更新・公開後取得照合。

途中のPhase J/Kの失敗は修正前の履歴であり、現在の失敗状態ではない。Phase Oのlogを最終基準にする。現在の source-inputs.json は配布前snapshot。sourceに変更が入った場合は再採取し、SDKを新しい未使用先へ発行する。

Phase P/Q: legacy Timeline gap互換を修正。Rig保存のrevisionからRuntimeとmodel cacheを更新し、Layer定義の保存・並替え時も独立clockを保つ。追加のECS/Editor回帰が成功。初回PublisherはProjectBuildの不足includeで停止し、旧公開版は変更していない。最終SDKはEngine-r2へ新規生成する。


## 最終SDKと公開検証（2026-11-18）

Engine-r2 buildId aa6ac5d5-303d-4133-b549-fddbdb856f79を発行。1976ファイル照合一致、Mono784一致、発行入力210ファイルのhash不一致0。Installed Host実IPC、C++/C#/Hybridの実typed build、Runtime/Editor GPU、sample表示を確認。ZIP186798652bytesを作成し公開工程へ進んだ。

Launcher1.3.1の変更履歴がGet-Contentの付加property付きobjectとなる問題を公開後検査で検出。生成処理をReadAllTextへ修正、Websiteにobject拒否回帰を追加し、Setupを差し替えずLauncher1.3.2を追加する。旧履歴catalogは文字列へ正常化。最終公開結果はRelease-1.4.1-Verification.mdを参照する。

公開後にSDK8版とSetup1.3.2の全9ファイル取得・hash照合が成功。旧Launcher実assemblyで更新一覧6版と最新Setup取得も成功。最終文書をサイトへ反映する。
