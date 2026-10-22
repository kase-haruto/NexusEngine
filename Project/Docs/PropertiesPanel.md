# Detailsカテゴリ編集 開発ノート

## 方針

Propertiesの専門ページをDetailsへ統合し、Entity編集の窓口を一つにした。Details上部には名前とComponent追加を常時表示し、設定項目は縦アイコンで切り替える。独立したPropertiesPanelとそのWindow登録は廃止した。

既存のReflection、PropertyDrawerRegistry、AssetPropertyWidget、PropertyEditService、Undo/Redo、Dirty管理、Level保存を再利用する。Runtime、Renderer、GPU資源の管理経路はこのUI統合で変更しない。

## 調査と再利用する責務

| 対象 | 既存実装と統合後の扱い |
|---|---|
| Window管理 | PanelRegistryがDetailsを一つだけ所有し、Begin/End、Windowメニュー、表示状態を管理する |
| 選択 | EditorSessionのSelectionServiceを全カテゴリで共有。Entity/Componentの生ポインタを保存しない |
| 型と値 | Runtime TypeRegistry / TypeDescriptor / PropertyDescriptorを利用する |
| 編集と履歴 | ReflectedPropertyEditor → PropertyEditServiceのBegin/Preview/Commit/Cancelを利用する |
| 型別UI | PropertyDrawerRegistryとPropertyWidgetsを利用し、ページに数値入力処理をコピーしない |
| Asset参照 | AssetPropertyWidgetの検証、選択、Drop、プレビューを再利用する |
| 材質 | MaterialPageと既存のInstance Override、Raster/DXR/CPUベイクへの転送を維持する |
| 言語と見た目 | LocalizationService、en/ja.lang、共通Editorテーマ・倍率を利用する |
| 保存 | PropertyEditServiceとEditorDocumentServiceのDirty・保存経路を維持する。Level形式はv8のまま |

## 依存方向

```text
EditorWorkspace
  ├─ PropertyCategoryRegistry（表示順・可視条件・ページ・Component分類）
  └─ PanelRegistry → DetailsPanel
                       ├─ 共通ヘッダ（名前、Component追加）
                       └─ PropertyCategoryView（縦ナビ、ページ別スクロール）
                            └─ 登録ページ（全般 / 描画 / Material / Camera / Light / その他）
                                 └─ ReflectedPropertyEditor
                                      ├─ PropertyDrawerRegistry / AssetPropertyWidget
                                      └─ PropertyEditService → Runtime Reflection → Entityの値
```

DetailsPanelは具体的なComponent型やGPU APIを参照しない。カテゴリの表示部品はIEditorPanelを継承せず、Windowの生成・登録を行わない。専門ページの登録APIは`EditorWorkspace::PropertyCategories()`を引き続き利用する。

## カテゴリと編集範囲

| 安定ID | 日本語表示 | 対象 |
|---|---|---|
| Object | 全般 | Transform。名前は共通ヘッダに置き、ここには重複表示しない |
| Rendering | 描画 | PrimitiveRenderのMesh、Node、enabled、staticLightingなど。既知の材質項目は除外する |
| Material | マテリアル | Slot、PBR Override、Base Color画像、Tint |
| Camera | カメラ | CameraのReflectionプロパティ |
| Light | ライト | DirectionalLightのReflectionプロパティ |
| Other | その他のComponent | 表示可能な専用カテゴリを持たないComponentのReflectionプロパティ |

名前・全般・描画・カメラ・ライト・その他は従来の共通する複数選択プロパティ編集を維持する。Materialページは従来どおり主選択のみを編集し、複数選択時にはその旨を表示する。

Renderingは材質ページで扱う既知項目だけを除外し、今後追加された描画プロパティも自動表示する。新しいComponentに専用カテゴリを登録しなくても、「その他」から編集できる。

カテゴリ登録の`componentTypes`はEditor専用の分類情報。表示可能なカテゴリに型IDが含まれる場合だけ「その他」から除外する。専用カテゴリが選択条件によって非表示なら、そのComponentは「その他」へ戻る。その他カテゴリ自身は型の所有を宣言しない。

## UI・寿命・履歴

- 名前とComponent追加はナビゲーションの外側に表示し、ページをスクロールしても利用できる。
- 空選択と破棄済みEntityはEmpty Stateを表示する。
- 可視条件は毎frame評価し、現在のカテゴリが使えなくなれば最初の利用可能カテゴリへ切り替える。
- ページ別Child IDによってカテゴリごとのスクロール位置を分離する。
- IDはDocument世代・永続Entity ID・カテゴリ・型・propertyから構成する。表示名や言語を内部識別子として使用しない。
- Component追加はSceneEditServiceへ委譲する。World交換後はそのframeの描画を終了し、古いEntityを参照しない。
- プロパティは共通サービスでPreviewし、入力終了・カテゴリ切替でCommitする。Sliderの中間値を個別Undoへ分割しない。
- Play中の値変更とComponent追加は従来どおり無効化する。
- 新規コードは公開ImGui APIのみを使用する。既存DockBuilderとテストのID指定入力ではInternal APIを引き続き使うため、ImGui更新時は配置・テスト入力も確認する。

## 旧Window設定との互換性

新規配置とLayout Resetでは右側をDetailsに使用し、Properties用の分割領域は生成しない。既存の保存済みDockレイアウト全体をリセットしない。

User設定に旧Propertiesの表示状態がある場合、Detailsまたは旧Propertiesのどちらかが開いていればDetailsを開く。旧Propertiesの表示キーを削除し、終了時の共通設定保存へ反映する。両方が閉じていた場合は閉じた状態を維持する。

## Materialの既存仕様

- 共有モデル材質は変更しない。PrimitiveRenderComponentがCPU値のMaterialOverrideを所有する。
- Entityあたり1 SlotのOverride。Slot変更はSource値を取り込み、前のSlotのOverrideを置き換える。
- Source値の取り込みとSlot変更は複数プロパティを一つのUndoとして適用する。
- Base Color、Metallic、Roughness、Normal Scale、Emissive、Occlusion Strengthを編集できる。
- Base Color画像とTintはEntity全体へ適用する。ほかの埋込画像はモデルから継承する。
- 値は既存Frame CBとDXR metadataへコピーする。Sliderごとのモデル再UploadやDescriptor再生成は行わない。
- CPUベイクにも同じOverrideを適用し、照合signatureへ含める。静的間接光の変更には明示的な再ベイクが必要。
- Level v1〜7はOverrideなしで読み込み、v8でOverrideを保存する。

## 新しいカテゴリの追加手順

1. Editor側にページの可視条件・描画関数を作る。
2. Runtimeの値は既存TypeRegistryへ検証付きgetter/setterを登録する。新しい永続データには保存・読込・Play cloneも対応する。
3. 入力には`DrawReflectedProperty(context, "TypeId", "propertyId")`を利用する。特殊な一覧選択でも共通PropertyEditServiceを利用し、Componentへ直接書き込まない。
4. 起動時のAddEditorExtensionからカテゴリを登録する。DetailsPanel本体は変更しない。

```cpp
[](EditorWorkspace& workspace) {
    workspace.PropertyCategories().Register({
        "MyPhysics", "properties.myPhysics", 600,
        MyPhysicsPage::IsAvailable,
        MyPhysicsPage::Draw,
        MyPhysicsPage::DrawIcon,
        {"PhysicsComponent"} // 「その他」から移すReflection型ID
    });
}
```

5. en.lang / ja.langへ表示キーを追加し、cppをvcxprojへ登録する。登録は起動時に完了させ、Draw中には行わない。
6. 可視条件、選択解除、対象破棄、Component追加・Undo、共通履歴、保存／再読込を検証する。

`componentTypes`を省略すると分類の所有を宣言しない補助ページになる。型全体の専門ページでは型IDを指定し、その他カテゴリへの二重表示を避ける。

## 主な変更ファイル

- `Editor/Gui/Panels/DetailsPanel.{h,cpp}`：共通ヘッダとカテゴリ編集を統合。
- `Editor/Gui/Properties/PropertyCategoryView.{h,cpp}`：旧PropertiesPanelからWindow責務を除いた埋込表示部品。
- `Editor/Gui/Properties/PropertyCategoryRegistry.{h,cpp}`：型のカテゴリ分類情報を追加。
- `Editor/Gui/Properties/PropertyPages.cpp`：描画・ライト・その他を追加し、既存ページとの重複を整理。
- `Editor/Core/EditorWorkspace.cpp`：単一Windowの登録と旧表示設定の移行。
- `Editor/Gui/DockingLayout.cpp`：Properties専用の初期分割を削除。
- en/ja辞書、main / EditorTestsのvcxproj、`Tests/DetailsCategoryTests.inl`、`Tests/EditorServicesTests.cpp`。
- `Editor/Gui/Panels/PropertiesPanel.{h,cpp}`と旧テスト名は廃止。

## 検証

```powershell
& .\Project\Tools\ValidateEditorFoundation.ps1
& .\Project\Tests\Invoke-GraphicsBuild.ps1 -Project Project/NexusEngine.vcxproj
& .\Project\Tests\Invoke-GraphicsBuild.ps1 -Project Project/Tests/NexusEditorTests.vcxproj
& .\Generated\Outputs\EditorTests\NexusEditorTests.exe
```

統合テストは単一Detailsでカテゴリ遷移、常時ヘッダのComponent追加、材質Source取り込み、未分類Componentの編集と分類変更、Camera/Light追加とUndo、材質保存／読込、日英切替、対象破棄、旧Window表示設定の移行を検証する。UI統合ではRendererや材質データを変更しない。

## 制約

- 独立Material Assetの直接選択・作成・編集／保存、複数Slot同時Overrideは未実装。
- 編集可能な画像参照は既存のEntity全体のBase Color。ほかの埋込画像の差し替えは未実装。
- Alpha Mode、Double Sided、Physicsの新規機能、Component削除UIは追加していない。
- 数値範囲・単位・TooltipのReflection metadataは既存にないため、共通setterによる値検証を維持する。
- カテゴリ選択はsession内状態。Panel表示とDock配置はUser設定へ保存する。
