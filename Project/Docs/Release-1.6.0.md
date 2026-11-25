# NexusEngine 1.6.0

Windows x64 / Release / MT / MSVC v145。ゲームプロジェクトは使用する SDK を選択して再ビルドしてください。

- C++／C# の Raycast で Trigger と指定 Entity を除外できるようになりました。
- C# の `World.ProbeGround` で、足元の接触位置、法線、距離、相手 Entity、傾斜角と歩行可否を確認できます。1 本のレイによる照会です。
- Dynamic RigidBody の直線速度減衰、角速度減衰、自動休止を Details と Component API から調整できます。旧 Level の省略値は従来の Jolt 既定値を使います。

この版は `feature/physics` のソースを使用します。1.5.6 までの公開物には、このソースで確認できていない C# 自動ビルド、IDE 参照設定、PDB エラーからの自動復旧が含まれます。これらを必要とするプロジェクトは 1.5.6 を引き続き選択できます。

- Windows PowerShell のモジュール自動読み込みが無効な環境でも、ゲームのビルド記録を生成できるよう修正しました。

[物理 API の使い方](GameReferencesTimePhysicsREADME.md)
