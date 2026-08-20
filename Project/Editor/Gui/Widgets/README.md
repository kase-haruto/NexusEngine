# NexusEngine GUI Widgets

Widget APIを追加する場所です。継承型ではなく、`UI::Context&`とDescriptorを受け取る自由関数として実装します。

```cpp
ItemResult ImageButton(Context& context, const ImageButtonDesc& desc);
```

- 公開HeaderにImGui、Win32、DirectX 12の型を含めない
- 共通項目は`ItemOptions`、操作結果は`ItemResult`を使用する
- Textureは`TextureHandle`で受け取り、`ITextureProvider`経由で解決する
- Widget固有の永続状態は、将来Context内のStateStorageへ追加する
- `ImGui::Button`など単純なAPIを無条件にラップせず、Engine固有機能があるものだけ追加する
