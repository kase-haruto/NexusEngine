# Game開発基盤 1.1.0 完了作業

対象指示：残るScript機能とInput/Audio/Physics Backendを実装・検証した後、新バージョンをLauncherからインストール可能な形で配置する。ユーザーは2026-11-12にBackend新規実装を含むこと、Native状態移行は明示的な保存/復元契約とすることを指定した。

## 固定方針

- 既存ECS/Level/Reflection/Editor/Project/IPC/Build/配布経路を再利用する。
- Engine版は新規1.1.0。既存1.0.0や利用者Projectを上書きしない。
- Native状態移行はModuleが宣言した値の保存/復元だけ。pointer/GPU資源の自動移行は保証しない。
- Inputは所有Windowのmessageから値frameを生成し、UI入力をゲームへ漏らさない。
- AudioはWindows XAudio2。PhysicsはMITライセンスのJolt Physics 5.6.0を固定利用する。
- Physicsの導入停止指示は、その後のJolt採用指示で解除された。PhysXへの切替は取り消し、既存のJolt試作を共通ECSへ接続する。
- 別PC/全hardware/あらゆる状態の保証を、同一PCのテスト成功にすり替えない。
- Build/実行は最新ユーザー指示で許可済み。他作業者の差分・Git変更を破棄しない。

## 実装・確認リスト

- [x] Input Backend、C++/C#公開API、focus/transition、CPU回帰テスト。実Editorの操作確認は下記に残す。
- [x] Audio Backend、WAV asset、voice Handle、停止/寿命、実XAudio2回帰テスト。
- [x] Jolt Physics Backend、共通Component/保存/Reflection、query/force、テスト。
- [x] Asset/Levelのゲーム向け操作を既存資源/Projectと接続。
- [x] 同じScript型の複数instanceと、instanceを識別する共通Property編集/保存。
- [x] Native Moduleの明示状態契約と、安全境界での保存/復元/失敗処理。
- [x] 実Editorで追加/解除/編集/保存/再読込/Reloadを通して確認。
- [x] Debug/Release回帰・Mono/CoreCLR・失敗経路・配布構成の確認。
- [x] 1.1.0 Installed SDK発行、新ZIP/sha256/catalog、Launcherの一覧/インストール確認。
- [x] README/API/リリースノート/既知制約と実行ログを更新。

作業Root：Generated/GameplayCompletion-1.1.0-13d4bcbd47824bd884f0331df9d7f703。

Jolt上流：https://github.com/jrouwe/JoltPhysics/releases/tag/v5.6.0 。取得zip SHA-256：0af9beea51637ef805e624fe838ea2870f7b68cd48cbe1615c853bd9bcf4f1d7 。PhysicsBackendを新規実装するため、ゲーム用の成熟したsolverを利用し、簡易な独自solverを完成品として扱わない。SourceはProject/ThirdParty/JoltPhysicsへ固定配置した。

公式API資料：

- https://learn.microsoft.com/en-us/windows/win32/inputdev/keyboard-input-functions
- https://learn.microsoft.com/en-us/windows/win32/xaudio2/how-to--play-a-sound-with-xaudio2
- https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2-createmasteringvoice

## 進捗

### Physics Backendの選定確定（2026-11-12）

ユーザーの最新指示に従い、Jolt Physicsを採用する。PhysXは公式仕様の調査だけで、SDK取得、ソース追加、ビルド設定、リンク設定への組込みは行っていない。プロジェクト側に削除対象のPhysX実装・依存設定は存在しない。

Joltの固定ソースを利用し、Engine/旧一括Project/直接ソース型テストへのtargets import、Physicsコンパイル対象登録、Level/SystemSchedulerへの接続、Scene交換時のsolver破棄、共通ReflectionとLevel保存、C++/C#公開API（Raycast/Force/Impulse）を追加した。詳細は[JoltPhysicsREADME.md](JoltPhysicsREADME.md)。落下・床・保存・Scene交換・無効Handleなどの検証ソースも追加したが、今回の接続は静的確認のみ。独立Collider、接触イベント等と実行確認が残るため、Backendの完了チェックは付けない。依存ライブラリ単体のDebugビルド成功を、Engineへの導入完了として扱わない。

既存コード調査、Launcherのローカルcatalog/ZIP installとInstalled SDK発行経路を確認済み。前ターンのMono統合/既存CoreCLR回帰は成功済み。今回追加するBackend/新機能の検証をその成功で代用しない。現在はBackendの接続と依存構成を実装中。完了前に配布catalogを公開しない。

### 実装時点の経過記録（最終結果は末尾）

ユーザーが2026-11-12にビルド・テスト・Editor確認・配布までの禁止解除を明示承認した。InputのWindow frameとGame View focus gate、固定800 byte snapshot、C++/C# Facadeを追加した。AudioはXAudio2、厳密RIFF PCM/float WAV読込、Project Root解決、client所有Voice、上限付きcache、Pause、Stop/Volume/IsPlaying、Reload中の副作用拒否を追加した。Collider単体のStatic形状とRigidBody併用も追加した。Native Moduleは任意の対となるSave/Restore exportで明示ゲーム値を最大1 MiBのEngine所有bufferへ保存する契約を追加中。

- `EditorTests.Debug.build.log`と`EditorTests.Debug.run.log`：終了コード0。Jolt基本物理、Input edge/focus/thread/寿命、実XAudio2の無音WAV再生・停止・音量・Pause・owner解放を含む。Native state追加後の再ビルドと回帰は未実施。
- `ManagedAPI.CoreCLR.build.log`、`ManagedAPI.Mono.build.log`：両API構成とも警告0・エラー0。新APIを使うGameplay assemblyの実行確認は別途行う。
- Build-Jolt.ps1はWindows PowerShell 5.1で日本語コメントを正しく解釈するためUTF-8 BOM付きとし、依存構築の実行成功を確認した。

現在の次作業は同じScript型の複数instance、永続instance IDを使う共通Property編集/個別解除/Undo/保存、Native値移行の回帰、Asset/Level操作、実Editor確認、Release/SDK発行、Launcher配布である。

## 最終結果（2026-11-13）

1.1.0のRelease SDK/ZIP/カタログを作成し、Launcherを更新した。実Launcherから1.1.0のインストール完了とRegistry登録を確認した。インストール先1006ファイルのinventoryは一致し、同配置のRuntimeもGPU描画後に正常終了した。共通カタログは1.0.0/1.1.0を維持する。

Debug EditorTests、実Mono同型複数Script/サービス/Reload、CoreCLR回帰、Installed SDKからのHybrid ReleaseビルドとCPU/GPU、実Editor追加/保存/Play/Managed Reload/Stopを確認した。詳細・失敗経路・既知の制約と配布先は[最終検証記録](Release-1.1.0-Verification.md)。このチェックリストの完了は1.1.0の記載範囲を指し、未提供機能の実装やHTTPS公開を意味しない。
