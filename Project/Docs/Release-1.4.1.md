# NexusEngine 1.4.1

2026-11-17 / Windows x64 / Release / MT / v145。

## 追加した機能

- RetargetProfile assetとRig Editor。source/targetのrest・inverse bind・署名を検証し、joint mapping、軸、translation倍率を補正します。
- Root Motionの抽出と任意のEntity適用。loop/reverse/旋回cycle、軸指定、描画poseからの除去に対応し、ゲームから差分を消費できます。
- Animation Layer asset、joint/subtree mask、Override/Additive、順序、独立clock、Speed/Weight Curve。
- 新Keyframe/Skeletal componentで使えるState Machine、parameter/trigger、Any State/exit time、1D/2D Blend Tree。編集は既存の共通ImNodes wrapperへ統合。
- Timelineが同じclip assetと新componentを制御する経路。借用中のcontroller時計を保持し、区間外と解除時に復元します。
- glTF morph delta/default weight/channelの読込、Step/Linear/Cubic補間、blend、Forward/Shadow/Selectionへの反映。
- 可変長GPU pose bufferにより固定64 joint paletteを撤去。65 jointとmorph/skin併用の実機pixel検証を追加。
- Curve P2のclipboard、範囲選択、snap、軸制限、preset、read-only overlay。時間方向weight付きBezier、解析積分、形状を維持したkey分割。
- CurveBindingからCamera FOV、色、材質、CameraEffectなどの数値propertyへ接続。Native numeric parameterの明示animatable metadataも使用できます。
- 別processのRuntimeへ未保存Curveをchunk送信。検証後の交換、Play/Pause/Stop/Seek、現在位置の返送、切断復帰・再接続。
- UUID sidecarとasset registry、旧path alias、参照を維持するファイル改名・移動、開いている編集文書の追従。
- SDKにSamplesを同梱。新AnimationSystemDemoではRetarget、Layer、Controller、Morph、Speed Curve、Camera Effect、同一clipを使うTimelineを試せます。

## 修正

- legacy .nxanimで省略されたdurationをglTF metadataから補完。日本語・長いWindows pathのファイル読込とinventory SHA-256を改善。
- crossfade割込を混合入力の固定へ変更し、dominant clipへのジャンプを解消。
- frame内で速度が正負反転しても通過eventを検出。逆eventは明示許可、Seek/停止は通知しません。
- 小さいnormalized weightでbind poseの残りを保持。propertyの無効binding・component削除時の復元とdraftの寿命を検証。

## 互換性と導入

.nxanim v1/v2/v3、通常.curve v1を読み込みます。retarget/skeletonSourceを持つclipはv4、weighted Curveはv2です。既存assetを一括変換しません。旧版のSDKと配布物は保持します。

新component、reflection metadata、GPU定数配置の変更があるため、Project host/Game Moduleは1.4.1で再ビルドしてください。既存Projectのengine関連付けを自動変更しません。Samplesは配布copyだけを1.4.1へ結び付けます。

## 明示的な制限

Retargetの意味対応は明示mappingで行い、Humanoidの自動人体推定や異なる表情indexの意味変換は行いません。glTFの圧縮拡張や8本influenceを追加した版ではありません。

モーフ/skinは同じ変形bufferでRasterの各passへ反映し、動的なDXR BLAS更新は行わずShadow Mapへfallbackします。Root Motionのfade/layer/timelineはframe区間の加重差分で、連続時間の厳密積分やDynamic Rigidbodyへの力適用ではありません。

Curveは100,000 key / 32 MiB。morph deltaはmeshあたり256 MiB、4,096 target、65,536 jointまで検証します。巨大モデルの実用性能は別途評価してください。実施済みの検証と配布識別は `Release-1.4.1-Verification.md` を参照。
