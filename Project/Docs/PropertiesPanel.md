# PropertiesPanel 開発ノート

## 既存構造の調査結果

実装前に以下の経路と所有者を確認した。

| 調査対象 | 既存実装・再利用する機構 | 制約・問題点 |
|---|---|---|
| Details | `Gui/Panels/DetailsPanel`がReflectionを列挙しComponentを追加 | Drawer呼び出しと編集transactionの結線がPanel内にある。専門ページにコピーすると履歴処理が重複する |
| Reflection | Runtimeの`TypeRegistry` / `TypeDescriptor` / `PropertyDescriptor` | Entity用の値アクセス。独立Material Asset用のhandle、動的Slotごとのプロパティ列挙、範囲・単位・Tooltip metadataは現状ない |
| 値編集 | `PropertyHandle` / `PropertyEditService` / `PropertyDrawerRegistry` / `PropertyWidgets` | 永続Entity IDと安定した型・値IDで解決。Preview/Commit/Cancel、複数値のatomic更新が利用可能 |
| 選択 | `EditorSession::Selection()`の`SelectionService` | 共通のEntity主選択・複数選択。Assetを選択対象として編集する機構はない |
| Runtimeの描画対象 | `PrimitiveRenderComponent`、`CameraComponent`、`RenderSceneExtractor` | MeshRenderer相当はPrimitiveRender。Mesh参照、Node、Tint、Base Color画像参照を持つ |
| Material | `ModelMaterialAssetData` → `ModelAsset` / `PrimitiveRenderer` | モデルのCPU材質はBase Color、Metallic、Roughness、Normal Scale、Emission、AO、埋込画像と光学特性を持つ。Alpha Mode / Double Sided、独立材質ファイルの編集・保存はない |
| GPU更新 | `PrimitiveRenderer`のFrame CB、`OpticalBackend`のFrame metadata | GPU資源はGraphicsが所有。既存の画像差し替えは`ModelAssetLibrary`のVariantとApplicationのRenderer交換を利用 |
| Panel / Docking | `IEditorPanel` / `PanelRegistry` / `DockingLayout` | Begin/End、安定Window ID、Windowメニュー、表示設定、配置保存を再利用可能 |
| 履歴・Dirty・保存 | `PropertyEditService` → `EditorHistory`、`EditorDocumentService` → `LevelSerializer` | Reflection登録だけでは保存されない。保存形式への追加が必須。構造UndoはWorldを交換するためComponentポインタを保持できない |
| アイコン | 検索Widget等に個別図形描画あり | 汎用Icon registryや専用Icon fontはない。新しいfont依存は設けない |
| 言語・設定 | `LocalizationService`、en/ja.lang、`EditorSettingsStore::User` | Preferencesの言語・テーマ・倍率、Panel表示、workspace配置をそのまま利用 |
| Asset編集 | `AssetPropertyWidget`、Content Browserの`NEXUS_ASSET_PATH` | Model/画像選択、型検証、Drop、画像・モデルpreviewが再利用可能。Material Asset直接編集は未対応 |

## 設計と依存方向

```text
EditorWorkspace
  ├─ PropertyCategoryRegistry（登録情報と表示順のみ）
  ├─ PanelRegistry → PropertiesPanel（カテゴリ選択・スクロール・表示のみ）
  │                    └─ 登録callback → MaterialPage / Object / Camera
  └─ DetailsPanel ───────────┬─ ReflectedPropertyEditor
                            ├─ PropertyDrawerRegistry / AssetPropertyWidget
                            └─ PropertyEditService → Runtime Reflection → Entityデータ

PrimitiveRenderComponent → RenderSceneExtractor → RenderScene（CPU値のコピー）
                                              → GraphicsのRaster / DXR / LightBaker
```

PropertiesPanelにはComponent型やMaterial分岐を置かない。カテゴリの可視条件・ページの描画・アイコンは登録callbackで拡張する。ページは現在statelessなため、別のPageRegistry、基底Pageクラス、生成Factoryは設けない。状態が必要な拡張はcallbackに所有オブジェクトをcaptureできる。

Detailsの入力処理を`ReflectedPropertyEditor`へ抽出し、既存の追加UIとReflection列挙を維持した。型別入力・Asset Drop・Preview・Commit・複数選択・検証失敗処理を両Panelで共有する。専門ページは表示順・group・表示条件だけを定義する。

Runtime→Editor、Graphics→Editorへの依存は追加していない。Runtimeは従来からGraphicsのAPI非依存描画データに依存するため、`Graphics/Material/MaterialOverride.h`のCPU値を参照する。PanelからNative GPU APIを呼ばない。

### Materialの所有と編集範囲

- 共有モデルの材質は変更しない。新しい`MaterialOverride`はEntityの`PrimitiveRenderComponent`が値として所有する。
- **Entityあたり1 SlotのOverride**。Slotを変更するとそのSlotのSource値を取り込み、前のSlotのOverrideを置き換える。Slotごとの複数Override配列ではない。
- Materialページの操作対象は主選択のみ。複数選択時はその旨を表示する。Detailsの既存の複数選択編集は維持する。
- Source材質一覧は`AssetPropertyWidget::SourceModel()`で取得するCPUデータをframe内だけ参照する。同名材質でもSlot番号でIDを区別する。
- 「元の材質からOverrideを作成」「元の材質の値に戻す」とSlot変更は、Source値と有効フラグをまとめて一つのUndoとして適用する。
- SurfaceはBase Color、Metallic、Roughness、Normal Scale、Emissive、Occlusion Strength。既存のPBR値に対応する項目だけを公開する。
- 既存のBase Color画像参照とTintも同じ共通編集UIで表示する。この2項目は**Entity全体**に適用する。UIにも適用範囲を表示する。
- Normal / Metallic Roughness / Emissive / Occlusionの埋込画像は継承する。独立した画像参照の編集欄を捏造しない。
- Surface値はFrame CBとDXRのFrame metadataへコピーする。SliderごとのModel Upload、Variant増殖、Descriptor再生成はない。
- 統合モデルのSlot offsetは`ModelAssetLibrary::Range::materialBegin` → Application → RenderItemへ渡し、Source Slotと混同しない。
- 静的ベイクも同じOverrideを参照し、照合signatureに有効なOverride値を含める。既存のベイク結果は値の変更だけで自動再ベイクされないため、LightingのBake操作で更新する。

### 寿命・履歴・保存

PanelはカテゴリIDだけを保持する。可視条件は毎frame共通選択から再評価し、選択解除・削除・Scene交換でComponentの生ポインタを残さない。使用中カテゴリが使えなくなったら最初の利用可能カテゴリに移り、空ならEmpty Stateを表示する。

通常の入力はActivated → Begin、Changed → Preview、EditFinished → Commit。カテゴリ切替でも未確定入力をCommitする。Escape、選択切替、Document切替、Play制御は既存Workspace/Serviceの経路を利用する。Properties専用の履歴・Dirtyフラグ・保存処理は作らない。

Level形式はv8へ更新。v1〜7はOverride無効で読み込み、従来描画を維持する。全Override値を保存前／読込時に有限値・範囲検証する。独立Material Assetファイルの保存は行わない。

### UIとInternal API

PropertiesはWindowメニューから開閉できる独立Docking Window。初期配置・Layout Reset時には右側のDetailsの下へ配置する。保存済みlayoutを自動破棄しないため、既存layoutでは新規Windowを手動でDockできる。

左の縦アイコンには選択強調と翻訳Tooltip、右にはページごとのChildスクロールとCollapsingHeaderを使用する。IDはWindow / Document世代 / Entity永続ID / カテゴリ / 型 / propertyで分離し、翻訳文字列と内部IDを分離する。アイコンは公開DrawList APIで描画し、Editorテーマ・文字サイズに追従する。

新規Panel・ページ・アイコンはImGui Internal APIを使用しない。既存`DockingLayout`だけがDockBuilderのInternal APIを継続利用するため、ImGui更新時にはDockingの配置構築を確認する。テストのID指定入力は既存テストと同様にInternal APIを利用する。

## 検討した代替案

| 代替案 | 採用しなかった理由 |
|---|---|
| 共有`ModelMaterialAssetData`をPanelで直接変更 | 他Entityへの波及、Undo対象の所有者、モデルファイルへの書き戻しが未定義になる |
| スカラー変更ごとにModelAssetLibraryのVariantを生成 | Sliderの全中間値でGeometryと画像の再Upload・cache増大が発生する |
| Material用の独立Undo・翻訳・Asset pickerを作る | Detailsと履歴・検証・Dirtyの動作が分岐する |
| Entity以外も扱う新しい汎用Resource編集サービス | 今回の範囲に対して変更が大きい。独立Material Assetの所有・保存を設計する時に検討する |
| CategoryRegistryとPageRegistryとPageFactoryを分離 | 現在のstatelessページに生成管理は不要。登録情報と描画callbackだけで依存を分離できる |
| 全Slotの動的Reflection配列を一度に実装 | 現在の固定property handle / serializerの大幅変更になる。今回は1 Slotを明示し、複数Slot永続Overrideは将来の独立課題とする |

## 変更ファイル

| 領域 | 追加・変更 |
|---|---|
| Panel基盤 | 新規`Editor/Gui/Panels/PropertiesPanel.{h,cpp}`、`Properties/PropertyCategoryRegistry.{h,cpp}`、`Properties/PropertyPages.cpp` |
| Materialページ | 新規`Editor/Gui/Properties/MaterialPage.{h,cpp}` |
| 共通編集 | 新規`Editor/Gui/ReflectedPropertyEditor.{h,cpp}`、変更`DetailsPanel.cpp`、`AssetPropertyWidget.h` |
| 接続・配置 | `EditorWorkspace.{h,cpp}`、`PanelRegistry.cpp`、`DockingLayout.cpp`、`EditorApplication.cpp` |
| CPU材質値 | 新規`Graphics/Material/MaterialOverride.h`、`PrimitiveRenderComponent.h`、`TypeRegistry.cpp`、`LevelSerializer.cpp`、`RenderSceneExtractor.cpp` |
| 描画 | `RenderScene.h`、`PrimitiveRenderer.cpp`、`OpticalSystem.h`、`OpticalBackend.cpp`、`LightBaker.cpp`、`ModelAssetLibrary.{h,cpp}` |
| 言語 | `Resources/Editor/Localization/{en,ja}.lang` |
| 検証・ビルド | 新規`Tests/PropertiesPanelTests.inl`、`EditorServicesTests.cpp`、`GraphicsShadowTests.cpp`、`BakedLightingTests.inl`、main / EditorTestsのvcxproj |
| ドキュメント | 本書、`EditorUserGuide.md` |

既存のDetails・Content Browser・Gizmo等の作業中差分は維持する。利用者のSceneファイルは本実装のために書き換えない。

## 新しい専門カテゴリの追加手順

1. `Editor/Gui/Properties/`または拡張側にページを作る。可視条件と描画関数を用意する。
2. Runtime値は既存TypeRegistryへgetterと検証付きsetterを登録する。失敗setterは元の値を変更しない。新しいRuntimeデータには保存・読込・Play cloneの対応も追加する。
3. `DrawReflectedProperty(context, "TypeId", "propertyId")`で共通編集経路を使う。特殊な一覧選択は`PropertyEditService::PreviewValues`と`Commit`を使い、直接Componentへ書き込まない。
4. `AddEditorExtension`へ渡す起動時callbackから登録する。内蔵ページなら`RegisterPropertyPages`へ追加してもよい。PropertiesPanel本体の変更は不要。

```cpp
// Application側の既存AddEditorExtensionに渡すcallback。
[](EditorWorkspace& workspace) {
    workspace.PropertyCategories().Register({
        "MyPhysics", "properties.myPhysics", 500,
        MyPhysicsPage::IsAvailable,
        MyPhysicsPage::Draw,
        MyPhysicsPage::DrawIcon // 省略時は円形アイコン
    });
}
```

5. en.lang / ja.langに同じ表示キーを追加し、cppをvcxprojへ登録する。カテゴリIDは翻訳しない。登録は起動時に完了させ、Draw中には追加しない。
6. 選択解除、対象削除、UndoによるWorld交換、Play、値のCancel、保存／再読込を検証する。

## 検証コマンド

```powershell
& .\Project\Tools\ValidateEditorFoundation.ps1
& .\Project\Tests\Invoke-GraphicsBuild.ps1 -Project Project/NexusEngine.vcxproj
& .\Project\Tests\Invoke-GraphicsBuild.ps1 -Project Project/Tests/NexusEditorTests.vcxproj
& .\Generated\Outputs\EditorTests\NexusEditorTests.exe
& .\Project\Tests\Invoke-GraphicsBuild.ps1 -Project Project/Tests/NexusGraphicsTests.vcxproj
& .\Generated\Outputs\GraphicsTests\Debug\NexusGraphicsTests.exe --material-override
& .\Generated\Outputs\GraphicsTests\Debug\NexusGraphicsTests.exe --baked-cpu
```

Editor統合テストはDetailsとPropertiesを同時描画し、カテゴリ操作、登録拡張、Source取り込み、Undo/Redo、無効値拒否、保存／再読込、翻訳切替、削除／復元、Empty Stateを確認する。Graphicsテストは実描画のpixel差分、無関係Slotの不変、Override無効化による元の描画復元、Bake signatureを確認する。

### 実行結果（2026-10-21）

- Debugのmain / EditorTests / GraphicsTestsビルド成功。
- EditorTests成功。v7読込、v8 roundtrip、保存前の不正値拒否、独立Dock領域、外部カテゴリ登録を確認。
- `--material-override`成功。RTX 3060 Ti上でRasterと光学Sceneの描画を確認し、GPU Based Validationの警告・エラーなし。
- `--baked-cpu`成功。共有Sourceを変更せず、Instanceの天井Slotの発光Overrideだけで赤い間接光が発生することを確認。
- 静的検査成功。166組の日英翻訳、source登録、Runtime/UI依存、PanelのGPU境界、共通Widget、`git diff --check`を確認。
- 実画面で既存layoutへの追加時の極小初期幅を検出し、新規Panelの初期サイズを修正。利用者の保存済みlayoutとSceneファイルは変更していない。

## 今回の制約

- Material Assetの直接選択・作成・共有Asset編集／保存、複数Slotの同時Overrideは未実装。
- Texture参照として編集できるのは従来のEntity全体のBase Color画像。ほかの埋込画像はモデル由来の値を継承する。
- Alpha Mode / Double Sided / Physicsの新規データやUIは未実装。光学特性の専用ページも追加しない。
- Component削除UIなど、既存Detailsにない構造操作は今回追加しない。
- UIの数値範囲・単位・TooltipをReflection metadataで記述する機能は既存にない。今回は既存Drawerと共通setterの範囲検証を使用し、材質ページに範囲を表示する。
- カテゴリ選択はPanelのsession内状態。Panel表示とDock配置は従来どおりUser設定へ保存するが、カテゴリ自体は再起動時に初期化する。
