# NexusEngine Webサイト

公開先: https://nexus-engine.nexus-engine-website.workers.dev/

紹介・Launcher setup.exe・1.0.0〜1.2.1の変更点・導入手順・開発ドキュメントをまとめた日本語サイト。1.2.1はMono Runtimeを同梱し、新規C# / Hybridプロジェクトの別途Mono設定を不要にします。UEのビジュアルと導入導線、Blenderのダウンロードとリリース案内を参考にし、独自の配色とNexusEngine自身のPBR描画画像を使っています。

## 従量課金を使わない構成

- サイト: Cloudflare Workers FreeのStatic Assets。HTML/CSS/JS/JSONだけを配信。サーバーコード・R2・KV・D1・有料プランは使いません。
- setup.exeとSDK ZIP: 公開リポジトリ`kase-haruto/NexusEngine`のGitHub Releases。Git LFSは使いません。
- カタログ: `/releases.json`はLauncherのschemaVersion 1形式、`/data/catalog.json`はWeb表示用。

GitHub公式仕様ではRelease添付は1ファイル2GiB未満、1 Release最大1,000添付。Release全体の容量やダウンロード帯域の上限はありません。今回の各ファイルは約73〜167MiBです。CloudflareのStatic Assetsへのリクエストは無料・無制限です。サービス規約と将来の仕様変更は適用されます。

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
.\scripts\Publish-Website.cmd -LauncherSetup "C:\MyProject\NexusEngine\Generated\LauncherDistribution\Website-Public-20261113-002\Setup\launcherSetup.exe"
```

`-LauncherSetup`を省略すると公開URLを埋め込んだsetupを新規生成します。Visual Studio C++ / Windows SDK / .NET SDKが必要。ローカルカタログのパスを設定したReview用setupは公開しません。

公開処理: 実ZIPのSHA-256・サイズを確認 → テスト → GitHubの下書きへアップロード → GitHubのSHA-256と照合 → Release公開 → 静的カタログ生成 → Cloudflareへデプロイ → 全配布ファイルを実際に取得し再照合。同一バッチは再利用し、異なるファイルの上書きを拒否します。

`published-assets.json`は公開URL・サイズ・SHA-256だけを保持。`upload-manifest.json`はPC内の絶対パスを含むためGit管理・配布ZIPから除外します。配布タグのGitHub自動生成Source codeを各旧SDKのソース履歴と誤認しないようRelease説明に注意書きを入れています。

通常の紹介ページ更新も、本番setupを保持するため`Publish-Website.cmd -LauncherSetup ...`を使用してください。

## ランチャーのGitHub対応

GitHubは添付URLをCDNへ302転送します。`Project/Launcher/Standalone/Distribution.cs`でHTTPSのみ・認証情報なし・最大5回の転送を許可しています。HTTP降格と無限転送を拒否し、従来のサイズ・SHA-256・SDK識別検査も維持します。

```powershell
dotnet run --project ..\Tests\LauncherDistribution\DistributionTests.csproj -c Release
dotnet run --project ..\Tests\LauncherDistribution\DistributionTests.csproj -c Release -- --verify-public-download
```

## 開発資料と画像

`Project/Docs/*.md`をHTML化し、危険なHTML・URLを除去して`/docs/`へ出力。現行ソースの資料と公開SDKの機能範囲は異なるため注意書きを表示します。画像`public/render-preview.png`の元は`Generated/PBRGridPreview.png`です。
