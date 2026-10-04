# Editor Session

## 現在の構成

- `EditorApplication`: Framework更新とGUI・Renderer接続を担当する。
- `EditorSession`: 編集用Level、永続IDによる選択、変更状態を所有する。ImGuiとGPUに依存しない。
- `ScenePanels`: Hierarchy一覧とTransform Inspectorを描く自由関数。

Hierarchyは現時点では平坦なEntity一覧。InspectorはLocal座標、Quaternion回転、Scaleを編集する。
デモModelはMeshを持つNodeごとにEntityを生成し、PrimitiveRenderComponentのnodeIndexで描画対象を指定する。
TransformはModel内のNode配置に対する追加変換であり、元アセットのNode Transformを書き換えない。
通常描画・Shadow・Opticsは同じNode指定を使う。既定のnodeIndexはModel全体の描画を維持する。
Level保存形式v2にNode指定を保存し、v1は従来どおりModel全体として読み込む。
編集は値コピーから `SetTransform(PersistentId, translation, rotation, scale)` に渡す。
SessionがIDを現在のWorldへ解決し、値を検証して適用する。回転は正規化する。
派生Matrixは既存のLevel更新によって再計算する。

選択状態にEntityやComponentのアドレスを保持しない。Worldを作り直す際は選択と変更状態を解除する。
Levelへの直接アクセスは既存デモの構築・更新・抽出との接続に使用しており、通信APIではない。
変更状態はTransform編集を検出するだけで、保存、Undo、他の診断ウィンドウの変更は未接続。

## 次の段階

1. SceneDocumentへ保存先、ロード・保存、変更状態を移す。
2. Transform編集をトランザクションへまとめ、Undo / Redoを実装する。
3. 編集用カメラとTexture描画先を持つViewportを導入する。
4. Editor非依存のPlayerを単独起動し、再生用Worldを編集Worldから分離する。
5. PreviewSessionの境界にScene同期・入力・選択応答を実装する。
6. 別Processホスト、IPC、GPU共有TextureとFence同期を実装する。

Runtime本体へImGuiやEditorSessionへの依存を追加しない。
IPC化時は永続IDとLocal値を明示的に符号化し、C++構造体のメモリをそのまま通信しない。
通信にはセッション識別、要求ID、Scene更新番号を追加し、再接続時はScene全体から復元する。
現在の実装は同一Processであり、Viewport転送、Play / Stop、保存機能は提供しない。
