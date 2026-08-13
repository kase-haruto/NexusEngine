# Editor GUI Architecture

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

WidgetはImmediate Modeの自由関数として作り、PanelやWidgetの継承ツリーは設けません。公開APIでは`ItemId`を表示文字列から分離し、画像は`TextureHandle`から`ITextureProvider`を通してbackend参照へ変換します。

## 拡張順

1. ImGui用のID・Style・Disabled scopeアダプター
2. ImageとImageButton、およびDX12 Texture provider
3. ToolbarとLayout helper
4. Property系Widgetと編集トランザクション
5. Drag & Drop、Popup、StateStorage
