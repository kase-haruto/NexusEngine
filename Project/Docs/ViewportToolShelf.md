# Viewport Tool Shelf

Viewport左端にSelect／移動／回転／拡縮を表示します。通常は幅36、ボタン28、余白4、間隔3の論理ピクセルを使い、UI倍率へ変換します。
「>」でツール名付きの幅150へ展開し、「<」でアイコン表示へ戻せます。
Headerは右上に配置し、Local/World、Snap、視点、表示の4つの設定を持ちます。
端の矢印でHeader全体を折り畳めます。各アイコンには詳細用の矢印を分離し、Popupの開閉はその矢印で行います。
アイコンは座標系・Snap・ギズモ表示の直接切替と状態表示を担当します。視点の変更は詳細内で行います。
表示のPopup内部もガイド・情報表示・プレビューサイズのカテゴリごとに折り畳めます。

## 構成

| ファイル | 責務 |
| --- | --- |
| `Editor/Gui/Panels/UtilityPanels.cpp` の `ViewportPanel` | Scene画像と各Overlayの構成、実際の小領域による入力遮断 |
| `Editor/Gui/ViewportChrome.h` の `ViewportToolShelf` | Descriptorの反復描画、選択状態、Tooltip、展開・Group境界 |
| 同ファイルの `ViewportHeader` | 設定を小幅の子Windowへ構成 |
| `Editor/Services/ViewportToolRegistry.h` | Tool記述と既存 `EditorCommandRegistry` の接続 |
| `Editor/Gui/ViewportTools.cpp` | Tool選択、座標系、Snap、視点、既存ImGuizmo・Transformトランザクション |
| `Editor/Gui/Core/GuiTheme.h` | `ViewportToolShelfStyle` の寸法データ |
| `Editor/Gui/EditorChrome.cpp` | 共通ツールバーの自動配置と手動配置・リサイズ補正 |
| `Editor/Services/EditorSettings.cpp` | 個人用配置の保存・復元と旧オフセットの読込 |

上表は `Project/` を基準とします。Viewport UIの追加はEditor内に収めています。
Scene画像は従来どおりの全表示領域で描画・Resizeし、その前面へ小さなImGui子Windowを描きます。
左Shelfと右Headerの実際の矩形、診断・プレビュー領域、開いたPopupで入力を遮断します。
残りの画像領域では既存のPicking・視点移動・ギズモ操作を継続します。

## ToolとCommand

`ViewportToolDescriptor` は安定ID、翻訳キー、画像キー、Group、図形のフォールバック、Shortcut、派生元IDを保持します。
`ViewportTools::RegisterTool` が選択状態と実行可否を既存Commandへ接続し、Tool ShelfはCommandへ選択要求を渡します。
独自Toolは通常のギズモを停止してから選択時callbackを実行します。Groupは複数Toolを持てます。
`variantOf` は派生Tool用の情報です。Box/Circle/Lasso等の選択処理は今回の実装範囲に含めていません。

既存のW＝移動、E＝回転、R＝拡縮を維持しています。
Tool用CommandのDispatchはViewportとその子Windowのフォーカス範囲で行い、文字入力・Popup・視点移動・ギズモ操作中は抑止します。
Selectはクリック選択を使い、Transformギズモを描画しません。

PNGが配置されていれば既存のEditorアイコン管理を使います。
PNGがない場合も固定幅の図形アイコンを描き、Tooltipに翻訳したツール名とShortcutを表示します。
色は現在のEditor Themeから取得します。画像名と追加コード例は
[アイコンと配置ガイド](../Resources/Editor/Icons/README.md)にまとめています。

## 共通ツールバーのリサイズ補正

手動配置した項目は、近い側の左右端からの距離と、上端からの距離を論理座標で保持します。
ウィンドウの幅が変わったら、端の位置と現在のボタン実寸から表示位置を再計算します。
自動配置が折り返しても、手動項目の高さは保存した値を使います。
これに合わせて旧APIのオフセットも現在の自動配置位置から再計算します。

狭い画面へのクランプは表示時だけに適用します。元の配置は保存してあるので、幅を戻すと元の端の距離へ復元します。
`User.settings` には従来の `toolbar.offset.<ID>` と、新しい `toolbar.placement.<ID>` を保存します。
旧設定は初回描画の位置を基準に移行し、終了・再起動・Reinstancingでも配置を保持します。

## 検証

`NexusEditorTests` で以下を確認しています。

- Select／移動／回転／拡縮と追加Toolの選択、重複ID・Shortcutの拒否
- W/E/R、Local/World、Snap、視点・表示のPopup、Popup中のShortcut抑止
- Shelfの展開・折り畳み、狭いViewportでのHeader配置
- Overlay上の操作による誤選択の防止、画像領域での選択操作
- Shelf展開前後でSceneのResize領域が変わらないこと
- 既存のImGuizmo操作、Undo/Redo、カメラプレビューと診断表示
- 共通ツールバーの右端配置、縮小・拡大・復元、UI倍率変更
- 個人設定の保存・復元、不正な配置データの除外、リセット

Debug構成の `NexusEditor` と `NexusEditorTests` を対象にビルドします。
