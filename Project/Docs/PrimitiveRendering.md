# プログラムで生成する基本形状

`PrimitiveRenderComponent::shape` で `Cube`、`Sphere`、`Plane`、`Cylinder`、`Cone` を指定すると、モデルファイルなしでメッシュを生成します。既定の `Asset` は `meshAsset` で指定したモデルを使用します。メッシュ未設定では描画・ピッキングを行いません。追加したコンポーネントの「メッシュ」でアセットを選ぶか、「形状」で組み込み形状を選んでください。

```cpp
auto entity = level.CreateEntity("Sphere");
auto* primitive = entity.AddComponent<PrimitiveRenderComponent>();
primitive->shape = PrimitiveShape::Sphere;
primitive->tint = {0.2f, 0.6f, 1.0f, 1.0f};
entity.GetComponent<TransformComponent>()->scale = {2, 2, 2};
```

形状は原点中心です。Cube は一辺1、Sphere は半径0.5、Plane はXZ平面の1×1（法線+Y）、Cylinder/Cone はY軸方向で高さ1・半径0.5です。位置・回転・大きさは Transform で指定します。Sphere は24×32分割、Cylinder/Cone は周方向32分割です。

エディタでは Details の「描画」→「形状」で選択できます。組み込み形状では `meshAsset` と `nodeIndex` を使用せず、テクスチャ・色倍率・材質の上書き（Slot 0）・影・選択表示は通常の描画経路に従います。レベル形式v10では空のメッシュ参照を未設定として保存します。v9以前の起動時モデル参照は、互換性のため明示的な `:startup` 参照へ移行します。メッシュ欄の X で解除すると描画されなくなります。

CPU生成だけを利用する場合は `PrimitiveMeshBuilder::Build(shape)` が1メッシュ・1ノード・1材質の `ModelAssetData` を返します。独自の描画ホストでは `ModelAssetLibrary::Resolve(item.shape, item.meshAsset, item.baseColorTexture)` で共有スナップショットへ統合し、解決したノード・材質範囲を描画項目へ設定してください。エディタのホストはこの解決を描画前に行います。
