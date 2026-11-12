# Native DLL Component / Entity Logic

Native Game DLLが値Componentを明示宣言し、既存ECSのEntityへ配置する実装。実装ソースを追加した段階であり、今回の変更のBuild・ゲーム・テスト実行は行っていない。

## 既存構造の調査と設計判断

既存の`GameModuleLoader`がDLLを所有し、`NativeGameSystemScope`が既存`SystemScheduler`へSystemを登録する。`GameApiBridge`と`Components`は共通WorldのReflection値APIを提供する。Editorは`TypeRegistry → PropertyDescriptor → PropertyEditService → PropertyDrawerRegistry`を描画し、`SceneEditService`がScene複製とUndoを担当する。`LevelDocument / LevelSerializer / ComponentReflection`が保存・復元を担当する。Source Content Browserは`TypeDescriptor.sourceFiles`から追加対象を解決する。

不足していたのは独立DLLのComponent宣言をLevel読込前に取得する経路と、DLLを越えて保持可能な保存値、EntityごとのNative実行objectの寿命契約だった。

C++クラスのfactory/getter/setterをEditorへ直接登録する案は、DLL解放後のcallback、CRT境界、保存時のlayout依存を増やすため採用していない。ホストだけへ具体型を静的リンクする方式は既存サンプルでは利用できるが、独立DLLの宣言に対応できない。今回の値ComponentはEngine所有の普通のECS Componentへ格納し、ゲーム固有C++ objectはDLL内に限定する。

専用ECS・Inspector・Level・System Managerは追加していない。複数の値Componentは一つの`GameComponentData` Storageを共有し、Schedulerのアクセス宣言も同じStorageへ解決する。ネイティブな`T`のtrivial/nontrivialな任意メモリ配置をそのままECSへプラグインする機能とは区別する。

## 責務と依存方向

| ファイル/クラス | 責務 |
|---|---|
| [GameComponentMetadata.h](../Public/Nexus/GameComponentMetadata.h) | 任意の純粋Metadata export。固定C ABIと借用UTF-8だけ |
| [NativeComponentMetadata](../Runtime/Reflection/NativeComponentMetadata.h) | identity・JSON検証、値コピー、Engine実装の共通Descriptor生成 |
| [GamePropertyMetadata.h](../Runtime/Reflection/GamePropertyMetadata.h) | C#とNativeで同じ値型・Alias・Version・状態形式を検証 |
| [GameComponentData](../Runtime/Scene/Components/GameComponentData.h) | 既存ECSが所有する保存値と、保存しない関連付けtoken |
| [ReflectionRegistration](../Runtime/Reflection/ReflectionRegistration.h) | 既存ホスト型、格納型、immutable Metadataを既存登録へ合流 |
| [EntityBehaviourSystem.hpp](../Public/Nexus/EntityBehaviourSystem.hpp) | DLL内のEntity Logic補助。既存Native SystemとしてLifecycleを実行 |
| [ProjectHost.inl](../Core/Hosting/ProjectHost.inl) | Level/Editor構築前のMetadata取得 |
| [GameRuntime](../Runtime/Application/GameRuntime.cpp) | Play前の再検査とロード時schema固定 |
| [GameComponentAccess](../Runtime/Gameplay/GameComponentAccess.cpp) | 既存Engine値protocolでの世代付きPropertyアクセス |
| [SceneEditService](../Editor/Services/SceneEditService.cpp) / [DetailsPanel](../Editor/Gui/Panels/DetailsPanel.cpp) | 共通履歴による追加・解除、共通Property描画 |

`Game DLL → Public/Nexus`、`Runtime → 既存ECS/Reflection`、`Editor → 値Metadata/既存編集サービス`という依存方向。RuntimeはEditor/ImGuiへ依存しない。Editorが保持するCatalogはEngineコードとコピー済み値だけであり、DLL objectやManaged Runtimeを所有しない。

## 宣言と使用方法

DLLは通常の`NexusGetGameModule`に加え、`NexusGetGameComponentMetadata`を任意exportできる。省略した従来DLLは従来どおり動作する。exportは`abi=1`で`NexusGameString`を返し、文字列はDLL解放まで有効とする。ゲーム初期化、World生成、worker起動を行わない。DLLのロードではDllMain/CRT初期化は実行されるため、そこにもゲーム開始の副作用を置かない。

```json
{
  "schemaVersion": 1,
  "types": [{
    "id": "Game.Mover", "fullName": "Game.NativeMover",
    "version": 1, "aliases": [],
    "sourceFiles": ["NativeMover.h", "NativeMover.cpp"],
    "properties": [
      {"id":"enabled","label":"Enabled","aliases":[],"kind":"Bool","default":true},
      {"id":"speed","label":"Speed","aliases":[],"kind":"Float","default":1.0}
    ]
  }]
}
```

IDはProjectのModuleをまたいで一意にする。別名は保存値の探索用で、API検索は正規IDを使う。`sourceFiles`はModule Sourceディレクトリ相対で、絶対パス・`..`を拒否する。Editorは開く際にもProject内を再検査する。配布ではSourceの実在を要求しない。

対応kindはBool / Int / Float / String / Vector2 / Vector3 / Vector4。type/property IDは512 byte、Stringの新規既定値と編集は3072 byteまで。Metadata全体1 MiB、256型、型ごと256 Property、保存状態はEntityごと256 KiBまで。列挙型・Asset参照・配列・任意objectの自動展開は今回のexportに含めない。

`EntityBehaviourSystem<T>`はTを継承させない。Tは`EntityBehaviourContext&`を受け取り、必要なLifecycleメソッドだけを実装する。メソッドはvoidまたはNexusGameStatus、destructorはnoexceptとする。共通の`enabled: Bool` Propertyを宣言する。

```cpp
class NativeMover {
public:
    explicit NativeMover(NexusGame::EntityBehaviourContext& context) noexcept : context_(context) {}
    NexusGameStatus OnUpdate(float dt) noexcept;
private:
    NexusGame::EntityBehaviourContext& context_; // System所有の借用先
};
```

System登録は`EntityBehaviourSystem<NativeMover>::Descriptor(systemId)`を既存`registration.systems.add`へ渡す。services接続時には既存`find / access`の借用callback内で`Connect(state, services, typeId)`を呼ぶ。metadata宣言だけでは任意のNative関数をC#へ公開しない。Native Systemの明示Bindingは既存経路を使う。

具体例は[GameComponents.h.in](../Templates/NativeModule/GameComponents.h.in)、[NativeMover.h.in](../Templates/NativeModule/NativeMover.h.in)、[NativeMover.cpp.in](../Templates/NativeModule/NativeMover.cpp.in)。Native/Hybridの新規テンプレートと`Add-GameModule.ps1`がこれらを生成する。既存Projectへファイルを自動上書きしない。既存Projectでは同じ宣言・登録・接続をゲーム側へ追加する。

ユーザーによる通常BuildとHost再起動の後、Content BrowserのSourceにあるNativeMover.h/.cppをEntityへdrop、またはDetailsの「Componentを追加」から選ぶ。共通Property画面でenabled/speedを編集し、「Native Componentを解除」で外す。追加・解除・Property変更は共通Undo/Redoの対象。右クリックのソースを開く操作は既存機能を利用する。

C#からも同じ値へ`entity.GetComponent("Game.Mover").Get<float>("speed")`等でアクセスする。C#用ECSや別保存形式は必要ない。C++からは既存`Components.Add/Remove/Read/Write/Query`を使う。

## データフロー・所有権・Lifecycle

### Native Systemの共有設定

サンプルの`CrowdSimulationSystem::ConnectSettings`は、Moduleが明示する`Game.CrowdSettings`（Hybridでは`NativeCombat.CrowdSettings`）を読む。ゲーム開発者は設定用Entityを一つ作り、このComponentを「Componentを追加」から配置する。Property `speed`を共通Detailsで編集し、通常のLevel保存を行う。0以上の有限値が必要で、0は移動停止。未配置なら従来速度1を使い、複数Entityへ配置した場合は実行エラーにする。

設定値の所有者はEntity/Levelであり、Systemの一時入力要求やNative objectではない。設定Entityの削除後は次のUpdateから既定値へ戻り、同じ型を別Entityへ配置すればその値を読む。Systemは毎回数値Handleを解決し、設定のComponent pointerを保存しない。DLL再生成後も同じLevel値を利用する。任意Systemへこのownerを自動割当する仕組みは追加していない。`CrowdSimulationSystem.h/.cpp.in`と両Moduleのservices接続を更新した。

1. HostがModuleのidentityを確認し、Metadataをコピーして検証し、inspectionで得たDLL参照を解放する。`module.create`は呼ばない。
2. 共通Reflectionへ格納型と値Descriptorを登録し、LevelとEditorを生成する。
3. Play時にMetadataがEditor起動時と一致することを再検査する。Loaderは実ロード後にも同じJSONか検査する。
   GameRuntimeではSetup成功と取消/Level生存確認の後、Bridge/System/Module生成前に再検査する。Setupが停止を要求した場合はDLL検査も開始しない。ProjectHostのLevel読込前取得とは別の段階である。
4. Module生成 → 既存System登録 → services接続。SystemはEngineのSchedulerがadapterを所有し、実体とTは確保元DLLで所有・破棄する。
5. System.Updateが全Queryページを回収してからTを生成する。OnCreate → enabledならOnEnable → OnUpdate。無効化時はOnDisable、再有効化時はOnEnable。
6. Entity削除・Component解除・再追加は次のSystem.Update境界で旧TのOnDisable/OnDestroyへ反映する。Entity slot世代とComponent attachmentの双方を検証する。
7. StopではLoaderがservicesを失効させ、Systemを逆登録順に解除・破棄し、Module stateを破棄してDLLを解放する。その後Worldを破棄する。

ContextをTより先に破棄しない。Contextは固定Engine関数表、数値Handle、Type IDだけを保持する。終了callbackでは自分のworker、購読、所有resourceを解放し、World変更を行わない。ContextのWrite/SetTransformはOnDestroy前に停止する。既に削除されたEntityの読取も成功を保証しない。

すべてWorld所有threadで逐次実行する。Contextをworkerへ渡さず、値計算の結果だけを所有threadで反映する。Tが作るthread/task/eventの解除はTの責務。destructorで解除不能な非同期処理を黙って残さない。

## Serialization・互換性・エラー

値は既存ComponentReflectionの`GameComponents.propertyState`としてLevelに保存する。実行tokenとTは保存しない。Scene.Cloneは値とtokenをコピーし、Level読込はtokenを初期化する。初回追加時の既定値を保存するため、コードの既定値変更で既存Levelの意味が変わらない。

未知のType/Property/kindは保持する。renameにはAliasを明示し、現在IDを優先して読む。型変更時の旧kindは保持し、現在kindがなければ既定値を使う。未来Versionの既知型は共通Descriptorで編集を拒否し、理由を表示する。自動migrationは行わない。旧Hostは新しいGameComponentsを解釈できないため、古いEngineへの逆互換は保証しない。

不正JSON、ID/alias衝突、Module identity不一致、Source逸脱は起動時にエラーとする。setterは候補値を検証してから置換し、失敗時は元の値を保持する。Game APIは既存statusを返し、Managedは既存例外変換を利用する。Behaviour callbackのC++例外はDLL内でFAILEDへ変換し、そのSystemを再実行しない。Runtimeは既存失敗経路で停止する。

終了callbackは一度ずつ試行し、失敗は保持する。失敗後の自動再試行やrollbackを保証しない。System破棄が失敗した場合は既存Scope/Loaderのquarantineに従ってstateとDLLをProcess終了まで保持し、再起動を要求する。

## Reloadへの影響と制約

Engine/Editor内部の型とReflectionRegistrationのlayoutを変更したため、今回の変更自体は通常BuildとHost再起動が必要。C ABIの既存structSize、Bootstrap、Game API v1、Levelの外側の形式は変更していない。Component protocolへ操作7/8/9を追加した。旧操作の値とDTOの56-byte layoutは維持する。

Live++の関数本体パッチでは既存T/System stateを保持する。既存の固定header/翻訳単位/patch契約に加え、Metadataを適用前・適用境界・適用後に照合する。schema変更は再起動を要求し、適用後の不一致は既存quarantineへ進む。任意のclass layout・所有権・保存schemaの変更をパッチ可能とは扱わない。Game AssemblyのReloadとは別処理である。

各フレームのQuery、JSON値アクセスは初期実装であり、大規模Entity数での計測は未実施。並列実行、ネイティブTの任意storage登録、Input/Audio/Physics等の包括的API、任意Native state移行、自動Bindingは残作業。共有設定のowner/重複拒否/保存はCrowdSettingsサンプルで明示しており、任意Systemの意味を推測した自動設定は行わない。C#の複数Script配置は[後続実装](MultipleScriptsREADME.md)を参照。

## 変更ファイルと静的確認

### ComponentStorageのMSVC move制約への修正

ユーザーのBuildでComponentStorage.hのnothrow move構築assertが失敗した。追加したGameComponentDataがstd::mapを直接所有していたことが原因。使用中のMSVC STLではmapのmove構築でsentinelを確保し、noexcept契約を持たない。ECSのassertを外したり、確保を行う処理へ無条件noexceptを付けたりせず、最大256件の実行tokenをvector<pair<string,uint64_t>>へ変更した。既定allocatorのstring/vectorはbuffer所有権をnoexceptで移動でき、Scene Cloneは引き続き深い値コピーとなる。

GameComponentData.hに検索/一意更新/解除と型側のmove契約assertを追加し、GameComponentData.cppに重複検査、NativeComponentMetadata.cpp/GameComponentAccess.cppに同じtoken操作を接続した。検索は上限256件の線形走査。tokenは従来どおり保存対象外なのでLevel形式・公開ABI・Module/Entityの寿命契約は変更しない。MSVC STL source、呼出箇所、所有権と保存対象を静的確認した。修正後のBuild/テストはエージェントでは実行していない。

### Setup取消とMetadata検査の順序修正

ユーザーのEditorTests実行でCancellationAndUpdateFailureの取消期待assertが失敗した。GameRuntime::StartへMetadata inspectionをSetupより先に追加したため、無効Native設定を持つ取消ケースがSetupへ到達する前に247で失敗していた。GameRuntime.cppでSetup→Setup失敗/取消/Level生存確認→Metadata検査→Bridge/Module/Managed初期化の順序へ修正した。検査でのLoadLibraryもDllMain/CRT初期化を行うため、取消時には検査自体を行わない契約を維持する。

GameRuntimeTests.cppの取消テストでは、期待234を維持したままSetupの実行回数と生成済みSystemの解放も検証するよう補強した。起動時Catalogの取得を行うProjectHost、実ロード時/パッチ時のschema照合、停止・所有thread・保存形式には変更なし。修正後のBuild/テストはエージェントでは実行していない。

上表の新規ファイルに加え、既存`ManagedScriptMetadata.cpp`を共通値検証へ接続した。`ProjectJson.h/.cpp`へファイルと同じ上限・重複キー検証付き文字列Parseを追加した。`ModuleDeployment.h`がSource基準を解決し、`GameModuleLoader.h/.cpp`がinspectionと実ロード/patchのschema一致を検査する。`Level.h`は起動時登録のためGameRuntimeだけをfriendとする。

`LevelSerializer.cpp`、`EditorWorkspace.cpp`、`EditorReinstancing.cpp`、`GameApiBridge.cpp`、`NativeGameSystemScope.cpp`は既存登録をReflectionRegistrationの共通窓口へ接続した。`TypeRegistry.h`は格納方式の値flag、`GameComponents.h/.hpp`は世代確認付き操作を追加した。`SceneEditService.h/.cpp`、`DetailsPanel.cpp`、`en.lang/ja.lang`が共通解除UIを提供する。

Native/Hybridテンプレート定義、両Module entry/state、`Add-GameModule.ps1`、`Add-GameSource.ps1`を更新した。後者の既存明示登録anchorを維持し、追加サンプルとの名前衝突を拒否する。SDK配布は既存のPublicヘッダー・Templatesコピーを利用する。

`NexusRuntime.vcxproj`、`NexusEngine.vcxproj`、`NexusEditorTests.vcxproj`、`NexusEcsTests.vcxproj`へ必要source/link対象を追加した。`NativeComponentTests.cpp`と`GameRuntimeTests.cpp`は既存EditorTestsへ検証経路を追加する。Metadata拒否、Alias/未来Version、Property履歴、追加解除Undo、未知値の保存、Entity Lifecycle、再アタッチtoken、client失効後終了を検証するソースであり、今回実行していない。

include/namespace、宣言と定義、Project compile一覧、C ABI layout、所有権、世代検証、Serializationの保存対象、thread・登録解除順、生成ファイル参照を静的に照合した。38 sourceのUTF-8・競合marker・区切り、182件のlocal include、105件の資料内local link、4 Project XMLと明示source入力の存在・重複、2 Template JSONと生成元、2 PowerShell AST、サンプルC++文字列が宣言する2型のJSON、既存Source生成anchor、git diff --checkを確認した。区切り確認はC++コンパイラの構文検査ではない。コンパイル/リンク、実Editor操作、Mono/Live++連携、性能はユーザーによる実行確認が必要。
