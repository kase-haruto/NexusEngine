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

1. ImGui用のID・Style・Disabled scopeアダプター
2. ImageとImageButton、およびDX12 Texture provider
3. ToolbarとLayout helper
4. Property系Widgetと編集トランザクション
5. Drag & Drop、Popup、StateStorage
