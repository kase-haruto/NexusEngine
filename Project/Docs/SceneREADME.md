# NexusEngine Scene Architecture

Level、System Scheduler、永続ID、Serialization、Benchmarkを追加した現在の設計は[ECS / Level Architecture](ECSREADME.md)を参照してください。Sceneは互換性のため名前を保持したECS Worldであり、型別Storageは独立したComponentStorage.hへ分離されています。

## 現在の責務

Scene基盤はRuntime層に属し、Entity slotとComponentの所有を担当します。

```text
Scene
  ├─ EntitySlot[]
  │    ├─ generation
  │    └─ alive
  ├─ free entity indices
  └─ ComponentStorage<T>
	   ├─ sparse indices
	   ├─ dense entity indices
	   └─ dense components

Entity
  └─ Scene* + EntityId（非所有Handle）

EntityId
  └─ index + generation
```

`Entity`はComponentを所有しません。生成元`Scene`より長く保持してはならず、操作時には`EntityId`のindexとgenerationをScene側で検証します。破棄済みslotを再利用した場合もgenerationが変わるため、古いHandleから新しいEntityを操作できません。

## Component Storage

Componentは型ごとのSparse Set形式の`ComponentStorage<T>`が所有します。Entity indexからdense indexをO(1)で取得し、Component本体は型ごとの`std::vector`へ連続配置します。

```text
sparseIndices[entityIndex] → denseIndex
denseEntities[denseIndex]  → entityIndex
denseComponents[denseIndex]→ Component
```

削除時は末尾Componentを削除位置へ移動するswap-and-pop方式を使い、追加・取得・削除を平均O(1)に保ちます。この不変条件を例外経路でも維持するため、Component型にはnoexceptなmove構築とmove代入を要求します。

複数Componentの反復には`Scene::ForEach<Components...>()`を使用します。指定Storageのうち要素数が最も少ないものを走査し、他のComponentをSparse indexで照合します。

```cpp
scene.ForEach<TransformComponent, VelocityComponent>(
	[](Entity entity, TransformComponent& transform, VelocityComponent& velocity) {
		transform.translation = transform.translation + velocity.linear;
	});
```

callback中はComponent値を変更できますが、dense配列の参照を無効化するEntity生成・破棄・Component追加削除は拒否されます。将来、System実行中の構造変更が必要になった段階でEntity Command Bufferへ遅延記録します。

`CreateEntity`は全Entityに次の基本Componentを追加します。

- `NameComponent`: Editor表示とデバッグ用の名前
- `TransformComponent`: Local translation、Quaternion rotation、scaleと計算済みMatrix

Math型はCalyxEngineの規約を参考にし、NexusEngineでは次の規則へ統一しています。

- `Vector3`、`Quaternion`、`Matrix4x4`はFoundationに置き、DirectXへ依存しない
- Matrixはrow-majorデータ、row-vector規則
- TranslationはMatrixの4行目へ格納する
- Local Matrixは`Scale * Rotation * Translation`
- World Matrixは`Local * ParentWorld`

`TransformSystem`は親子関係の循環を設定時と更新時に検出します。親Entityが先に破棄された場合、子の古い親参照を解除してrootとして評価します。

## Renderer境界

`CameraComponent`と`PrimitiveRenderComponent`は描画対象であることを表すRuntimeデータだけを保持します。GPU ResourceやDirectX型はComponentへ含めません。

`RenderSceneExtractor`は更新済みのECSから、`RenderScene`へMatrix、色、生成元EntityIdを値コピーします。RendererはSceneやComponent Storageを直接参照しないため、描画中にECSの構造変更が起きてもComponent参照がぶら下がりません。

```text
Scene / Component Storage
        ↓ RenderSceneExtractor
RenderScene（frame snapshot）
        ↓
Renderer / Graphics API
```

Camera未配置は有効な状態として扱います。Runtime描画に使う`primary`かつ`enabled`なCameraが複数存在する場合は、反復順に依存した選択を避けるため抽出エラーになります。投影は前方+Z、深度範囲0～1のDirectX規約です。

## 現段階で担当しない機能

- Scene更新スケジュール
- SerializationとPrefab
- Scene遷移
- RenderSceneのGPU Constant Bufferへの転送
- Editor Inspector

これらはScene本体へ集中させず、`TransformSystem`、`SceneSerializer`、Render Scene抽出処理などの利用側責務として追加します。
