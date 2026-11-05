# 距離フォグ

カメラと描画位置のワールド空間距離に応じた、単純な線形距離フォグです。

```
amount = saturate((distance - startDistance) / (endDistance - startDistance))
result = lerp(sceneRadiance, fogColor, amount)
```

開始距離より近い場所は元の色、終了距離より遠い場所はフォグ色となり、その間を補間します。距離はカメラからのユークリッド距離です。深度バッファの値を距離として直接使用しません。

EditorでEntityを選択し、コンポーネント追加から **距離フォグ / Distance Fog** (`FogComponent`) を追加します。Propertiesのfogページで有効状態・開始距離・終了距離・色を調整できます。初期値は開始8、終了40、線形RGB `{0.45, 0.55, 0.65}` です。色選択はsRGBで行い、内部では線形Rec.709に変換します。コンポーネントの追加と編集はUndo/Redoに対応し、Levelファイルへ保存します。

コンポーネントがなければfogは無効です。シーン全体で有効にできるfogは1つで、追加時に別のfogが有効なら新しいコンポーネントは無効になります。範囲や色の不正値は編集・保存・読み込み時に拒否し、抽出時にも不正値と複数の有効なfogを検出します。描画APIは引き続き `RenderScene::fog` / `FogSettings` です。ECSからの抽出時に毎フレーム初期化・転送するため、無効化や削除後に以前の値は残りません。終了距離は開始距離より大きくする必要があります。無効な設定が直接描画入力へ渡された場合はフォグを無効として描画します。

照明・反射後の線形HDRで合成し、Bloom・露出・トーンマッピング・sRGB符号化より前に適用します。従来描画とMesh Shaderは同じPixel Shaderを使用します。RTによる鏡・透過の最終色にも、カメラからその表面までの距離で一度だけ適用します。反射・屈折した二次レイの経路中の霧は計算しません。

背景のClear ColorとEditor UIには適用しません。高さによる密度変化や光の散乱・光芒を扱うボリューメトリックフォグは含みません。

GPU Validation付きの読み戻しテストで、開始距離前・終了距離後・途中の補間・alpha維持・無効化・無効範囲・VS/Mesh Shader描画・RT鏡/透過への適用を確認します。

実行: `Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe --fog`
