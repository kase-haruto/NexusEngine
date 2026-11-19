# Animation System Demo (1.4.1)

データのみのProject。Sourceと130%のTargetを並べ、同じglTF内のIdle/Walk/Wave/Smileを独立 `.nxanim` として使う。Target側は別 `.nxretarget` のrest/軸/体格補正を通す。

1. このsampleを作業用にコピーし、Launcherで1.4.1を明示して開く。元Projectの関連付けを変更しない。
2. Run後、SourceまたはTargetを選ぶ。Skeletal AnimationのState Machine parameter `Speed` を0..1で変更する（数値はEnter確定）。`Wave` はtrigger。
3. Animation Studioで `Controllers/Source.nxcontroller` を開く。Locomotionの1D Blend Tree、Waveへの遷移、exit timeを共通ノードUIから編集できる。
4. Animation Rig / Layersで `Layers/Source.nxlayers` を開く。上半身のAdditive Wave、表情のOverride Smile、maskと順序を調整する。Target側も別のlayer定義から同じsource motionを参照する。
5. `Rigs/Taller.nxretarget` はモデル、署名、対応joint、translation倍率を確認・変更できる。既存の対応を確認してから保存する。
6. SkeletalのSpeed CurveからCurve Editorを開く。未保存のキー・接線変更をviewportで確認する。Source/Targetは同じSpeed Curveを共有するが、時計は個別。
7. Camera / Curve Effectを選ぶ。Curve BindingはBrightness.curveをCameraEffect.parameters0のchannel 0へ接続する。同じCurve Editorはアニメーション速度以外にも使える。
8. Root Motionを試す場合はSourceのcontrollerAsset/layerAssetを解除してWalkを単独再生し、Root Motionを有効にする。Root JointはRoot。actorが進み、mesh内のroot移動が除去される。長時間再生すると画角外へ移動する。
9. `Timelines/Sequence.nxtimeline` は上と同じSourceのclip assetを使用する。Sourceを選んでStudioからAttach/Previewする。Timeline制御中はState Machineを保持し、区間終了と解除の動作を確認できる。
10. 別ウィンドウのRunで保存済みLevelを起動し、同じEntityとCurveを選ぶと、未保存Curve・Play/Pause/SeekをIPCで反映できる。ToolbarのRuntime表示をhoverして接続状態を確認する。

Assetsの `.nxmeta` と `.nexusassets.json` をコピーにも含める。改名・移動はContent Browserのcontext menuから行うと旧path参照も追従する。Source生成用コードはEngine testの `--build-animation-demo`。通常利用時の実行は不要。
