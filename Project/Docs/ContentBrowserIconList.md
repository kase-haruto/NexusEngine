# Content Browser Icon List

Google Fontsの[Material Symbols](https://fonts.google.com/icons)から、56種類の操作・ファイル種別・状態に対応する図柄を選びました。すべて**32×32px・RGBA・透過PNG**です。Outlined、weight 400の公式SVGを32pxへrasterizeしています。UIの実際の描画サイズは既存のfont／DPIスケールに追従します。

配置先: `Project/Resources/Editor/Icons/`。Installed SDKは`Data/Resources/Editor/Icons/`。下記56 IDに未配置・汎用代用はありません。既存登録を優先する5 IDは登録先のPNGを直接更新し、同じ用途の重複ファイルは作っていません。その他のPanel専用画像は保持しています。

配色はフォルダー＝黄系、Model＝青、Texture＝シアン、Material／C#＝紫、Animation＝橙、保存／追加＝緑、警告＝黄、エラー／削除＝赤、移動系／表示系＝青〜灰色です。Disabledは既存のAlpha、Hover／Selectedは既存Widgetの強調表示を使います。

| Icon ID | 用途 | Google Symbol | 実ファイル名 | サイズ | 色 | 表示箇所 |
|---|---|---|---|---|---|---|
| `back` | back | `arrow_back` | `back.png` | 32×32 | `#B4BFCE` | Content Browser |
| `forward` | forward | `arrow_forward` | `forward.png` | 32×32 | `#B4BFCE` | Content Browser |
| `folder_up` | folder_up | `drive_folder_upload` | `folder_up.png` | 32×32 | `#DFB660` | Content Browser |
| `add` | add | `add_circle` | `add.png` | 32×32 | `#63BE94` | Content Browser |
| `import` | import | `file_upload` | `import.png` | 32×32 | `#62B9C5` | Content Browser |
| `save` | save | `save` | `file.save.png` | 32×32 | `#63BE94` | Content Browser |
| `save_all` | save_all | `library_add_check` | `save_all.png` | 32×32 | `#63BE94` | Content Browser |
| `refresh` | refresh | `refresh` | `refresh.png` | 32×32 | `#78ADE3` | Content Browser |
| `filter` | filter | `filter_list` | `filter.png` | 32×32 | `#78ADE3` | Content Browser |
| `search` | search | `search` | `search.png` | 32×32 | `#B4BFCE` | Content Browser |
| `settings` | settings | `tune` | `settings.png` | 32×32 | `#B4BFCE` | Content Browser |
| `view_grid` | view_grid | `grid_view` | `view_grid.png` | 32×32 | `#78ADE3` | Content Browser |
| `view_list` | view_list | `view_list` | `view_list.png` | 32×32 | `#78ADE3` | Content Browser |
| `view_details` | view_details | `table_rows` | `view_details.png` | 32×32 | `#78ADE3` | Content Browser |
| `sort_ascending` | sort_ascending | `arrow_upward` | `sort_ascending.png` | 32×32 | `#B4BFCE` | Content Browser |
| `sort_descending` | sort_descending | `arrow_downward` | `sort_descending.png` | 32×32 | `#B4BFCE` | Content Browser |
| `folder` | folder | `folder` | `asset.folder.png` | 32×32 | `#DFB660` | Content Browser |
| `folder_open` | folder_open | `folder_open` | `asset.folderOpen.png` | 32×32 | `#DFB660` | Content Browser |
| `favorite` | favorite | `star` | `favorite.png` | 32×32 | `#DFB660` | Content Browser |
| `collection` | collection | `collections_bookmark` | `collection.png` | 32×32 | `#B191DA` | Content Browser |
| `project` | project | `deployed_code` | `project.png` | 32×32 | `#78ADE3` | Content Browser |
| `content` | content | `folder_special` | `content.png` | 32×32 | `#DFB660` | Content Browser |
| `source` | source | `folder_code` | `source.png` | 32×32 | `#62B9C5` | Content Browser |
| `plugin` | plugin | `extension` | `plugin.png` | 32×32 | `#B191DA` | Content Browser |
| `file` | file | `draft` | `file.png` | 32×32 | `#B4BFCE` | Content Browser |
| `asset_model` | asset_model | `view_in_ar` | `asset.model.png` | 32×32 | `#78ADE3` | Content Browser |
| `asset_texture` | asset_texture | `image` | `asset.texture.png` | 32×32 | `#62B9C5` | Content Browser |
| `asset_material` | asset_material | `palette` | `asset_material.png` | 32×32 | `#B191DA` | Content Browser |
| `asset_level` | asset_level | `map` | `asset_level.png` | 32×32 | `#63BE94` | Content Browser |
| `asset_prefab` | asset_prefab | `widgets` | `asset_prefab.png` | 32×32 | `#78ADE3` | Content Browser |
| `asset_animation` | asset_animation | `animation` | `asset_animation.png` | 32×32 | `#DDA06A` | Content Browser |
| `asset_curve` | asset_curve | `show_chart` | `asset_curve.png` | 32×32 | `#62B9C5` | Content Browser |
| `asset_audio` | asset_audio | `audio_file` | `asset_audio.png` | 32×32 | `#DFB660` | Content Browser |
| `asset_shader` | asset_shader | `code_blocks` | `asset_shader.png` | 32×32 | `#B191DA` | Content Browser |
| `source_cpp` | source_cpp | `data_object` | `source_cpp.png` | 32×32 | `#78ADE3` | Content Browser |
| `source_header` | source_header | `integration_instructions` | `source_header.png` | 32×32 | `#62B9C5` | Content Browser |
| `source_csharp` | source_csharp | `code` | `source_csharp.png` | 32×32 | `#B191DA` | Content Browser |
| `open` | open | `open_in_new` | `open.png` | 32×32 | `#78ADE3` | Content Browser |
| `rename` | rename | `edit` | `rename.png` | 32×32 | `#DFB660` | Content Browser |
| `duplicate` | duplicate | `file_copy` | `duplicate.png` | 32×32 | `#78ADE3` | Content Browser |
| `delete` | delete | `delete` | `delete.png` | 32×32 | `#E87978` | Content Browser |
| `copy` | copy | `content_copy` | `copy.png` | 32×32 | `#B4BFCE` | Content Browser |
| `move` | move | `drive_file_move` | `move.png` | 32×32 | `#78ADE3` | Content Browser |
| `copy_path` | copy_path | `link` | `copy_path.png` | 32×32 | `#62B9C5` | Content Browser |
| `explorer` | explorer | `folder_managed` | `explorer.png` | 32×32 | `#DFB660` | Content Browser |
| `code_editor` | code_editor | `terminal` | `code_editor.png` | 32×32 | `#B191DA` | Content Browser |
| `references` | references | `call_received` | `references.png` | 32×32 | `#62B9C5` | Content Browser |
| `dependencies` | dependencies | `account_tree` | `dependencies.png` | 32×32 | `#B191DA` | Content Browser |
| `reimport` | reimport | `published_with_changes` | `reimport.png` | 32×32 | `#62B9C5` | Content Browser |
| `dirty` | dirty | `edit_note` | `dirty.png` | 32×32 | `#DFB660` | Content Browser |
| `warning` | warning | `warning` | `warning.png` | 32×32 | `#DFB660` | Content Browser |
| `error` | error | `error` | `error.png` | 32×32 | `#E87978` | Content Browser |
| `loading` | loading | `progress_activity` | `loading.png` | 32×32 | `#78ADE3` | Content Browser |
| `missing` | missing | `broken_image` | `missing.png` | 32×32 | `#E87978` | Content Browser |
| `lock` | lock | `lock` | `lock.png` | 32×32 | `#B4BFCE` | Content Browser |
| `info` | info | `info` | `info.png` | 32×32 | `#78ADE3` | Content Browser |

## 差し替え方

上表の実ファイルを同じ名前で置き換えるだけで反映します。推奨32×32px、透過PNG。PNGの追加・変更・削除は既存のOS通知と非同期decodeで実行中にも反映します。通知が利用できない場合や退役画像の上限に達した場合は再起動してください。

今回はPNGに色を含め、`IconSources.json`の`monochrome`をfalseにしています。テーマ色で描画したい場合は`IconTheme.json`に`{"search": true}`のように実ファイルの拡張子なしの名前を指定します。色をそのまま使う場合はfalseです。このJSONも実行中に再読込します。

GPU資源はImGuiRendererが所有し、Panelでは画像パスやDescriptorを扱いません。差し替えた旧Texture／SRVはGPU idle後のShutdownまで保持します。登録128画像・退役128画像の上限を維持し、UIごとの重複ロードは行いません。

## 出典

公式サイト: https://fonts.google.com/icons

公式配布元: https://github.com/google/material-design-icons

固定commit: `10ecc14790612ca8fd164e3f4ffe9c16fc50d5b7`。

各SVGのURL、Symbol名、色、出力サイズ、SHA-256は`IconSources.json`へ記録。Apache-2.0のライセンス本文は`MaterialSymbols.LICENSE.txt`を同梱しています。今回適用した画像は56枚、対応IDの画像未配置は0件です。
