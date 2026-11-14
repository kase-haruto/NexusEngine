# NexusEngine ゲーム開発API一覧 — C++ / C#

参照保存・時計・タイマー・固定更新・Collider／衝突レイヤーの追加APIは[利用ガイド](GameReferencesTimePhysicsREADME.md)を参照してください。

更新日: 2026-11-14。現在の公開ヘッダーとManaged sourceを基準にした一覧。

対象はGame Project向けAPI。Engine / Renderer / Editor内部の全クラス一覧ではない。C++とC#は同じECS / Entity / Level / Componentを操作する。Game DLLからSceneやRendererの内部実装へ直接依存する構成は推奨APIに含めない。

現行ソースのC#数学補助、Entity行列・座標変換、重要度別ログ・例外・Assert・デバッグ描画は[数学・デバッグAPI](GameMathDebugREADME.md)を参照。公開済みSDKへ自動反映されるものではありません。

## 目次

### 1.1.xで追加したサービスAPI

| C# | C++ | 契約 |
|---|---|---|
| `World.ReadInput()` | `Input.Read(snapshot)` | VK keyのDown/Pressed/Released、mouse、wheel、focus。800 byte snapshot |
| `World.ReadGamepads()` | `Input.ReadGamepads(snapshot)` | 1.1.1追加。XInput 4 slot、button edge、stick/trigger、接続/切断。272 byte snapshot |
| `World.PlayAudio(path, volume, loop)` | `Audio.Play(...)` | Project Root内のPCM/float WAV、client所有Voice |
| `AudioVoice.Stop/Volume/IsPlaying/Dispose` | `Audio.Stop/SetVolume/IsPlaying` | session・owner・thread・tokenを検査 |
| `World.Raycast/AddForce/AddImpulse` | `Physics.Raycast/AddForce/AddImpulse` | 共通ECS/Jolt、世代付きEntity Handle |
| `World.GetPhysicsContacts(out dropped)` | `Physics.ReadContacts(...)` | 非消費page snapshot、Enter/Stay/Exit、Sensor |
| `World.EnablePhysicsContactEvents(bool)` | `Physics.EnableContactEvents(bool)` | 既存Event queueへ明示購読。毎frame消費が必要 |
| `World.AssetExists(path)` | `WorldControl.AssetExists(...)` | Root内のファイル存在検査。別Asset Managerは作らない |
| `World.RequestLevel(path)` | `WorldControl.RequestLevel(...)` | callback完了後に既存LevelSerializerで遷移。要求受理とロード成功は別 |
| `Entity.AddScriptInstance<T>()` | ScriptAttachments protocol operation 6 | 同型の別instanceを追加。AddScript<T>は既存返却の互換動作 |
| — | `NexusSaveNativeGameState/NexusRestoreNativeGameState` | 任意の対となるModule export。最大1 MiBの値とschema、復元失敗時は停止 |

詳細と使用例は[RuntimeServices](RuntimeServicesREADME.md)、[Physics](JoltPhysicsREADME.md)、[Native状態](NativeGameStateREADME.md)。

- [提供状況](#availability)
- [共通の寿命・Thread・エラー契約](#contracts)
- [C#：World・Entity・Transform](#managed-world)
- [C#：Component・Query・バッチ](#managed-components)
- [C#：Script・公開Property](#managed-script)
- [C#：Native API・Event](#managed-native)
- [C++：Component・Transform Facade](#cpp-facades)
- [C++：公開C ABI・Module・System登録](#cpp-abi)
- [主なComponent / Property ID](#component-ids)
- [Host / Interop実装者向け契約](#hosting)
- [使用例](#examples)
- [未提供の機能・関連資料・更新方法](#remaining)

<a id="availability"></a>
## 提供状況

| 表記 | 意味 |
|---|---|
| 既存 | 以前からsourceにあるAPI。全構成・全動作の検証済みを意味しない |
| 追加 | 1.1.0で追加。検証構成と制約はRelease-1.1.0.mdと各READMEを参照 |
| 未提供 | ゲーム向け公開APIとして利用可能と扱わない |

| 分野 | 状況 | 入口 |
|---|---|---|
| Entity生成・削除・列挙、Local Transform、Logger | 既存 | GameWorld / Entity / GameApi.h |
| C# Scriptの関連付け・Lifecycle・公開値 | 既存 | ScriptBehaviour / ScriptType / Expose |
| Native System登録と明示Binding、値Event | 既存 | NativeGameSystems / NativeGameBindings / GameServices |
| 登録済みComponentの追加・削除・Property取得変更 | 追加 | GameComponent / Components |
| Componentを条件にしたEntity検索 | 追加 | GameWorld.Query / Components.Query |
| 最大64件のTransform一括取得・更新 | 追加 | GameWorld.GetTransforms / SetTransforms / Transforms |
| Input / Audio / Physics / Asset / Level遷移 | 追加 | ReadInput、PlayAudio、Physics API、AssetExists、RequestLevel。全形式/全機能の包括対応ではない |
| Jolt RigidBody / Raycast / Force / Impulse | 追加 | 共通ECS・Reflection・C++/C# Facadeを接続。詳細は[Jolt Physics](JoltPhysicsREADME.md) |
| Native DLLの値Component宣言とNative Entity Logic配置 | 追加 | NativeComponentMetadata / EntityBehaviourSystem。任意C++ objectの直接登録は含まない |
| Native Systemの共有Property保存owner | 追加 | サンプルCrowdSettingsを唯一のEntityに配置し、共通Details/Levelから編集・保存 |

追加APIの利用にはEngine / Host / Nexus.Managed / Bootstrapを揃えて通常Buildし、再起動する。現在起動している既存Editorにsource変更が自動反映されたとは扱わない。実行記録は[Mono検証範囲](MonoRuntimeREADME.md#検証)と[従来のHybrid検証](HybridVerificationREADME.md)を参照する。

<a id="contracts"></a>
## 共通の寿命・Thread・エラー契約

- World / EntityはEngine所有。ゲームコードは寿命tokenとEntity世代を含むHandle、またはそれを包むEntityを使う。
- Engine Game APIの呼出しはWorldの所有Threadに限定する。Workerでの値計算と、所有ThreadでのWorld反映を分ける。
- Native Entity / Component / Systemの内部pointerをC#へ長期保持させない。文字列・配列・Transformは値で転送する。
- Stop / 別World / Entity削除後は古いHandleを拒否する。再PlayでHandleを引き継がない。
- Local Transformの取得値はコピー。変更しただけではWorldへ反映されず、setterか一括更新が必要。
- C++境界はWindows x64 / pack 8 / Cdecl。STL所有物やC++例外をC ABIへ渡さない。Native stateは確保したDLLで破棄する。
- C#の通常Game APIは失敗statusを主にInvalidOperationExceptionへ変換する。引数不正にはArgumentException系、停止済みsessionにはObjectDisposedExceptionを返す場合がある。
- Native Bindingの失敗はNativeApiException.Statusで判定できる。エラーを無条件に再送しない。

### Native status

定義: [GameApi.h](../Public/Nexus/GameApi.h)、[NativeGameBindings.h](../Public/Nexus/NativeGameBindings.h)。

| 値 | 定数 | 意味 |
|---|---|---|
| 0 | NEXUS_GAME_OK | 成功 |
| 1 | NEXUS_GAME_INVALID_ARGUMENT | 引数・値・buffer・操作が不正 |
| 2 | NEXUS_GAME_INVALID_HANDLE | World / Entity / client / 対象等が無効・不在 |
| 3 | NEXUS_GAME_WRONG_THREAD | World所有Thread外の呼出し |
| 4 | NEXUS_GAME_BUFFER_TOO_SMALL | 容量不足。対応する読取APIで必要数を確認する |
| 5 | NEXUS_GAME_FAILED | callback失敗・再入・更新境界違反等 |
| 6 | NEXUS_GAME_INCOMPATIBLE_ABI | ABI / Version / 署名不一致 |
| 7 | NEXUS_GAME_SYSTEM_NOT_REGISTERED | Native System未登録 |
| 8 | NEXUS_GAME_FUNCTION_NOT_REGISTERED | Native Function未登録 |
| 9 | NEXUS_GAME_BINDING_DISABLED | Bindingが一時停止中 |
| 10 | NEXUS_GAME_STALE_BINDING | callback実行前に検出した旧Binding。専用Facadeが一度だけ再解決・再送できる |

<a id="managed-world"></a>
## C#：World・Entity・Transform

Namespaceは`Nexus.Managed`。主なsource: [GameWorld.cs](../Managed/Nexus.Managed/GameWorld.cs)、[Entity.cs](../Managed/Nexus.Managed/Entity.cs)。

### GameWorld

通常はScriptBehaviour.Worldから取得する。ゲーム側で直接newするAPIは公開していない。

| API | 戻り値 | 用途・注意 |
|---|---|---|
| CreateEntity(string name = "Entity") | Entity | 永続IDと標準Componentを持つEntityを既存Levelへ生成 |
| Entities() | Entity[] | 生存EntityのHandleを一括取得 |
| Log(string message) | void | 共通LoggerのGameカテゴリへ通知 |

### Entity

| API | 型 / 戻り値 | 用途・注意 |
|---|---|---|
| World | GameWorld | 所属Worldのcapability |
| IsAlive | bool | Entityの生存確認。session終了後もfalseを返せる |
| HasTransform | bool | Transformの存在確認 |
| TryGetTransform(out LocalTransform value) | bool | Entity / Transform不在ならfalse。その他の失敗は例外 |
| Transform | LocalTransform、get / set | Local値の一括コピー・反映 |
| HasScript | bool | ScriptComponentの存在確認 |
| ScriptEnabled | bool、get / set | 旧主Scriptだけの有効化。各ScriptはGameScript.Enabledで操作 |
| SetScript<T>(bool enabled = true) | void | T : ScriptBehaviour。旧主Scriptだけを設定・置換。追加Scriptは維持 |
| AddScript<T>(bool enabled = true) | GameScript | 別Typeを追加。同じ正規IDがあれば既存関連付けを返す。追加 |
| GetScripts() | GameScript[] | 主Script→追加順で現在の関連付けwrapperを取得。追加 |
| RemoveScript() | void | 従来互換として全Scriptを解除。個別解除はGameScript.Remove |
| Destroy() | void | Entityを破棄 |
| Equals(Entity?) / Equals(object?) | bool | World / index / generationによる同一性比較 |
| GetHashCode() / == / != | int / bool | Handleに基づく比較。wrapperの有効な操作権限とは別 |

### LocalTransform

| メンバ | 型 | 意味 |
|---|---|---|
| Translation | System.Numerics.Vector3 | Local位置 |
| Rotation | System.Numerics.Quaternion | Local回転 |
| Scale | System.Numerics.Vector3 | Local拡縮 |
| Identity | static LocalTransform | 原点・単位Quaternion・Scale=1 |

取得後に値を変更し、`Entity.Transform = value`で反映する。Native Componentへの参照ではない。

<a id="managed-components"></a>
## C#：Component・Query・バッチ

**追加。** Source: [GameComponents.cs](../Managed/Nexus.Managed/GameComponents.cs)、[Entity.cs](../Managed/Nexus.Managed/Entity.cs)。詳細は[共通Component API](GameComponentApiREADME.md)。

### Entityの追加API

| API | 戻り値 | 用途・注意 |
|---|---|---|
| HasComponent(string typeId) | bool | 登録済みの公開Type IDで存在確認 |
| GetComponent(string typeId) | GameComponent | 現在のComponentを参照するproxy。不在なら例外 |
| TryGetComponent(string typeId, out GameComponent? component) | bool | 登録済みだがEntityに不在ならfalse。未知Type IDや無効Entityは例外 |
| AddComponent(string typeId) | GameComponent | 共通Reflectionの既定値で追加。すでに存在すればそのComponentを参照 |
| RemoveComponent(string typeId) | void | 既存Storageから削除。必須型・Scriptには制約あり |

### GameComponent

| API | 戻り値 | 用途・注意 |
|---|---|---|
| TypeId | string | Reflectionの安定型ID |
| Entity | Entity | 所属Entity |
| Get<T>(string propertyId) | T | 公開Propertyの値コピー |
| Set<T>(string propertyId, T input) | void | Detailsと同じ検証付きsetterへ反映 |

Getの対応型はbool / int / float / string / Vector2 / Vector3 / Vector4 / int32を基底型とするenum。Propertyのkindに対応する型を指定する。Quaternionそのものの汎用変換はないため、回転はLocalTransformまたはVector4表現を使う。

proxyはComponent instanceを所有しない。削除中のアクセスは失敗し、同じEntityへ再追加した後は現在のComponentへ解決する。

### GameWorldの追加API

| API | 戻り値 | 用途・注意 |
|---|---|---|
| Query(string componentTypeId) | IReadOnlyList<Entity> | 一つのComponentを条件に生存Entityを検索。所有Threadで全ページを回収 |
| GetTransforms(params Entity[] entities) | LocalTransform[] | 最大64件を指定順に一括取得 |
| SetTransforms(Entity[] entities, LocalTransform[] values) | void | 同じ要素数・最大64件。全件検証後に反映し、不正要素では部分更新しない |

Play Worldへの更新であり、EditorのUndo履歴や編集Levelへの自動保存は作らない。Name / Transform / Hierarchy / PersistentIdの追加・削除は禁止。Scriptは存在確認・Queryだけを汎用窓口で許可し、編集は専用Script APIへ委譲する。

<a id="managed-script"></a>
## C#：Script・公開Property

Source: [ScriptBehaviour.cs](../Managed/Nexus.Managed/ScriptBehaviour.cs)、[ExposeAttribute.cs](../Managed/Nexus.Managed/ExposeAttribute.cs)。Entityごとに異なるTypeのScriptを最大32件扱うソース実装を追加した。実行未検証。詳細は[複数Script](MultipleScriptsREADME.md)。同じ正規Typeの複数instanceは対象外。

GameScriptはEntity / TypeId / IsAttached / Enabled / Remove()を提供する。EnabledとRemoveは自身の関連付けtokenを検査する。Entity.ScriptEnabledは旧主Scriptだけ、引数なしのEntity.RemoveScript()は従来互換の全解除。公開Propertyと有効状態は共通Metadata/Detailsで編集し、有効状態のProperty ID `@enabled`は予約する。

### ScriptBehaviour

| メンバ | 可視性 | 用途 |
|---|---|---|
| Entity | public | 関連付けられた共通Entity。未関連付け時は例外 |
| World | public | Entity.World |
| Enabled | public get / set | Entity.ScriptEnabled |
| Lifetime | protected CancellationToken | Stop / Reloadでcancel。自前workerの終了確認はゲーム側で行う |
| TrackResource<T>(T resource) where T : IDisposable | protected T | event購読等の解除を登録。所有Thread限定、終了時に逆順Dispose |
| OnCreate() | protected virtual | 関連付けごとの初期化。無効状態でも呼ばれる |
| OnEnable() | protected virtual | Create成功後の有効化 |
| OnUpdate(float deltaTime) | protected virtual | 有効frameの更新 |
| OnDisable() | protected virtual | 有効だったbehaviourの無効化・停止 |
| OnDestroy() | protected virtual | 関連付け終了。自前worker / event等を終了する |

LifecycleはRuntimeが呼ぶ。ゲーム側から直接Create / Update等を呼ぶAPIは公開していない。通常はCreate → Enable → Updateの繰り返し → Disable → Destroy。Entityがすでに破棄されている場合のcleanupでは、そのEntityのPropertyへアクセスしない。

### Attribute

| Attribute / 設定 | 意味 |
|---|---|
| [ScriptType("game.player")] | class名から独立した保存用Type ID |
| ScriptType.Version | 既定1。保存値との互換性番号 |
| ScriptType.Aliases | 過去のType ID。FullNameも互換名として扱う |
| [Expose] | public Propertyを共通Details・Level保存へ公開。field用Attributeではない |
| Expose.Id | 保存用Property ID。省略時はProperty名 |
| Expose.Label | 表示名。IDとは独立 |
| Expose.Aliases | 過去のProperty ID。暗黙の型変換を保証しない |

Expose対応型はbool / int / float / string / Vector2・3・4。Native Bindingの型一覧とは異なる。publicなget / setが必要で、static / indexer / init-only / 未対応型などは拒否する。constructor / 公開getterはMetadata生成時にも実行されるため、World非依存で副作用のない実装にする。

<a id="managed-native"></a>
## C#：Native API・Event

### 型付きNative System呼出し

Source: [NativeBindings.cs](../Managed/Nexus.Managed/NativeBindings.cs)。System登録とAPI公開は別。明示公開された機能だけを呼べる。

| API | 戻り値 | 用途 |
|---|---|---|
| GameWorld.BindNativeFunction(string module, string system, uint systemVersion, string function, uint functionVersion, NativeType result, params NativeType[] parameters) | NativeFunction | Module / System / Function / Version / 署名を照合して解決 |
| NativeFunction.Invoke(params NativeValue[] arguments) | NativeValue | 型とHandleの世代を確認して実行 |
| NativeFunction.Refresh() | void | 同じIDと署名で明示的に再解決 |
| NativeApiException.Status | int | Native失敗status |
| NativeValue.Type | NativeType | 値の境界型 |
| NativeValue.As<T>() | T | 対応する戻り値を取り出す。別型では例外 |

NativeFunctionはGame DLLのfunction pointerを保持しない。旧BindingはEngineで検出し、status 10の場合だけcallback実行前に一度再解決・再送する。通常のNative実行失敗を再送しない。

| 型定義・値生成API | 内容 |
|---|---|
| NativeType(NativeValueKind kind, string? enumId = null) | 署名の型。Enumには安定IDを指定 |
| NativeType.Void / Entity / Vector3 | よく使う型のstaticプロパティ |
| NativeValue.From(bool / int / uint / float) | スカラー入力 |
| NativeValue.From(Vector2 / Vector3 / Vector4) | Vector入力 |
| NativeValue.From(string / Entity / float[]) | UTF-8 / Handle / float32配列入力 |
| NativeValue.Enum(string typeId, int value) | Enum ID付き入力 |

NativeValueKindはVoid / Bool / Int32 / UInt32 / Float32 / Vector2 / Vector3 / Vector4 / Utf8 / Entity / Enum32 / Float32Array。引数は最大16件。文字列・配列は境界bufferの4096 byte制限に従う。任意object・pointer・任意型配列は渡せない。

### 値Command / Event

Source: [GameServices.cs](../Managed/Nexus.Managed/GameServices.cs)。型付きSystem Bindingと別の既存窓口。

| API | 戻り値 | 用途・注意 |
|---|---|---|
| InvokeNative(string id, uint version, Entity? target, ReadOnlySpan<byte> request) | byte[] | 登録済みCommandを一回実行。request / responseは各4096 byte以下 |
| PublishEvent(string type, uint version, Entity? source, ReadOnlySpan<byte> payload) | void | 同じWorldの他clientへ値通知 |
| TryPollEvent(out GameEvent? value) | bool | queueから一件取得。空ならfalse |
| GameEvent | record | Sequence / Type / Version / Source / Payload |

各clientのEvent queueは最大256件。Managed側からNative Commandのcallback登録はできない。EventのSourceはすでに破棄済みの場合があるので、利用時に生存確認する。queueや購読状態をReload後へ自動保存する契約ではない。

<a id="cpp-facades"></a>
## C++：Component・Transform Facade

**追加。** Namespaceは`NexusGame`。Facadeは既存services contextを借用し、clientの開閉・Systemの所有は行わない。

Source: [GameComponents.hpp](../Public/Nexus/GameComponents.hpp)、[GameTransforms.hpp](../Public/Nexus/GameTransforms.hpp)。

| クラス / API | 戻り値 | 用途 |
|---|---|---|
| Components(const NexusGameServicesContextV1&) | constructor | 接続済みcontextを借用 |
| Components.Has(entity, typeId, bool& present) | NexusGameStatus | 存在確認 |
| Components.Add(entity, typeId) | NexusGameStatus | 既定値で追加 |
| Components.Remove(entity, typeId) | NexusGameStatus | 削除 |
| Components.Read(entity, typeId, propertyId, value, text = nullptr, capacity = 0, attachment = 0) | NexusGameStatus | NexusComponentValueV1と任意のUTF-8 bufferへコピー。非0 tokenは関連付け世代を検査 |
| Components.Write(entity, typeId, propertyId, value, text = {}, attachment = 0) | NexusGameStatus | 共通Propertyへ書込。非0 tokenは関連付け世代を検査 |
| Components.Attachment(entity, typeId, token) | NexusGameStatus | 値Componentの実行世代を取得。必要時にEngineが生成し、削除・再追加で変更 |
| Components.Query(typeId, offset, output, capacity, page) | NexusGameStatus | 最大255件のページ。offsetは0または前回nextOffset、UINT32_MAXは終端 |
| Transforms(const NexusGameServicesContextV1&) | constructor | 接続済みcontextを借用 |
| Transforms.Read(const NexusEntityHandle*, count, NexusLocalTransform*) | NexusGameStatus | 最大64件の一括取得 |
| Transforms.Write(const NexusTransformUpdateV1*, count) | NexusGameStatus | 最大64件を全件検証後に更新 |

Native Moduleの任意export `NexusConnectGameServices` でcontextを受け取る。Module.create / System.create時点ではまだ接続されていない。Systemへは接続後に既存access visitor等から明示的に渡す。すべてのNativeテンプレートへ自動注入済みという意味ではない。

`NexusGame::EntityBehaviourContext`のEntity / Read / Write / GetTransform / SetTransformは、この関連付け世代の検査を包む。`EntityBehaviourSystem<T>::Descriptor`を既存Native Systemへ登録し、services接続時に`Connect`することで、EntityごとのOnCreate / OnEnable / OnUpdate / OnDisable / OnDestroyを実行する。共通のenabled:Boolが必要。詳細・所有権・終了失敗時の扱いは[Native Entity Component](NativeEntityComponentsREADME.md)。

### Component protocolの値

定義: [GameComponents.h](../Public/Nexus/GameComponents.h)。

| DTO / 種別 | 内容 |
|---|---|
| NexusComponentValueV1 | kind / stringLength / integer / reserved / number[4]、32 byte |
| NexusComponentRequestV1 | operation / typeLength / propertyLength / offset / limit / reserved / value、56 byte |
| NexusComponentQueryV1 | count / nextOffset、8 byte |
| NexusTransformBatchV1 | operation / count、8 byte。1=Read、2=Write |
| NexusTransformUpdateV1 | EntityHandle + LocalTransform、56 byte |
| Component操作 | HAS / ADD / REMOVE / READ / WRITE / QUERY |
| Component値kind | BOOL / INT / FLOAT / STRING / VECTOR2 / VECTOR3 / VECTOR4 / ENUM |

Type / Property IDは各512 byte、文字列値は3072 byteまで。合計request / responseは4096 byte以下。未使用fieldは0。予約Command IDは`nexus.engine.components`と`nexus.engine.transforms`、versionは1。`nexus.engine.`の登録によるEngine APIの上書きは禁止する。

<a id="cpp-abi"></a>
## C++：公開C ABI・Module・System登録

### 基本World API

定義: [GameApi.h](../Public/Nexus/GameApi.h)。Module.createの`NexusGameModuleContextV1.api`、System.createの`NexusNativeSystemContextV1.game`から利用する。

| NexusGameApiV1 | 用途 |
|---|---|
| createEntity(world, name, output) | Entity生成 |
| destroyEntity(entity) | Entity破棄 |
| isEntityAlive(entity) | World / index / generationを含む生存確認 |
| entities(world, output, capacity, count) | 件数照会・一括列挙。容量不足では部分コピーしない |
| getLocalTransform(entity, output) | 現在のLocal値コピー |
| setLocalTransform(entity, value) | 有限値とQuaternionを検証して反映 |
| log(world, message) | 共通Game Logger |

| 基本DTO | 内容 |
|---|---|
| NexusGameString | data + length。UTF-8 byte数、終端NUL不要 |
| NexusEntityHandle | world + index + generation。16 byte |
| NexusLocalTransform | translation[3] + rotation[4] + scale[3]。40 byte |
| NexusGameBytes | data + length。一時借用するbyte列 |

### Game Module

任意export `NexusGetGameComponentMetadata(uint32_t abi, NexusGameString* output)`はゲーム開始前に値Metadataを宣言する。ABI1、UTF-8 JSON最大1 MiB、返却文字列はDLL解放まで有効。Engineがコピーし、共通Reflectionへ登録する。ゲームobjectを生成せず、宣言省略は従来DLLと互換。サンプルは[GameComponents.h.in](../Templates/NativeModule/GameComponents.h.in)。

定義: [GameModule.h](../Public/Nexus/GameModule.h)。

| 契約 | 用途 |
|---|---|
| NexusGetGameModule(requestedAbi, output) | 必須export。NexusGameModuleV1の識別情報とcallbackを返す |
| module.create(context, state) | DLL内stateを生成 |
| module.update(state, deltaTime) | World Simulation前のModule更新 |
| module.destroy(state) | 自分のcallback / workerを停止し、DLL内でstateを解放 |

Module LifecycleはEntity単位のBehaviour Lifecycleではない。Engine / Project / Moduleの識別とABIを検証してから起動する。詳細は[Game Module設計](GameModulesREADME.md)。

### Native System登録と公開

定義: [NativeGameSystems.h](../Public/Nexus/NativeGameSystems.h)、[NativeGameSystemApi.h](../Public/Nexus/NativeGameSystemApi.h)、[NativeGameBindings.h](../Public/Nexus/NativeGameBindings.h)。

| 関数表 / export | メンバ / 用途 |
|---|---|
| NexusRegisterGameSystems | V1の任意export。System登録と公開署名宣言 |
| NexusRegisterGameSystemsV2 | V2の任意export。上記にBinding登録を追加 |
| NexusNativeSystemsApiV1.add | scopeへSystem descriptorを登録 |
| NexusNativeSystemsApiV1.find | System ID / Versionから数値Handleを解決 |
| NexusNativeSystemsApiV1.access | Native限定visitor内で一時借用。System pointerをcallback外へ保存しない |
| NexusNativeSystemPublicApiV1.declareFunction | Systemと別に関数ID / Version / 引数 / 戻り値を宣言 |
| NexusNativeSystemPublicApiV1.findFunction | 宣言済み関数Metadataの照会 |
| NexusNativeBindingRegistrationV1.bind | 宣言済み関数へ明示callbackを接続 |
| NexusNativeBindingRegistrationV1.setEnabled | idle Scheduler上で一時停止・再開 |
| NexusNativeBindingApiV1.resolve | World / Module / System / Function / Version / 署名を照合 |
| NexusNativeBindingApiV1.invoke | 数値Binding世代を検査して実行 |

System descriptorはid / version / after / reads / writes / create / update / destroyを持つ。afterは同Module内の先に登録したSystemへの依存。登録しただけでクラス全体をC#へ公開しない。現Schedulerは逐次実行で、reads / writesの宣言が自動並列化を意味するものではない。

### 値Command / Event

定義: [GameServices.h](../Public/Nexus/GameServices.h)。

| NexusGameServicesApiV1 | 用途 |
|---|---|
| open(world, client) | 登録権限を持たないclientを作成 |
| close(client) | callback登録とqueueを失効。実行中の解除は拒否 |
| registerCommand(client, id, version, callback, state) | Loaderが発行したNative権限付きclientで登録 |
| invoke(client, id, version, target, request, response, capacity, length) | 一回の呼出し。応答長照会で副作用を二重実行しない |
| publish(client, type, version, source, payload) | 同Worldの他clientへ配送 |
| poll(client, header, buffer, capacity, length) | 一件取得。容量不足ではqueueを進めない |

`NexusConnectGameServices(state, context)`は任意export。Native登録権限は公開openでは付与されず、Engine Loaderがcontextを通じて渡す。

<a id="component-ids"></a>
## 主なComponent / Property ID

**追加APIで使うIDの例。** C++ class名・翻訳表示名と同一とは限らない。実際の公開集合は、ホストのTypeRegistry登録・生成ReflectionとLevelのComponentReflectionの双方に存在する型から決まる。下表は主要Propertyであり、生成器による補完を含む全Property一覧ではない。

| Type ID | 主なProperty ID | 値の型 / 注意 |
|---|---|---|
| Name | name | string。追加・削除不可 |
| Transform | translation / scale / rotation | Vector3 / Vector3 / Vector4。追加・削除不可 |
| Camera | fov / near / far / frustumSize / enabled / primary | float / enum / bool等。専用setterの範囲・一意性検査あり |
| PrimitiveRender | shape / meshAsset / nodeIndex / baseColorTexture / enabled / staticLighting / tint | enum / Asset path文字列 / int / bool / Color等 |
| PrimitiveRender | materialEnabled / materialSlot / materialBaseColor / materialMetallic / materialRoughness / materialNormalScale / materialEmissive / materialOcclusion | material overrideの公開値 |
| AnimationPlayer | animationIndex / playbackSpeed / loop / playing / time | int / float / bool。clip indexは割当Model内の番号 |
| DirectionalLight | intensity / color / direction / enabled | float / Vector3 / bool |
| SkyRenderer | intensity / enabled | float / bool |
| Fog | enabled / startDistance / endDistance / color | bool / float / Color。endDistanceはstartDistanceより大きい |
| Script | 存在確認・Queryのみ | 内容の変更は専用Script API |

基準source: [TypeRegistry.cpp](../Runtime/Reflection/TypeRegistry.cpp)、[ReflectedInspector.h](../Runtime/Reflection/ReflectedInspector.h)、[生成登録](../Runtime/Reflection/EngineGenerated/GeneratedReflection.cpp)。Color等は共通Descriptorの表示値を使用し、内部GPU表現を直接操作するAPIではない。Asset pathの編集はAsset Manager全体の公開を意味しない。

<a id="hosting"></a>
## Host / Interop実装者向け契約

以下は通常のゲームScriptから直接呼ぶものではなく、Runtime / Loader / Bridge用の公開境界。

| ヘッダー | 契約 |
|---|---|
| [ScriptApi.h](../Public/Nexus/ScriptApi.h) | NexusScriptApiV1.setScript / removeScript / getState / setEnabled。ScriptBindingはEntity / Type / attachment / instance / enabledを値で伝える |
| [ScriptPropertyApi.h](../Public/Nexus/ScriptPropertyApi.h) | read / write。Entity世代とattachmentを検査して公開値の保存状態をコピー |
| [ManagedBootstrap.h](../Public/Nexus/ManagedBootstrap.h) | Bootstrap / Gameplay context V1～V5とstart / stop / tick / reloadの関数型 |
| [NativeGamePatch.h](../Public/Nexus/NativeGamePatch.h) | 固定stateのNative patch契約、Binding再登録、SDK通知observer |

### Managed開始契約

| context | entry | 内容 |
|---|---|---|
| V1 | Start | World tokenとLogger、Bootstrapのみ |
| V2 | StartGameplay | 共通Game / Script API、Game Assembly |
| V3 | StartReflectedGameplay | 公開Property状態のcopy APIを追加 |
| V4 | StartHybridGameplay | Native Command / 値Eventを追加 |
| V5 | StartBoundGameplay | 明示Native System Bindingを追加。現在のGameRuntimeの標準経路 |

TickはScriptBindingを一括同期し、StopはManaged参照を解除する。従来Reload契約のoperationは0=Prepare、1=Commit、2=Abort、3=Complete。MonoではNative側が子Domainの差替えを統括し、同じReload段階へ接続する。CoreCLR用Reload entryをMonoへそのまま呼ぶ構成ではない。

`Nexus.Managed.Interop`のNativeString / EntityHandle / ScriptState / ScriptBinding / NativeBytes / NativeGameEvent / NativeGameApi / NativeScriptApi / NativeScriptPropertyApi / NativeGameServicesApiと、NativeBindings.cs内のNativeCallValue / NativeBindingHandle / NativeApiType / NativeApiParameter / NativeFunctionDescriptor / NativeBindingApiは境界DTO・関数表。public宣言でも通常のゲーム向け操作入口にはせず、前述のGameWorld / Entity / NativeFunctionを使う。MonoInteropのdelegate群も同じ位置付け。

### Native patch export

| export | 内容 |
|---|---|
| NexusGetNativePatchContract | stateSchema / moduleSize / moduleAlignment / systemsLayout等の固定契約 |
| NexusRebindGameSystems | 既存Systemをfindし、Bindingだけを再登録 |
| NexusSetNativePatchObserver | BackendからSDK hookへの非所有通知先を設定。nullで解除 |

observer通知はPRE / POST / COMPILE_ERROR / LINK_ERROR / SOURCE。任意のlayout移行やDLL再ロードのAPIではない。Live++専用profile・SDK接続が必要で、通常Standard Buildでは自動的に利用可能にならない。詳細は[Live++ Backend](NativeModuleLivePPREADME.md)。

<a id="examples"></a>
## 使用例

### 既存APIだけを使うC#の移動

```csharp
using Nexus.Managed;

[ScriptType("game.mover", Version = 1)]
public sealed class Mover : ScriptBehaviour
{
    [Expose(Id = "moveSpeed", Label = "Move Speed")]
    public float MoveSpeed { get; set; } = 5.0f;

    protected override void OnUpdate(float deltaTime)
    {
        var transform = Entity.Transform;
        transform.Translation.X += MoveSpeed * deltaTime;
        Entity.Transform = transform;
    }
}
```

Buildで型を登録後、共通EditorでScriptをアタッチする。詳細は[Source操作](SourceContentBrowserREADME.md)。

### 追加Component APIを使うC#（実行未検証）

```csharp
// ScriptBehaviourのOnCreate等、Worldが有効な所有Thread上で使用する。
var fog = Entity.AddComponent("Fog");
fog.Set("startDistance", 3.0f);
float start = fog.Get<float>("startDistance");

var candidates = World.Query("Transform");
var transforms = World.GetTransforms(Entity); // 最大64件
transforms[0].Translation.X += 1;
World.SetTransforms(new[] { Entity }, transforms);
```

### C#から明示公開されたNative Systemを呼ぶ

```csharp
var setTarget = World.BindNativeFunction(
    "NativeCombat", "CrowdSimulation", 1, "SetTarget", 1,
    NativeType.Void, NativeType.Entity, NativeType.Vector3);

_ = setTarget.Invoke(
    NativeValue.From(Entity),
    NativeValue.From(new System.Numerics.Vector3(5, 0, 0)));
```

Module / Systemが登録済みであることが前提。Game Projectの型付きFacadeへこの処理を包む例は[Hybrid/CrowdSimulation.cs.in](../Templates/Hybrid/CrowdSimulation.cs.in)。

### 追加Component APIを使うC++（実行未検証）

```cpp
#include "Public/Nexus/GameComponents.hpp"

NexusGameStatus ConfigureFog(
    const NexusGameServicesContextV1& context, NexusEntityHandle entity) noexcept {
    NexusGame::Components components(context);
    auto status = components.Add(entity, {"Fog", 3});
    if(status != NEXUS_GAME_OK) return status;
    NexusComponentValueV1 value{};
    value.kind = NEXUS_COMPONENT_FLOAT;
    value.number[0] = 3.0f;
    return components.Write(entity, {"Fog", 3}, {"startDistance", 13}, value);
}
```

<a id="remaining"></a>
## 未提供の機能・関連資料・更新方法

Native DLLの値Component宣言、共通Editorへの配置・保存、既存SchedulerによるC++ Entity Lifecycleを追加した。詳細は[Native Entity Component](NativeEntityComponentsREADME.md)。Debug EditorTestsとMono/CoreCLR統合の検証範囲は[1.1.0](Release-1.1.0.md)を参照。

Native System共有PropertyはCrowdSettingsをEntityに明示配置するサンプルを追加した。任意Systemの保存ownerを自動定義するものではない。C#の複数Scriptアタッチ・個別解除・共通保存は[追加実装](MultipleScriptsREADME.md)を参照。Input snapshot / WAV Audio / Jolt基本Physics / AssetExists / Level切替を提供する。gamepadのaction mapping/振動、3D Audio、Constraint/Character Controller、汎用Native Extension、任意C++ object型の自動Binding / Serializationは未提供。Native stateは明示的な保存/復元契約で、pointer/GPU資源の自動移行は保証しない。

| 資料 | 内容 |
|---|---|
| [共通Component API](GameComponentApiREADME.md) | 新APIの設計、ABI、所有権、制限、変更一覧と静的確認 |
| [Native Entity Component](NativeEntityComponentsREADME.md) | DLL Metadata・値Storage・Editor配置・Entity Lifecycle・生成サンプル |
| [Mono Runtime](MonoRuntimeREADME.md) | Hosting、net48、Domain Reload、配布、検証範囲 |
| [Native Game Systems](NativeGameSystemsREADME.md) | 登録・実行・破棄とScheduler |
| [Native / Managed API](NativeManagedApiREADME.md) | 明示Bindingと値型契約 |
| [共通Reflection](GameLogicReflectionREADME.md) | Details・公開Property・保存 |
| [Hybrid Project](HybridProjectREADME.md) | Module追加、Build、Package |
| [Source操作](SourceContentBrowserREADME.md) | アタッチ、開く、解除 |
| [Jolt Physics](JoltPhysicsREADME.md) | 共通RigidBody、C++/C# Raycast/Force/Impulse、ABI、所有権と未検証事項 |

APIを変更したら、宣言元・実装・SDKコピー対象と本書を合わせて更新する。「既存」「追加」「未提供」を実行記録と混同しない。境界APIの変更ではABI番号・structSize・旧Projectの互換性も確認する。本書の作成ではsourceとリンクの静的確認だけを行い、Build / ゲーム / テストは実行していない。
