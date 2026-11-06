# 配布用NexusEngine Launcher

`Project/Launcher/Standalone/`はエンジン本体を同梱せず起動するWindows用ランチャーです。`Project/Launcher/Setup/`はランチャー一式を内蔵した単一の`launcherSetup.exe`を生成します。管理者権限は不要です。

普段の使い方、エンジンとランチャーそれぞれの更新、配布時の利用者の操作は[運用・更新・配布ガイド](LauncherOperations.md)を参照してください。

## 配置と既存経路との統合

以下のコマンドはリポジトリルートで実行します。

| 対象 | 正式な配置・用途 |
| --- | --- |
| 独立ランチャー | `Project/Launcher/Standalone`：配布するユーザー向けの入口 |
| セットアップ | `Project/Launcher/Setup` |
| 配布テスト | `Project/Tests/LauncherDistribution` |
| ビルド・ZIP作成 | `Project/Tools/Project/Build-Distribution.ps1`、`New-EngineRelease.ps1` |
| MSBuild入口 | `Project/Build/NexusDistribution.proj`：ReleaseのCLIをビルドしてセットアップを生成 |
| C++ランチャー | `Project/Build/NexusLauncher.vcxproj`：ソース開発・エンジン移行用、SDKにも引き続き同梱 |

`Project/Tools/Project`は既存CLIと補助スクリプトの配置として正しく、独立アプリとテストのみを責務ごとの場所へ移しました。既存のC++ランチャーのビルド・ショートカット・SDK発行経路は保持します。独立版は`NexusProjectTool.exe`を同梱して既存の作成・登録・ビルド・起動・inventory検証へ委譲し、同じユーザーのRegistryを共有します。EditorやEngineを先にインストールする必要はありませんが、生成したC++プロジェクトのビルドにはVisual StudioのC++ツールが必要です。

前回の`Project/Tools/Project/Artifacts/Distribution-001`と旧ディレクトリ内の`bin/obj`は保存しています。これらは新しいソースや成果物の入口ではありません。移行時の11ファイルのパス・SHA-256記録は`Generated/LauncherMigration/25495f0562914d8d9df19c0493e665ca/migration.json`です。

UIは`Project/Tools/UI/ImGuiStyle.h`のEditorStylePaletteに合わせた黒・グレーとオレンジのアクセントを使用します。Unreal Project Browserを参考に左ナビゲーション、独立した新規作成画面、エンジン選択と詳細の分割を採用しています。WinForms側の色定義は`LauncherTheme.cs`です。

## 利用者の操作

### 現在のv1.0.0配置

- Installed SDK：`Generated/EngineReleases/v1.0.0/Engine`
- 実パッケージ：`Generated/EngineReleases/v1.0.0/Packages/nexus-1.0.0-Release.zip`
- ローカル一覧：`Generated/EngineReleases/v1.0.0/Packages/releases.local.json`
- この一覧を起動時に読み込むランチャー：`Generated/LauncherDistribution/v1.0.0-005/Launcher/NexusLauncher.exe`
- 更新セットアップ：`Generated/LauncherDistribution/v1.0.0-005/Setup/launcherSetup.exe`
- 変更点：`Project/Docs/Releases/v1.0.0.md`

内部バージョンは`1.0.0`、ランチャーでは`v1.0.0`と表示します。開発用ソースのEngineIdentityは変更していません。

変更点欄は初期の高さを約3倍に広げ、下端のグリップを上下にドラッグして高さを変更できます。長い文章は欄内でスクロールでき、保存先などの下側の項目は右ペインのスクロールで確認できます。

`Build-Distribution.ps1 -CatalogFile <releases.local.json>`で生成した確認用ランチャーは、起動時にこの一覧を読み込み、実際のZIPからインストールできます。設定ファイルにはローカル一覧の絶対パスを保存するため、この確認用セットアップを別PCへ持ち出す際にはZIPと一覧も配置して「ローカルの一覧を選択…」から選び直してください。エンジンZIP自体はランチャーやセットアップには同梱しません。

オンライン一覧の取得はHTTPSのみを許可します。ローカル一覧のZIPパスは一覧ファイルのフォルダー内に限定し、HTTPSカタログにローカルパスを混ぜても拒否します。サイズ・SHA-256・識別情報・native inventoryの検証は両方で実行します。

2026-11-06のv1.0.0確認では、実ZIPの導入、native inventory検証、BlankテンプレートからのGame作成・登録、GameEditorのビルド・整合性確認・`--check-game`起動までを含む43項目に成功しました。通常の表示を伴うGPU Editor操作やCloudflareからのダウンロードの検証とは区別してください。取得欄は固定高さから自動計測の行へ変更し、プレビューでは取得ボタンの欠けと一覧との重なりも判定します。

1. `launcherSetup.exe`を実行し「インストール」を押す。
2. ランチャーでバージョン一覧を取得する。
3. バージョン、変更点、ダウンロード元、サイズ、保存先を確認してインストールする。
4. 「新規プロジェクト」で作成、「プロジェクト」で既存`.nxproject`の追加と起動を行う。「Gitから追加」ではHTTPSリポジトリを新しいフォルダーへクローンし、`.nxproject`を選んで追加する。

エンジン保存先の配下に`<id>-<version>-<configuration>`を作成するため、複数バージョンを共存できます。プロジェクトとエンジンは既存C++ツールと共通のLOCALAPPDATA内Registryを利用します。既存ファイルを別バージョンで上書きしません。Gitで複数プロジェクトを含む場合も選択できます。クローン後にファイル選択をキャンセルした場合は、既存プロジェクト追加から登録してください。

## Cloudflare R2の準備

R2 Standardに公開配布用バケットを作り、公開HTTPSドメインを接続します。開発用`r2.dev`にはレート制限があるため、本番は独自ドメインを推奨します。管理用APIトークンやS3キーをランチャーへ同梱しないでください。

公式資料（2026-11-06確認）ではStandardの無料枠は月10 GB-month、Class A 100万回、Class B 1,000万回で、インターネットへの転送料は無料です。保存する全バージョンの合計が枠を超える場合などは課金されます。独自ドメイン取得費は別です。

- [R2の料金と無料枠](https://developers.cloudflare.com/r2/pricing/)
- [公開バケットと独自ドメイン](https://developers.cloudflare.com/r2/buckets/public-buckets/)

## エンジンを公開する

既存の`Project/Tools/Engine/Publish-Engine.ps1`でInstalled SDKを発行してから、次を実行します。ZIPの直下に`NexusEngine.nxengine`が置かれる構成です。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Project\Tools\Project\New-EngineRelease.ps1 -EngineDirectory 'D:\Releases\Nexus-0.1.5' `
  -DownloadUrl 'https://downloads.example.com/engines/nexus-0.1.5-Release.zip' `
  -ChangesFile 'D:\Releases\changes-0.1.5.txt' -OutputDirectory '.\Generated\EngineRelease\0.1.5'
```

出力されたZIPを指定URLにアップロードし、`releases.json`を公開します。複数版を配布するときは既存`releases`配列へ新しい項目を追加します。ZIPは先に公開し、一覧を最後に更新してください。ZIP URLを版ごとに固定し、既存版を差し替えないでください。JSONは短いキャッシュ期間を推奨します。

`-DownloadUrl`を省略すると、ZIPと相対パスを使う`releases.local.json`を作成します。これはローカル導入用です。公開用の`releases.json`には実際の最終HTTPS URLが必要で、仮のURLを配布済みとして扱いません。変更点はプレーンな文字列として読み込み、Windows PowerShell 5.1のProvider付加情報をJSONに含めません。`Project/Tests/EngineRelease.Tests.ps1`でローカル・公開向け両形式の生成を検証できます。

```json
{
  "schemaVersion": 1,
  "releases": [{
    "id": "nexus",
    "version": "0.1.5",
    "configuration": "Release",
    "changes": "変更点をここに記入",
    "url": "https://downloads.example.com/engines/nexus-0.1.5-Release.zip",
    "sha256": "ZIPから生成した64桁のSHA-256",
    "bytes": 123456
  }]
}
```

`id`・`version`・`configuration`はSDK manifestと一致させます。ダウンロード後にサイズとハッシュ、識別情報を確認し、既存`NexusProjectTool engine-register`でinventory全体を検証します。登録失敗時には展開先を表示します。「インストール済み」の登録操作で再試行できます。

## セットアップを生成する

.NET 10 SDKとVisual StudioのC++ v145ツールを使用します。`-ProjectToolDirectory`を省略すると`Project/Build/NexusProjectTool.vcxproj`のRelease/x64を自動ビルドします。明示指定する場合は互換性のあるRelease版EXEと実行に必要なDLLを配置してください。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Project\Tools\Project\Build-Distribution.ps1 `
  -CatalogUrl 'https://downloads.example.com/releases.json' `
  -OutputDirectory '.\Generated\LauncherDistribution\Public-001'

# Visual Studio Developer PowerShellからの同等の入口
msbuild .\Project\Build\NexusDistribution.proj /t:Build `
  /p:CatalogUrl=https://downloads.example.com/releases.json `
  /p:DistributionOutput=C:\Releases\NexusLauncher-001
```

`Setup/launcherSetup.exe`を配布します。通常は.NETを自己完結で同梱し、利用者による.NETインストールは不要です。`-FrameworkDependent`は開発用で、対象PCに.NET 10 Desktop Runtimeが必要になります。ネイティブツールには配布対象と互換性のあるReleaseビルドを使用してください。ランチャーの署名・公開はこのスクリプトでは行いません。

出力先を省略した場合は`Generated/LauncherDistribution/<uuid>`に生成します。既存出力先を指定すると停止し、成果物を上書きしません。payload ZIPは出力先に一時生成し、ソースツリーに置きません。

`-CatalogUrl`を省略すると、利用者が画面で公開URLを指定する確認用パッケージになり、ビルド時に警告します。今回の`Generated/LauncherDistribution/Review-005/Setup/launcherSetup.exe`も未設定構成です。確認用ランチャー単体は同じ出力先の`Launcher/NexusLauncher.exe`から起動できます。実際の配布時は公開URLを指定して再生成してください。

セットアップは`%LOCALAPPDATA%/Programs/NexusEngineLauncher/<installation-id>`へ配置し、スタートメニューとデスクトップにショートカットを作成します。再インストール時には新しいフォルダーへ配置し、以前のファイルを保持します。現在はHKCUのWindowsアプリ一覧へアンインストール操作も登録します。ランチャーの「ランチャーの管理」からも同じ削除処理を実行できます。所有記録にない配置や、追加・変更されたデータのある配置は自動削除しません。エンジンは「インストール済み」から個別に削除でき、プロジェクト・別バージョン・共有履歴を保護します。詳細は[アンインストール手順](LauncherOperations.md#利用者アンインストールする)を参照してください。自動更新は未実装です。

## 検証

```powershell
dotnet build Project/Launcher/Standalone/Launcher.csproj -c Release
dotnet build Project/Launcher/Setup/LauncherSetup.csproj -c Release
dotnet run --project Project/Tests/LauncherDistribution/DistributionTests.csproj -c Release

# 配布ビルド後、アンインストール用スクリプトの内包・文字コードと実CLIの連携も検証
dotnet run --project Project/Tests/LauncherDistribution/DistributionTests.csproj -c Release -- `
  Project/Launcher/Setup/bin/Release/net10.0-windows/win-x64/launcherSetup.dll `
  Generated/Modules/x64/Release/NexusProjectTool/NexusProjectTool.exe

# 第3引数で実v1.0.0パッケージを指定すると、導入・起動・安全なアンインストールまで検証
dotnet run --project Project/Tests/LauncherDistribution/DistributionTests.csproj -c Release -- `
  Project/Launcher/Setup/bin/Release/net10.0-windows/win-x64/launcherSetup.dll `
  Generated/Modules/x64/Release/NexusProjectTool/NexusProjectTool.exe `
  Generated/EngineReleases/v1.0.0/Packages/releases.local.json

powershell -NoProfile -ExecutionPolicy Bypass -File Project/Tests/EngineRelease.Tests.ps1

# 個人Registryに触れず、サンプルデータで全6画面を2サイズでPNG化
.\Generated\LauncherDistribution\v1.0.0-005\Launcher\NexusLauncher.exe --preview .\Generated\LauncherPreview-New
```

2026-11-06の再検証：独立版・セットアップ・実CLIのReleaseビルド、自己完結セットアップ生成、34項目のテストに成功しました。HTTPテストはメモリー上の応答と登録用子プロセスを使い、版別インストール・共存・サイズ/ハッシュ/識別情報不一致・失敗時の一時領域削除を検証しています。別途実CLIで日本語名のプロジェクト作成・重複しない登録・UTF-8出力・不完全inventoryの登録拒否を確認しています。個人Registryは使いません。

pcuseは利用できませんでした。`--preview`による描画は手操作・ファイル選択・DPI間移動・インストーラーのショートカット作成までを保証しません。実際のCloudflareアカウントと公開URLの設定、実SDKのHTTPSダウンロード、GUIの手操作、Git認証、テンプレート作成からEditor起動までの一連の動作は未検証です。配布可能とは判定していません。HTTPSリダイレクトは許可しないため、JSONには最終配布URLを記載してください。現行C++ランチャーの移行UIはこの新ランチャーへは移植していません。

アンインストールの検証では専用のテスト配置・HKCUテストキー・テスト用ショートカットを使い、実際のランチャーや利用者のプロジェクトは削除していません。追加・変更ファイル、ロック、範囲外パス、所有記録の不一致、ジャンクションを拒否し、旧版だけの削除で新版のアプリ登録とショートカットを保持することを確認しています。
