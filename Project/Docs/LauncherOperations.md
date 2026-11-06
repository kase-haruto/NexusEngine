# ランチャーの使い方・更新・配布

2026-11-06時点の実装に基づく運用手順です。開発者向けの構成と検証コマンドは[ランチャー配布手順](LauncherDistribution.md)を参照してください。

## 今使うランチャー

デスクトップの **NexusEngine Launcher** から起動します。

現在のショートカットは、次のファイルを直接指しています。

```text
C:\MyProject\NexusEngine\Generated\LauncherDistribution\v1.0.0-002\Launcher\NexusLauncher.exe
```

これは作業フォルダー内のランチャーなので、この`Launcher`フォルダー全体を残してください。EXEだけを移動すると設定・.NETランタイム・CLIなどの付属ファイルが不足します。

PCへインストールして使う場合は、次を実行し「インストール」を押します。

```text
C:\MyProject\NexusEngine\Generated\LauncherDistribution\v1.0.0-005\Setup\launcherSetup.exe
```

セットアップはランチャー一式をユーザーのアプリ領域へ配置し、デスクトップとスタートメニューの`NexusEngine Launcher`ショートカットをその配置先へ設定します。今回作成した同名のデスクトップショートカットも、その際にインストール先を指すものへ置き換わります。

現在のv1.0.0一覧とZIPはローカル配置です。ランチャー本体をインストールした後も、次の`Packages`フォルダーは参照するので残してください。

```text
Generated/EngineReleases/v1.0.0/Packages/
  releases.local.json
  nexus-1.0.0-Release.zip
```

## 更新対象の違い

| 更新する対象 | 配布するもの | 利用者の操作 | 既存データへの影響 |
| --- | --- | --- | --- |
| エンジン | 新版のSDK ZIPとバージョン一覧JSON | 一覧を読み直して新版をインストール | 旧版と共存。既存プロジェクトの要求版は自動で変わらない |
| ランチャー本体 | 新しくビルドした`launcherSetup.exe` | ランチャーを終了し、新セットアップを実行 | プロジェクト・エンジンの共有Registryと保存先を継続利用 |
| 既存プロジェクトの使用エンジン | プロジェクトごとの移行操作 | 互換性をレビューして明示的に適用・再ビルド | `.nxproject`のEngine要求が変わる |

エンジンの`1.0.0`などの版と、ランチャーのビルド識別子は別に管理します。ランチャーのUIだけを直した際に、エンジンZIPを再発行する必要はありません。逆に、カタログの形式やCLIとの互換性を保ってエンジンを追加する場合は、ランチャーの再配布は不要です。

現時点では両方とも**手動更新**です。起動時の新版チェック、更新通知、バックグラウンドでの更新、差分パッチはありません。アンインストールは下記の手順で行えます。

## 利用者：エンジンを更新する

### 現在のローカル配布

1. 配布者から新版のZIPと`releases.local.json`を受け取り、両方を同じフォルダーに置きます。ZIP名や一覧内の相対パスを変えずに保存します。
2. 「エンジンを入手」→「ローカルの一覧を選択…」で、新しい一覧を選びます。
3. 新版のバージョン、変更点、配布元、保存先を確認し「選択したバージョンをインストール」を押します。
4. サイズ・SHA-256・エンジン識別情報・inventory検証後に登録され、「インストール済み」に表示されます。
5. 新規プロジェクトは「使用するエンジン」で新版を選んで作成します。

例：v1.0.0とv1.0.1は保存先の下に別々のフォルダーとして配置されます。同じ保存先の既存版を上書きすることはできません。

```text
Versions/
  nexus-1.0.0-Release/
  nexus-1.0.1-Release/
```

ローカル一覧を手動で選んだ履歴は現状では保存しません。再起動時は`launcher.json`の`catalogFile`が読み込まれます。継続運用では、設定済みの一覧の場所を固定してそこへ新版の項目を追加するか、起動後に新しい一覧を選び直してください。一覧を別の場所へ移すときは、その一覧が参照するZIPも一緒に配置します。

### Cloudflareへ公開した後

1. 「配布元のバージョン一覧URL」に実際の`releases.json`のHTTPS URLを設定します。
2. 「バージョン一覧を取得」を押します。
3. 新版を選んでインストールします。残りの検証・配置はローカル版と共通です。

オンライン一覧はボタン操作で再取得します。URLを保存しても、新版がバックグラウンドで自動導入されることはありません。HTTPSリダイレクトは拒否するので、短縮URLやダウンロード確認ページではなく、JSON・ZIPそのものを返す最終URLが必要です。

### 既存プロジェクトを新版で使う

新版をインストールしただけでは既存の`.nxproject`は変更されません。独立した.NET版ランチャーには、既存プロジェクトのエンジン移行UIはまだありません。

プロジェクトをバックアップまたはGitへ記録し、Editor/Runtimeを閉じます。そのうえで、SDK同梱のC++版`Bin/NexusLauncher.exe`の`Change engine...`、または`NexusProjectTool`の移行CLIでレビュー・適用します。操作と復旧方法は[エンジン移行](ProjectMigration.md)を参照してください。

CLI例では、計画保存先の親フォルダーを先に作成してください。`migration-apply`は計画を読んで互換性と変更内容を確認した後に実行します。

```powershell
$tool = '.\Generated\LauncherDistribution\v1.0.0-002\Launcher\Tools\NexusProjectTool.exe'
& $tool migration-plan 'D:\Games\MyGame\MyGame.nxproject' `
  'D:\NexusEngines\nexus-1.0.1-Release\NexusEngine.nxengine' `
  'D:\Games\MyGame\Build\EngineChange.json'

# 計画の確認後、明示的に適用する
& $tool migration-apply 'D:\Games\MyGame\Build\EngineChange.json' --accept-api-risk
```

適用後は新版でGameEditorを再ビルドして開き、プロジェクトの動作を確認します。旧Engineへの復旧は生成された`Recovery.json`を使います。移行機能が戻すのはEngine要求で、移行後に行ったC++コードやアセットの編集全体ではありません。

## 利用者：ランチャーを更新する

1. 新しい`launcherSetup.exe`を受け取ります。
2. 起動中のランチャーを終了します。
3. 新セットアップを実行し「インストール」を押します。
4. 新しいランチャーが起動します。以後は更新されたデスクトップまたはスタートメニューのショートカットを使います。

セットアップは毎回、新しいインストールIDのフォルダーを作成します。

```text
%LOCALAPPDATA%/Programs/NexusEngineLauncher/
  <以前のinstallation-id>/
  <新しいinstallation-id>/
```

旧版はそのまま残ります。エンジンZIPを取り直したり、プロジェクトを作り直したりする必要はありません。新しいランチャーでも同じユーザーの次のRegistryを使います。

```text
%LOCALAPPDATA%/NexusEngine/Engines/Registry.json
%LOCALAPPDATA%/NexusEngine/Projects/RecentProjects.json
```

問題があれば旧フォルダーの`NexusLauncher.exe`を直接起動して戻せます。新しいセットアップはWindows「インストールされているアプリ」に現在のランチャーのアンインストール操作を登録します。以前の版のフォルダーをまとめて削除する機能ではありません。

今回は作業フォルダーを指すショートカットを作成しました。セットアップを使わずに、次のビルドフォルダーのEXEを直接使う場合は、ショートカットの「リンク先」と「作業フォルダー」も新しいフォルダーへ変更します。補助スクリプト`Create-LauncherShortcut.ps1`は既存の別のリンクを上書きせず拒否するので、新規作成時には別名の`-Destination`を指定できます。

## 利用者：アンインストールする

### ランチャー本体

アンインストール対応のセットアップは次のファイルです。旧セットアップで入れた版や作業フォルダーから直接起動している版には、Windowsのアンインストール登録がないため、一度このセットアップでインストールしてください。

```text
Generated/LauncherDistribution/v1.0.0-005/Setup/launcherSetup.exe
```

セットアップから導入した版は、次のどちらかで削除します。

1. ランチャーを終了し、Windows「設定 → アプリ → インストールされているアプリ → NexusEngine Launcher → アンインストール」を選ぶ。
2. ランチャーの「ランチャーの管理 → ランチャーをアンインストール」を選ぶ。

どちらも確認画面に削除対象のフォルダーを表示します。承認後、ランチャーが終了してから削除処理が動作し、完了またはエラーを表示します。

- そのインストールIDのランチャー配置だけを削除します。旧版を一括削除しません。
- 対象のEXEを指す標準ショートカットと、対象配置を指すアプリ登録を削除します。新版や別の配置へ切り替わったリンク・登録は残します。
- エンジン、プロジェクト、共有Registry、ランチャー設定とエンジンの所有記録は保持します。
- 開発用・配布物のフォルダーはこの機能で削除しません。

セットアップは`.nexus-launcher-install.json`に所有ファイルとハッシュを記録します。削除時には記録・パス・追加ファイル・変更ファイル・リンク・ファイルロックを確認します。追加したデータや変更された設定がある場合は削除を拒否するので、必要なデータを別の場所へ保管してから配置を確認してください。インストールとアンインストールの同時実行も防止します。

### エンジンの特定バージョン

「インストール済み」で対象を選び、次の操作を使います。EditorとRuntimeを終了してから実行してください。

| 操作 | 動作 |
| --- | --- |
| 選択したエンジンをアンインストール | 対象版のファイルとEngine Registryの登録を削除 |
| 選択したエンジンの登録のみ解除 | Engine Registryの登録だけを削除し、実ファイルを保持 |

削除の確認画面には版・構成・フォルダーと、その版を要求する登録済みプロジェクト数を表示します。プロジェクト本体は削除しませんが、対象版を使うプロジェクトは再導入またはエンジン移行まで起動できなくなります。未登録のプロジェクトは件数に含まれません。

新しいランチャーで導入したエンジンは、`%LOCALAPPDATA%/NexusEngine/Launcher/Installations`の所有記録と照合します。旧ランチャーで導入した版は、専用の`<id>-<version>-<configuration>`フォルダーであることと、ネイティブツールによるSDK全体のinventory検証を条件に対応します。

Developmentエンジンのソースや、任意の名前のフォルダーへ手動登録したSDKは自動削除しません。「登録のみ解除」を使い、実ファイルは別途管理してください。追加・変更されたファイルやジャンクション、使用中・読み取り専用のファイルがあれば削除を拒否します。登録解除後の削除中に権限変更などが発生した場合は、一部のファイルが残ることがあるため、画面に表示された配置を確認します。

## 配布者：エンジンの新版を作る

以下はリポジトリルートのVisual Studio Developer PowerShellで行う例です。`1.0.1`は次版の例であり、現時点では未作成です。変更点ファイルを用意し、既存の版とは別の出力先を使います。

### 1. Installed SDKを生成する

通常のDevelopment manifestを作るために、ReleaseのCLIと汎用ホストをビルドします。

```powershell
$ErrorActionPreference = 'Stop'
$repository = (Get-Location).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild `
  -find 'MSBuild\**\Bin\amd64\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'Visual Studio MSBuild not found.' }
foreach ($target in @('NexusProjectTool','NexusEditor','NexusRuntimeHost')) {
  & $msbuild ".\Project\Build\$target.vcxproj" /m:1 /t:Build /p:Configuration=Release /p:Platform=x64 /v:minimal
  if ($LASTEXITCODE -ne 0) { throw "Build failed: $target" }
}

$sourceRegistry = Join-Path $repository 'Generated/ReleaseWork/SourceEngines.json'
$tool = '.\Generated\Modules\x64\Release\NexusProjectTool\NexusProjectTool.exe'
& $tool engine-dev $repository Release $sourceRegistry
if ($LASTEXITCODE -ne 0) { throw 'Development manifest creation failed.' }

powershell -NoProfile -ExecutionPolicy Bypass -File .\Project\Tools\Engine\Publish-Engine.ps1 `
  -SourceManifest '.\NexusEngine.Release.nxengine' `
  -Destination '.\Generated\EngineReleases\v1.0.1\Engine' `
  -EngineId nexus -Version 1.0.1
if ($LASTEXITCODE -ne 0) { throw 'Installed SDK publishing failed.' }
```

`engine-dev`はリポジトリ直下のDevelopment manifestを生成・再生成し、ここでは個人の既定Registryではなく専用のSourceRegistryへ登録します。既存の同名Development manifestを別用途で使用している場合は、その内容を確認してから実行してください。Installed SDKの既存出力先は上書きしません。

発行処理はSDKライブラリ・資源・テンプレートを配置し、配布用の版情報を持ったEditor/Runtimeをビルドして起動チェックを行い、ファイルinventoryを生成します。**既存SDKのmanifestのversionだけを編集して新版にしないでください。** 実行ファイルの識別情報とinventoryも一致する必要があります。

### 2. ZIPと一覧項目を生成する

ローカル配布用の例です。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Project\Tools\Project\New-EngineRelease.ps1 `
  -EngineDirectory '.\Generated\EngineReleases\v1.0.1\Engine' `
  -ChangesFile '.\Project\Docs\Releases\v1.0.1.md' `
  -OutputDirectory '.\Generated\EngineReleases\v1.0.1\Packages'
```

公開用には同じスクリプトへ`-DownloadUrl 'https://実際の配布ドメイン/engines/nexus-1.0.1-Release.zip'`を指定し、別の新規出力先を使います。ZIPに対応したSHA-256とサイズを持つ`releases.json`が生成されます。

このスクリプトが作る一覧には**今回の1版だけ**が入ります。運用する一覧を丸ごと置き換えて旧版の項目を消さず、新項目を既存の`releases`配列へ追加します。ローカルの一覧をまとめる場合は、一覧が置かれたフォルダーからの相対パスで全版のZIPを参照できるように配置します。ZIPを作り直したらハッシュとサイズも変わるため、対応する一覧を必ず使ってください。

### 3. 導入を検証して配布する

空の保存先へZIPからインストールし、inventory検証、テンプレートからのプロジェクト作成、GameEditorのビルド・起動と主要操作を確認します。現在の実ZIP用テストはv1.0.0を対象にしているため、別の版を検証する場合はテストの対象バージョンも更新してください。

旧版ZIPと既存ユーザーのEngine配置を上書きせず、新版を別フォルダー・別URLで公開します。

## 配布者：Cloudflareで公開する

現在はローカル配布までで、実アカウントへの公開は未実施です。ZIP・JSONの公開配布先にはR2の公開バケットとHTTPSドメインを使います。`r2.dev`は非本番用で、独自ドメインによる公開とキャッシュの設定方法は[Cloudflare公式の公開バケット資料](https://developers.cloudflare.com/r2/buckets/public-buckets/)を参照してください。

配置例のドメインは説明用です。

```text
https://downloads.example.com/
  releases.json
  engines/nexus-1.0.0-Release.zip
  engines/nexus-1.0.1-Release.zip
  launcher/<build-id>/launcherSetup.exe
```

公開の順序は次のとおりです。

1. 新版ZIPを版別のURLへアップロードします。
2. そのHTTPS URLからZIPを取得でき、サイズ・ハッシュが一致することを確認します。
3. 新旧両方の項目を持つ`releases.json`を最後に公開します。
4. ランチャーで一覧を再取得し、公開URLからの実インストールを確認します。

R2にはダッシュボードやS3互換ツールからアップロードできます。具体的なアップロード方法は[公式手順](https://developers.cloudflare.com/r2/objects/upload-objects/)を参照してください。このリポジトリの生成スクリプト自体にアップロード処理はありません。

ZIPのURLは版ごとに固定して既存版を差し替えず、一覧JSONは新版が反映されるキャッシュ設定で運用します。R2の管理用キーを利用者向けのランチャーやセットアップへ入れる必要はありません。

## 配布者：ランチャーを更新して配布する

ランチャーのUI・処理や、同梱CLIを更新したときは、次のように新しい出力先へ再生成します。実際の公開HTTPS URLに置き換えてください。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Project\Tools\Project\Build-Distribution.ps1 `
  -CatalogUrl 'https://downloads.example.com/releases.json' `
  -OutputDirectory '.\Generated\LauncherDistribution\Public-Next'
```

通常はRelease版のCLIを自動ビルドし、.NETランタイムも含むセットアップを生成します。エンジン本体は含みません。ビルド後に`Setup/launcherSetup.exe`を配布し、利用者には「既存ランチャーを終了して新セットアップを実行」と案内します。

ローカル確認用を作る場合は`-CatalogUrl`の代わりに`-CatalogFile <releases.local.jsonのパス>`を使います。現在の確認用セットアップには作業PCの一覧の絶対パスが入っているので、他のPCへ配るときには、そのままオンライン配布用として使わず公開URLを指定して再生成してください。

取得したオンライン一覧のURLは`%LOCALAPPDATA%/NexusEngine/Launcher/catalog.txt`へ保存され、次回起動時にはパッケージの既定URLより優先されます。配布元URLを変えた際は、利用者にも画面で新URLを入力して取得し直すよう案内します。Engineの一覧JSONはランチャー更新用のmanifestではなく、これを書き換えてもランチャー本体は更新されません。

## 現在の確認範囲

| 確認済み | 未確認・未実装 |
| --- | --- |
| v1.0.0 ZIP生成・実ZIPの導入・inventory検証 | 実Cloudflare公開とそこからの導入は未確認 |
| 新規Game作成・GameEditorビルド・`--check-game`起動 | 通常のGUI/GPU操作全体とGit認証は未確認 |
| ランチャーとセットアップのRelease生成、内包ファイル確認 | セットアップを操作しての再インストール一式は未確認 |
| 2サイズの画面描画と重なり検査 | 自動更新・更新通知は未実装 |
| デスクトップショートカット作成とリンク先確認 | 独立ランチャーのエンジン移行UIは未実装 |
| 隔離した配置と専用HKCUテストキーでのアプリ登録・削除、旧版削除時の新版保持 | 実ユーザーのWindows設定画面から行うアンインストール操作全体は未確認 |
| 実v1.0.0の削除、追加・変更・ロック・リンク・パス不一致の拒否 | 旧インストール配置の一括削除は未実装 |

セットアップの署名も生成スクリプトでは行いません。公開前には、公開URLを使うセットアップで実導入・エンジン取得・プロジェクト起動までを対象PCで確認します。
