# Jolt Physicsと共通ゲーム世界

## 機能と現在の確認範囲

Physics BackendはJolt Physics 5.6.0（MIT）を固定使用する。ソースは`ThirdParty/JoltPhysics`、取得情報とライセンスは同ディレクトリの`UPSTREAM.md`と`LICENSE`にある。PhysXへの依存は追加していない。

接続範囲はCPU Rigid Body、独立Collider、Box/Sphere/Capsule、Triangle Mesh/Convex Hull、Static/Dynamic/Kinematic、重力、摩擦・反発、Raycast、Force/Impulse、Dynamic CCD、接触Enter/Stay/Exit、および共通Reflectionによる編集・Level保存である。2026-11-13にDebug EditorTestsと実Mono統合テストがexit 0で成功した。Release SDKと実Editorの確認は配布記録を参照する。

## 既存構造を使う理由

### Meshと接触の追加契約

`Collider.shape`の3はTriangle Mesh、4はConvex Hull。`meshAsset`は既存ModelAssetLibraryの参照を指定する。Hostが同じCPU model cacheをLevelへ借用するため、新しいAsset Managerは作らない。node bind poseとEntity scaleを焼き込み、skinned modelは拒否する。Dynamic Triangle Meshは拒否し、Convexを使用する。上限は1 model 250000 triangles、Convex 4096 unique vertices、World cache 128 shapes/1000000 triangles。cache未参照shapeだけを回収し、solverが利用中のshapeを破棄しない。

`RigidBody.continuous=true`はDynamicのLinearCast CCD。Colliderは形状/材質、RigidBodyは運動/質量を所有する。既存RigidBody単独の形状fieldは旧Levelの互換fallbackとして残す。

Jolt callbackは`PhysicsContactCollector`が値だけを収集し、job完了後にEntity pairへ集約する。ゲームコード/Managed callbackをsolver lock中に呼ばない。Nativeは`Physics.ReadContacts`、C#は`GameWorld.GetPhysicsContacts(out dropped)`で最後のstepを非消費照会する。接触のkindはEnter=1、Stay=2、Exit=3。ExitのEntityは既に破棄済みの場合があり、IsAliveを検査する。normalはFirstからSecond向き、Sensorはどちらかがsensorならtrue。

予約Event `nexus.engine.physics.contacts`（version 1）も利用できる。Native `EnableContactEvents`、C# `EnablePhysicsContactEvents`でclientごとに明示購読し、既存TryPollEventで消費する。未購読clientへ通知しない。payloadは16 byteのcount/next/dropped/reserved headerと64 byteの接触DTOを最大63件。next=UINT32_MAXが終端。snapshotは最大4096件、内部subshape cacheは65536件。欠落はdropped、Event queue満杯はLoggerで通知する。通知が必要な全frameでqueueを消費すること。Scene交換で旧接触を破棄し、購読はservices client終了で解除する。

EntityとComponentの所有者は既存Sceneのままにする。`RigidBodyComponent`が形状・材質・速度の正本であり、JoltのBodyは計算用の派生状態である。C#用ECS、Level、Inspector、TypeRegistry、独立System Managerは追加しない。

`PhysicsWorld`はJolt固有の所有権と型をPimplに閉じ込めるために必要である。`PhysicsUpdateSystem`は既存Schedulerの更新順へ接続する小さなAdapterであり、独自Schedulerではない。`GamePhysicsAccess`は固定ABIの検査・変換をsolver処理から分離するために置く。

Joltを各Componentに直接持たせる案は、SceneのClone・Undo・DLL/Managed Reloadでpointerを複製してしまうため採用しない。各言語でsolverを別々に持つ案も、同じLevelを共有できなくなるため採用しない。

## 責務と依存方向

| ファイル | 責務 |
| --- | --- |
| `Runtime/Scene/Components/RigidBodyComponent.h` | 保存可能な値と共通検証。solver pointerを持たない |
| `Runtime/Physics/PhysicsWorld.*` | Body/Shape生成・破棄、ECS同期、simulation、query |
| `Runtime/Physics/PhysicsUpdateSystem.*` | Simulationの後、Transformの前に実行するAdapter |
| `Runtime/Physics/GamePhysicsAccess.*` | 既存GameServicesのPhysics要求を検証する |
| `Public/Nexus/GamePhysics.h` | C ABIの固定DTOとprotocol version |
| `Public/Nexus/GamePhysics.hpp` | C++の型付きFacade |
| `Managed/Nexus.Managed/GamePhysics.cs` | 同じ入口を利用するManaged API |
| `Build/Nexus.Jolt.targets`、`Tools/Engine/Build-Jolt.ps1` | Source Engineで依存を構築・リンクする |

依存は`Game Logic → Public API / Nexus.Managed → GameServices → 既存Level/Scene + PhysicsWorld → Jolt`。RuntimeからEditor/ImGuiへの依存はない。Public APIへJolt header、Body ID、Native pointerを公開しない。

## 更新・所有権・終了順序

1. Level生成時に既存SchedulerへPhysics Adapterを登録する。solverの所有slotはLevelにある。
2. `Simulation`段階でNative SystemとScriptが既存ECSを更新する。
3. `Physics`段階で有効なRigidBodyがある場合だけsolverを遅延生成する。
4. 世代付きEntityId、形状、pose、値を検査し、追加・削除・形状変更を同期する。
5. Force/Impulseを消費し、simulation後のposeと速度を同じComponentへ書き戻す。
6. `Transform`段階が結果から派生Matrixを確定する。

Editor編集用の`Level.Update(0)`ではsolverを生成・進行しない。Raycastは問い合わせ時のECS値を同期するが、時間を進めない。

`LoadEmpty`、`Unload`、`ExchangeScene`、Levelファイルの読込成功時には旧solverを破棄する。SceneのアドレスやEntityIdが再利用されても旧Bodyを引き継がない。Level終了時のmember順序は、Scheduler/System、solver、Sceneの順で解放されるようにする。solverは旧Sceneを破棄時にdereferenceしない。

Process共通のJolt Factory/型登録はRAIIで保持し、Worldより先に確立、World破棄後に解除する。別所有者のFactoryを上書きしない。ModuleやManaged objectをJolt callbackに登録しない。

## Componentの編集と保存

共通DetailsのComponent追加で`RigidBody`を選ぶ。`motion`は0=Static、1=Dynamic、2=Kinematic、`shape`は0=Box、1=Sphere、2=Capsule。Boxは`halfExtent`、Sphere/Capsuleは`radius`、Capsuleは円柱部分の`halfHeight`を使う。半高0のCapsuleはSphereとして生成する。

単位はm、kg、s。重力はY下向き9.81 m/s²。Transformの正のscaleを形状寸法へ適用する。非一様scaleのSphereは最大軸、Capsule半径はXZの最大軸を使う保守的な近似で、楕円体ではない。

既存のReflection生成器が安定Type ID `RigidBody`をComponentReflectionとTypeRegistryへ登録する。共通PropertyEditService/PropertyDrawerRegistryの経路で編集し、LevelSerializerの既存拡張Componentレコードへ保存する。既存Levelの形式バージョンは変更しない。古いLevelにはRigidBodyがないため既存動作を保つが、新しいComponentを含むLevelを古いEngineで読む互換性は保証しない。

poseは既存Transform、速度はRigidBodyに保存する。`force`と`impulse`は次step用の一時値で、Levelファイルには保存しない。Scene Cloneでは値として複製される。Body/Shape ID、contact cache、sleep状態などJolt内部状態は保存しない。

## C++からの使用

```cpp
#include "Public/Nexus/GamePhysics.hpp"

// Module接続時に受け取ったservicesを借用する。Module停止後に使わない。
NexusGame::Physics physics(services);
NexusPhysicsHitV1 hit{};
const auto status = physics.Raycast({0, 5, 0}, {0, -1, 0}, 20, hit);
if(status == NEXUS_GAME_OK && hit.entity.world != 0) {
    // 命中Entityを既存Game APIで操作する。
}
const auto pushed = physics.AddImpulse(entityHandle, {0, 3, 0});
```

## C#からの使用

```csharp
// entityは共通WorldのEntity。Componentの追加・編集も既存の共通APIを使う。
entity.AddComponent("RigidBody");
entity.AddImpulse(new System.Numerics.Vector3(0, 3, 0));
if (entity.World.Raycast(new(0, 5, 0), new(0, -1, 0), 20, out var hit)) {
    // hit.Entity、Point、Normal、Distanceは値と世代付きHandle。
}
```

GameWorldのsession/thread検査とReload中の書込禁止を共通利用する。Facadeから呼ぶ対象はEngine常駐のGameServices入口であり、ManagedはJolt/ゲームDLL内の関数pointerを保持しない。

## ABI・エラー・Thread

予約commandは`nexus.engine.physics`、versionは1。要求40 byte、Raycast応答48 byte、pack 8。Booleanは転送しない。未使用fieldとreservedは0。引数bufferは呼出中だけ借用し、保持しない。引数・戻り値はコピーで、STL・例外・allocatorは境界を越えない。

World、Entity世代、所有thread、protocol version、byte数、有限値、値域をNativeで検査する。無衝突は成功で空Handle、不正値は`INVALID_ARGUMENT`、古いWorld/Entityは`INVALID_HANDLE`、誤threadは`WRONG_THREAD`、応答容量不足は必要byte数付き`BUFFER_TOO_SMALL`、solver失敗は`FAILED`を返す。Managedでは既存`GameWorld.Check`が失敗を例外へ変換する。C++例外をManagedへ通さない。

Force/Impulseは有効なDynamic RigidBodyだけを対象とし、全成分の検査後に蓄積する。無効値で部分更新しない。solver更新失敗時の自動rollbackは保証せず、GameRuntimeの既存失敗経路が実行を停止する。

## Reloadと配布

Managed ReloadのScene交換ではsolverを破棄し、新しいScene値から再生成する。失敗時に旧Sceneへ戻る際も同じ境界を使う。Force/ImpulseはECS内の値であり、DLL固有stateを変更するサービスとは区別する。Native APIの世代管理とLive++の役割は変更しない。Jolt自体の版変更やEngineのComponent layout変更にはEngineの再ビルド・再起動が必要である。

Source Buildでは既存RuntimeとLevel.cppを直接含む旧テスト/一括Projectが同じtargetsをimportする。Debug/DevelopはJolt Debugと`/MTd`、ReleaseはJolt Releaseと`/MT`を対応させる。依存の生成を名前付きmutexで直列化し、同じ出力先の競合を防ぐ。GPU・上流samples/tests/viewerは構築対象にしない。

Runtimeのstatic archiveにはJolt libraryを統合し、Installed SDK利用者にJoltソースの再ビルドを要求しない構成とする。Managed sourceの配布対象にも`GamePhysics.cs`を追加した。SDK発行・最終リンク・Launcher配布の結果は1.1.0の配布記録へ分けて記載する。

## 追加した検証コードと残る制約

`Tests/GameServicesTests.cpp`の`PhysicsOperations`に、初回Raycast、無衝突、不正方向、無効世代、非有限Force、誤thread、Impulse消費、落下と床、Level保存/再読込、Scene交換、親付きbody拒否、無効化、Unload後の照会を追加した。既存のEditorTestsから呼び出し、exit 0を確認した。PhysicsMeshAndContactsではTriangle Mesh/Convex cooking、sleep中のStay、破棄後のExit、Sensor、購読Eventを検証する。

- root EntityのRigidBodyに限定する。親付きBodyはlocal/world変換を誤魔化さずエラーにする。剛体の子として通常の描画Entityを置くことは可能。
- Physics有効時のdeltaTimeは0〜1秒。60 Hz以下の長さへsubstep分割するが、固定時間accumulatorではない。
- 容量は16384 body。数値上限・寸法範囲はComponentと同期処理で制限する。
- `sensor`は接触応答を無効化する。接触snapshotと明示購読による既存Event通知に対応する。
- Constraint、Character Controller、レイヤー編集、1 Entity内の複数Colliderは未提供。これらを1.1.0の対応機能と表示しない。
- 今回のManaged Facadeは単発query/force。大量処理のバッチ化は計測後に検討する。
- 実MonoでCollider/Raycastと空contact snapshot、共通Input/Audioの呼出しを検証する。CCDの設定経路は確認したが、高速体の全条件での貫通防止や長時間性能は保証しない。

次の拡張も同じECS/Reflection/GameServicesを利用し、言語ごとのPhysics Worldを増やさない。

## 今回の変更ファイル

| ファイル | 変更 |
| --- | --- |
| `Runtime/Physics/PhysicsWorld.h` | Pimplと値結果、World所有権の契約 |
| `Runtime/Physics/PhysicsWorld.cpp` | Jolt RAII登録、同期、simulation、query、事前検証 |
| `Runtime/Physics/PhysicsUpdateSystem.h` | Levelのsolver slotを借用するScheduler Adapter |
| `Runtime/Physics/PhysicsUpdateSystem.cpp` | Physics段階、遅延生成、編集WorldのUpdate(0)分離 |
| `Runtime/Physics/GamePhysicsAccess.h` | Engine側protocol入口宣言 |
| `Runtime/Physics/GamePhysicsAccess.cpp` | DTO検査、Handle解決、force蓄積、例外変換 |
| `Runtime/Scene/Components/RigidBodyComponent.h` | 保存値、数値範囲、消費型force/impulse |
| `Runtime/Scene/SystemScheduler.h` | SimulationとTransformの間にPhysics段階を追加 |
| `Runtime/Level/Level.h` | solver所有slot、Raycast宣言、Scene交換の宣言 |
| `Runtime/Level/Level.cpp` | System登録、問い合わせ、交換/破棄前のsolver解放 |
| `Runtime/Level/LevelSerializer.cpp` | 読込成功時もExchangeScene経由で交換 |
| `Runtime/Gameplay/GameApiBridge.cpp` | 既存servicesの予約Physics commandとWorld lease接続 |
| `Runtime/Reflection/EngineGenerated/GeneratedReflection.h` | 生成器によるRigidBody traitsとheader追加 |
| `Runtime/Reflection/EngineGenerated/GeneratedReflection.cpp` | 生成器によるComponent/Inspector登録 |
| `Runtime/Reflection/EngineGenerated/ReflectionSchema.json` | 生成器による保存Property schema追加 |
| `Public/Nexus/GamePhysics.h` | C ABI、protocol version、layout assertion |
| `Public/Nexus/GamePhysics.hpp` | C++向けRaycast/AddForce/AddImpulse |
| `Managed/Nexus.Managed/GamePhysics.cs` | Managed DTO、PhysicsHit、GameWorldの型付きAPI |
| `Managed/Nexus.Managed/Entity.cs` | EntityからのForce/Impulse操作 |
| `Managed/Nexus.Managed/Nexus.Managed.csproj` | 明示Compile対象にPhysics source追加 |
| `Build/Nexus.Jolt.targets` | source追加、CRT整合、依存構築、archive/link設定 |
| `Tools/Engine/Build-Jolt.ps1` | 固定版CPU構成と依存ビルドの排他 |
| `Build/NexusRuntime.vcxproj` | 共通Physics targetsをimport |
| `NexusEngine.vcxproj` | 旧一括Projectにも同じtargetsをimport |
| `Tests/NexusEcsTests.vcxproj` | Levelを直接含む構成のリンク対象を整合 |
| `Tests/NexusEditorTests.vcxproj` | 同じBackend sourceと依存を利用 |
| `Tests/GameServicesTests.cpp` | Physicsの成功/失敗/保存/寿命の検証ソース |
| `Tools/Engine/Publish-Engine.ps1` | Managed Physics sourceをSDKへコピー |
| `Resources/Editor/Localization/ja.lang` | 共通Component名「剛体」 |
| `Resources/Editor/Localization/en.lang` | 共通Component名「Rigid Body」 |
| `Docs/GameApiReference.md` | 追加APIと未実行の状態を記載 |
| `Docs/GameplayCompletionPlan.md` | 接続済み範囲と残作業を更新 |
| `Docs/JoltPhysicsREADME.md` | 設計、使用方法、寿命、変更一覧、制約 |
| リポジトリ直下の`README.md` | Physics資料への案内 |

## 静的確認記録

- includeの実在、namespaceと宣言/定義、C++ facadeのcontext field順を確認した。
- 変更したMSBuild/Managed Project XMLをparseし、Level.cppを直接コンパイルする全4 Projectでtargets importを確認した。
- Engine Reflection生成器だけを実行し、18型の生成とRigidBodyのtraits/登録/schemaを確認した。
- Managedの明示Compile項目とSDKコピー対象、Build-JoltのPowerShell構文を確認した。
- ABIは要求40 byte・Raycast応答48 byte・接触64 byte・page header16 byte。C++ static_assert、Managed sizeofと実Mono呼出しで確認する。
- 所有権、解放順、Entity世代、Scene交換、所有thread検査、既存Level形式への影響をソース上で確認した。
- 追跡済み変更の`git diff --check`に空白エラーなし。生成済みReflectionの改行変換に関するGitの通知がある。
- DebugビルドとEditorTests/Mono統合は成功。Releaseとインストール結果は配布記録に記載する。
