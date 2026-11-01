# Installed Engine SDK

## 目的と方式

Development Engineはソースのvcxprojを参照し、Engineの開発結果を明示的なGame再ビルドで利用する。Installed Engineは別Rootにコピーした静的ライブラリ・ヘッダ・Shader・Editor資源・ツール・テンプレート・汎用ホストを使う。Source Engineを変更しても、配置済みSDKやそのGameの参照先を自動変更しない。

GameごとのEngineソースコピーや必須Git submoduleを避け、既存MSBuildのままビルド済みSDKを選択できる方式を採用した。CMake packageは現在の構成にないため追加していない。利点は既存Targetとリンク方式を再利用できること、欠点はMSVC toolset・CRT・configurationが一致するSDKを用意する必要があること。現在はWindows x64/v145、DebugとDevelopはMTd、ReleaseはMTを前提とする。

## 発行

`EnginePublisher`は選択したDevelopment配置と発行先を検証し、`Publish-Engine.ps1`を所有する子Jobで実行する。Project変更・Engine登録・移行は担当しない。

```powershell
$tool = './Generated/Modules/x64/Debug/NexusProjectTool/NexusProjectTool.exe'
& $tool engine-publish './NexusEngine.Debug.nxengine' 'D:/NexusEngines/0.1.0-Debug' 'nexus-0.1.0' '0.1.0'
& $tool engine-register 'D:/NexusEngines/0.1.0-Debug/NexusEngine.nxengine' './Generated/MyEngines.json'
```

発行処理はRuntime、EditorSupport、Launcherの単一vcxprojをそれぞれビルドしてから、新しい専用stagingへ配置する。Solution全体、既存Game、Cleanは実行しない。Engineを変更しながら発行するのではなく、発行対象のcheckoutと変更内容を確定してから実行する。

配置内のSDKライブラリだけをリンクして、Installed identityを持つ汎用Runtime/Editorホストを個別ビルドする。空ProjectのCPU起動も検証してからinventoryを作成し、同一volume上の完成Directoryを公開する。既存発行先は開始時・公開時とも置換しない。別version/configurationは別Rootへ発行する。

## 配置と依存

```text
<Installed Root>/
  NexusEngine.nxengine
  SDK/
    Foundation/ Core/ Runtime/ Graphics/ Platform/ Editor/ Tools/
    ThirdParty/ Shaders/ Build/
    Lib/x64/<configuration>/NexusRuntime.lib
    Lib/x64/<configuration>/NexusEditorSupport.lib
  Data/Shaders/
  Data/Resources/Editor/
  Data/Resources/Assets/Fonts/
  Templates/
  Bin/NexusEditor.exe
  Bin/NexusRuntimeHost.exe
  Bin/NexusLauncher.exe
  Bin/dxcompiler.dll
  Bin/dxil.dll
```

SDKにはEngine実装cpp、Game/Samples、ゲームのModels/Levels/Texturesを含めない。既存include構造を維持するため、現在の公開ヘッダ範囲は広い。内部APIの縮小は互換性を確認しながら段階的に行う。ホストの組み立て用inlとGame Reflection生成スクリプトはSDKに含める。

0.1.6の発行では、Sourceで互換維持している旧`Runtime/Reflection/Generated`も除外した。この一括生成headerはGame型を含むため共有APIにはしない。EngineGeneratedだけをSDKへ配布し、Source側の旧ファイルは変更しない。SDKヘッダのGame/Samples includeと実装cpp不在を専用テストで検証する。

0.1.7 Debugには空必須DirectoryをGitへ保持するMarker生成と、LauncherのPerMonitorV2 manifestも含めた。同梱LauncherのDPI context・リサイズ・描画、BlankのGame固有Runtime/EditorでのGPU実行を確認済み。

GameはInstalled association時に`Nexus.Game.props`のライブラリリンク分岐を使う。Development用ProjectReferenceは選択しない。生成ゲームのビルドに必要な絶対パスはGit除外のlocal propsとビルド成果物にだけ保持する。Project Descriptorはid/versionだけを共有する。

## 検証とエラー

Manifestはconfiguration・toolset・CRT・固定buildId・相対配置・runtimeFiles・SHA-256 inventoryを持つ。Installedでは全inventoryエントリを照合し、必要なライブラリとホストを確認し、未追跡の追加ファイルも拒否する。壊れたSDKや別版への自動fallbackは行わない。これは内容の混在検出であり署名による発行元の認証ではない。

通常の失敗は所有するstagingだけを削除し、既存発行先を保持する。キャンセルでJobを強制停止した場合はPowerShellのfinallyが実行されず、`.nexus-install-<uuid>`が残る可能性がある。次回発行でその残骸を自動削除しない。発行ログはSource Repositoryの`Generated/EnginePublish`に保存する。

2026-10-31 Debug/x64で次を確認した。

- 0.1.0/0.1.1/0.1.2の独立Rootへの発行、SDKだけを使った汎用Runtime/EditorのリンクとCPU起動。
- 0.1.1 SDKを使ったBlank/Basic 3DのGame固有Runtime/Editor、Game.Health登録、再ビルド、成果物不一致拒否：`ProjectWorkflow.Tests.ps1 -EngineManifest <installed manifest>`。
- 0.1.2と0.1.1のRegistry共存、同一Engineの再登録、SDKに実装cppがないこと、ヘッダ変更・未追跡ファイル・inventoryからのライブラリ欠落・既存版上書きの拒否：`InstalledEngine.Tests.ps1`。破損検証は専用SDKコピーだけを変更する。

0.1.3以降の今回発行したSDKには`Build/Nexus.Compatibility.json`を含め、[Project Migration](ProjectMigration.md)で変更前後の確認・明示適用・旧Descriptor復旧を行える。安定版へ切り替える際にC++ APIやAssetを自動変換する処理はない。

2026-11-01にはDebugとReleaseの0.1.4 SDKを発行し、それぞれSDKだけを参照するGame固有Runtime/Editorの3 GPU frameと正常終了を確認した。DevelopmentはDebug/Develop/Releaseで確認済み。[GPU・standalone検証](ProjectGpuValidation.md)に構成とログを記録している。

0.1.5 Debugは最新のstandaloneサービスとLauncherを含む。`Bin/NexusLauncher.exe`はSDKのData/Fontsを実行ファイル付近から探索し、Source Data Rootの明示なしで起動・描画できる。LauncherはEditorSupportへリンクしない。同梱したからといってそのSDKへ選択を固定せず、登録済みの別Engineも利用できる。

別マシンの実環境・全GPU機能は確認対象外。移動・クローン相当の検証は専用SDK/Gameコピーで行い、既存の発行先を移動・更新しない。
