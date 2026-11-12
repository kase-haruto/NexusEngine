<h1 align="center">NexusEngine</h1>

<p align="center">
ツール・エディタ、グラフィックス、フィジックスを学習することを目的とした自作ゲームエンジンです。
</p>

## 概要

**NexusEngine** は、ゲームの実行環境だけでなく、エディタや制作支援ツールを含めた開発環境の構築を目的としています。

機能同士の依存関係や責務を明確にし、保守性と拡張性を意識した設計を目指します。

## 開発方針

* 各機能の責務を明確にする
* モジュール間の依存を最小限にする
* 拡張しやすいインターフェースを設計する
* エディタとランタイムの責務を分離する
* 制作中の試行錯誤を効率化する
* 実装理由や設計意図を記録する

## 開発環境

<!-- 使用する環境が確定したら更新 -->

* Language: C++
* Platform: Windows

ゲーム開発言語の段階的な拡張は[C++ / C# Project基盤](Project/Docs/CppCSharpREADME.md)を参照してください。
公開されているクラス・関数・Component ID・使用例は[ゲーム開発API一覧（C++ / C#）](Project/Docs/GameApiReference.md)にまとめています。追加APIの未検証範囲も記載しています。

C#の異なるScriptを同じEntityへ追加し、個別に有効化・解除・保存する実装は[複数Scriptアタッチ](Project/Docs/MultipleScriptsREADME.md)を参照してください。

2026-11-12にMono複数Script統合テストと既存CoreCLR回帰テストを実行し、両方の成功を確認しました。[実行結果と確認範囲](Project/Docs/MultipleScriptsVerificationREADME.md)を参照してください。
Content BrowserのSource閲覧、Component / Scriptのアタッチ、テキストエディタ起動は[ソース操作の手順](Project/Docs/SourceContentBrowserREADME.md)を参照してください。

新規C# / Hybrid Projectの実行BackendはMonoです。[Mono RuntimeとScript編集](Project/Docs/MonoRuntimeREADME.md)にSDK、Build、Domain Reload、アタッチ・解除と従来CoreCLR Projectとの互換性を記載しています。

C++／C#からの登録済みComponent操作、Entity Query、Transform一括取得・更新のソース実装は[共通Component API](Project/Docs/GameComponentApiREADME.md)を参照してください。今回の変更は静的確認のみで、Build・実行は未検証です。

独立Native DLLの値Component宣言、共通Editorへのアタッチ・解除・保存、既存SchedulerからのC++ Entity Lifecycleは[Native Entity Component](Project/Docs/NativeEntityComponentsREADME.md)を参照してください。Native/HybridテンプレートにNativeMoverサンプルを追加しています。これらの変更もBuild・実行は未検証です。
改訂仕様の[Phase 3：Native Game System](Project/Docs/NativeGameSystemsREADME.md)では、Game DLLのSystemを既存Schedulerへ登録し、公開関数の契約宣言をSystem登録から分離しています。以下のPhase番号は旧仕様の実装履歴です。
改訂仕様の[Phase 4：C# Runtime / Game Logic](Project/Docs/CSharpGameLogicREADME.md)では、既存Hosting・Managed Gameplayを再利用し、World検証、ScriptSystem登録解除、Component値取得を補完しています。
改訂仕様の[Phase 5：Native / Managed API](Project/Docs/NativeManagedApiREADME.md)では、明示公開したNative System関数を署名・登録世代・寿命を検証してC#から呼ぶ経路を追加しています。
改訂仕様の[Phase 6：Reflection / Editor / 保存](Project/Docs/GameLogicReflectionREADME.md)では、共通Property経路を使い、保存Versionの保護とHybrid設定値の編集を補完しています。
改訂仕様の[Phase 7-A：Reload境界と状態通知](Project/Docs/HotReloadCoordinationREADME.md)では、Managed適用を描画から分離し、Interop sessionと未対応Nativeパッチの競合を拒否しています。Game DLL接続は後続7-Cを参照してください。
改訂仕様の[Phase 7-B：Native Patch Runtime契約](Project/Docs/NativePatchRuntimeREADME.md)では、固定stateを保持する準備・完了通知、Bindingの一括差替えとManaged Handle再解決を追加しています。動的Module登録・同期Backend接続は後続7-Cで実装しています。
改訂仕様の[Phase 7-C：Game DLL専用Live++ Backend](Project/Docs/NativeModuleLivePPREADME.md)では、明示的な開発profile、動的DLLのEnable / Disable通知、SDK hookによる結果通知と7-Bの同期実行を接続しました。実SDKでのBuild・Agent接続・起動に加え、World / Entityを保持したNative実コードパッチを確認しました。任意state移行や複数DLLの協調は対象外です。
改訂仕様の[Phase 8：Hybrid Project / Build / Packaging](Project/Docs/HybridProjectREADME.md)では、複数Native / Managed Moduleの共通実行、言語の途中追加、共通開発Panel、Build・依存DLL検査、可搬Packageと任意のprivate .NET Runtime配置を接続しています。[実行検証記録](Project/Docs/HybridVerificationREADME.md)にDebugのビルド・回帰テスト、C++ / C# / Hybrid Projectの起動、Hybrid Editor / RuntimeのDirectX 12描画、private .NET付きPackage起動の確認結果と使用方法を記載しました。
Phase 1の共通Project基盤に続き、[Phase 2：Game API / Native Module](Project/Docs/GameModulesREADME.md)を追加しました。
[Phase 3：.NET Runtime Hosting](Project/Docs/CSharpRuntimeREADME.md)ではManaged bootstrapの起動・終了まで追加しました。
[Phase 4：C# Gameplay / 共通ECS](Project/Docs/ManagedGameplayREADME.md)ではScriptBehaviourから既存Entity / Transformを操作する経路を追加しました。
[Phase 5：Managed Reflection / 共通Editor・保存](Project/Docs/ManagedReflectionREADME.md)では公開Propertyを既存Details・Undo / Redo・Level保存へ接続しました。
[Phase 6：Managed Hot Reload](Project/Docs/ManagedHotReloadREADME.md)ではC#のCompile・状態復元・collectible contextの解放確認を追加しました。
[Phase 7：Hybrid C++ / C#](Project/Docs/HybridGameplayREADME.md)では同一WorldでNativeとManagedを併用し、明示的なPublic APIと値Eventを接続しました。
上記の各Phase文書には当時の静的確認記録が含まれます。最新の実行済み範囲と未確認項目は実行検証記録を参照してください。

## ブランチ作成規則

ブランチ名は、変更内容を表す接頭辞と作業内容を組み合わせて作成します。

```text
接頭辞/作業内容
```

作業内容には、camelcaseを使用します。

### `feature/`

新しい機能を追加する場合に使用します。

```text
feature/sceneSystem
feature/modelRenderer
feature/editorWindow
```

### `fix/`

不具合を修正する場合に使用します。

```text
fix/modelLoading
fix/sceneTransition
fix/editorCrash
```

### `refactor/`

既存機能の動作を変更せず、設計やコードを改善する場合に使用します。

```text
refactor/renderSystem
refactor/assetManager
```

### `docs/`

READMEや設計資料など、ドキュメントを変更する場合に使用します。

```text
docs/updateReadme
docs/addRenderingDesign
```

### `chore/`

ビルド設定や依存ライブラリの更新など、機能実装以外の作業に使用します。

```text
chore/updateBuildSettings
chore/addLibrary
```

## コミットメッセージ規則

コミットメッセージには、変更内容を表す接頭辞を付けます。

```text
接頭辞: 変更内容
```

例：

```text
feat: シーン管理機能を追加
fix: モデル読み込み時のクラッシュを修正
refactor: 描画処理の責務を分割
docs: ブランチ作成規則を追加
chore: ビルド設定を更新
```


## ビルド方法

Windows x64、C++23/v145を含むVisual Studio、Windows SDK（DXC）、PowerShell 7を使用します。リポジトリRootから、必要なターゲットだけを順にビルドできます。

ランチャーとエンジンを一緒に開発するときは、リポジトリ直下の **[NexusEngine.slnx](NexusEngine.slnx)** をVisual Studioで開きます。.NET 10のWindowsデスクトップ開発環境も必要です。`Launcher/Launcher`が新しいC#ランチャー、`Engine`がC++エンジン・エディタ、`Legacy Launcher/NexusLauncher`が既存C++ランチャーです。スタートアップ切り替えと構成別のビルド方法は[Visual Studio共通ソリューション](Project/Docs/VisualStudioDevelopment.md)にまとめています。

```powershell
foreach($target in @('NexusProjectTool','NexusEditor','NexusRuntimeHost','NexusLauncher')) {
    & ./Project/Tests/Invoke-GraphicsBuild.ps1 -Project "Project/Build/$target.vcxproj" -Configuration Debug
    if($LASTEXITCODE -ne 0) { throw "Build failed: $target" }
}
& ./Generated/Modules/x64/Debug/NexusLauncher/NexusLauncher.exe
```

初回はLauncherのEngine library → Add an engineでDevelopment repositoryとしてこのリポジトリを登録するか、Installed SDKのmanifestを登録します。Create projectでEngineとテンプレートを明示選択し、作成後にProjectsからOpenします。SDK同梱の`Bin/NexusLauncher.exe`も利用できます。

作成後のC++開発はGame側の`Game.slnx`をVisual Studioで開き、`GameEditor`をスタートアップにしてF5実行できます。単体ゲームのデバッグは`GameRuntime`を選びます。毎回Launcherから開く必要はありません。Engine登録は共有ID/バージョンをPC内のSource/SDK配置へ結び付けるローカル情報で、オンライン認証ではありません。

標準の入口をデスクトップ → Project Browser → Editorにする場合は`Project/Tools/Project/Create-LauncherShortcut.ps1`でshortcutを作成できます。Editorの`Tools → Open game in Visual Studio`から、現在のGameのSolutionをIDEで開けます。具体的な操作と別プロセスRuntime通信の現在の範囲は[開発フロー](Project/Docs/DevelopmentWorkflow.md)を参照してください。

Editorの`実行 → 実行モード`で`エディタ内`または`別ウィンドウ`を選び、共通の`実行 / 停止`ボタン（F5 / Shift+F5）を使います。別ウィンドウではGameRuntimeだけを非同期に増分ビルド・検証して保存済み起動Levelを実行します。停止ボタンはビルド取消にも対応し、一時停止はエディタ内の再生だけが対象です。

VSで`Project/Build/NexusEngineModules.slnx`を開く場合は、NexusLauncherをスタートアップProjectに指定します。既存サンプル用のSampleGameEditor/Runtimeも保持しています。通常のGameは生成されたGameEditor/GameRuntimeで起動し、特定Gameの型を汎用ホストへ暗黙に混在させません。

ここに示す手順は個別ターゲットのビルドです。この作業中は指示どおりSolution全体のビルドや既存成果物のCleanを実行していません。

## ドキュメント

Project System導入に向けた現状調査と、Engine / Game分離の変更対象・ビルド方式・移行順序は、
[Project System：現状調査とPhase 1設計](Project/Docs/ProjectSystemSurvey.md)を参照してください（実装前の調査・設計記録）。

Phase 1の実装構成、個別ビルド、独立Gameの参照方法と制限は、
[Engine / Game分離](Project/Docs/EngineGameSeparation.md)を参照してください。

`.nxproject`、Project作成CLI、最近使ったProject、Editor / Runtimeへの接続は、
[Project System（Phase 2）](Project/Docs/ProjectSystem.md)を参照してください。

テンプレート生成、Engine登録、Game固有ホストの個別ビルドと起動は
[Project Workflow](Project/Docs/ProjectWorkflow.md)、Project Browserの操作と検証は
[Launcher](Project/Docs/Launcher.md)を参照してください。

配布用の独立ランチャーは`Project/Launcher/Standalone`、セットアップは`Project/Launcher/Setup`にあります。`Project/Tools/Project/Build-Distribution.ps1`または`Project/Build/NexusDistribution.proj`でReleaseのCLIと.NETランタイムを含む`launcherSetup.exe`を生成できます。既存C++ランチャーは開発・エンジン移行用として保持しています。Cloudflare R2での版別配布、ビルド・起動方法、検証状況は[ランチャー配布手順](Project/Docs/LauncherDistribution.md)を参照してください。

普段の起動、エンジン・ランチャーの更新、旧版への復旧、配布時の手順は[ランチャー運用ガイド](Project/Docs/LauncherOperations.md)にまとめています。
安定版SDKの発行とDevelopment / Installedの参照方法は
[Installed Engine](Project/Docs/InstalledEngine.md)を参照してください。
Engine変更前のレビュー、明示適用、バックアップと旧版復旧は
[Project Migration](Project/Docs/ProjectMigration.md)を参照してください。
GPU起動、構成別の実行結果、別プロセスRuntimeの通常終了は
[Project GPU / standalone検証](Project/Docs/ProjectGpuValidation.md)を参照してください。
独立Git管理、SDK/Gameの移動、clone後の再バインドは
[Project Git Workflow](Project/Docs/ProjectGitWorkflow.md)を参照してください。
実装・検証済みの範囲と未完了の作業は[Project System進捗](Project/Docs/ProjectSystemWorkStatus.md)に記録しています。

EditorのPlayとゲーム単体起動で共通のSystemを動かす方法は、[ゲーム実行基盤](Project/Docs/GameRuntime.md)を参照してください。

エディタの起動、編集操作、拡張方法、検証と制限は[Editor利用ガイド](Project/Docs/EditorUserGuide.md)を参照してください。

Live++ の導入・操作、Reflection の自動生成と Reinstancing による状態移行は
[ライブコーディング](Project/Docs/LiveCoding.md)を参照してください。

設計方針や各機能の仕様は、`Project/Docs`ディレクトリに記録します。

エディタの構成、問題点、改善優先順位、設計案、実装ロードマップは
[Editor現状調査・設計レビュー](Project/Docs/EditorArchitectureReview.md)を参照してください。

機能を追加する際は、可能な範囲で以下の内容を残します。

* 機能の目的
* 採用した設計
* その設計を採用した理由
* 他の案との比較
* モジュール間の依存関係
* 今後の課題

## ライセンス

ライセンスは現在未設定です。

## Physics

Jolt Physics 5.6.0を既存ECSへ接続し、共通RigidBodyの編集・保存とC++/C#のRaycast・Force・Impulse APIを追加しています。Engine統合の実行確認は未完了です。構成、使用方法、所有権と制約は[Jolt Physics](Project/Docs/JoltPhysicsREADME.md)を参照してください。

## レンダリング

Metallic/Roughness PBR、HDR、Compute IBL（Diffuse Irradiance / GGX Prefilter / BRDF LUT）、DDGI（動的照度フィールド）、ベイクGI＋ライトプローブ（比較用）、拡散RTGI（比較用）、Emissive / AO、RT Shadow、鏡・ガラス、Mesh Shaderに対応しています。

通常描画のGIは[DDGI](Project/Docs/DDGI.md)、事前計算方式は[ベイクGI＋ライトプローブ](Project/Docs/BakedLighting.md)を参照してください。RTGIの設定・検証・制約は[拡散GI設計](Project/Docs/GlobalIllumination.md)を参照してください。環境HDRの設定、責務分離、リソース寿命、テストと制約は[IBL設計資料](Project/Docs/IBL.md)、既存の描画基盤は[Graphics設計](Project/Docs/GraphicsREADME.md)を参照してください。

## ゲームパッド入力（1.1.1）

XInput対応機器を4台まで共通Inputへ接続します。C++/C#の取得方法、対応範囲、focusと切断の契約は[Gamepad Input](Project/Docs/GamepadInputREADME.md)を参照してください。
