# SearchBar

> Editor統合追記: Outliner / Output Log / Content BrowserがこのWidgetを使用する。
> 共通Styleは`Editor/ImGui/ImGuiStyle.cpp`へ移動し、元の配色と寸法を維持する。
> 寸法補正はWindow DPI × FontScaleMain（User UI Scale）に対応する。
> 下記の過去のテスト実行記録とは別に、今回の変更はビルド・実行せず静的確認のみ実施した。

虫眼鏡付きの検索入力Widget。`NexusEngine::UI` の自由関数として描画し、UTF-8の検索文字列はPanelが所有する。検索、ソート、キャッシュ、永続化、GPUリソース管理は行わない。

## 調査結果と配置

- 同梱Dear ImGuiは **1.92.9b / 19291**、Docking対応。
- `Editor/ImGui/ImGuiRenderer` がContext、Win32入力、DX12描画、日本語フォント、黒基調とオレンジのスタイルを初期化する。
- フレームはRenderer BeginFrame → UI Context BeginFrame → UI描画 → UI Context EndFrame → Renderer Recordの順。
- `Editor/Gui/Core` は公開契約、`Widgets` はエンジン固有GUI部品、`Panels` は画面単位。既存Widget用READMEに合わせて公開ヘッダーにImGui型を出さない。
- 調査時点でPropertyWidgetsとOutlinerPanelの未追跡ファイルが存在したが、中身は空だった。これらには変更を加えていない。
- `UI::Theme` はContextが保持するが、ImGuiスタイルへの自動適用はない。通常版は既存ImGuiスタイル、Context版はThemeのsurface/text/textDisabled/accentを使用する。枠の通常色はImGuiCol_Border。
- エディタ独自のimgui_internal.h利用は調査時点で見つからなかった。今回の依存はSearchBar.cppに閉じる。
- 公式 `misc/cpp/imgui_stdlib.h/.cpp` は同梱済みだが、既存vcxprojのコンパイル対象ではない。追加リンクせず、書き込み可能なvectorの要素領域を使う。
- RendererにDPIサイズ補正の共通機構はない。本WidgetはGetWindowDpiScale()で寸法を補正する。モニターDPI認識、フォント倍率、OS側DPI awarenessの設定はホストの責務。
- 添付画像はこの会話では参照できないため、文章の寸法指定と既存テーマを使用した。

## 公開API

```cpp
bool NexusEngine::UI::SearchBar(
    const char* label, std::string& text,
    const char* hint = "Search...", float width = 0.0f);

bool NexusEngine::UI::SearchBar(
    Context& context, const char* label, std::string& text,
    const char* hint = "Search...", float width = 0.0f);
```

```cpp
#include "Editor/Gui/Widgets/SearchBar.h"

// Panelのメンバーとして保持する。
std::string searchText_;

// ImGui::Begin()とEnd()の間で呼ぶ。
if (NexusEngine::UI::SearchBar("##AssetSearch", searchText_, "Search Assets...")) {
    UpdateFilteredAssets(searchText_); // Panel側の処理
}

// 既存Themeを使用したい場合
NexusEngine::UI::SearchBar(guiContext, "##HierarchySearch", hierarchySearch);
```

labelは必須の非空識別子で、表示しない。同じWindow内では異なるlabel、または外側のPushIDで区別する。hintはIDに含めず、nullptrは空のHintとする。widthは描画座標系のピクセル幅で、0以下は現在のセルやWindowの利用可能幅。正のwidthを追加でDPI倍しない。

返り値は呼び出し開始時の文字列と入力処理後の値の比較。外部変更だけではtrueにならず、そのフレームにユーザーが追加編集した場合はtrue。空への削除も検出する。外部変更による検索結果の更新はPanel側で行う。

## 描画と入力

1. ビューポートDPIから高さ32、左余白10、アイコン14、間隔8を拡大する。大きなフォントでは高さを拡張する。
2. labelをPushIDし、内部の非表示ラベルを固定する。
3. アクティブなImGui入力状態と外部文字列が異なる場合、ReloadUserBufAndKeepSelection()を要求する。
4. std::stringを終端付きvectorへコピーし、InputTextEx()を一度呼ぶ。入力のレイアウト登録、ID、マウス、Tab、選択、カーソル、コピー＆ペースト、IMEは標準実装に委ねる。
5. InputTextExが背景とHint/入力を描画する。描画後にフォーカス枠と虫眼鏡を追加する。

ItemSize/ItemAddは重複して呼ばない。カーソル位置を手動変更せず、LastItemDataも標準入力のものを保つ。アイコンも入力欄のFramePadding内なので、クリックは同じ入力Widgetに届く。

FramePadding.xは `左余白 + アイコン幅 + 間隔`。ImGuiは左右対称のpaddingを使うため、右側にも同じ余白ができる。これは標準テキストクリップと水平スクロールを維持するための意図的な設計。基本サイズで90px程度以上の幅を推奨する。非常に狭い欄では入力表示に十分な幅を確保できない。

枠の角丸は高さの50%。色はテーマを優先するため、参考色#121212/#414141等とは一致しない場合がある。通常版のフォーカス色はエディタでアクセントが設定されるImGuiCol_CheckMark。入力文字色とHint色はText/TextDisabled。

虫眼鏡は左中央の14px正方形内にAddCircleで円を描き、右下45度のAddLineで持ち手を描く。線幅、円中心、半径、持ち手端点を定数化した。Window/TableのClipRectとWidget矩形を交差させて描画し、入力中もアイコンは常時表示する。

StyleScopeが3個のStyleVarと3個のStyleColorを復元する。検索文字列やPanel情報をstatic変数に保持しない。

## std::stringとバッファ安全性

string::capacity()の未構築領域へ書き込まず、vectorのsize分のchar要素を入力先にする。CallbackResizeでvectorをresizeし、再確保後のBufを返す。終端を含めたサイズを渡し、入力終了後にstd::stringへ反映する。固定長による切り詰めは行わない。

一時vectorは毎回構築されるため文字列長に比例したコピーがある。通常の検索入力では保守性を優先した。ImGuiの長さ型がintなのでINT_MAX付近の文字列は描画を拒否する。文字列はUTF-8、埋め込みNULなしを前提とする。

## Internal APIを選んだ理由

| API | 用途 |
|---|---|
| GetCurrentWindow / ImGuiWindow::GetID | SkipItemsの確認、入力IDの特定、現在の描画リスト |
| InputTextEx | 高さと幅を直接指定し、標準の入力処理とHintを利用 |
| GetInputTextState | アクティブな入力内容と外部文字列を比較 |
| ReloadUserBufAndKeepSelection | 外部変更を再読込し、選択を可能な範囲で維持 |
| GImGui->ActiveId | 残存する非アクティブ入力状態を再読込対象から除外 |

InputTextWithHint()だけでもHint付きの入力はできるが、今回は高さ指定、アイコン用余白、丸枠、入力中の外部変更を追加する。IMEや文字編集自体を独自実装する必要はない。内部依存は1.92.9bのソースで確認しており、ImGui更新時にはシグネチャと再読込動作を再検証する。

## 実装方式の比較

| 方式 | メリット | デメリット／判断 |
|---|---|---|
| Public APIのみ | バージョン更新に強い。標準入力を再利用できる | paddingと装飾は実装できるが、入力中の外部変更の再読込を扱いにくい |
| Internal API併用（採用） | 高さ指定と再読込ができ、標準入力機能を維持 | ImGuiの更新時に検証が必要 |
| テキスト入力を完全独自実装 | 描画と挙動を全面制御できる | IME、選択、Undo、クリップ等の維持が重く、今回不要 |
| 関数型・外部状態（採用） | Panelが所有権を持ち、複数配置と外部更新が明確 | 呼び出し側が文字列を保持する必要がある |
| 関数型・内部状態 | 呼び出しが簡単 | IDごとの寿命管理と外部同期が複雑 |
| Widgetクラス型 | 複雑な永続状態をまとめられる | 単一入力欄には不要な所有関係と初期化が増える |

Context版は既存の自由関数＋Context方針を維持し、簡易版は依頼のAPIで呼べる。公開APIにDescriptorとItemResultを要求しないのは、検索入力の最小契約を優先したため。

## 検証

`Project/Tests/NexusGuiTests.vcxproj` は既存テストと同じDebug/x64構成。Contextの既存std::expected依存に合わせC++23でビルドする（SearchBar独自の処理はC++20範囲）。

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' Project/Tests/NexusGuiTests.vcxproj /p:Configuration=Debug /p:Platform=x64
& ./Generated/Outputs/GuiTests/NexusGuiTests.exe
```

自動テストは実際のImGuiフレームを動かし、英字入力、UTF-8入力イベント、8192文字へのresize、Backspaceによる全削除、変更時のみtrue、入力中の外部変更、Hint変更、複数ID、サイズ、Styleスタック復元、Child/Table/Group/SameLineの組合せを検証する。

実OS上の日本語IME変換、クリップボード操作、マウス／Tabの操作感、Docking操作、モニター間DPI移動、外観はヘッドレステストでは未検証。UTF-8入力テストはIME変換の検証を意味しない。既存バックエンドの機能は置き換えていないが、エディタでの手動確認が必要。

今回の専用Debugテストはビルド・実行成功。最初の全体Debugビルドは成功したが、並行して編集された最新の全体構成ではOutlinerPanel.cppのHierarchyPanel.h参照とPropertyWidgets.cppの警告エラーで失敗した。これらはSearchBar以外の変更であり、その修正は行っていない。

## 今後の拡張

クリアボタン、候補ポップアップ、件数、履歴、アニメーションは追加していない。必要になった時点でDescriptorに外観設定や任意の操作を追加し、文字列の所有は外部に維持する。検索非同期化や結果管理はPanelまたは検索サービス側に置く。複数Widgetで実装補助が共通化した場合のみInternalへ切り出す。
