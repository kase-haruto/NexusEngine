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

## レンダリング

Metallic/Roughness PBR、HDR、Compute IBL（Diffuse Irradiance / GGX Prefilter / BRDF LUT）、DDGI（動的照度フィールド）、ベイクGI＋ライトプローブ（比較用）、拡散RTGI（比較用）、Emissive / AO、RT Shadow、鏡・ガラス、Mesh Shaderに対応しています。

通常描画のGIは[DDGI](Project/Docs/DDGI.md)、事前計算方式は[ベイクGI＋ライトプローブ](Project/Docs/BakedLighting.md)を参照してください。RTGIの設定・検証・制約は[拡散GI設計](Project/Docs/GlobalIllumination.md)を参照してください。環境HDRの設定、責務分離、リソース寿命、テストと制約は[IBL設計資料](Project/Docs/IBL.md)、既存の描画基盤は[Graphics設計](Project/Docs/GraphicsREADME.md)を参照してください。
