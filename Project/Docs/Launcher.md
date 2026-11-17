# NexusEngine Launcher

現在のLauncherは `Launcher/Standalone/Launcher.csproj` のWinForms版です。C++版Launcherと専用のcaptureテストは削除しました。Engine / Project生成・移行・起動のC++サービスと `NexusProjectTool` は引き続き使用します。

## 新規Project

1. Engine Versionを選択します。実際のRegistryからVersion、Installed / Development、構成、既定EngineのCurrent状態を表示します。
2. 画像付きカード全体をクリック、またはTab・Enter・SpaceでTemplateを選びます。Hoverは背景、Selectedはアクセント枠とチェックで区別します。
3. 選択Templateの拡大Previewと説明を確認し、名前・親フォルダーを入力してCreate Projectを押します。

| 表示名 | 安定ID | 初期Level | 生成コード |
| --- | --- | --- | --- |
| Empty | `blank` | Entityなし | なし |
| Basic Level | `basic-3d` | Camera / Directional Lightの2 Entity | なし |

Project設定、Assets / Levels / Source / Config、初期Level、空Directory保持用の`.gitkeep`と`.gitignore`だけを作成します。Game ModuleやDemo Assetは生成しません。汎用Editor / Runtimeを既存のProjectBuild経路で起動できるデータProjectです。EmptyはCameraも持たないため、単体Runtimeの起動にはユーザーによるCamera追加が必要です（既存GameApplicationのCamera要件を維持）。Basic Levelは既存RuntimeのCPU起動チェックも通過しています。

## 責務とMetadata

- `ProjectCreationPanel.cs`: 入力、カード、選択Preview、作成可否。Scene型には依存しません。
- `ProjectCreationService.cs`: CLIの検証済みMetadata取得と生成要求。`ProjectTemplatePreview`は画像Sourceを読み込みます。
- `Tools/Project/ProjectTemplates.{h,cpp}`: `.nxtemplate`の検索、検証、ファイル展開。Sceneの自動補完はしません。
- `Runtime/Project/ProjectService`: staging、Project設定、ディレクトリ、atomicな公開。既存Directoryを上書きしません。
- `Tools/Engine/EngineRegistry`: Engine一覧・厳密な配置検証と解決。Launcherが指定していない別Engineへ切り替えません。

既存JSON形式 `.nxtemplate` を拡張し、新しい独自設定形式は導入していません。

```json
{
  "schemaVersion": 1,
  "id": "blank",
  "name": "Empty",
  "description": "Completely empty Level.",
  "category": "project",
  "generateModule": false,
  "initialLevel": "@LEVELS@/Startup.level",
  "requiresCamera": false,
  "preview": { "kind": "image", "source": "Blank/Preview.png" },
  "files": [{ "source": "Blank/Startup.level", "target": "@LEVELS@/Startup.level", "replace": false }]
}
```

`initialLevel`はProject Root相対で、`@LEVELS@`はProjectのLevels設定に置換します。PreviewのSourceはMetadata相対です。`image`と`thumbnail`は現在どちらも静的画像を読み込み、未知のSource種別や画像欠落・破損は`Preview unavailable`へFallbackします。新しいSource種別はPreview読込サービスで拡張できます。Editorによるthumbnail生成やRuntime Previewは今回実装していません。

PreviewはHierarchyとViewportを示す静的な構成図です。実際のEditor capture画像ではありません。FloorやCubeは含まれず、グリッドは編集補助線として表しています。

Launcherは自身の`Templates/`を同梱します。既存SDKのimmutable inventoryを変更せずに新UIを利用できます。追加TemplateはMetadataと資源をこのRootへ配置すれば列挙され、ビルド時にもSamples / Behaviour以外の資源を自動同梱します。Engine側TemplateとSamplesはCLIの`create-template`で引き続き利用できます。

```text
NexusProjectTool template-catalog <engine.nxengine> [template-directory]
NexusProjectTool create-project <new-root> <name> <engine.nxengine> <template-directory> <template-id>
NexusProjectTool create-template <new-root> <name> <engine.nxengine> <template-id> [Cpp|CSharp]
```

Metadata不正、入力ファイル欠落、Level破損、パス逸脱、重複IDは該当項目を無効にし、正常なTemplateの利用を維持します。画像欠落だけでは作成を禁止しません。Engine切替中は旧Templateを破棄し、遅れて到着した別Engineの応答を採用しません。作成時にもMetadataとEngineを再検証します。

## Samplesの整理

`Templates/Samples/`にはNative Module、Managed Gameplay、Hybrid Gameplayのみを残します。Bootstrap-onlyはGameplay以前の移行用であり、未参照だった定義・専用ファイルを削除しました。共通のManaged hostプロジェクトはManaged Gameplayでも使用するため残します。

旧Empty / Basic用のHealthデモ雛形は`Tests/Fixtures/StaticProjectTemplates/`へ移し、既存の型付きホスト回帰テストだけが明示的に使います。LauncherやSDKのTemplate資源には含めません。Module / Source追加ツールは新しいSamples配置と旧SDK配置の両方を認識します。

## ビルドと確認

ルートの`NexusEngine.slnx`または`Launcher/Standalone/Launcher.csproj`を使用します。SDK発行は旧C++版Launcherをビルド・同梱しません。配布更新は`Tools/Project/Build-Distribution.ps1`を使用します。公開・インストール先の更新はこの変更には含みません。

今回の確認ではスクリーンショット・captureによる画像確認は行っていません。静的画像のデコード、UIのコントロール構造と選択操作、幅変更、生成ファイル、破損時のエラー処理をプログラムで確認します。

- `Tests/LauncherDistribution`: 既存配布テスト69項目。
- 同テストexeの`--verify-project-creation <new-output> <manifest> <tool>`: 最小Project生成、Demo非混入、画像読込、カード選択、Fallback、Engine切替時の無効化。
- `Tests/TemplateCatalog.Tests.ps1 -Tool <tool> -EngineManifest <manifest>`: 破損Metadata / Level / パス / ID / 配置と、残した全Sampleの生成。
- `Tests/HybridGeneration.Tests.ps1`: Samples配置変更後のModule / Source追加回帰。
- `Build/NexusProjectTests.vcxproj`: 最小Level内容と既存のProject / Module共通処理。

Launcher 1.3.0の配布準備では、現在のソースからRuntimeとCLIのRelease成果物を別フォルダーへ生成し直しました。この組合せでは、事前に観測した旧Releaseライブラリ再利用時の異常終了は再現せず、Template検証・Sample生成・SDK 1.3.0での新規Project生成・配布回帰テストが通過しています。既存SDKとユーザーのEngine関連付けは変更していません。詳細は[Launcher 1.3.0配布検証](Launcher-Release-1.3.0-Verification.md)を参照してください。
