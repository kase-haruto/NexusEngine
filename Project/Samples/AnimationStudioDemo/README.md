# Animation Studio Demo

移動/待機の.nxanim、条件で切り替える.nxcontroller、時間軸で再生する.nxtimelineのサンプル。

開発版NexusEditorでAnimationStudioDemo.nxprojectを開く。Actorを選び、コンテンツブラウザーのAssets/Animations内のアセットをダブルクリックする。「プレビュー」やスタジオ内の「再生」は編集Worldを変えない。ゲームのPlayではActorに設定済みのScene.nxtimelineを再生する。

Actor.nxcontrollerのプレビューではSpeedを0から1にするとIdleからMoveへ遷移する。Move.nxanimの目盛りはドラッグでシークでき、キーを選んで値や補間を編集できる。Scene.nxtimelineでは区間を移動・伸縮できる。

詳細はProject/Docs/Animation.md、共通ImNodesラッパーはProject/Docs/NodeEditor.mdを参照。
