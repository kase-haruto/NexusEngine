# 参照保存・時間・固定更新・Collider

## EntityとComponentの参照保存

C#の公開プロパティに値型の参照を指定します。

```csharp
[Expose] public EntityReference Target { get; set; }
[Expose] public ComponentReference TargetTransform { get; set; }

protected override void OnUpdate(float deltaTime)
{
    // 未設定・削除済み・別Levelの参照はnullになります。
    var target = Target.Resolve(World);
    if (target == null) return;

    var transform = TargetTransform.Resolve(World);
    if (transform != null)
        Debug.Log(transform.Get<Vector3>("translation"));
}
```

Detailsの選択欄からEntity／Componentを選べます。OutlinerのEntityをドロップすることもできます。Component参照へのドロップはTransformを初期候補にします。Noneで解除できます。存在しない対象はMissingと表示し、値は消しません。編集はUndo／Redoと複数選択に対応します。

コードからは`entity.Reference`、`component.Reference`で取得します。保存値はEntityの128bit永続ID、Componentではさらに安定Type IDです。実行中のHandleやNative pointer、CLR objectを保存しません。Level保存・再読込、Play用Clone、C#リロードで維持されます。`ToString()`／`Parse()`も利用できます。

Component参照は、そのEntityに現在存在する指定型のComponentを解決します。除去中はnull、同じ型を再追加した場合は現在のComponentを指します。同型の複数Script attachmentを個別に指す参照ではありません。Entityの複製やPrefabの配置では参照先のIDを自動で新Entityへ置き換えません。動的生成したEntityもLevelへ保存しなければ、ゲーム再起動後には存在しません。

## 時間と固定更新

`World.Time`（Behaviour内では`Time`）は値のスナップショットです。

| 値 | 意味 |
| --- | --- |
| DeltaTime | 現在の更新で経過したゲーム時間。固定callbackでは固定間隔 |
| UnscaledDeltaTime | TimeScaleを掛ける前のframe時間 |
| Elapsed / UnscaledElapsed | ゲーム時間／実時間の累計（秒） |
| FixedElapsed / FixedTick | 実行済み固定更新の時間／回数 |
| Frame | 実行済みframe数 |
| FixedDeltaTime | 固定更新の間隔。既定1/60秒 |
| InFixedUpdate | 固定callback内ならtrue |
| DroppedFixedTime | 追従上限を超えて捨てた固定更新時間 |

`World.TimeScale = 0`でゲーム時間と物理を停止できます。OnUpdateはdeltaTime=0で呼ばれ、実時間は進みます。0～100を指定できます。`World.FixedDeltaTime`は.001～1秒です。どちらも時間積算への変更は次frameから反映します。EditorのPauseではOnUpdate／固定更新／実時間タイマーも停止します。

```csharp
protected override void OnFixedUpdate(float fixedDeltaTime)
{
    // 物理計算の直前。1frame内で0回～複数回呼ばれます。
    Entity.AddForce(Vector3.UnitX * 10);
}
```

更新順はNative Moduleの通常Update、固定step群、通常OnUpdateです。各固定stepではSimulationのOnFixedUpdate→Physics→Transformを実行し、そのstepの接触通知を発行します。通常OnUpdateの後にもTransformを確定します。固定更新は最大64step/frameです。極端な遅延で無制限に追従せず、捨てた分はDroppedFixedTimeへ記録します。

C++の生成Behaviourにも`OnFixedUpdate(float)`を用意しました。EntityBehaviourSystemがcallbackの有無を検出し、既存のOnUpdateと振り分けます。従来のNative System／ModuleのUpdateは従来どおりframeごとです。手動SystemはDescriptor.reservedの`NEXUS_NATIVE_SYSTEM_FIXED_UPDATES`で固定stepにも参加し、GameTime protocolのinFixedStepで区別できます。新規Behaviourの登録後はBuildとHost再起動が必要です。

Playの新sessionは時計を0から開始します。C# Reloadでは時計を継続します。Levelファイルには時計の実行状態を保存しません。旧Bootstrapには新しいTick modeを送らず、固定更新への対応をfeature exportで確認します。低水準の`Level.Update`は既存のテスト／ツール用の可変更新契約を維持し、通常のゲーム実行は`GameRuntime.Update`が固定stepを管理します。

## タイマー

```csharp
protected override void OnCreate()
{
    // Behaviourの破棄・Reloadで自動取消。
    After(2, () => Debug.Log("2秒経過"));
    Every(1, () => Debug.Log("実時間で1秒"), unscaled: true);
}
```

World単位では`World.After`／`World.Every`を使い、戻り値のGameTimerを`Dispose()`すると取り消せます。ScriptBehaviourのAfter／Everyは所有資源に登録するため、終了時に自動で取り消します。無効化だけでは取り消しません。OnDisableで止めたい場合は戻り値を保持してDisposeしてください。

callbackは通常OnUpdate群の後、同じゲーム更新threadで実行します。0秒も次frame以降です。繰返しの遅延は一frame一回にまとめます。callback内の追加・取消に対応し、callbackが例外を投げるとそのtimerを取消してログへ記録します。最大4096件/Worldです。Task.RunやOS timerは使いません。callbackと残り時間は保存・Reloadへ引き継ぎません。OnCreateで必要なtimerを登録し直してください。C++は公開時計DTOを利用できますが、このdelegateタイマーはC#向けです。

## Colliderと衝突レイヤー

Collider自体は既存の実装を利用し、Detailsの形状選択を名前で表示するようにしました。

| 形状 | 用途 |
| --- | --- |
| Box | 箱。Half Extentは半寸法 |
| Sphere | 球。Radiusを指定 |
| Capsule | カプセル。Radiusと円筒部分のHalf Heightを指定 |
| Mesh | Triangle Mesh。Static／Kinematic専用 |
| Convex Mesh | 凸包。Dynamicにも利用可能 |

Mesh／Convex Meshは、形状とCollision Meshをどちらからでも設定できます。未指定のCollision Meshは編集中の状態として保存できますが、シミュレーション開始前にはモデルAssetの指定が必要です。DynamicのRigidBodyでは三角形Meshを選択できず、Convex Meshを使います。Collider単独はStatic、同じEntityにRigidBodyを追加するとその運動設定を使います。Colliderがある場合、形状・材質・所属LayerはCollider側の設定を優先し、RigidBody側の重複欄は非表示になります。RigidBodyだけの既存Entityにも所属Layerを設定できます。親付きRigidBodyなど、既存物理backendで未対応の配置は引き続き非対応です。

```csharp
var collider = GameCollider.Add(Entity, ColliderShape.Box);
collider.HalfExtent = new Vector3(.5f, 1, .5f);
collider.Layer = 1; // 0～31。0はDefault。
// 衝突相手はProject共通のPhysics設定で編集します。
collider.IsTrigger = false;

// 動く複雑な形状にはConvex Mesh。
// var collider = GameCollider.Add(Entity, ColliderShape.ConvexMesh, "Assets/Models/Body.glb");

if (World.Raycast(Entity.WorldPosition, -Vector3.UnitY, 10,
    CollisionLayers.Mask(0), out var hit))
{
    Debug.Log(hit.Distance);
}
```

DetailsのCollision Layerで所属レイヤーを選び、読み取り専用の衝突相手一覧を確認します。「衝突レイヤー設定を開く」または編集メニューの「Physics / 衝突レイヤー」から共通設定画面を開きます。名前付きの三角形マトリクスと選択レイヤーのチェック一覧は同じ設定を編集します。

衝突可否はProject共通のマトリクスだけで決まります。旧Collider／RigidBodyの`collisionMask`は保存互換のため保持しますが、接触判定には使用せず、Inspectorにも表示しません。共通設定は`Config/PhysicsLayers.settings`に自動保存し、すべてのレベルとPlay／単体Runtimeへ反映します。独自のConfigパスを持つProjectも対応します。詳細は[衝突レイヤーの設定](CollisionLayersREADME.md)を参照してください。

Level保存形式は`NEXUS_LEVEL 14`です。共通ルールをLevelファイルへ重複保存しません。旧v1～13は読込に対応し、Project設定がある場合は旧v13のLevel内マトリクスより優先します。新形式はv14未対応エンジンでは開けません。
Raycastのmaskは対象の所属Layerで絞り込みます。maskを省略すると全Layerです。C++の`NexusGame::Physics::Raycast`も最後の引数にmaskを指定できます。

## 検証

- EditorTests：参照のLevel保存／再読込、削除とslot再利用、32bit maskの保存、bilateral衝突filter、mask付きRaycast、時計の入力制約。衝突マトリクスのクリック／Undo／Redo／Play複製、bit31の衝突禁止と再許可、v14保存／v13読込、未設定Meshの編集順と実行拒否、未選択Colliderの既定描画も検証。
- 実CoreCLR：参照・Collider API、タイマー登録／取消、固定更新回数、TimeScale=0と復帰、Pause、Reload後の時計とcallback寿命。
- 生成した実C++ DLL：OnCreate／OnUpdate／OnFixedUpdate、空Behaviour、登録と停止。
- C#：CoreCLRとMono用net48をビルド。1.3.0配布時にはSDK同梱Mono上でも参照・時計・タイマー・固定更新・Reloadを実行検証済み（Release-1.3.0-Verification参照）。

## ビューポートでコライダーを表示する

Collider、または形状を内蔵したRigidBodyを持つEntityは、未選択でもビューポートへワイヤーフレームを表示します。右上の「表示」（目のアイコン）の詳細矢印から「コライダー」を開き、「非表示」「選択したコライダー」「すべてのコライダー」を切り替えられます。既定はすべてのコライダーです。既存のreinstancingスナップショットに表示モードがある場合は、その明示設定を維持します。

- 緑：通常の衝突形状
- 橙：Trigger（sensor）
- 灰：無効化された形状。「無効なコライダーも表示」で確認できます。

Box・Sphere・Capsuleに加え、MeshとConvex Meshを表示します。プリミティブはJoltと同じ寸法・scale規則で描き、Mesh／Convexは衝突用にcookingしたJolt形状を使います。モデルのnode transformとConvexの重心offsetにも対応します。物体の背後の線も表示する診断用overlayです。

Play中も表示でき、実行World内の移動・回転・scaleや動的生成に追従します。表示設定はPlay中も変更できます。表示だけでシミュレーションを進めたり、力・速度・接触通知・Level保存値を変更したりすることはありません。単体ゲームの画面には表示しません。

全体表示では選択対象を優先し、最大32,768本まで描画します。上限に達した場合や、衝突meshの取得失敗・不正な寸法・未対応の親付き物理Entityなどは、ビューポート下部へ表示します。対応していない形状を別の外枠で代用しません。表示モードはエディタのreinstancing時にも保持します。

## 足元の調査と剛体の減衰

C# の FixedUpdate などで、キャラクターの足元より少し上の World 座標を指定します。距離はレイ原点からの距離です。原点での距離0のhitは、形状内部からの誤接地を避けるため接地候補から外します。

```csharp
bool grounded = World.ProbeGround(feet + Vector3.UnitY * 0.08f, 0.16f, 45,
    out var ground, exclude: Entity) && ground.IsWalkable;
```

`ground.Hit` から Entity、接触位置、面法線、距離を、`ground.SlopeDegrees` から傾きを取得できます。上向きの急斜面を検出した場合は `ProbeGround` が true、`IsWalkable` が false になります。接地のための許容距離や角度はゲーム側で調整してください。1本のレイで調べるため、足元の一点が崖の外に出れば接地候補を失います。

C++ では同じフィルタ付きレイを使えます。歩行可能条件は面法線のY成分と傾斜上限の余弦を比較します。

```cpp
NexusPhysicsHitV1 ground{};
const auto status = physics.Raycast(feetProbeOrigin, {0, -1, 0}, 0.16f, ground,
    UINT32_MAX, false, selfHandle);
// cos(45 degrees)
const bool grounded = status == NEXUS_GAME_OK && ground.entity.world != 0 && ground.distance > 0 && ground.normal[1] >= 0.70710678f;
```

既存の Raycast 呼び出しは引き続き Trigger を含みます。追加引数を指定する場合は新しい Engine と SDK をそろえてください。除外 Entity は同じ World の生存ハンドルを渡します。Query のレイヤーマスクは Project の衝突マトリクスから独立しています。

Dynamic RigidBody の Details では `linearDamping`、`angularDamping`、`allowSleeping` を編集できます。減衰率の単位は 1/s、設定範囲は 0～100 です。C++／C# の共通 Component API でも同じプロパティを使います。旧 Level でこれらのフィールドが省略されている場合、減衰率はともに 0.05、自動休止は有効です。
