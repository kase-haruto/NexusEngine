# Animation System — 1.4.1

正式なCurve仕様は `NexusEngine_CurveEditor_Spec.md`、導入例は `Samples/AnimationSystemDemo/README.md` を参照。1.4.0の制限を引き継いだ説明は、当該版のRelease資料に保存している。

## 1. 全体構成と依存方向

```text
Editor: Studio / Curve Editor / Animation Rig / Details / shared ImNodes wrapper
  → draft + PropertyEditService + EditorHistory + isolated preview World
Runtime: immutable Clip / Curve / Layer / Retarget assets
  → Entity-owned Playback / Controller / Timeline / pose / Root Motion delta
  → PropertyEvaluator or ModelInstance
Graphics: shared immutable mesh/morph buffers + per-instance frame pose buffers
```

CurveとPlaybackはEditor/ImGui/Entity/DirectXに依存しない。AnimationDocumentはCPUのModelAnimation型を共用する。NamedPipeのOS処理はPlatformへ置き、Curve転送は受信・検証とWorld適用を分ける。旧Animator/AnimationPlayerも保存互換のため残す。

## 2. KeyframeとSkeletal

`KeyframeAnimationComponent`は安定component/property IDで値を操作し、`SkeletalAnimationComponent`はモデルのlocal TRS、骨格、モーフを操作する。アセットにEntityの時計・選択・遷移状態を保存しない。両者を継承させない。

KeyframeはTransform TRS、PrimitiveRenderのtintと材質係数、Camera.fovに接続できる。追加のC++ componentは `IsPropertyAnimatable(std::string_view)` を宣言し、cppで許可IDを返す。Native DLLの数値metadataはpropertyの `animatable: true` で許可する。文字列や参照型を数値として書き換えない。無効bindingは診断してskipする。

## 3. 共通Playback

Play/Pause/Stop/Seek、独立経過秒、速度積分、loopと完了判定を共用する。Speed Curveはnormalized elapsedの速度倍率を解析積分し、Seekを更新履歴に依存させない。X=1以後は終端速度を保持する。負の基本速度は終端から開始する。非loopは最初の終端到達で完了し、往復Curveでも過去の終端を越えて再開しない。

完了後はStopまたはSeekしてPlayする。Root MotionやイベントはSeekの飛び区間から発生させない。

## 4. AssetとInstanceの所有権

Clip/Curve/Layerは `shared_ptr<const ...>` のsnapshotを返す。再生状態とLayer時計はEntity専有。未保存Curveは別snapshotでoverrideし、既存利用者の保存済みsnapshotを書き換えない。

保存時に `.nxmeta` のUUIDとAssets rootの `.nexusassets.json` を発行し、参照には `asset://UUID` を使う。旧相対pathも解決し、改名履歴をaliasとして維持する。Content Browserのファイルcontext menuから改名・移動できる。移動先の既存fileは上書きしない。開いているCurve/Rig/Studioは、履歴と未保存内容を維持して新pathへ追従する。外部移動ではassetとsidecarを一緒に保ち、配布時はregistryも含める。

## 5. .nxanimと互換性

v1/v2を読み込み、新規v3はKeyframe/Skeletalのkindを明示する。v4は `retargetProfile` / `skeletonSource` を追加する。source/clip型、編集可能なtrack型、duration/loop/eventsを維持し、拡張子を変更しない。旧source型でdurationが省略されていた場合はglTFから補完する。

importしたキーを編集可能に変換する際は元のskeletonSourceを残す。rest/bindを失ったまま他のモデルへ暗黙適用しない。モーフは `$node.weights` の可変次元trackとして編集できる。

## 6. .curveと数値規則

用途非依存のFloatCurve。Constant/Linear/Cubic Hermiteに加え、時間方向weight付きBezierを使用できる。in/out weightは区間幅に対する水平比0..1で、既定1/3はHermiteと一致する。v1の通常Curveは維持し、weightを使用する場合はv2として保存する。

Xは厳密昇順、範囲外は端点保持、空Curveは1。Auto/Clamped Auto、手動接線、極端な値・非有限値の検証を共用する。Bezierは時間を逆算し、積分は解析式を使う。key追加は既存segmentを分割して形状を保持する。上限は100,000 key、保存・読込・IPCは32 MiB。制限を超えた保存で既存fileを変更しない。

## 7. Bindingと値の復元

AnimationPropertyEvaluatorは階層pathと安定IDで対象を解決し、世代付きEntity handleを使う。component pointerを長期保持しない。毎frameに元値へ戻してから評価し、track終了やcomponent削除で前frameの値を残さない。複数writerは評価順と診断で明確にする。

`CurveBindingComponent`はCurve出力を数値propertyの一つのchannelへ接続する。出力は `Curve(input) * outputScale + outputBias`。手動input、duration/速度/loopの自動入力、Pause/Seekに対応する。Detailsのproperty pickerには許可済みbindingだけを表示する。vectorの他軸を維持し、Quaternion一軸の置換は拒否する。

## 8. Skeletal / Morphと描画

glTF/GLBの複数clip、TRSとweight channel、モーフのPOSITION/NORMAL/TANGENT delta、node優先の既定weightを読み込む。Step/Linear/Cubicのweight補間を行う。モーフもinstanceのposeに保持し、assetを書き換えない。

Forward/Shadow/Selectionは同じCPU poseとframe別SRVを使用する。joint paletteの固定64行列を廃し、glTFのUInt16 joint index全域を扱う。明示上限は65,536 joint、4,096 morph target、meshのmorph deltaは256 MiB。GPUメモリと処理時間の実用上限はモデルに依存する。頂点のjoint influenceは既存の4本形式。

VSで変形するモデルはmesh shaderの静的経路へ送らず、影は変形に対応したShadow Mapへfallbackする。未変形のBLASをモーフやskinの正しいDXR geometryと扱わない。動的DXR geometry更新・cloth・圧縮glTF拡張を今回追加したとは扱わない。

## 9. イベント

更新前後の経過秒から速度の零点で移動区間を分割し、前進の `(begin,end]` を通知する。netの移動が0でも、途中で通過したeventを失わない。逆方向は `reverseEvents` を明示した場合だけ `[end,begin)` を通知し、event.reverseを設定する。

Pause、dt=0のSeek、Curveの再評価だけでは通知しない。複数loopや混合入力の同一Entity/asset/eventはframe内でまとめ、無制限のcallbackを避ける。通知はstorage反復と姿勢評価の後に行う。

## 10. EditorとPreview

Curve Editorはdock可能。直接key/接線drag、Ctrl/Shift複数選択、範囲選択、copy/paste、snap、X/Y制限、数値入力、preset、最大8本のread-only overlay、zoom/pan/frame、右クリックメニュー、Undo/Redo、保存/再読込を備える。連続操作は共通Historyへ一操作として記録する。

Detailsから使用中Curveの編集と新規作成を行える。Animation StudioはComponent→Propertyのtimeline、ruler scrub、keyの追加/移動/削除を持つ。morph weightは4channelずつ編集・表示する。保存確認と未保存終了確認を共用する。編集Worldは変えず、複製Worldへdraftを反映する。

別ウィンドウのRuntimeは同一ユーザーACL付きのローカルNamed Pipeへ接続する。Curveはchunk転送してworkerでparseし、完成snapshotだけをmain threadへ反映する。Play/Pause/Stop/Seekと現在Xを返送する。切断時は保存値へ戻し、再接続時は現在draftを再送する。選択した永続Entity IDとCurve参照が保存済みLevelのRuntime側に存在する必要がある。未保存Level全体を送る仕組みではない。

## 11. Blend / Layer / State Machine

Crossfade割込は、その時点の混合入力を固定したsourceとして次の遷移へ使い、dominant clipへの切替ジャンプを防ぐ。無効な遷移先は直前のsampleを保って診断する。

`.nxlayers` はclip、独立速度、weight、Speed/Weight Curve、target階層mask、Override/Additive、順序を持つ。不変定義とEntityごとのclockを分ける。Additiveはrest相対。毎frameにrestから作り、過去frameへ加算し続けない。

新componentは `.nxcontroller` を共用し、parameter/trigger、遷移、exit time、Any State、1D/2D Blend Treeを実行する。編集は既存の共通ImNodes wrapperを使う。controllerの内部状態はassetへ保存しない。Detailsからparameterを操作できる。

Timelineも同じclip参照を使う。対象に新componentがある場合はそれを借用し、Timeline制御中はcontroller時計を保持する。区間外は元値/bind poseへ戻し、Timelineを解除すると元のplayerへ戻る。TimelineのRoot Motionは各通過区間の差分とfade weightで合成する。連続時間の厳密な重み付き運動積分とは区別する。

## 12. Retargeting

`.nxretarget` はsource/targetモデル、rest/bindの署名、明示joint mapping、軸Quaternionとtranslation倍率を保存する。Animation Rig panelで作成・編集する。名前が一意なjointの候補を作れるが、曖昧な名前を暗黙対応させない。Clip v4からprofileを指定する。

同じrigは階層/rest/inverse bindを検証して再利用し、異なるrigはprofileを通してrest相対TRSを補正する。Humanoidの意味ラベルや人体比率を自動推測する仕組みは追加していない。morphのindex対応は同じ次元を要求し、異なる表情の意味対応を自動推測しない。

## 13. Root Motion

SkeletalのRoot Motionを有効にし、target modelのroot jointをpickerから選ぶ。抽出する平行移動軸と回転を指定できる。clipのloop/reverseと旋回cycleを合成し、描画poseから同じ成分を除いて二重移動を防ぐ。Pause/SeekではEntityを移動しない。

`applyRootMotion=false` は抽出差分を公開するだけにする。ゲームは `extractedRootMotion` をCharacter Controllerや物理へ渡せる。Dynamic Rigidbodyへ自動的な力や速度を与えない。通常のfadeやLayerのRoot Motionは更新frame単位の混合であり、物理運動の厳密積分ではない。

## 14. 設計判断と移行

巨大AnimationManager、独自のUndo/serializer、Runtime→Editor依存を増やさない。Property適用、clip/curve repository、CPU pose、OS transport、GPU bufferの境界を保つ。Retarget/Layer編集文書は共通Historyを利用する。CurveをAnimation専用にせず、Time Remappingへ意味を置き換えない。

新component/asset形式とGPU定数配置が追加されたため、1.4.1のSDKでProject hostとGame Moduleを再ビルドする。旧Level/clip/curveを読み込めるが、新形式を保存したfileを旧SDKで編集できるとは保証しない。既存ProjectのEngine関連付けは自動変更しない。

## 15. 検証と制限の確認先

対応機能と移行は `Release-1.4.1.md`、実行済みの検査・SDK識別・公開結果は `Release-1.4.1-Verification.md` を参照。実装・build・実機検証・公開を分けて記録する。APIだけを置いた状態や未実行検査を成功と扱わない。
