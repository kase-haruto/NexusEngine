# Project生成・ビルド・起動の連携

## 目的と現在の範囲

EngineのソースをGameへコピーせず、Project Descriptorで選んだEngineを使ってGame固有RuntimeとEditorを生成する。既存MSBuild構成を維持する。Premakeへの移行は必須ではなく、既存のvcxproj・Reflection生成Target・依存ライブラリを再利用する方が移行時の影響が小さいため、現時点では採用しない。

Launcher GUIの実装と描画確認は[Launcher](Launcher.md)、安定版SDKの発行と検証は[Installed Engine](InstalledEngine.md)、明示的なEngine変更と復旧は[Project Migration](ProjectMigration.md)を参照する。受け入れ結果と検証限界は[要求監査](ProjectSystemAcceptance.md)にまとめる。基盤の調査と依存関係は[調査資料](ProjectSystemSurvey.md)、分離理由は[Engine/Game分離](EngineGameSeparation.md)、Descriptorとパスの規則は[Project System](ProjectSystem.md)を参照する。

## 責務と依存方向

| 構成 | 責務 |
| --- | --- |
| `Runtime/Project` | 共有可能なDescriptor、検証、不変Context、Project生成 |
| `Tools/Engine/EngineRegistry` | マシン固有のEngine配置・既定値・要求の厳密な解決 |
| `Tools/Project/ProjectTemplates` | データ駆動テンプレートの列挙・変数置換・生成後検証 |
| `Tools/Project/ProjectBuild` | 選択済みEngineで個別ターゲットをビルド、成果物の照合、子プロセス起動 |
| `Platform/Process` | Unicodeの引数引用、CreateProcess、終了照会、所有するビルドJobの停止 |
| `Core/Hosting/ProjectHost.inl` | ホストごとの型登録とGame初期化を既存起動処理へ接続 |
| `NexusProjectTool` | GUIから独立した操作・検証用CLI |

RuntimeのライブラリからTools/Editor/特定Gameへ依存しない。GameのMainがReflectionとSetupを定義し、ホスト実装を組み込む。静的リンクを選んだためEditorにもGameの型が含まれる。Game Module DLLはまだ必要とせず、今後ReflectionRegistrationの受け渡し境界から検討できる。

汎用`NexusEditor`と`NexusRuntimeHost`はEngine組み込み型のみ。生成Projectの`GameEditor`と`GameRuntime`はProject自身の型を登録する。Sourceが存在するのに汎用ホストで開き、Gameコードを無視する起動は禁止している。

## Engineの選択

Engine登録はNexusEngineを使う資格やオンライン認証ではない。共有DescriptorのEngine ID/版を、そのPCのSource checkoutまたはSDK配置へ対応付けるローカル情報である。同じNexusEngineでも開発版と固定SDK・複数版・構成を共存できるため分離する。PCやGameの移動時も共有Descriptorへ各マシンの絶対パスを書き込まずに済む。既定値の変更は既存Gameの版を変更しない。

`.nxproject`にはassociation・id・versionを保存し、ソースやSDKの絶対パスを保存しない。マシン固有Engine Registryは`.nxengine`の絶対パスと登録時のidentity/configuration/buildIdを保持する。

DevelopmentはEngineのvcxprojへProjectReferenceする。Installedは指定SDK内の静的ライブラリへリンクする。SDKを発行し、Installedでも両テンプレートのGame固有Runtime/EditorのビルドとCPU起動を検証した。InstalledではファイルinventoryのSHA-256照合を要求する。これは破損・混在検出であり電子署名ではない。

解決はassociation・id・要求version・configurationの一致に限定する。未配置・登録時からmanifestが変更された場合・複数候補で曖昧な場合はエラーにする。既定Engineは新規作成用であり、既存Projectの要求を置き換えない。Development更新後は明示的に再登録・Game再ビルドする。

## テンプレートとビルド

`Templates/*.nxtemplate`がファイル一覧を定義する。BlankはCameraと空Level、Basic 3DはCamera・Directional Light・Cubeを持つ。既存Level形式を使用し、Game側にHealth Componentの登録例を生成する。追加テンプレートは同じスキーマの定義と素材ファイルを追加する。

生成対象は新しい空きRootに限定する。所有する一時Rootでファイル生成・起動LevelのCPUロードを検証し、成功後に公開する。既存ProjectやEngine Sourceを上書きしない。

Gameには`GameRuntime.vcxproj`、`GameEditor.vcxproj`、`Game.slnx`、`Config/BuildTargets.json`を生成する。BuildTargetsがターゲットと実行ファイル位置を定義する。ビルドは単一vcxproj単位で実行し、Editorをビルドする際はRuntimeも先にビルドする。Solution全体のビルドやCleanは実行しない。

最新版テンプレートにはVSのデバッグ起動引数も含める。local propsで解決したEngine Data RootとProject Descriptorを明示して起動し、DebuggerのCWDに資源探索を依存させない。

マシン固有参照はGit除外した`Nexus.Engine.local.props`へ書く。成果物はGameの`Build`、中間物は`Intermediate`、Engine開発ビルドは`Generated/Modules`を使用する。Game/EngineのGit履歴を分離でき、submoduleを必須にしない。CMake packageは現在のビルド構成にないため導入していない。

`*.nxbuild`にはProject UUID、Engine identity/buildId、configuration、ホスト種別、実行ファイルSHA-256を記録する。起動前に照合し、Engineの動的依存も選択配置と照合する。GameやEngineを更新した場合は明示的な再ビルドが必要。これはGame内のビルド成果物でありGitには含めない。

## 操作例

標準の入口はLauncherのProject Browserとして利用できる。デスクトップshortcut → Project選択 → Editor → ToolsからVisual Studio → GameRuntimeの個別ビルド → Editorからstandalone実行、という流れは[開発フロー](DevelopmentWorkflow.md)を参照する。以下のIDE直接起動は補助的なデバッグ経路であり、毎回IDEを先に開くことを要求しない。

日常のC++開発では、初回作成・Engine解決後にGame Rootの`Game.slnx`をVisual Studioで開く。`GameEditor`をスタートアップにして登録したEngineと一致するx64/configurationでF5実行できる。生成されたデバッグ引数はProjectとEngineを明示するため、毎回Launcherから開く必要はない。単体ゲームをデバッグするときは`GameRuntime`をスタートアップにする。Editorからstandaloneを実行する場合も、C++変更後はGameRuntimeを個別にビルドしておく。

NexusEngine側のSolutionはEngine本体を開発するときに使う。Development参照のGameでは依存するEngineもビルドされ、PDBとソースがあればEngine側へステップ実行できる。clone直後やEngine移動後はLauncherのOpenまたはCLIの個別Gameビルドでマシン固有local propsを解決し直す。ゲームのC++コードはGame側のSourceへ追加する。

前提としてDebug/x64の`NexusProjectTool`、`NexusEditor`、`NexusRuntimeHost`を個別にビルドしておく。次はリポジトリRootからのPowerShell例。Engine登録は明示操作であり個人Registryを書き換えるため、テストでは専用Registryを渡す。

```powershell
$tool = './Generated/Modules/x64/Debug/NexusProjectTool/NexusProjectTool.exe'
& $tool engine-dev (Get-Location).Path Debug './Generated/MyEngines.json'
& $tool template-list './NexusEngine.Debug.nxengine'
& $tool create-template 'D:/Games/MyGame' 'MyGame' './NexusEngine.Debug.nxengine' blank
& $tool check 'D:/Games/MyGame/MyGame.nxproject' './Generated/MyEngines.json'
& $tool build-editor 'D:/Games/MyGame/MyGame.nxproject' './Generated/MyEngines.json'
& $tool verify-editor 'D:/Games/MyGame/MyGame.nxproject' './Generated/MyEngines.json'
& $tool open 'D:/Games/MyGame/MyGame.nxproject' './Generated/MyEngines.json'
```

`check`はGame固有ホストをビルドして`--game --check-game`でLevel読み込みを検証する。Graphicsを初期化しないため、GPU描画・ウィンドウ表示成功を証明するテストではない。`verify-*`は再ビルドせず成果物の結び付きを検証する。ビルドログはGameの`Build/Logs`へ保存する。

Editorの`実行 → 実行モード`で別ウィンドウを選び、共通の実行ボタンを使うと、Runtimeだけを非同期で増分ビルド・検証して保存済みProjectを別プロセスで起動する。Project DescriptorとEngine Data Rootを引用済みコマンドラインで渡し、EditorはRuntimeのWorldを直接操作しない。共通の停止ボタンはビルド中ならそのJobを取消し、実行中なら対象WindowへWM_CLOSEを送る。Editor終了で独立Runtimeを強制終了しない。エディタ内再生との違いとログは[開発フロー](DevelopmentWorkflow.md)を参照する。未保存編集内容の転送や双方向IPC制御はまだ実装していない。

## 検証と今後の完成条件

`Tests/ProjectWorkflow.Tests.ps1`は専用Root・専用Registryを使い、両テンプレートの生成、日本語と空白を含むパス、Game固有Componentのロード、Runtime/Editorの個別ビルド、再ビルド、ホスト種別・ハッシュ・未配置Engineの不一致拒否を検証する。Sourceの変更を含むため実行にはC++ビルド環境が必要。

実行済みの範囲と今後の拡張は以下のとおり。

- Launcherは作成・Recent登録・日本語検索・Tab移動・Openした子EditorのGPU実行・Engine変更・復旧を実入力で確認済み。ファイルDialogの全操作と物理モニタ間のDPI切替は未検証。
- Installed SDKは移動・明示再登録・Game移動/clone・日本語Shader RootでのGPU実行まで確認済み。別マシンの実環境は未検証。Debug/ReleaseのGPU実行、複数版共存、SDKだけのGameビルド、破損拒否、同梱Launcher起動も確認済み。
- Project Migrationは計画・契約の事前確認・明示適用・バックアップ・復旧・Launcher実入力を確認済み。移行はDescriptorだけを変更し、API/Asset/Levelの自動変換は行わない。形式変換器は将来の追加。
- 既存サンプル個別ビルドとEditorの編集/保存/Undo/Play等の回帰、Developmentの3構成とInstalled Debug/ReleaseのGPU frame loop、standaloneサービスの実起動・停止を確認済み。
- 配布：BuildTargetsでGame Executableを識別できる。将来の専用パッケージ処理で必要Asset・Shader・Config・Runtime依存を収集し、開発用manifest/local propsを除外する。現時点では完成したパッケージ機能はない。
