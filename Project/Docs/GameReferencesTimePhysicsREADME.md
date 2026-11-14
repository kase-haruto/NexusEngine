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

Mesh／Convex Meshは、先にCollision MeshへモデルAssetを指定してから形状を選びます。Collider単独はStatic、同じEntityにRigidBodyを追加するとその運動設定を使います。Colliderがある場合、形状・材質・Layer／MaskはCollider側の設定を優先します。RigidBodyだけの既存EntityにもLayer／Maskを設定できます。親付きRigidBodyなど、既存物理backendで未対応の配置は引き続き非対応です。

```csharp
var collider = GameCollider.Add(Entity, ColliderShape.Box);
collider.HalfExtent = new Vector3(.5f, 1, .5f);
collider.Layer = 1; // 0～31。0はDefault。
collider.CollisionMask = CollisionLayers.Mask(0, 2);
collider.IsTrigger = false;

// 動く複雑な形状にはConvex Mesh。
// var collider = GameCollider.Add(Entity, ColliderShape.ConvexMesh, "Assets/Models/Body.glb");

if (World.Raycast(Entity.WorldPosition, -Vector3.UnitY, 10,
    CollisionLayers.Mask(0), out var hit))
{
    Debug.Log(hit.Distance);
}
```

DetailsのCollision Layerで所属、Collides Withで相手を選択します。32レイヤーで、両方のmaskが互いの所属を許可した場合だけ衝突します。既定はLayer 0／Everythingなので既存Levelの挙動を維持します。Triggerにも同じfilterを適用します。Static同士の接触は従来どおり生成しません。

Raycastのmaskは対象の所属Layerで絞り込みます。maskを省略すると全Layerです。C++の`NexusGame::Physics::Raycast`も最後の引数にmaskを指定できます。

## 検証

- EditorTests：参照のLevel保存／再読込、削除とslot再利用、32bit maskの保存、bilateral衝突filter、mask付きRaycast、時計の入力制約。
- 実CoreCLR：参照・Collider API、タイマー登録／取消、固定更新回数、TimeScale=0と復帰、Pause、Reload後の時計とcallback寿命。
- 生成した実C++ DLL：OnCreate／OnUpdate／OnFixedUpdate、空Behaviour、登録と停止。
- C#：CoreCLRとMono用net48をビルド。Mono runtime実行は未検証。
