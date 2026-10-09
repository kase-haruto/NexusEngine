# 距離フォグ

カメラと描画位置のワールド空間距離に応じた、単純な線形距離フォグです。

```
amount = saturate((distance - startDistance) / (endDistance - startDistance))
result = lerp(sceneRadiance, fogColor, amount)
```

開始距離より近い場所は元の色、終了距離より遠い場所はフォグ色となり、その間を補間します。距離はカメラからのユークリッド距離です。深度バッファの値を距離として直接使用しません。

Editorの **Distance Fog** ウィンドウで、有効状態・開始距離・終了距離・色を調整できます。デモでは開始8、終了40、線形RGB `{0.45, 0.55, 0.65}` で有効です。色選択はsRGBで行い、内部では線形Rec.709に変換します。APIは `RenderScene::fog` / `FogSettings` です。通常のRenderSceneでは既定で無効です。終了距離は開始距離より大きくする必要があり、非有限値や負の値を拒否します。無効な設定が直接描画入力へ渡された場合はフォグを無効として描画します。

照明・反射後の線形HDRで合成し、Bloom・露出・トーンマッピング・sRGB符号化より前に適用します。従来描画とMesh Shaderは同じPixel Shaderを使用します。RTによる鏡・透過の最終色にも、カメラからその表面までの距離で一度だけ適用します。反射・屈折した二次レイの経路中の霧は計算しません。

背景のClear ColorとEditor UIには適用しません。設定は実行中のみ保持し、Levelファイルには保存しません。高さによる密度変化や光の散乱・光芒を扱うボリューメトリックフォグは含みません。

GPU Validation付きの読み戻しテストで、開始距離前・終了距離後・途中の補間・alpha維持・無効化・無効範囲・VS/Mesh Shader描画・RT鏡/透過への適用を確認します。

実行: `Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe --fog`
