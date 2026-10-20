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

## Editorでの使用方針

- 色・余白・角丸の基準は`Editor/ImGui/ImGuiStyle.cpp`の既存黒基調/Orange Accent。
  `ImGuiRenderer`初期化とPreferencesは同じ`UI::ApplyEditorStyle()`を使う。
  標準Dark/Lightテーマでこの配色を上書きしない。
- 検索欄は既存`SearchBar`を使用する。Outliner、Content Browser、Output Logで再利用する。
  Editorでは通常版を使い、全Panelに適用済みのImGuiStyleを継承する。
  Context版は呼出側が明示した別Themeを使用する場合のAPIとして維持する。
- 型別入力は`PropertyWidgets.h`に定義する。ReflectionのDrawerは値の変換だけを行い、
  入力Widgetや配色を独自実装しない。
- Button/Menu/Tree/Table等の標準部品はImGuiを直接使える。
  検索、Property編集状態、Asset選択など複数Panelで共有する意味のある部品をこの場所へ追加する。

## PropertyWidgets

```cpp
#include "Editor/Gui/Widgets/PropertyWidgets.h"

UI::ItemOptions options;
options.id = UI::ItemId::FromString("Transform.Translation");
options.label = localizedTranslation; // 呼出中に有効な文字列
auto result = UI::PropertyVector(options, translation);
if(UI::HasStatus(result.status, UI::ItemStatus::Activated)) BeginEdit();
if(result.Changed()) PreviewEdit();
if(result.EditFinished()) CommitEdit();
```

`PropertyBool/Int/Float/Text/Vector/Enum`が共通Style、表示名と独立したID、Tooltip、enabled/visible、
編集開始/変更/確定を扱う。Vectorは2〜4成分、TextはUTF-8 byte上限付き。
同じIDを異なるEntityやComponentへ配置する場合は外側のID scopeを分ける。
翻訳とRuntime値検証、Selection、Undo履歴は呼出側サービスが担当する。
編集終了だけが起こるframeもItemStatus::EditCompletedで通知する。

公開ヘッダーにImGui/DirectX/Runtimeの型は出さない。
これらのProperty WidgetはContext固有サービスを必要とせず、適用済みの共通Styleを継承するため
`ItemOptions`と値を受け取る自由関数として提供する。

今回の統合作業は静的検査のみ。ビルド・実行・外観確認は実施していない。
