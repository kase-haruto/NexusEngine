# C#複数Scriptアタッチ

同じEntityへ同型を含む複数のC# ScriptBehaviourを配置する実装。既存ECS、ScriptComponent、LevelDocument、Reflection、DetailsPanel、Undo、Mono Domain Reloadを利用する。実装時点では静的確認のみだったが、後続指示で実Build/テストへ進めた。

**実行確認（2026-11-12）:** Mono複数Script統合と既存CoreCLR回帰がexit 0で成功した。[結果とログ](MultipleScriptsVerificationREADME.md)に現在の確認範囲を記載する。本書末尾の未検証記述は実装時点の記録であり、今回の成功をRelease/実GUI等の全構成へ拡大して扱わない。

ユーザー向けのBuildコマンド、テスト順序、期待結果は[テスト手順](GameplayTestingREADME.md)を参照。

## 調査と設計判断

従来のScriptComponentはscriptType/enabled/propertyStateと実行instance/attachmentを一組だけ持ち、ScriptSystemとScriptRuntimeも主Scriptだけを検証していた。ManagedScriptMetadataのaddは型を置換し、Detailsの解除はComponent全体を削除していた。公開Propertyは既存のType ID/Property IDとEntityの組で解決するため、同じ型を同じEntityへ複数置くと編集先が曖昧になる。

主Scriptの保存fieldを維持し、追加Scriptを最大31件の値配列として同じECS Componentへ保持する。現在の契約は**同型を含め合計32 Scriptまで**。各slotへ既存PersistentIdを保存し、PropertyHandle.instanceで個別編集する。旧LevelにslotIdがなければ読込時に生成し、次回保存から維持する。AddScript<T>は既存instanceを返す互換動作、AddScriptInstance<T>は明示的な追加。Detailsから同じ型を追加すると別instanceになる。2026-11-13のEditorTestsで編集/Undo/保存/解除を、実Monoで同型2 instanceの独立値・Enabled・Reload後の復元を検証した。

追加Scriptを別の隠しEntityへ配置する案は、Entityの同一性・階層・削除・Level保存を分断するため採用しない。別Script ECS/Inspector/System Managerは作らない。Native/Managed関数表のlayoutを増やす案に対し、既存servicesのEngine予約protocolを追加する方式を採用した。

## 責務・依存関係

| ファイル | 変更と責務 |
|---|---|
| [ScriptComponent.h](../Runtime/Scene/Components/ScriptComponent.h) | ScriptAttachment値、主Script+additional、上限・重複検査、token/型検索、主Script解除時の昇格 |
| [GeneratedReflection.h](../Runtime/Reflection/EngineGenerated/GeneratedReflection.h) | 宣言markerと一致するadditionalの保存登録だけを追加 |
| [ReflectedInspector.h](../Runtime/Reflection/ReflectedInspector.h) | 基底のScriptAttachmentが宣言する公開member pointerも共通生成Descriptorへ接続 |
| [ManagedScriptMetadata.cpp](../Runtime/Reflection/ManagedScriptMetadata.cpp) | 型ごとに現在slotを解決、追加、個別解除、共通Property編集と@enabled |
| [ScriptAttachments.h](../Public/Nexus/ScriptAttachments.h) | 24-byte requestと既存services Command ID |
| [GameApiBridge.cpp](../Runtime/Gameplay/GameApiBridge.cpp) | token指定の状態・有効化・解除・一覧、各slotのPropertyコピー、旧主Script APIを維持 |
| [ScriptSystem.cpp](../Runtime/Scripting/ScriptSystem.cpp) | 一つのEntityから全関連付けを一括同期、保存順序を維持、token指定書戻し、全instanceの終了/reset |
| [NativeManagedBridge.h](../Runtime/Scripting/NativeManagedBridge.h) / [NativeManagedBridge.cpp](../Runtime/Scripting/NativeManagedBridge.cpp) | Bound Gameplay Bootstrapの任意feature確認。非対応版への複数Script実行を拒否 |
| [GameScripts.cs](../Managed/Nexus.Managed/GameScripts.cs) | GameScript非所有wrapper、関連付けの値protocol、高頻度状態検査の固定stack値転送 |
| [Entity.cs](../Managed/Nexus.Managed/Entity.cs) | AddScript<T>/GetScripts。旧SetScript/RemoveScriptは意味を明示 |
| [ScriptBehaviour.cs](../Managed/Nexus.Managed/ScriptBehaviour.cs) | 自身のtokenをBindし、Enabledが主Scriptを誤操作しないようにする。旧Bind overloadは保持 |
| [ScriptMetadata.cs](../Managed/Nexus.Managed/ScriptMetadata.cs) | 共通有効状態用Property ID @enabledを予約 |
| [ScriptRuntime.cs](../Managed/Nexus.Managed.Bootstrap/ScriptRuntime.cs) | token別Current検査、Aliasを含む同一型の重複拒否、全slotの既存Lifecycle/状態保存 |
| [GameplayEntryPoints.cs](../Managed/Nexus.Managed.Bootstrap/GameplayEntryPoints.cs) / [MonoEntryPoints.cs](../Managed/Nexus.Managed.Bootstrap/MonoEntryPoints.cs) | SupportsScriptAttachments(abi=1)の明示feature応答とDomain所有delegate |
| [SceneEditService.h](../Editor/Services/SceneEditService.h) / [SceneEditService.cpp](../Editor/Services/SceneEditService.cpp) | 型ごとの個別解除を既存Clone/交換/Undo transactionへ通す |
| [DetailsPanel.cpp](../Editor/Gui/Panels/DetailsPanel.cpp) | Script追加選択、各Scriptの解除/ソースを開く、全解除を区別。Property描画は既存共通ページ |
| [ja.lang](../Resources/Editor/Localization/ja.lang) / [en.lang](../Resources/Editor/Localization/en.lang) | Script追加・全解除の表示文言 |
| [Nexus.Managed.csproj](../Managed/Nexus.Managed/Nexus.Managed.csproj) / [Publish-Engine.ps1](../Tools/Engine/Publish-Engine.ps1) | 新Managed sourceのcompile/SDK配布対象 |
| [ScriptApi.h](../Public/Nexus/ScriptApi.h) | 従来APIの主Script/全解除契約のコメント |
| [ManagedReflectionTests.cpp](../Tests/ManagedReflectionTests.cpp) | 置換を期待する旧試験を複数追加へ更新。個別有効化・解除Undo・保存token非永続化の検証ソース |
| [GameServicesTests.cpp](../Tests/GameServicesTests.cpp) | 同Entityの状態分離・主Script昇格・旧token拒否・旧API互換の検証ソース |
| [Probe.cs](../Tests/Fixtures/MonoGameplay/Probe.cs) / [MonoGameplayTests.cpp](../Tests/MonoGameplayTests.cpp) | 第二Scriptによる同じTransformのY更新、独立したenabled、Domain Reload後の値/ID保持の検証ソース |

RuntimeはEditor/ImGuiへ依存しない。Editorは保存値とコピー済みMetadataだけを扱い、Managed objectやDomainを所有しない。資料入口はREADME、GameApiReference、SourceContentBrowserREADME、MonoRuntimeREADME、NativeEntityComponentsREADMEにも反映する。

## 使用方法

ユーザーによる通常BuildとHost再起動が必要。Nexus.Managed.dll、Bootstrap、Mono fixtureも現在のsourceから更新する。新規の.NET Hosting方式やRuntimeバージョンは導入していない。

EditorではSourceファイルをEntityへdrop、またはDetailsのScript選択から追加する。別の型を追加しても既存Scriptは残る。各行の解除はその型だけ、全解除は全Scriptを対象とする。公開Propertyと各ScriptのEnabledは共通のProperty画面で編集し、同じUndo/RedoとLevel保存を利用する。ソースを開く操作は各行から利用できる。

```csharp
var move = entity.AddScript<PlayerController>();
var combat = entity.AddScript<CombatController>();
combat.Enabled = false;       // PlayerControllerには影響しない
var scripts = entity.GetScripts();
combat.Remove();             // この関連付けだけを解除
bool alive = move.IsAttached; // Entity/World/tokenを再検証
```

AddScript<T>は同じ正規ID/宣言Alias/FullNameがあれば既存tokenを返し、既存enabledを上書きしない。GameScriptはBehaviour実体を返さず、Propertyは既存の共通Metadata編集/各Behaviour内で扱う。ScriptBehaviour.Enabledは自分自身の関連付けを操作する。

互換APIのEntity.SetScript<T>は主Scriptだけを置換し、追加Scriptを残す。追加Scriptに既にある型への主Script置換は拒否する。Entity.ScriptEnabledは主Scriptだけ、Entity.RemoveScript()は従来互換として全解除。複数Scriptの操作にはGameScriptを使う。

## 保存互換性・所有権・寿命

旧LevelのscriptType/enabled/propertyStateはそのまま読み込む。additionalがない場合は空配列の既定値となる。新Levelは追加Scriptの同じ三fieldだけを保存し、instance/attachmentは保存しない。ScriptAttachmentは値Reflectionであり、新しいECS Component型の登録は増やさない。未知TypeやPropertyの保存値は各slotに保持する。型renameのAlias、Version、旧kind保持は既存Metadata規則を利用する。

ScriptComponentの内部C++ layoutが変わるためEngine/Editor/Hostは通常Buildが必要。古いEngineへ新形式を逆読込させる互換性は保証しない。個別状態は256 KiB、Entityの全Script状態は1 MiBまで。型IDは512 byte。Aliasで同じ実型が二重登録されたデータは、Managed callbackより先に拒否する。

Scene/Levelが値を所有し、ScriptRuntimeがManaged instanceを所有する。GameScript wrapperはWorld capability、EntityHandle、関連付けtokenを保持するだけ。追加配列へのpointerはNative処理中だけ借用し、callbackやフレームを越えて保持しない。

初期化時に全slotの実行IDをリセットし、tokenを採番、Scene反復終了後に全bindingをManagedへ渡す。同じEntity内では主Script→追加順でOnCreate/OnEnable/OnUpdateを実行する。解除時は次の同期でOnDisable/OnDestroyと参照解除を行う。主Scriptを解除した場合は追加の先頭を昇格するが、そのtoken/値/instanceは維持する。World/Entity/slot世代が一致しない書戻しは拒否する。

Stopでは全slotの実行参照を解除し、既存BridgeのManaged cleanup結果とDomain/ALC解放確認に従う。終了中の自身のEnabled変更は拒否する。Threadは従来同様World所有threadのみ。処理をworkerへ移す場合は値計算だけを渡す。

## API境界・エラー・Reload

予約Commandはnexus.engine.scripts、version1。requestはpack8の24 byte、返却状態は既存16-byte ScriptState。操作1=追加、2=token状態、3=token有効化、4=token解除、5=index一覧。後続型IDはUTF-8コピー。一覧は一件ずつ取得する低頻度操作で、Lifecycle中の状態確認は固定stack値を使う。STL/GCHandle/Native objectを境界へ渡さない。

旧Script APIとBootstrap V1〜V5のstructSize/関数表は維持する。複数Script実行には現在のBound Gameplay Bootstrapが必要で、任意export SupportsScriptAttachmentsがABI1成功を返すことをNative側が確認する。旧Bootstrapの単一Script経路は維持し、複数Scriptは明示エラーとする。

無効Entity/token、上限超過、重複、誤thread、API不一致は既存statusとManaged例外で通知する。token状態照会だけは解除済みをtoken0で返す。Native例外はstatusへ変換し、Script例外/cleanup失敗の報告は既存Logger/Console経路に従う。一般のmutationを自動再送しない。

Reloadでは既存Scene snapshot/rollbackに追加配列も含まれる。各slotの公開値を停止前に保存し、新世代ではinstanceだけを再生成する。tokenとEntityの同一性を保って復元し、旧世代の書込は既存GameWorld.DisallowWritesで拒否する。Mono Domainのthunkは旧Domain解放前に破棄する。Live++による今回のEngine layout変更適用や任意状態rollbackを保証するものではない。

## 静的確認・未実装・未検証

宣言/定義、Reflection markerと登録、Native/Managedの24/16-byte境界、namespace/include、compile/SDKコピー対象、Handle/slot所有権、個別解除と主Script昇格、共通Property/保存、Reload時の全slot reset、thread制約を照合した。24 sourceのUTF-8/競合marker/区切り、98 local include、2 Managed Project XMLと明示compile対象、SDK PublishのPowerShell AST、138資料link、git diff --checkを確認した。区切り走査はコンパイラによる構文検査ではない。実ビルド・リンク・Editor操作・Mono/CoreCLR実行・Reload・性能は未検証。追加した検証ソースは実行していない。

同じ型の複数instance、Editorでの実行順序入替え、Script objectの言語横断取得は未実装。非対応MetadataのScriptは保存値を保持するが、Editorからの型別操作には対応Metadataが必要で、全解除は可能。Input等のBackend、任意Native state移行、包括的サービスAPIは今回の作業に含めていない。
