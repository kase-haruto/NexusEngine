# 1.1.0 配布・検証記録

2026-11-13。ユーザーの明示許可によりビルド、テスト、実Editor、Launcher更新とEngineインストールを実施した。旧1.0.0配布物と既存Projectは維持した。今回の公開先はこのPCのローカルカタログで、HTTPSサーバーへのアップロードは行っていない。

## 調査結果

- `Publish-Engine.ps1`は既存Runtime/EditorSupportをビルドし、Installed SDKだけで汎用Hostをリンク・CPU検査して発行する。既存の新規directory/staging方式を再利用した。
- `New-EngineRelease.ps1`はInstalled SDKのZIP、サイズ、SHA-256、ローカルカタログを生成する。既存のinventoryとEngine identityを維持した。
- `Launcher/Standalone/Distribution.cs`はローカルZIPのサイズ/hash、Engine manifest、全inventoryを検査して既存ProjectToolへ登録を委譲する。新しい配布Managerは作らない。
- デスクトップは旧生成Launcher、スタートメニューにはさらに古いインストール済みLauncherが存在した。既存Setupの版別配置で更新し、両ショートカットを同じ最新Launcherへ接続した。旧Launcher本体は削除していない。
- Physicsは既存Level/SystemScheduler/Scene、ModelAssetLibraryを使用する。Jolt listenerは値収集だけを行い、共通GameServicesでC++/C#へ公開する。

## 設計判断

Engine ID `nexus`、Version `1.1.0`、Windows x64 Releaseの新規Installed SDKとして配布した。開発Repositoryの`nexus-development / 0.1.0-dev`は利用者Projectの関連付けを壊さないため別identityのまま維持する。

既存ZIP/カタログ/Registry/Setupを拡張・再利用し、別インストーラーや別Asset/Reflection/ECSを作らない。共通カタログへ1.1.0と1.0.0を登録した。SDK資料もinventoryへ含めるようPublisherを拡張した。

Mono GACコピーではWindows PowerShell 5.1の長いpath制約に遭遇したため、同期ファイル操作を拡張長path対応の.NET APIへ変更した。OSの長いpath policyやセキュリティ設定は変更していない。net48/MSBuildの生成config側には別のMAX_PATH制約が残り、短いProject RootではHybridビルドが成功した。長いRootでの失敗を成功扱いしていない。

## 変更ファイル

配布仕上げ時の変更。ここまでのBackend/Script/Native状態移行の全責務は各READMEとリリースノートも参照する。

| ファイル | 内容 |
|---|---|
| `Runtime/Physics/PhysicsMeshCooking.h/.cpp` | 既存CPU modelから静的mesh/convexを生成。頂点/三角形上限・範囲検査 |
| `Runtime/Physics/PhysicsContactCollector.h/.cpp` | Jolt接触をEntity pairのEnter/Stay/Exitへ集約。sleep/削除/通知上限を処理 |
| `Runtime/Physics/PhysicsWorld.h/.cpp` | ModelSource、shape cache、CCD、listener所有、接触snapshot |
| `Runtime/Physics/PhysicsUpdateSystem.h/.cpp` | 既存Scheduler内でmodel sourceと接触終了を同期 |
| `Runtime/Physics/GamePhysicsAccess.h/.cpp` | 接触snapshotの固定DTO/pageコピー |
| `Runtime/Scene/Components/ColliderComponent.h` | meshAsset、Triangle Mesh/Convexの保存値 |
| `Runtime/Scene/Components/RigidBodyComponent.h` | continuousの保存値 |
| `Runtime/Level/Level.h/.cpp` | hostのmodel source借用、接触照会 |
| `Runtime/Gameplay/GameApiBridge.h/.cpp` | client別の購読と既存Event配送、予約Eventの偽装拒否 |
| `Runtime/Gameplay/GameRuntime.cpp` | Physics成功後、安全な位置で接触Eventを公開 |
| `Runtime/Gameplay/GameApplication.cpp` | ゲーム開始前から同じModelAssetLibraryを物理へ提供 |
| `Editor/Core/EditorSession.h/.cpp` | 同じmodel sourceを編集/Play Levelへ接続 |
| `Editor/Core/EditorApplication.cpp` | Editorの既存model cacheを借用 |
| `Public/Nexus/GamePhysics.h/.hpp` | 接触DTO、型付きsnapshot/購読API、自己完結するinclude |
| `Managed/Nexus.Managed/GamePhysics.cs` | PhysicsContactと型付きpage照会/購読API |
| `Build/Nexus.Jolt.targets` | cooking/listenerのコンパイル対象 |
| `Tests/GameServicesTests.cpp` | Mesh/Convex、sleep/Exit/Sensor、非購読client、無効Dynamic Meshの実テスト |
| `Tests/Fixtures/MonoGameplay/Probe.cs` | 実Monoからの接触ABI照会を追加 |
| `Tools/Project/Deploy-MonoRuntime.ps1` | 長いGAC pathでもcopy/hash比較する |
| `Tools/Engine/Publish-Engine.ps1` | SDKへDocsを含める |
| `Docs/GameApiReference.md` | 新しいC++/C#サービス一覧と提供範囲 |
| `Docs/JoltPhysicsREADME.md` | Mesh/CCD/接触、ABI、上限、実検証範囲 |
| `Docs/MultipleScriptsREADME.md` | 同型複数instanceの永続IDと編集・保存 |
| `Docs/RuntimeServicesREADME.md` | 最新統合/Releaseの確認記録 |
| `Docs/Release-1.1.0.md` | 配布版の機能、依存、互換性、既知の制約 |
| `Docs/GameplayCompletionPlan.md` | 作業結果を更新 |
| `Generated/EngineReleases/v1.1.0` | 新規SDK、ZIP、版別カタログ、検証記録 |
| `Generated/EngineReleases/releases.local.json` | 1.1.0/1.0.0の共通カタログ |
| `Generated/LauncherDistribution/v1.1.0` | 自己完結LauncherとSetup |

## ライフサイクル

Source Releaseビルド → staging SDKのHostリンク/CPU検査 → 新規Engine directoryへ発行 → SDK資料とinventory確定 → ZIP/hash/サイズ確定 → 共通カタログ → Launcher Setupの版別配置 → LauncherによるZIP検証/展開 → manifest/inventory検証 → Engine Registry登録。

配布済み1.1.0のZIPとInstalled SDKは確認後に書き換えていない。検証結果はこの外部記録に追記する。既存のProject関連付けは自動移行しない。

## 実行確認

ログRoot: `Generated/GameplayCompletion-1.1.0-13d4bcbd47824bd884f0331df9d7f703`。

| 確認 | 結果 / ログ |
|---|---|
| Debug EditorTests全体 | exit 0 / `Final.EditorTests.build.log`、`Final.EditorTests.run.log` |
| 実Mono Hosting/同型2 instance/公開値/Enabled/Reload/Input/Physics/Audio | exit 0 / `Final.MonoGameplay.run.log` |
| 既存CoreCLR回帰 | exit 0 / `Final.CoreCLR.build.log`、`Final.CoreCLR.run.log` |
| Release SDK発行とCPU smoke | exit 0 / `Publish-Engine.log` |
| 配布版Runtime 10 GPU frame | exit 0 / `Installed.Runtime.GPU.log` |
| 実Editor RigidBody追加/共通Details/保存/Play/Stop | 成功 / `Installed.Editor.UI.log`と画面確認 |
| Installed SDKからHybrid生成/Native DLL/Mono/Host Releaseビルド | exit 0 / `Installed.Hybrid.ShortPath.build.log`、生成ProjectのBuild/Logs |
| Hybrid CPU起動と30 GPU frame | exit 0 / `Installed.Hybrid.CPU.log`、`Installed.Hybrid.GPU.log` |
| Hybrid Editor生成/同型Script 2個/個別Property/保存/Play/Reload C#/Stop | 成功 / `Hybrid.Editor.UI.log`。`Managed: Reload Success`を実画面で確認 |
| Launcher Setup、両ショートカット更新 | 成功。以前のLauncherを保持し、新しい版別directoryへ配置 |
| Launcher 1.1.0/1.0.0一覧 | 実画面で2版を確認 |
| Launcher 1.1.0インストール | 実ボタンから完了、RegistryでAvailable、インストール済み一覧に表示 |
| インストール先Runtime 10 GPU frame | exit 0 / `Launcher.Installed.Runtime.GPU.log` |
| インストール先inventory | 全1006ファイルのSHA-256一致 |

## 静的確認

include、namespace、宣言/定義、MSBuild対象、Native/Managed layoutと既存API表互換、Entity世代、client所有権、Reload/Scene交換、thread制約を確認した。MSBuild/Managed XMLをparseし、Mono deployment scriptのPowerShell構文を確認した。`git diff --check`はexit 0。実ビルド/link/実Mono呼出しでも整合を確認した。

## 配布先

- ZIP: `Generated/EngineReleases/v1.1.0/Packages/nexus-1.1.0-Release.zip`
- ZIP bytes: `107137056`
- SHA-256: `8979163e81f241fe49afb6bea37144e7c3616e171d109d9cb2ee015d1945c6d2`
- 共通カタログ: `Generated/EngineReleases/releases.local.json`
- Engineインストール先: `%LOCALAPPDATA%/NexusEngine/Versions/nexus-1.1.0-Release`
- 最新Launcher配置: `%LOCALAPPDATA%/Programs/NexusEngineLauncher/d990b6fd9b8245f78f2793a6fc4e1593`
- デスクトップ/スタートメニューの`NexusEngine Launcher`から起動する。現在はインストール済み一覧を表示したままにしている。

## 未実装・未検証

1.1.0の機能範囲と未提供機能は[リリースノート](Release-1.1.0.md)を参照。Constraint/Character Controller/複数Collider、gamepad/action mapping、3D Audio/streaming、任意Native pointer/GPU資源の自動移行を実装済みとは扱わない。

今回の最新Native状態移行はCPU protocolで確認した。Live++の実パッチに対する最新Save/Restore、あらゆる変更layout、別PC/全hardware/長時間/大規模負荷は未検証。Standard配布HostではLive++ Backendなしの`Native: Unavailable`が正常で、Native DLLの実行可否とは別。Managed Contactは実Monoで空pageのABI、実接触データはNative共通経路のテストで確認した。

HTTPS公開用URL/サーバーへのアップロードは未実施。このPCではローカルZIP/カタログからインストールできる。クリーンPCへ渡す場合はカタログとZIPを一緒に配置するか、実在するHTTPS配布先を設定する必要がある。
