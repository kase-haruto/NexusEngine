# AI用 NexusEngineリリース手順

`AGENTS.md`と`release-policy.json`を入口とする。ユーザーがエンジンの公開を依頼したとき、この手順を実行する。長いプロンプトや公開先・保持方針の再入力を求めない。通常の開発やこの指示ファイルの編集だけでは公開を開始しない。

## 1. 現状と次のバージョンを確定する

1. 作業場所がこのリポジトリであること、追加のAGENTS指示、`git status`、差分、ログを確認。既存変更の削除・リセット・別作業の混入を避ける。
2. `Project/Website/deployment-status.json`、`published-assets.json`、実サイトの`/releases.json`、既存Release資料、ローカル統合カタログを確認する。状態ファイルだけを公開の証拠にしない。
3. 指定されたバージョンを採用する。省略時は実公開の最新版から変更内容に応じてSemVerを選び、着手時に番号を伝える。通常の選択のために作業を止めない。既存版を差し替えず、衝突時は新番号を選ぶ。
4. 直近の公開SDKとの差分を調査。配布バッチの`distribution-*`タグが各旧SDKのビルド元を正確に示すとは仮定しない。既存の検証記録・リリースノート・Gitログ・未コミット差分も突き合わせ、確認できない由来を推測で断定しない。

## 2. リリースノートと検証記録を作る

- `Project/Docs/Release-<version>.md`へ、新機能、修正、互換性、移行・再ビルドの必要性、依存環境、既知の問題と未検証範囲を書く。
- `Project/Docs/Release-<version>-Verification.md`へ、ソースの状態、ビルド条件、実行した検査と結果、SDK識別、配布物のサイズ・SHA-256、公開先を残す。
- 実装済み・ビルド済み・動作確認済みを区別する。検証失敗を成功と記載しない。コア機能に関する必須検証が失敗した配布物は公開しない。

## 3. 配布用Installed SDKを発行する

`Project/Tools/Engine/Publish-Engine.ps1`の現在の引数と、既存のRelease資料を読む。`NexusEngine.PackageSource.nxengine`は候補であり、存在・Release構成・実際のpathsとビルド入力を確認する。開発環境依存のmanifestがなければ、リポジトリ構成を調べて作成する。

```powershell
# リポジトリ直下で実行。<version>などは実値へ置き換える。
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Project/Tools/Engine/Publish-Engine.ps1 -SourceManifest NexusEngine.PackageSource.nxengine -Destination Generated/EngineReleases/v<version>/Engine -EngineId nexus -Version <version>
```

既存の出力先を上書きしない。中断済みの出力がある場合は由来を確認し、未使用の新規作業先で再生成する。ソースフォルダーをそのまま配布ZIPにしない。

変更に対応したテストに加え、Installed SDKの識別・inventory・Host起動と必要な描画・テンプレートのビルドなどを確認する。C++/C#/Hybrid、入力・物理・Audio・Reloadなどは今回の変更範囲に応じて検証する。実機が必要で実行できない検査は、その制約を公開資料へ明記する。

## 4. ZIPと統合カタログを追加する

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Project/Tools/Project/New-EngineRelease.ps1 -EngineDirectory Generated/EngineReleases/v<version>/Engine -ChangesFile Project/Docs/Release-<version>.md -OutputDirectory Generated/EngineReleases/v<version>/Packages
```

このスクリプトは単一版のカタログを作る。生成されたエントリのurlを、統合カタログのルートからの相対パス（例：`v<version>/Packages/nexus-<version>-Release.zip`）へ直して、`Generated/EngineReleases/releases.local.json`へ新規追加する。

旧エントリと旧ZIPを保持する。`id/version/configuration`の重複と同一版の内容差し替えを拒否し、最新順に並べる。schemaVersion 1、実ファイルのサイズ・SHA-256・Engine内のバージョンが一致することを確認する。

## 5. Launcher setupを選ぶ

- エンジンのみの更新では、公開状態・`published-assets.json`・`Generated/LauncherDistribution`から正常な本番setupを特定して再利用する。特定の日付や`Website-Public-20261113-002`を永続的な固定値にしない。
- ランチャーや同梱ProjectTool、導入設定、公開URLを変更した場合は、`Build-Distribution.ps1`で新規生成する。`-CatalogUrl`は設定のsiteOriginに`/releases.json`を付けたURL。PC内のCatalogFileを設定したReview版を公開しない。
- GitHub ReleasesはCDNへ転送される。HTTPS限定・上限5回の転送、サイズ・SHA-256検査を維持する。
- 再利用するsetupが必要な新SDK操作と互換性を持つことを確認する。手元に必要な配布物がなければ、公開済みファイルを取得してSHA-256を確認するか、現在のソースから再生成する。

## 6. GitHubとサイトを公開する

`Project/Website/README.md`と現在の公開スクリプトを確認して、同ディレクトリから実行する。

```powershell
.\scripts\Publish-Website.cmd -LauncherSetup "<確認済みの本番launcherSetup.exeの絶対パス>"
```

このスクリプトはローカルカタログを入力に、配布物の検証、GitHubの下書きReleaseへのアップロード、SHA照合、Release公開、静的カタログの再生成、Cloudflareへのデプロイ、公開物の取得検証を実施する。ソースからSDKを発行する工程と統合カタログへの追加は、このコマンドの前に行う。

新しい版への対応に必要な小さなスクリプト修正も公開作業に含める。とくに`github-releases.mjs`のRelease説明に以前の版一覧が固定で残っていないか確認し、今回の実カタログから生成する。Webの最新情報・開発環境・制約に古い版固有の説明が残る場合も実装内容に合わせて更新する。既存のUE/Blenderを参考にしたデザインは維持する。

バッチ単位で複数版を添付する現在の実装では、旧版ZIPが新しいReleaseにも添付される場合がある。旧Releaseを削除せず、旧版のダウンロードと整合性を維持する。GitHubのSource codeアーカイブを各旧SDKの正確なビルド元として説明しない。

CloudflareはStatic Assetsのみ、ファイル本体はGitHub Releases。R2、Git LFS、有料Worker・有料プラン、独自ドメイン購入、従量課金の契約は追加しない。制限が変わった場合は無断で課金方式に変更しない。

有効な認証を再利用する。認証切れ、本人の契約同意・アカウント操作が必要な場合だけ具体的な操作を依頼し、必要のない再確認を繰り返さない。認証情報をソース・設定・ログ・配布物へ保存しない。

## 7. 公開後の確認と記録

- 実サイトで新しい版・変更点・SDKとsetupのリンクを確認。
- `/releases.json`と`/data/catalog.json`の新旧一覧・URL・SHA・サイズが一致することを確認。
- 公開URLから配布ファイルを取得して検証。GitHub APIの成功だけで完了としない。
- 必要に応じてランチャー実コードからカタログ取得とSDKダウンロードを確認。既存テストの版数や`1.1.1`の固定値が、新版検証の妨げになっていれば実公開の最新版から選ぶよう修正する。
- `deployment-status.json`、公開先README、今回の検証記録を実際の結果に更新。固定3版・固定4ファイルを次回にも当てはめない。
- ユーザーには公開バージョン、サイトURL、主な変更、検証結果と残る制約を簡潔に報告する。旧版を維持し、従量課金なしの構成を継続したことも伝える。

## 短い依頼の例

「新しいバージョンを公開して」／「1.2.0を公開して」だけで、この作業全体を実施する。ユーザーが版を指定していない場合は手順1の基準を使う。ここに書かれた例を、自動的な1.2.0公開の指示と解釈しない。
