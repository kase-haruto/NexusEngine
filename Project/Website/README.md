# NexusEngine Webサイト

公開先: https://nexus-engine.nexus-engine-website.workers.dev/

紹介・Launcher setup.exe・SDK 1.0.0〜1.4.2とLauncher 1.3.2の変更点・導入手順・開発ドキュメントをまとめた日本語サイト。UEのビジュアルと導入導線、Blenderのダウンロードとリリース案内を参考にし、独自の配色とNexusEngine自身のPBR描画画像を使っています。

## 従量課金を使わない構成

- サイト: Cloudflare Workers FreeのStatic Assets。HTML/CSS/JS/JSONだけを配信。サーバーコード・R2・KV・D1・有料プランは使いません。
- setup.exeとSDK ZIP: 公開リポジトリ`kase-haruto/NexusEngine`のGitHub Releases。Git LFSは使いません。
- カタログ: `/releases.json`はLauncherのschemaVersion 1形式、`/data/catalog.json`はWeb表示用。

GitHub公式仕様ではRelease添付は1ファイル2GiB未満、1 Release最大1,000添付。Release全体の容量やダウンロード帯域の上限はありません。各ファイルのサイズは公開カタログに記載します。CloudflareのStatic Assetsへのリクエストは無料・無制限です。サービス規約と将来の仕様変更は適用されます。

公式: https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases / https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/

R2の契約・支払い登録・有効化は行っていません。

## ローカル確認

```powershell
npm.cmd ci
npm.cmd run build
npm.cmd test
npm.cmd run dev
```

`http://127.0.0.1:8787`で確認。エンジンリポジトリ内のこのディレクトリで実行してください。`Generated/EngineReleases/releases.local.json`と実ZIPが必要です。

## 配布と公開

Cloudflare CLIとGit Credential ManagerのGitHub認証を使用。認証情報をソースやWebへ埋め込みません。

```powershell
.\scripts\Publish-Website.cmd -LauncherSetup "C:\MyProject\NexusEngine\Generated\LauncherDistribution\Launcher-1.3.2\Setup\launcherSetup.exe"
```

`-LauncherSetup`を省略すると公開URLを埋め込んだsetupを新規生成します。Visual Studio C++ / Windows SDK / .NET SDKが必要。ローカルカタログのパスを設定したReview用setupは公開しません。

公開処理: 実ZIPのSHA-256・サイズを確認 → テスト → GitHubの下書きへアップロード → GitHubのSHA-256と照合 → Release公開 → 静的カタログ生成 → Cloudflareへデプロイ → 全SDKとsetupを実際に取得し再照合。同一バッチは再利用し、異なるファイルの上書きを拒否します。

`published-assets.json`は公開URL・サイズ・SHA-256だけを保持。`upload-manifest.json`はPC内の絶対パスを含むためGit管理・配布ZIPから除外します。配布タグのGitHub自動生成Source codeを各旧SDKのソース履歴と誤認しないようRelease説明に注意書きを入れています。

通常の紹介ページ更新も、本番setupを保持するため`Publish-Website.cmd -LauncherSetup ...`を使用してください。

2026-11-27 に SDK 1.7.0 を GitHub Releases の最新版として公開し、公開 ZIP のサイズと SHA-256 を確認しました。旧版を含む Web／Launcher カタログを準備済みです。Cloudflare 認証後に本番へ反映します。[1.7.0 配布検証](../Docs/Release-1.7.0-Verification.md)

## ランチャーのGitHub対応

GitHubは添付URLをCDNへ302転送します。`Project/Launcher/Standalone/Distribution.cs`でHTTPSのみ・認証情報なし・最大5回の転送を許可しています。HTTP降格と無限転送を拒否し、従来のサイズ・SHA-256・SDK識別検査も維持します。

```powershell
dotnet run --project ..\Tests\LauncherDistribution\DistributionTests.csproj -c Release
dotnet run --project ..\Tests\LauncherDistribution\DistributionTests.csproj -c Release -- --verify-public-download
```

## 開発資料と画像

`Project/Docs/*.md`をHTML化し、危険なHTML・URLを除去して`/docs/`へ出力。現行ソースの資料と公開SDKの機能範囲は異なるため注意書きを表示します。画像`public/render-preview.png`の元は`Generated/PBRGridPreview.png`です。


## 2026-11-15 公開記録

SDK 1.3.0とLauncher 1.1.0を公開。SDK 6版と新setupの計7ファイルを公開URLから取得し、サイズ・SHA-256を照合済み。従来のSDKと旧setupを保持しています。詳細は[1.3.0配布検証](../Docs/Release-1.3.0-Verification.md)。

## 2026-11-16 公開記録

プロジェクト移行機能を追加したLauncher 1.2.0を公開。エンジンSDKの版数・内容は維持しています。旧ランチャー1.1.0の実コードから更新一覧・変更点・新Setupの取得とSHA-256検査が成功。SDK 6版と新Setupの計7ファイルも公開URLから照合済みです。旧ランチャー2版と既存SDKを保持しています。詳細は[Launcher 1.2.0配布検証](../Docs/Launcher-Release-1.2.0-Verification.md)。

## 2026-11-16 Launcher 1.3.0 公開記録

Empty / Basic Levelの画像付きTemplateカードと拡大Previewを追加したLauncher 1.3.0を公開しました。SDKは再発行していません。公開済みSDK 6版と新Setupの計7ファイルを取得し、サイズ・SHA-256を照合済みです。旧Launcher 1.2.0の実コードでも更新一覧と変更点、新Setupの取得・検証が成功しました。旧Launcher 3版と既存SDKを保持しています。詳細は[Launcher 1.3.0配布検証](../Docs/Launcher-Release-1.3.0-Verification.md)を参照してください。
## 2026-11-17 SDK 1.4.0 公開記録

アニメーション再設計・汎用Curve Editorを追加したSDK 1.4.0を[GitHub Releases](https://github.com/kase-haruto/NexusEngine/releases/tag/distribution-3c1098d89213)へ公開しました。サイトとWeb/Launcher catalogも反映済みです。SDK 7版と既存Launcher 1.3.0 setup、計8ファイルを公開URLから取得し、サイズ・SHA-256を照合しました。Launcher実コードでも公開7版の一覧と最新1.4.0の取得・検証が成功しています。旧SDK・旧Launcherを保持し、既存Projectの関連付けは変更していません。詳細は[1.4.0配布検証](../Docs/Release-1.4.0-Verification.md)。


## 2026-11-18 SDK 1.4.1 / Launcher 1.3.2 公開記録

SDK1.4.1とLauncher1.3.2を[GitHub Releases](https://github.com/kase-haruto/NexusEngine/releases/tag/distribution-57972387f1a5)へ公開しました。Retarget、Root Motion、Layer、State Machine、Timeline連携、morph描画とCurve編集・別Runtime previewを追加しています。

SDK8版と最新Setupの計9ファイルを実公開URLから取得し、サイズ・SHA-256を照合済み。Launcher実コードでSDK1.4.1、旧Launcher1.3.0 assemblyで更新一覧6版とSetup1.3.2の取得・hash検証も成功しました。旧SDK7版・旧公開asset11件の内容は維持しています。

途中でLauncher1.3.1の変更履歴JSON形式不備を公開後検査で検出したため、生成処理とcatalogを修正しSetupを差し替えず1.3.2を追加しました。Engine1.4.1は同じ検証済みZIPです。詳細は[1.4.1検証記録](../Docs/Release-1.4.1-Verification.md)。

## 2026-11-19 SDK 1.4.2 公開記録

SDK1.4.2を[GitHub Releases](https://github.com/kase-haruto/NexusEngine/releases/tag/distribution-45f2b2eceb38)と本番サイトへ公開しました。Component Browser、Engine / Native / C#共通の解除とUndo、Input Map、右クリックからのC# / C++作成、UV倍率・offset、Component開発ガイドを追加しました。

SDK9版と再利用Launcher1.3.2の計10ファイルを公開URLから取得し、サイズ・SHA-256照合が成功。本番Launcher1.3.2 assemblyからも公開一覧9版の読込とSDK1.4.2取得・hash検証が成功しました。旧8版を維持しています。詳細は[1.4.2検証記録](../Docs/Release-1.4.2-Verification.md)。無料Static Assets + GitHub Releases構成を継続しています。
