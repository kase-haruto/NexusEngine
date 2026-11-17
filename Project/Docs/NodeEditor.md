# 共通NodeEditor

ImNodesは`Editor/Gui/Nodes/NodeEditor.cpp`だけから直接操作する。現在はPostProcessGraphPanelとAnimationStudioPanelがこのラッパーを使用する。別々のContextを所有するため、同じ数値IDを使っても選択・接続・panが混在しない。

この層は描画と入力だけを担当する。型付きピンの接続検査、サイクルや状態の検証、アセットの保存、Undo/Redo、ノードIDの割り当ては各Documentが担当する。アニメーションとレンダリングの意味を共通UIへ持ち込まない。

## 最小例

Panelのメンバーに`UI::NodeEditor editor_;`を置き、Drawで次のように使う。Node、Pin、LinkのIDは同じCanvas内で一意にする。

```cpp
UI::NodeEditorSession session(editor_);
{
    UI::NodeEditorCanvas canvas(editor_, zoom_);
    editor_.SetNodePosition(10, {position[0]*zoom_, position[1]*zoom_});
    editor_.BeginNode(10);
    editor_.BeginTitle();
    ImGui::TextUnformatted("My Node");
    editor_.EndTitle();
    editor_.BeginInput(30);
    ImGui::TextUnformatted("Input");
    editor_.EndInput();
    editor_.BeginOutput(31);
    ImGui::TextUnformatted("Output");
    editor_.EndOutput();
    editor_.EndNode();
    // 他のNodeを描いた後、Document内の接続を描画する。
    // editor_.Link(linkId, outputPin, inputPin);
    editor_.MiniMap();
}
int output = 0;
int input = 0;
if(editor_.LinkCreated(output, input)) {
    // Document側でピンの方向・型・接続制約を検証して保存する。
}
```

Sessionは前のImNodes contextを復元し、Canvasはfont/styleの倍率を復元する。Canvas内に設定したstyleを他のパネルへ残さない。明示的に終了後の入力を処理したい場合は`canvas.Close()`を呼べる。いずれのスコープもコピーしない。

## 共有する操作

- Node/Title/Input/Output/Static属性のBegin/End。
- Link描画、作成・切断の検出、hover、選択と選択解除。
- Grid/Screen座標、Node寸法、pan。
- カーソルを基準にしたZoomAtとFrameAll。現在の倍率範囲は25～200%。
- ミニマップ、Ctrl+click/ドラッグによる接続の切断。
- Canvas IDとContextの所有、ネスト時の復元。

位置の保存は論理座標で行う。描画には論理座標×zoomを渡し、ドラッグ結果はNodePosition()/zoomでDocumentへ戻す。pan/zoomだけの変更でアセットをdirtyにしない。ポストエフェクトとAnimatorはこの規則を共用する。

静的属性は空のImGui Groupにしない。ImNodesのtitle paddingと前フレームの矩形がノード幅へ戻り、毎フレーム幅が増えるのを避ける。UIの意味ごとの色はPushTitleColor/PopTitleColorで指定できる。

NativeContext()はテスト・診断のために公開している。製品のパネルからImNodesを直接呼ぶためには使用しない。新しいノード機能もラッパーへ必要な操作を追加してから使用する。

NodeEditorTestsはContext・選択・viewの分離、ネストしたSessionの復元、style漏出防止、カーソル基準zoomを検証する。PostProcessGraphTestsは移行前からの操作と描画寸法の回帰検証を維持する。
