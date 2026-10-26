# Editor GUI Architecture

> 2026-10-16追記: Widgetは従来の自由関数方式を維持する。Panelには表示状態と動的登録が必要なため、
> IEditorPanel/PanelRegistryの最小契約を追加した。現在の全体像と制限は
> [EditorFoundationREADME.md](EditorFoundationREADME.md) を参照。

## レイヤー

1. `Editor/Gui/Core`: ImGuiやGraphics APIに依存しない公開契約
2. `Editor/Gui/Widgets`: NexusEngine固有WidgetとGUI Item
3. `Editor/ImGui`: Core契約をDear ImGuiへ接続する実装とRenderer backend
4. `Editor/Core`: Frame進行とEditor画面の組み立て

## Frame順序

```text
ImGuiRenderer::BeginFrame
UI::Context::BeginFrame
Editor UI / Widgets
UI::Context::EndFrame
ImGuiRenderer::Record
```

WidgetはImmediate Modeの自由関数として作り、Widgetの継承ツリーは設けません。
Panelには登録と開閉状態を管理するための最小契約`IEditorPanel`を使用します。
公開APIでは`ItemId`を表示文字列から分離し、画像は`TextureHandle`から`ITextureProvider`を通してbackend参照へ変換します。

## Styleと共通部品

グレーの面階層と控えめなOrange Accentを`Editor/ImGui/ImGuiStyle.cpp`へ集約しています。
タブ・パネル・入力欄を明度で分け、入力欄はコンパクトな余白と共通の角丸を使います。
Renderer初期化とPreferencesは共通`UI::ApplyEditorStyle`を呼び、ImGui標準themeで上書きしません。
寸法は元の基準値へ戻してから倍率を適用し、拡大を累積させません。
設定変更は次のImGui NewFrame前に適用します。Lightは同じアクセントを持つ明色派生です。

検索欄は既存の`Widgets/SearchBar`を使用します。
`Widgets/PropertyWidgets`はbool/int/float/string/vector/enumの共通入力を提供し、
`PropertyDrawerRegistry`はReflection値との変換だけを担当します。
DetailsはWidgetが返す`ItemResult`で編集区間を判断し、popup描画後のImGui最終Itemへ依存しません。
Reflectionの通常プロパティは左ラベル・右入力の2列に揃え、Assetはプレビュー用の全幅表示を維持します。
VectorとRotationは共通の軸入力を使い、XYZWの文字とThemeの軸色で成分を識別します。
幅が不足すると成分を縦に並べ、各入力の編集状態を集約してUndoへ渡します。
数値編集は標準DragFloatを使うため、ドラッグ・Ctrl+クリックの直接入力・キーボード操作を継承します。
新しい再利用可能な部品もWidgetsへ定義してPanelから使います。
標準Button/Menu/Tree/Tableを意味なくラップする必要はありません。

## 拡張順

### 共通ツールバーへの機能登録

Play/Stop、保存、Undo/Redo、Viewport操作は`EditorToolbarRegistry`を通して同じツールバーへ配置します。
`EditorChrome`は配置と描画だけを担当し、各機能の内部を参照しません。
登録の入口は`EditorWorkspace::Toolbar()`です。外部機能は既存の`EditorApplication::AddEditorExtension`から登録すると、Workspaceの再生成時にも再登録されます。

通常のボタンは、実行処理を`EditorCommandRegistry`へ登録し、表示名とCommand IDを`RegisterButton`で接続します。
既存Commandを使う場合は、その接続だけで追加できます。例えば、既存のフォーカス操作をボタンとして追加するコードは次のとおりです。

```cpp
// 実行処理と実行可否は、既に登録されているviewport.focusのCommandを使う。
// 最後の数値は表示順。Viewport操作のセクションより後ろへ配置する。
workspace.Toolbar().RegisterButton(
    "viewport.focus.button",
    "viewport.focus",
    "viewport.focus",
    400
);
```

新規の機能では`Command::execute`へ処理、`Command::canExecute`へ現在の実行可否を設定します。
ツールバーは毎フレーム実行可否を取得してボタンを無効化し、クリック時にも`Commands().Execute`で再確認します。
メニュー・Shortcut・ツールバーは同じCommand IDを使うため、入口ごとに処理を複製する必要がありません。
表示名のキーは英語・日本語の辞書へ登録します。

スライダー、チェックボックス、ドロップダウンなど複数のUIを含む機能は`RegisterSection`を使います。

```cpp
EditorToolbarRegistry::Section section;
section.id = "tools.previewSettings";
section.order = 500;

// フォントサイズを単位として、同じ行へ配置できるか判断するための最低幅を指定する。
section.minimumWidthEm = 20;

// 設定の所有者を借用する。所有者はWorkspaceのコールバックが解除されるまで保持する。
section.draw = [&previewSettings](const auto& text, const auto& commands) {
    previewSettings.DrawToolbar(text, commands);
};
workspace.Toolbar().RegisterSection(std::move(section));
```

各セクションのIDは一意とし、`order`が同じ場合は登録順を維持します。
描画中の登録・解除は拒否します。動的機能の解除はフレームの描画外で`Unregister(id)`を呼び出します。
Workspaceの終了・ライブコーディングによる再生成では、旧オブジェクトや旧コードを借用するコールバックを解除します。
標準ボタンは翻訳後の実寸、専用セクションは最低幅に応じて折り返します。折り返し後の高さは次フレームの作業領域へ反映します。

セクション上の右クリックメニューから、X/Yオフセットを調整できます。
オフセットは自動配置の位置を基準とする論理ピクセル値で、フォントサイズ18pxを基準に描画時の倍率へ変換します。
移動先も含む占有領域を後続の配置とツールバーの高さに反映し、左上や右端の画面外へ出る位置は制限します。
登録側からは`Toolbar().SetOffset(id, {x, y})`で同じ値を設定できます。初期位置へ戻す場合は`{0, 0}`を指定します。
設定値は`User.settings`の`toolbar.offset.<セクションID>`へ保存し、未登録の拡張機能のIDも保持します。
描画コールバックを再生成するライブコーディングでは、オフセット値だけをスナップショットへ含めます。

### GUI機能の拡張順

1. ImGui用のID・Style・Disabled scopeアダプター
2. ImageとImageButton、およびDX12 Texture provider
3. ToolbarとLayout helper
4. Property系Widgetと編集トランザクション
5. Drag & Drop、Popup、StateStorage
