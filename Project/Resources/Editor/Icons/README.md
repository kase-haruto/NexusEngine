# エディタのボタン画像

エディタの「ヘルプ → エディタのアイコンと配置ガイド」から、いつでもこのガイドを参照できます。
ウィンドウをドッキング・スクロールでき、再読み込み・全文コピーにも対応しています。

File／編集／実行などのメニューは、タイトルバーのNXロゴの右側、NexusEngine名の左側に表示します。
幅が足りない場合は「...」から全メニューを開けます。メニュー部分はクリック操作を受け取り、エンジン名や余白からウィンドウをドラッグできます。

このディレクトリはエンジン付属のUI画像の配置先です。
開発チェックアウトでは `Project/Resources/Editor/Icons/`、配布エンジンでは
`Resources/Editor/Icons/` にPNGを置き、エディタを再起動してください。
ゲームのAssetsフォルダに置く必要はありません。

Downloadsに用意された27枚を名前で対応付け、29個の表示先へコピー済みです。
元ファイル名・配置名・用途・寸法・SHA-256は同梱の `IconSources.json` に記録しています。
`physics.png` と `sqriptcomponent.png` は、既存の物理・スクリプトコンポーネント表示に使います。
フォルダ・展開中フォルダ・モデル・画像用のテクスチャはContent Browserに適用しています。

| 操作 | ファイル名 |
| --- | --- |
| 保存 | `file.save.png` |
| 元に戻す / やり直す | `edit.undo.png` / `edit.redo.png` |
| 再生 / 一時停止 / 再開 / 停止 | `play.start.png` / `play.pause.png` / `play.resume.png` / `play.stop.png` |
| C++リロード | `project.reloadCpp.png` |
| 選択（Select） | `viewport.select.png` |
| 移動 / 回転 / 拡縮 | `viewport.translate.png` / `viewport.rotate.png` / `viewport.scale.png` |
| ローカル / ワールド | `viewport.local.png` / `viewport.world.png` |
| スナップ | `viewport.snap.png` |
| カメラ設定 | `viewport.cameraTools.png` |
| 表示設定 | `viewport.show.png` |

ViewportのTool・設定ボタンは、選択／有効状態をエンジンのアクセント色の背景と枠で示します。
Localはアクセント色、Worldは通常色で座標系を示し、Tooltipに座標系の名前を表示します。
スナップは有効時、表示ボタンはギズモ表示中に強調します。視点ボタンは設定Popupを開いている間に強調します。
表示ボタンのTooltipでは、ギズモ・ガイド・診断それぞれの有効／無効も確認できます。
右上Overlayの端の矢印で操作部全体を折り畳み・再展開できます。
各アイコンの右側の矢印で詳細Popupを開閉します。アイコン自体では詳細を開きません。
詳細PopupはHeaderの下端から6論理ピクセル空けて表示し、ボタンやアイコンに重ならないようにします。
座標系アイコンはLocal/World、スナップアイコンはスナップ、表示アイコンはギズモ表示を切り替えます。
視点アイコンは状態表示で、視点の設定は隣の矢印から行います。
表示設定の詳細は「ガイド」「情報表示」「プレビューサイズ」ごとにも折り畳めます。
Tool・Headerのボタンは28論理ピクセルへ縮小し、画像と図形も小さくしています。

画像は通常のPNGを配置するだけで差し替えられます。状態ごとに画像を変える場合は、任意で
`viewport.translate.active.png`（選択中）、`viewport.translate.inactive.png`（非選択）などを追加してください。
状態画像を優先し、ない場合は通常の `viewport.translate.png`、通常画像もない場合は既存の図形を使います。
同じ規則を `viewport.rotate`、`viewport.scale`、`viewport.select`、`viewport.local`、`viewport.world`、
`viewport.snap`、`viewport.cameraTools`、`viewport.show` に使用できます。

## Details左側のコンポーネント選択タブ

同じディレクトリに次のPNGを配置し、再起動してください。
画像があるタブだけを置き換えます。未配置のタブは既存の図形アイコンと操作を維持します。
選択状態の強調、ツールチップ、ボタンのサイズ・並び順も維持します。

| タブ | ファイル名 |
| --- | --- |
| オブジェクト（Transform） | `details.Object.png` |
| レンダリング | `details.Rendering.png` |
| マテリアル | `details.Material.png` |
| カメラ | `details.Camera.png` |
| ライト | `details.Light.png` |
| 空 | `details.Sky.png` |
| 距離フォグ | `details.Fog.png` |
| 空の大気 | `details.SkyAtmosphere.png` |
| スカイライト | `details.SkyLight.png` |
| 高さフォグ | `details.ExponentialHeightFog.png` |
| その他 | `details.Other.png` |

拡張カテゴリには `details.<PropertyCategoryの登録ID>.png` を使用します。
ファイル名の大文字・小文字を上表に合わせてください。

透過PNG、正方形（32×32 または 64×64 推奨）。表示はフォントの大きさに合わせて
拡縮します。最大128枚、1枚4MiB・1024×1024までです。ファイル拡張子は `.png` を使用します。
未配置・読み込み失敗の場合は文字ボタンを表示します。画像の再読み込みには再起動が必要です。
画像ボタンでもツールチップ、操作の有効・無効、ショートカットとCommandの動作は共通です。

## 追加のボタンを画像化する

`Editor/Gui/Widgets/EditorIconButton.h` の `UI::EditorIconButton(iconKey, label, fallbackSize)` を使用します。
`iconKey + ".png"` が画像名、`label` が未配置時の表示名です。
ImGuiの `##` / `###` によるIDもそのまま使えます。

```cpp
if(UI::EditorIconButton("my.action", text.Label("my.action", "button").c_str())) {
    commands.Execute("my.action");
}
```

`EditorToolbarRegistry::RegisterButton` で登録したボタンは、登録IDを画像名として自動対応します。
描画側でGPUリソースを管理する必要はありません。画像は `ImGuiRenderer` がロードし、終了時に解放します。

## ツールバー配置

Select／移動／回転／拡縮はViewport左端のTool Shelfに配置しています。
初期状態はアイコンのみです。上部の「>」で名前付き表示へ展開し、「<」で戻せます。
移動＝W、回転＝E、拡縮＝Rは従来どおりです。Selectはギズモを隠し、クリック選択を行います。
Local/World、スナップ、視点、表示は右上の小幅Headerに分離しています。
Tool ShelfとHeaderは画像に重ねて表示し、レンダーターゲットのサイズを減らしません。
ボタン・Popupへの操作は背後の選択やギズモへ伝わりません。
これらは固定のViewport内配置です。以下の移動・保存操作は保存／Undo／再生などの共通ツールバーに適用します。

- Alt＋左ドラッグでボタンを移動できます。操作そのものは実行しません。
- ボタン上の右クリックでX/Yオフセットを数値調整できます。無効なボタンも配置調整できます。
- 同じメニューから対象ボタンまたは全項目を初期位置へ戻せます。
- 移動後は自動保存し、次回起動時に復元します。
- 保存先は `%LOCALAPPDATA%/NexusEngine/Editor/User.settings` の `toolbar.offset.<登録ID>` と `toolbar.placement.<登録ID>` です。
  個人設定として各プロジェクトで共用します。Dockウィンドウのレイアウト保存とは別です。
- UI倍率が変わっても論理座標で位置を保持し、画面外に隠れる位置は表示時に制限します。
  手動位置は近い側の左右端からの距離と、上端からの高さで保存します。
  最大化・縮小・復元でも同じ行と端の距離を維持し、現在の自動配置位置に対するオフセットを再計算します。
  一時的に収まらない小さな画面では表示だけを制限し、保存した位置は壊しません。
  以前のオフセット設定は読み込み時の描画位置から新方式へ移行します。

## Viewport Toolの追加

`Editor/Services/ViewportToolRegistry.h` のDescriptorを `ViewportTools::RegisterTool` へ登録します。
識別子、翻訳キー、画像キー、Group、必要ならShortcutと選択時callbackを指定します。
Tool ShelfはRegistryを走査するので、`ViewportPanel`にボタン分岐を追加する必要はありません。

```cpp
workspace.ViewportEditor().RegisterTool(
    {"viewport.measure", "viewport.measure", "viewport.measure", "debug",
     ViewportToolGlyph::Other, 0, "", {}},
    [] { /* 独自Toolの開始処理 */ });
```

Group名は拡張側でも追加でき、境界にSeparatorを表示します。`variantOf`は派生Tool用のメタデータです。
Box/Circle/Lasso等の実際の派生操作は今回追加していません。
登録と実行可否・選択状態・Tooltip・Shortcutは既存の `EditorCommandRegistry` を再利用します。
独自Tool選択時はControllerが編集を確定し、通常のTransformギズモを停止してからcallbackを呼びます。
標準ToolはこれまでのImGuizmoとUndo/Redo経路を使います。
サイズは共通Themeの `ViewportToolShelfStyle` が管理し、18pxフォント基準でUI倍率に追従します。

## ヒエラルキー

行の背景は `BeginTable` の `ImGuiTableFlags_RowBg` で交互に表示します。
背景色はエンジン共通Styleの `ImGuiCol_TableRowBg`／`ImGuiCol_TableRowBgAlt` に従います。
行、空白、下部のルート領域を右クリックし、「エンティティ作成」メニューから
Entity・Model・Camera・Light・Skyを作成できます。作成先はシーンのルートです。
既存の作成Commandを使うためUndo/Redoに対応し、再生中など編集不可の状態では作成できません。
ツールバーからはエンティティ作成ボタンを削除しました。
