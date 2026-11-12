# 共通Component API / Entity Query / Transform batch

全体のクラス・関数一覧は[ゲーム開発API一覧](GameApiReference.md)を参照する。本書は追加Component APIの設計と制約を説明する。

## 今回の実装単位

C++ / C#開発環境の残作業のうち、共通Component操作と一括Transform操作を先に実装した。独立DLLのComponent登録、Native BehaviourのEditor配置、Native System共有設定、各Backendの公開は別の責務として残る。本変更だけで残作業すべてを完了したとは扱わない。

ビルド、ゲーム起動、テスト実行は行っていない。以前のMono試験の成功は本変更の検証結果ではない。現在起動済みのEditorへこのソース変更が反映されたわけではなく、ユーザーによるEngine / Host / Managed APIの通常Buildと再起動が必要になる。

## 調査結果と再利用

| 既存機能 | 責務と今回の判断 |
|---|---|
| GameApiBridge | World token / Entity世代 / 所有Thread / 呼出し深さ / services寿命を検査。すべて同じ経路を利用 |
| GameServicesApiV1 | 明示ID / Versionで値を転送する既存C ABI。追加の関数表やV6 Bootstrapを作らず、限定的なEngine protocolを追加 |
| Scene / Entity / ComponentStorage | 唯一のECS。新しいComponent用StorageやC# ECSを作らない |
| TypeRegistry / PropertyDescriptor | 同じクラスをLevelのホスト登録方針から構成し、Editorと同じaccessorを利用。新しいRegistryクラスや独立した型ID体系は作らない |
| ReflectionRegistration | Levelごとのホスト登録方針。GameApiBridgeは同じinspector登録を利用し、EngineがGame型を固定で知る構造を避ける |
| ReflectedInspector | 既存Storageへの型付き削除callbackを補完。TypeRegistry内の専用setterは維持 |
| LevelDocument / LevelSerializer | 同じComponent実体を保存する既存経路。別形式・別保存先を追加しない |
| ScriptSystem | binding列挙後、Sceneの反復guardを抜けてからManaged callbackを呼ぶため、既存規則の範囲で構造変更が可能 |
| CSharpRuntime / MonoHost | Reload時のWorld clone / rollbackと旧Worldの書込禁止を利用。MonoのDomainや所有者は変更しない |
| Project Build / SDK Publish | 既存3つのC++ compile対象とManaged compile一覧、SDKコピー一覧へ接続 |

Input Backendはユーザー確認済みの別作業。Audio / Physics等のBackend全体の完成状況は本作業では不明であり、未実装のBackendをダミーAPIで包んでいない。

## 採用構造と代替案

```text
C++ Components / Transforms Facade ─┐
                                    ├─ GameServicesのEngine固定protocol
C# Entity / GameComponent / GameWorld┘          ↓
                                     GameApiBridgeのWorld lease
                                              ↓
                              TypeRegistry / PropertyDescriptor / 既存Scene
                                              ↓
                                共通ComponentStorage / Level保存
```

`GameComponentAccess`を追加した理由は、境界DTOの検証・Property変換・バッチの原子性がWorldルーティングとは別の責務だからである。RegistryやManagerではなく、借用Levelと既存TypeRegistryに対する同期処理だけを持つ。

新しいBootstrap V6 / Native Module contextを追加する案は、Mono / CoreCLR / 旧DLL / Reloadの開始契約すべてを変更する必要があるため、今回の限定的な操作には採用しなかった。既存GameServicesを利用し、予約ID `nexus.engine.components` / `nexus.engine.transforms`、protocol version 1を定義した。`nexus.engine.`で始まるIDはGame DLLから登録できず、Engine処理を上書きできない。汎用RPCや任意C++クラスの自動公開は追加していない。

JSONによる汎用メモリ書換えではなく、固定DTOと安定Type / Property IDを使用する。関数表V1～V5のlayoutは維持し、STL・例外・GC参照・Component pointerは境界を通さない。

## 対応範囲

- 登録済みComponentの有無、既定値での追加、削除。
- 公開Propertyの読み書き。bool / int32 / float / UTF-8 string / Vector2・3・4 / int32 Enum。
- Rotation / Color / Asset path等は既存PropertyDescriptorの意味とsetterを使う。例えばFog.colorはDetails同様のsRGB表示値であり、内部のlinear値へ直接書き込むAPIではない。
- Componentを一つ条件にしたEntity Query。複数条件の汎用Query DSLはまだない。
- 最大64件のLocal Transform一括取得・更新。値は既存Transform API同様に有限値とQuaternionを検証・正規化する。

Name / Transform / Hierarchy / PersistentIdの追加・削除は禁止する。Scriptは存在照会とQueryだけを許可し、値編集・追加・削除はinstanceとattachmentの専用契約を維持する。ManagedScript疑似型も汎用Component APIで編集しない。未登録Type / Propertyは失敗する。Game DLLの任意型をこのAPIだけで登録可能にしたものではない。

`GameComponent`はEntityとType IDから「現在のComponent」を解決するproxyであり、削除されたinstanceを所有しない。削除中のアクセスは失敗し、同じEntityへ再追加した後は現在のComponentに解決する。Component instance固有の永続Handleを追加していない。

## C#使用例

```csharp
using Nexus.Managed;
using System.Numerics;

// ScriptBehaviourのOnCreate / OnUpdateなど、Worldが有効な所有Threadから呼ぶ。
var fog = Entity.AddComponent("Fog");
fog.Set("startDistance", 3.0f);
float start = fog.Get<float>("startDistance");

var transform = Entity.GetComponent("Transform");
transform.Set("translation", new Vector3(1, 0, 0));

var entities = World.Query("Transform");
var values = World.GetTransforms(Entity); // 一回最大64件
values[0].Translation.X += 1;
World.SetTransforms(new[] { Entity }, values);

Entity.RemoveComponent("Fog");
```

`Entity.HasComponent` / `TryGetComponent`も用意した。未登録Type IDは使用ミスとして例外を返し、登録済みだがEntityに存在しない場合はfalseを返す。Enumはint32を基底型とするユーザー定義enumをGet / Setする。Quaternion自体の汎用Property変換は対象外であり、回転には従来のEntity.TransformまたはLocalTransform batchを使える。

GameコードはNative pointer / P/Invoke / GCHandleを扱わない。停止したsession、別WorldのEntity、所有Thread外の操作、旧世代cleanupからの書込を既存GameWorld検査で拒否する。Play Worldの変更なのでEditorのUndo履歴は生成せず、通常のPlay / Stopの分離を維持する。

## C++使用例

```cpp
#include "Public/Nexus/GameComponents.hpp"
#include "Public/Nexus/GameTransforms.hpp"

// NexusConnectGameServicesで受け取ったcontextを使う。clientの所有者は既存Loader。
NexusGame::Components components(*context);
bool exists = false;
auto status = components.Has(entity, {"Fog", 3}, exists);
if(status == NEXUS_GAME_OK && !exists) status = components.Add(entity, {"Fog", 3});
if(status == NEXUS_GAME_OK) {
    NexusComponentValueV1 value{};
    value.kind = NEXUS_COMPONENT_FLOAT;
    value.number[0] = 3.0f;
    status = components.Write(entity, {"Fog", 3}, {"startDistance", 13}, value);
}

NexusGame::Transforms transforms(*context);
NexusLocalTransform value{};
status = transforms.Read(&entity, 1, &value);
if(status == NEXUS_GAME_OK) {
    value.translation[0] += 1;
    const NexusTransformUpdateV1 update{entity, value};
    status = transforms.Write(&update, 1);
}
```

Facadeはservices clientを開閉しない。Module接続のclientを借用し、Module停止後はEngineが呼出しを拒否する。Native Systemで利用する際はModule stateから必要なFacade / contextを渡し、System descriptorにはアクセスするComponentを宣言する。Workerで純粋な値計算を行う場合も、Engine APIへの反映は所有Threadへ戻して行う。

Native側では任意export `NexusConnectGameServices` を実装してcontextを受け取る。Module.create / System.createの時点ではservices接続前なので、この新APIを先に呼ばない。Systemへは接続後、既存のSystem access visitor等から明示的に渡す。全Nativeテンプレートへservicesを自動注入する変更は今回行っていない。

## ABI・Error Handling

| DTO / 制約 | 内容 |
|---|---|
| ComponentValue | 32 byte。明示kind、文字列長、int32、reserved、float×4 |
| ComponentRequest | 56 byte。操作・Type / Property長・Query cursor / limit・値 |
| Query応答 | 8 byte header + 最大255 EntityHandle。UINT32_MAXが終端 |
| Transform batch | 8 byte header。ReadはHandle配列入力 / Transform配列応答、Writeは56 byte Update配列 |
| 文字列 | UTF-8、NULなし。Type / Propertyは各512 byte、値は3072 byteまで。全request / responseは既存4096 byte制限 |
| layout | Windows x64 / pack 8 / little endian。Nativeはmemcpyで非整列入力を読む |
| 不在 | INVALID_HANDLE。World / Entity世代 / Type / Property / client失効を検査 |
| 不正な値 | INVALID_ARGUMENT。kind、reserved、未使用field、UTF-8、finite、既存setterの値域を検査 |
| Thread違反 | WRONG_THREAD。登録したWorldの所有Threadだけを許可 |
| protocol不一致 | INCOMPATIBLE_ABI。未知versionを推測で処理しない |
| 容量不足 | 純粋な読取のみBUFFER_TOO_SMALLと必要数を返す。部分コピーや二重書込はしない |
| 例外 | NativeでFAILEDへ変換。Managedは従来のGameWorld.Checkに従い例外として通知 |

Transform batchは全Handleと全値を検査後に適用する。不正要素があれば一件も更新しない。同じEntityを複数指定した場合は最後の値が残る。64件を越える複数batch全体の原子性は保証しない。

Query cursorはSceneの既存反復guardで保護される。次のページは未走査位置から読み、全ページで先頭から再走査しない。Native呼出側はページ間でEntity / Component構造を変更しない。C#のQueryは所有Thread上で全ページを同期回収してから返す。結果はHandleの値リストであり、新しいECSや永続索引ではない。

## 所有権・初期化・終了・Reload

1. 既存Levelを生成し、ホストのReflectionRegistrationを確定する。
2. GameApiBridgeがRegisterSceneTypesと同じホストinspector登録からaccessorを得て、ComponentReflectionにも存在する型だけを公開する。Details専用の疑似型を保存可能なComponentと偽装しない。コードの寿命はホストに属し、独立Game DLLの生callbackを登録しない。
3. Native Module / Managed sessionが従来services clientで接続する。
4. 呼出し前にWorld / Scene / Thread / Entityを検証し、serviceDepthを増やす。
5. Reflection処理はWorld mutex外で実行する。getter / setterは既存の契約同様、副作用を限定したWorldの値アクセスとする。
6. callback完了後にdepthを戻す。呼出し中のclient解除・Reload境界取得は既存経路で拒否する。
7. StopでclientとWorld tokenを失効させ、その後Levelを破棄する。accessorはComponentやManaged objectの寿命を延長しない。

Native DLL固有stateの呼出しはManaged Reload中にfreezeする。一方、このEngine protocolの対象はsnapshot / rollbackされる共通Sceneなので、新世代OnCreateのComponent操作を許可する。旧世代cleanupの書込はManaged GameWorld.DisallowWritesで拒否する。APIからWorldを交換せず、CSharpRuntimeがRebindSceneした後もgetterは現在Entityから再解決する。

TypeDescriptorの内部layoutを拡張したため、Engine / Editor / Hostは通常Buildと再起動が必要。Native Module ABI / Bootstrap V1～V5 / Level保存形式は変更していない。Live++による今回のEngine変更の適用は対象外。

## 変更ファイル

| ファイル（Project/基準） | 内容 |
|---|---|
| Public/Nexus/GameComponents.h | 予約protocolと固定DTO |
| Public/Nexus/GameComponents.hpp | C++ Component操作 / Query Facade |
| Public/Nexus/GameTransforms.hpp | C++ Transform batch Facade |
| Runtime/Gameplay/GameComponentAccess.h | Reflection / batch同期処理の宣言 |
| Runtime/Gameplay/GameComponentAccess.cpp | 入力検証、値変換、Query、全件検証後のbatch適用 |
| Runtime/Gameplay/GameApiBridge.cpp | ホストReflection接続、予約ID routing、寿命・freeze制約 |
| Runtime/Reflection/TypeRegistry.h | 型付き削除callbackを既存Descriptorへ補完 |
| Runtime/Reflection/ReflectedInspector.h | 既存Storageの型付き削除登録 |
| Runtime/Scene/Scene.h | 既存Entity反復のcursor継続 |
| Managed/Nexus.Managed/GameComponents.cs | GameComponent / GameWorld Query / batch、DTOとtyped変換 |
| Managed/Nexus.Managed/Entity.cs | Has / Get / TryGet / Add / Remove Component |
| Managed/Nexus.Managed/Nexus.Managed.csproj | 新Managed sourceをcompile対象へ追加 |
| Tools/Engine/Publish-Engine.ps1 | Installed SDKへManaged sourceを同梱 |
| Build/NexusRuntime.vcxproj | Native実装のcompile対象 |
| NexusEngine.vcxproj | 旧一括ホストのcompile対象 |
| Tests/NexusEditorTests.vcxproj | 既存CPUテストのcompile対象 |
| Tests/GameServicesTests.cpp | 予約ID、Property値域、UTF-8、保存復元、Query、Thread、batch失敗時非更新の試験ソース |
| Tests/Fixtures/MonoGameplay/Probe.cs | Mono OnCreate / Reloadから共通Fog / Query / batchを使う試験ソース |
| Tests/MonoGameplayTests.cpp | 同じNative Fogへの反映を検査する試験ソース |
| Docs/GameComponentApiREADME.md | 本書 |
| ../README.md | 本書への入口 |

## 静的確認と未検証

include、namespace、宣言・定義、3つのNative compile一覧、Managed compile / SDKコピー一覧、DTO size / field順、所有権、世代付きHandle、Scene反復guard、Reloadの旧世代書込禁止とWorld rollback、従来Level形式を照合した。14ファイルのUTF-8・区切り・競合marker、38件のlocal include、5件のProject XML、明示compile入力の存在・重複、SDK PublishのPowerShell AST、git diff --checkに問題はなかった。区切り走査はC++ / C#コンパイラによる構文検証の代わりではない。動的なSystem登録や別Inspectorを追加していない。

現SystemSchedulerは逐次実行であり、今回のAPIは自動並列化を導入しない。Scriptから動的にComponentへアクセスできるため、将来の並列SchedulerではScriptSystemのアクセス範囲を固定のTransform書込だけと推測してはいけない。

試験ソースは追加したが実行していない。Native / Managedのコンパイル・リンク、Mono実行、実際のEditor / Save / Reload、性能は未検証。変更後のユーザー確認にはNexusEditorTestsと、API / Bootstrap / V1 / V2 fixtureを更新したNexusMonoGameplayTestsを使用する。古いfixture DLLを新API試験の代わりに使わない。

独立DLLの値Component Metadata / Storage / Level読込前登録と解除契約、Native Entity logicのEditor配置、Native System共有PropertyのEntity所有サンプルを後続変更として追加した。[Native Entity Component](NativeEntityComponentsREADME.md)を参照。GameApiBridgeはComponentReflectionに登録されたホスト型に加え、このCatalogが所有する値型を同じprotocolへ公開する。既存Backendの公開窓口、高度なNative state移行は残作業。並列Scheduler、自動Binding、汎用Native Extensionは追加していない。
