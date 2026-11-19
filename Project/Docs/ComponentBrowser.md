# Add Component Browser

DetailsのAdd Componentは、専用の軽量Popupを開きます。検索欄の下に、左側のカテゴリと右側のComponentカードを配置しています。カードは既存のIconRegistryのアイコン、表示名、説明、登録元とカテゴリを表示します。説明全文と安定型IDはTooltipから確認できます。

検索は表示名、安定型ID、C++型名、説明、カテゴリの名前・ID、検索語を対象にします。英字は大文字・小文字を区別しません。候補とカテゴリはTypeRegistryの登録情報から生成するため、Editorの具体型判定を追加する必要はありません。

追加済みの単一Componentや必須Component、Runtimeから公開された追加制約はDisabled表示します。複数選択では不足分だけを追加します。同型複数instanceの追加関数を持つ型は、既存のinstance追加transactionを利用します。C# Scriptの最大instance数やPlay中の変更禁止も共通の追加制約を使います。

カードを選ぶとSceneEditServiceから既存の追加関数を呼び、Undo/Redoと失敗時の復元を維持します。成功時、Escape、選択変更、Level交換、Play開始でPopupを閉じます。Browserは永続Entity IDと表示値だけを保持し、Scene交換を跨ぐEntityやDescriptorポインタをキャッシュしません。

## 登録情報

TypeDescriptor末尾のeditorはUI非依存の値ヒントです。Runtimeは翻訳、画像ロード、ImGuiを参照しません。

```cpp
descriptor.editor = {
    "Player Controller",  // displayName。未指定時はlabelKey
    "Gameplay",           // category。任意のカテゴリID
    "Controls the player.",
    "component.Script",   // IconRegistryの抽象ID。Textureのpathではない
    "Game",               // origin。Game / Engine / Plugin / Customなど
    {"movement", "input"} // keywords
};
```

未指定の型も、既存の表示名、Otherカテゴリ、既定の説明・アイコンで表示します。未登録カテゴリはIDを表示名として使えます。標準カテゴリの英日訳はLocalizationにあります。Game/Plugin型も同じTypeRegistryを通して表示されます。

複数追加の可否は既存のaddInstance callbackから取得します。別のallowMultipleフラグと二重管理しません。任意のWorld側制約はaddUnavailableReason callbackで翻訳キーまたは理由を返せます。追加直前にも既存の生成関数が検証するため、BrowserはFactoryを複製しません。

## EngineとReflection宣言

```cpp
NEXUS_REFLECT_COMPONENT(MyComponent, "Game.MyComponent", "game.myComponent", enabled)
NEXUS_COMPONENT_EDITOR(MyComponent, "Gameplay", "game.myComponent.description", "component.Script")
```

表示ヒントの宣言をComponentのheaderに置きます。Generate-Reflectionが既存のRegisterReflectedInspector呼出しへ値を渡します。保存ID・メンバ・生成関数の契約は変わりません。C++型名は検索語へ自動登録します。文字列引数は安定ID・翻訳キー・Icon IDを使います。

## C#

```csharp
[ScriptType("game.player")]
[ComponentEditor(
    DisplayName = "Player Controller",
    Category = "Gameplay",
    Description = "Controls the player using mapped input.",
    Icon = "component.Script",
    Keywords = new[] { "movement", "input" })]
public sealed class PlayerController : ScriptBehaviour
{
}
```

既存のMetadata Exporterがeditor objectをsidecarへ出力します。Build / Reload C#後の共通Registryへ反映されます。Attribute未指定の既存ScriptはGameplayカテゴリと既定のScript説明を使います。

## Nativeの値Metadata

既存schemaVersion 1の型objectに、任意のeditorを追加できます。

```json
{
  "id": "game.health",
  "fullName": "Game.Health",
  "version": 1,
  "aliases": [],
  "properties": [],
  "editor": {
    "displayName": "Health",
    "category": "Gameplay",
    "description": "Stores health and damage state.",
    "icon": "component.Default",
    "origin": "Game",
    "keywords": ["hitpoints", "damage"]
  }
}
```

旧sidecarはそのまま読めます。未知field、過大な文字列や検索語は共通Metadata Readerで拒否します。PluginはTypeDescriptorを登録するか、同じNative Metadata経路を利用できます。

## Editor側の責務

- ComponentBrowserModel: Registry世代・言語が変わった時だけ表示値とカテゴリを再構築。検索と現在Worldの追加可否も担当します。
- AddComponentWindow: Popup、検索欄、カテゴリ、固定高さカード、Tooltip、お気に入りの表示を担当します。
- DetailsPanel: ボタンからOpenを要求し、World交換後に古い参照を使わないよう描画を終了します。
- SceneEditService: 実際の追加・instance追加、複数対象、Undo、rollbackを担当します。

TypeRegistryStorageは登録・解除・生成情報の補完時にGenerationを変更します。copyしたRegistryの変更世代も一意なので、Module/Pluginの登録差替え後に古い一覧を表示しません。表示キャッシュにFactoryやEntityを保持しません。

お気に入りは安定型IDだけを既存User.settingsのcomponent.favorite.Nへ保存します。GameのLevelやComponentへ保存しません。未ロードのPluginのIDも残すため、再接続時にお気に入りへ復帰できます。Recentなどの将来のプリセットも、同じID集合とFilter経路へ接続できます。

## 検証

ComponentBrowserIntegrationが、型名・説明・カテゴリ・検索語・日本語検索、世代失効、Engine/Native/C#型、複数instance、カードのアイコン、お気に入りの保存、追加/Undo、重複無効、Escape、選択変更を検証します。既存のGame Componentの追加・Details・Undoのテストも新しいカード操作で維持します。C#のAttribute出力とCoreCLR/Mono向けAPIビルドも確認します。

## アイコンを変更する手順

### PNGの配置先とIcon ID

開発チェックアウトでは、次のディレクトリへPNGを置きます。

```text
Project/Resources/Editor/Icons/component.Health.png
```

配布版では、使用中のmanifestが指すdataディレクトリ内の`Resources/Editor/Icons/`へ置きます。一般的な配布構成では`Data/Resources/Editor/Icons/`です。ゲーム側のAssetsへ置く必要はありません。

Icon IDはPNGの拡張子を除いた名前です。上の例なら`component.Health`を指定します。大文字・小文字も合わせ、ASCIIのIDを推奨します。画像はディレクトリ直下へ置きます。サブディレクトリの再帰読込はありません。

- 推奨: 64×64または128×128の正方形・透過PNG。
- 読込上限: 1辺1024px以下、1ファイル4MiB以下、最大128個。
- 拡張子は小文字の`.png`を使います。
- 読込はEditor起動時です。PNGの追加・差し替え後はEditorを再起動してください。
- 同じIcon IDを使う全Componentが同じ画像を共有します。個別に変えたい場合は別のIDを用意します。
- 指定画像がなければBrowserは`details.Object`、それもなければ既定の図形へfallbackします。

既存アイコンを差し替える場合は、同名PNGを置き換えて再起動します。Component登録のIcon IDを変える場合は、対応言語のBuild／型情報の再読込も必要です。

### 言語ごとの指定例

Engine／Reflection宣言:

```cpp
NEXUS_COMPONENT_EDITOR(HealthComponent, "Gameplay", "component.health.description", "component.Health")
```

C#:

```csharp
[ComponentEditor(DisplayName = "Health", Category = "Gameplay",
    Description = "Stores the current health.", Icon = "component.Health")]
```

Native値Metadata:

```json
"editor": {
  "displayName": "Health",
  "category": "Gameplay",
  "description": "Stores the current health.",
  "icon": "component.Health"
}
```

Component Browserのカードアイコンと、Details左側のカテゴリタブのアイコンは別の指定です。後者は`details.<カテゴリID>.png`を使います。例: `details.Camera.png`、`details.Physics.png`、`details.Animation.png`。BrowserのIcon IDとして同じIDを指定した場合は、そのPNGを共有します。

## 新しいComponentを追加するときのチェックリスト

### EngineのC++ Component

1. `Runtime/Scene/Components/`など既存の責務に合う場所へ型を定義します。
2. `NEXUS_REFLECT_COMPONENT`で安定型ID・表示キー・保存メンバを宣言します。公開後は保存IDを不用意に変えません。
3. `NEXUS_COMPONENT_EDITOR`でカテゴリ・説明キー・Icon IDを同じheaderへ記入します。
4. 表示名と説明のキーを`Resources/Editor/Localization/en.lang`と`ja.lang`へ追加します。
5. 新規`.cpp`に処理を書く場合は`NexusEngine.vcxproj`、`Build/NexusRuntime.vcxproj`と必要なテストtargetへ登録します。自作の関数定義は.cppへ置きます。
6. Buildすると既存Reflection生成器がInspectorの登録を補完します。生成済みの`EngineGenerated`ファイルを手作業で編集しません。
7. 特別な生成・検証が必要な型は、Runtimeの登録箇所で既存TypeDescriptorのadd／removeを接続します。Browserへ具体型の分岐を追加しません。
8. Browserの表示・検索・追加・解除・Undo／Redo・保存／再読込を確認します。

最低限の宣言例:

```cpp
#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
NEXUS_REFLECT_COMPONENT(HealthComponent, "Health", "component.health", value)
NEXUS_COMPONENT_EDITOR(HealthComponent, "Gameplay", "component.health.description", "component.Health")
namespace NexusEngine {
struct HealthComponent {
    float value = 100;
};
}
```

翻訳例（langファイルの形式は`キー "文字列"`）:

```text
component.health "Health"
component.health.description "Stores the current health."
```

### ゲーム側のC#／C++ Behaviour

Content Browserの右クリック → 作成から、C#スクリプトまたはC++ Behaviourを作成します。所属言語のGame Moduleを選択してください。生成器が既存のModuleへソースと必要な登録を追加します。

- C#: `ScriptType`の安定IDを維持し、任意の`ComponentEditor` Attributeを記入します。Build／Reload C#後に表示情報へ反映されます。
- C++ Behaviour: 生成済みの.h/.cppへ処理を書きます。自動生成の登録ファイルを直接編集しません。既定の画像を変更する場合はcomponent.Default.pngを差し替えます（同じIDの型で共有）。型ごとの独自の表示情報を宣言する場合は、自前で管理する既存Native Metadataのeditor objectを使います。Build後、Editorを再起動して型を読み込みます。
- Nativeの値Component: 既存のNative Component Metadataへ型とeditor objectを追加します。DLL内の実行objectをEditorに所有させず、共通の値Component経路を使います。
- カスタムの登録処理: 既存TypeDescriptorのeditorを設定してTypeRegistryへ登録します。UIコードの追加は不要です。

BrowserのCategoryは追加時の分類です。追加後に専用Detailsページがない型は「その他」へ自動表示されます。専用の編集ページが必要な場合だけ、既存PropertyCategoryRegistryへ登録します。

## 追加済みComponentの解除

Entityを選択し、Detailsの「Componentを解除 / Remove Component」を押して、解除する型を選びます。

- Engine・Native・Gameの登録済み解除callbackを共通で使います。
- Name・Transformなど、追加を公開しない必須型は解除不可として表示します。
- 複数Entityを選択した場合、そのComponentを持つEntityから一括で外します。持たないEntityは変更しません。
- 同型の複数instanceは`#1`、`#2`のように表示します。選んだ順序番号だけを外し、他のinstanceを残します。
- Undoで元の値・関連付け・instance IDを復元でき、Redoで再び解除できます。
- Play中は解除できません。生成元の.cs/.h/.cppファイルは削除しません。
- 解除callbackが制約違反などで失敗した場合は、一括操作全体を元に戻します。

Reflection生成型は既存のEntity解除関数を登録します。独自Factoryを登録する場合は、対になるTypeDescriptor.removeも必ず接続してください。同型複数instanceを扱う場合はinstances／addInstance／removeInstanceをセットで公開します。解除できない必須型はaddを公開せず、removeも設定しません。
