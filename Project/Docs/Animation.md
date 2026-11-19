# アニメーションスタジオ

1.4.0の新規開発向けのKeyframe / Skeletal、汎用Curve Editor、Speed Curveは[Animation System](AnimationSystemREADME.md)を参照してください。以下には旧AnimationPlayer / Controller / Timelineの互換経路も含みます。新規Keyframe clipで選べるpropertyはanimatable登録に限定されます。

クリップ、Animator Controller、Timelineを同じアセット参照で作成・編集・再生する。ウィンドウメニューまたはツールメニューの「アニメーションスタジオ」から開く。コンテンツブラウザーでアニメーションアセットをダブルクリックしても開ける。

## すぐに試す

`Project/Samples/AnimationStudioDemo/AnimationStudioDemo.nxproject`に、移動クリップ、待機クリップ、条件付きController、クリップを並べたTimelineを用意している。

1. 開発版エディタでサンプルを開き、OutlinerのActorを選択する。
2. コンテンツブラウザーのAssets/Animationsを開く。
3. Move.nxanimをダブルクリックし、「プレビュー」を有効にして目盛りをドラッグする。
4. 「停止・シーンへ戻る」で複製Worldを閉じる。編集WorldのTransformは変更しない。
5. Actor.nxcontrollerを開くとEntry、Any State、Idle、MoveがImNodesで表示される。Speedを変えてスタジオ内の再生を確認する。
6. Scene.nxtimelineを開くとクリップ区間を移動・伸縮・スクラブできる。
7. 「選択Entityへ設定」でアセットを保存してコンポーネントへ設定し、Levelも保存する。ツールバーのPlayではゲームのWorldで再生する。

開発用の検証ビルドはGenerated/AnimationStudioLatestに作成している。配布済み1.3.0は更新していない。既存プロジェクトのエンジン関連付けも変更していない。

## クリップを作る

「新規 → Animation Clip」を選び、Assetsからの相対パスを入力する。「Property Trackを追加」から、選択EntityのTransform、色、透明度、数値やBoolのプロパティを選ぶ。対象が子Entityなら名前の相対階層パスを指定する。同名の子が複数ある曖昧な階層はバインドしない。

目盛りで時刻を設定し、「現在時刻へキー追加」でサンプル値のキーを追加する。「選択Entityの現在値を記録」は編集Worldの値をその時刻へ記録する。自動録画ではなく明示的なキー記録である。ダイヤをドラッグして時刻を変更できる。キーの値は下の数値欄で編集する。

Step、Linear、CubicSplineを選べる。CubicSplineのIn/Outは毎秒の変化量で、下のカーブに結果を表示する。Quaternionの回転trackは線形モードで最短経路補間する。長さは最後のキー・イベントより短くできない。

「イベント追加」で時刻・名前・Payloadを登録する。描画や停止中のシークでは通知しない。ゲームの更新時だけLevelのAnimationRuntimeへ通知される。

```cpp
level.Animations().SetEventHandler([](const NexusEngine::AnimationRuntime::Event& event) {
    if(event.name == "Footstep") {
        // ゲーム側の足音処理へ渡す。
    }
});
```

汎用プロパティは既存Reflectionの検証付きsetterを使う。対象のコンポーネントが存在しない、次元が違う、値がsetterの制約に違反する場合は診断を返す。

## glTF / GLBから取り込む

モデルをAssets内へ配置し、「モデルから取り込み」から選ぶ。Assets/Animations/モデル名にクリップ単位の.nxanimを作る。既存アセットは上書きしない。

取り込んだ参照型クリップを開いて「編集可能なキーへ展開」を押すと、骨格TRSを.nxanim内の編集可能なキーへ複製する。参照型のままなら元glTF/GLBとbuffer等の依存先を保持する必要がある。クリップ名が重複する場合は元モデルで名前を分ける。

骨格アニメーションは描画モデルへ階層パスでバインドする。同じ階層・軸・bind poseを持つ別モデルでも利用できる。Humanoidのような異なる骨格へのリターゲットは未対応。

## Animator Controller

「新規 → Animator Controller」で.nxcontrollerを作る。Stateに単一Clipまたは1D/2D Blend Treeを設定する。

- Entryの出力ピンからStateへ接続すると開始Stateになる。「Entryに設定」でも変更できる。
- Stateの出力から別Stateの入力へドラッグするとTransitionになる。
- Any Stateからの接続は、現在Stateに依存しない遷移になる。同じStateへの再遷移は実行しない。
- 接続を選ぶと対応するTransitionを示す。下のTransitionsで条件、遷移秒数、Exit Time、切替先の開始秒を編集する。
- Float/Int/Bool/TriggerのParameterを設定できる。条件はANDで評価する。成立して使われたTriggerだけを消費する。
- 遷移中は元と先の両方の時刻を進めて混ぜる。遷移中の別Transitionによる割り込みは未対応。
- 1Dは隣接する閾値の2入力を補間する。2Dは配置点の逆距離重みを使い、点の上ではその入力だけになる。Unityの2D Directionalアルゴリズムの互換実装ではない。

中ボタンでpan、ホイールでカーソル位置を基準にzoom、Deleteで選択State/Transitionを削除できる。最後のStateとEntryの開始線は削除しない。ノード位置はアセットへ保存し、pan/zoomは表示状態として扱う。

```cpp
#include "Runtime/Scene/Components/AnimatorComponent.h"
auto* animator = entity.GetComponent<NexusEngine::AnimatorComponent>();
animator->SetFloat("Speed", 1.0f);
animator->SetBool("Running", true);
animator->SetTrigger("Jump");
```

## Timeline

「新規 → Timeline」で.nxtimelineを作る。Animation Trackに同じ.nxanimを配置する。対象はTimelineコンポーネントを付けたEntity自身、または子の相対階層パス。

- 区間の本体をドラッグすると移動し、端をドラッグすると長さが変わる。
- Start、Duration、Clip In、Speed、Loop、Ease In/Outを数値でも編集できる。
- 重なる区間は共通ミキサーで合成する。2クリップに限定せず複数入力を扱う。
- Timelineの有効区間はAnimatorより優先する。区間外ではAnimatorへ戻り、Animatorがなければ未アニメーション値・bind poseへ戻す。
- Activation Trackは描画コンポーネントのenabledを区間に従って制御する。Entity全体のScriptやPhysicsを無効化する機能ではない。
- timeを設定してplayingをfalseにすると絶対時刻でシークできる。

音声・カメラ・イベント専用のTimeline Trackは未実装。

## 共通ノード編集

Animatorとポストエフェクトは`Editor/Gui/Nodes/NodeEditor`を共用する。ImNodesへの直接依存はこのラッパー内に集めている。新しいノード編集機能を作る場合は[NodeEditor.md](NodeEditor.md)を参照する。

## アセット形式と互換性

.nxanimのversion 1（sourceとclipの参照）を引き続き読み込む。version 2はduration/loop、編集可能なtracks、eventsを持つ。sourceを持たないクリップはモデルなしで作成できる。

```json
{
  "version": 2,
  "duration": 1,
  "loop": true,
  "tracks": [{
    "target": "",
    "component": "Transform",
    "property": "translation",
    "interpolation": 1,
    "keys": [
      {"time": 0, "value": [0, 0, 0]},
      {"time": 1, "value": [2, 0, 0]}
    ]
  }],
  "events": [{"time": 0.5, "name": "Midpoint", "payload": ""}]
}
```

インポート定義だけをPowerShellで作る場合はTools/Project/Import-Animations.ps1も利用できる。Levelの従来のAnimationPlayer番号指定と2クリップのCrossFade APIは維持している。

## 現在の制約

IK、Constraint、Humanoid/Avatar、リターゲット、レイヤー/部位マスク、Controller Override、ルートモーション、モーフ頂点変形、負の速度、連続自動録画、曲線ハンドルのマウス編集は未対応。モーフを含むglTFでもモデル全体を拒否せず骨格/TRSを読み込み、警告する。Skinは既存の64 joints上限。イベントは名前とPayloadの通知APIであり、任意のC#メソッドを選択するUIはない。

## 検証

- NexusEcsTests --animation-system：キー・カーブ・イベント、Controller遷移、1D/2D、Timeline優先とシーク、値の復元、保存・復元。
- NexusEcsTests --animation：従来再生とクロスフェード、Level保存。
- NexusEditorTests：共通ImNodesのcontext/選択/view分離、既存PostProcessのzoom/pan/接続/Undo、StudioのUndo/保存/プレビュー/Playと3種類の画面。
- NexusGraphicsTests --animation-assets：glTF/GLB、実モデル14クリップ、編集可能なボーンキー、多層入力、階層バインド。
- NexusGraphicsTests --animation：TRS/skinのGPU画像、個体別姿勢、2クリップと複数入力ミキサーの影への反映。
- サンプルプロジェクトをゲームモードで3 GPU frames実行して正常終了を確認する。

全ECSテストには、既存のBlank/Basic3DテンプレートにSkyとCameraがあることを要求する前提不一致が残る。アニメーション対象テストは別指定で検証する。
