# Visual Studio共通ソリューションでの開発

リポジトリ直下の **`NexusEngine.slnx`** をVisual Studioで開きます。

```text
C:\MyProject\NexusEngine\NexusEngine.slnx
```

Visual StudioのC++ v145ツール・Windows SDKと、.NET 10のWindowsデスクトップ開発環境を使用します。

## ソリューションの構成

| フォルダー | プロジェクト | 用途 |
| --- | --- | --- |
| Launcher | Launcher | 現在使用しているC# / WinFormsランチャー |
| Launcher | LauncherSetup | セットアップ画面とインストール処理 |
| Engine | NexusRuntime | エンジンのRuntimeライブラリ |
| Engine | NexusEditorSupport | エディタの機能・UIライブラリ |
| Engine | NexusEditor | 特定Gameの型を含まないエディタホスト |
| Engine | NexusRuntimeHost | 特定Gameの型を含まないRuntimeホスト |
| Engine | NexusProjectTool | プロジェクト作成・登録・ビルド・エンジン検証CLI |
| Samples | SampleGame / SampleGameEditor / SampleGameRuntime | リポジトリ内サンプル |
| Legacy Launcher | NexusLauncher | エンジン移行UIなどを保持した既存C++ランチャー |
| Tests | DistributionTests | 配布・ローカル導入・CLI連携の検証 |

共通ソリューションにまとめていますが、実行ファイルと責務は引き続き別です。C#ランチャーがC++エディタを直接リンクすることはありません。

`Project/NexusEngine.slnx`は既存の一括プロジェクト用、`Project/Build/NexusEngineModules.slnx`は既存のC++モジュール用として残しています。今後、ランチャーとエンジンを一緒に開発するときの入口はルートの`NexusEngine.slnx`です。

## 日常の操作

1. ルートの`NexusEngine.slnx`を開きます。
2. 構成を`Debug`、プラットフォームを`x64`にします。
3. 編集・デバッグしたいプロジェクトを右クリックし「スタートアッププロジェクトに設定」を選びます。
4. 対象プロジェクトをビルドし、F5で開始します。デバッグ不要の場合はCtrl+F5を使います。

| 作業 | スタートアップに指定するもの |
| --- | --- |
| 新ランチャーのUI・処理 | `Launcher`フォルダーの`Launcher` |
| サンプル上でエディタ機能を確認 | `Samples`の`SampleGameEditor` |
| 汎用エディタホストをデバッグ | `Engine`の`NexusEditor`。対象`.nxproject`などの起動引数を指定 |
| 生成済みの自分のGameを開発 | そのGameの`Game.slnx`を開き、`GameEditor`を指定 |
| 旧C++ランチャーの移行UI | `Legacy Launcher`の`NexusLauncher` |

スタートアップの選択はVisual Studioのユーザー設定です。他の開発者の選択を書き換える設定ファイルは追加していません。F5時にスタートアップとその依存だけをビルドしたい場合は、Visual Studioの「ツール → オプション → プロジェクトおよびソリューション → ビルド/実行」で対象を設定します。Ctrl+Shift+Bのソリューション全体ビルドとは区別してください。

## ランチャーをF5で開発する

共通ソリューションでは、`Launcher`に`NexusProjectTool`へのビルド依存を設定しています。CLIが依存するRuntimeもビルド対象になります。エディタ全体やGameの再ビルドは不要です。

開発ビルドの完了後、対応する構成のCLIとDLLをC#の出力先へコピーします。

```text
Project/Launcher/Standalone/bin/<Configuration>/net10.0-windows/
  NexusLauncher.exe
  launcher.json
  Tools/
    NexusProjectTool.exe
    dxcompiler.dll
    dxil.dll
```

開発用の`launcher.json`は出力先にだけ生成します。ソースの設定ファイルや、配布済みのランチャーには書き込みません。

ローカルのv1.0.0カタログが存在すれば、開発用ランチャーから自動で読み込みます。別のPCにカタログがない場合でも起動でき、画面からHTTPSの一覧を取得するかローカル一覧を選択できます。

```text
Generated/EngineReleases/v1.0.0/Packages/releases.local.json
```

構成は`Debug / Develop / Release`を共通に使用します。ソリューションは`x64`で、C#側は`Any CPU`への構成マッピングと`PlatformTarget=x64`を指定してWindows x64としてビルドします。C++側は対応する構成の`x64`です。

`Launcher.csproj`だけを単独で開いた場合、C++ツールは自動ではビルドしません。共通ソリューションを使うか、次のように同じ構成のCLIを先にビルドして開発用配置を明示します。

```powershell
# Visual Studio Developer PowerShell、リポジトリルートから
msbuild Project/Build/NexusProjectTool.vcxproj /p:Configuration=Debug /p:Platform=x64 /m:1
dotnet build Project/Launcher/Standalone/Launcher.csproj -c Debug -p:NexusDevelopmentBuild=true
```

開発用ランチャーも通常のランチャーと同じユーザーRegistryを使います。実プロジェクトの登録・作成などは実データへ反映されます。個人Registryを使わず画面だけ確認したい場合は`--preview`を使用します。

## コマンドラインで共通ソリューションの対象だけをビルドする

C++を含むため、Visual StudioのDeveloper PowerShellから`msbuild`を使用します。

```powershell
msbuild NexusEngine.slnx /t:Launcher\Launcher /restore /m:1 /p:Configuration=Debug /p:Platform=x64
msbuild NexusEngine.slnx /t:Engine\NexusEditor /restore /m:1 /p:Configuration=Debug /p:Platform=x64
```

対象を指定した場合、その対象と依存プロジェクトをビルドします。`dotnet sln NexusEngine.slnx list`ではソリューションの登録内容を確認できますが、`dotnet build`でC++を含む共通ソリューション全体をビルドする運用にはしていません。

## セットアップと配布物

`LauncherSetup`を通常ビルドしただけでは配布用payload ZIPは入りません。F5は画面の開発用で、実インストールを確認する際には、引き続き配布スクリプトで作った`launcherSetup.exe`を使います。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Project/Tools/Project/Build-Distribution.ps1 `
  -CatalogFile Generated/EngineReleases/v1.0.0/Packages/releases.local.json `
  -OutputDirectory Generated/LauncherDistribution/Development-Next
```

公開用には`-CatalogFile`の代わりに実際の`-CatalogUrl`を指定します。この配布ビルドは、通常の共通ソリューション開発とは別に実行します。出力先は毎回、新しい場所にしてください。

詳細は[更新・配布ガイド](LauncherOperations.md)と[配布の構成・検証](LauncherDistribution.md)を参照してください。

## 今回の確認

- 共通ソリューションの12プロジェクトを.NET CLIで読み込めることを確認しました。
- 共通ソリューションから`Launcher/Launcher`をDebug・Releaseで、`Engine/NexusEditor`をReleaseでビルドしました。
- Debug出力に同じ構成のCLIとDLL、ローカルカタログの設定が配置されることを確認しました。
- Debug・Developのソリューション構成を検証し、C#のLauncher・SetupはDevelopビルド、配布テスト29項目に成功しました。
- Visual StudioのGUIでのロード・ブレークポイント・F5操作そのものは今回実施していません。ソリューション全体のClean/Rebuildは行っていません。
