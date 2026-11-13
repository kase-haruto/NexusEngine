# NexusEngine 1.2.1 配布検証

1.2.0の実公開タグ `distribution-2f2d369c9882`（ソース `7be5a81`）を基準に、Mono同梱と自動検出の修正を適用する。Windows x64 / Release / v145 / MTで新規Installed SDKを発行する。

公式Mono 6.12.0.206 x64 MSIのMicrosoft署名はValid。SHA-256は `4125F57D97CFA88257915EDC969E913DE198CD8E22396A29849037479A0AC368` と一致。システムへのインストールやPATH変更は行わず、作業用フォルダーへ展開した。

`InstalledMono.Tests.ps1` で同梱Monoの検出、明示property / 環境変数の優先、配置済みRuntimeへのfallback、Runtime / GAC / licenseの配置、不完全なSDKの発行・ZIP生成拒否を確認。既存 `EngineRelease.Tests.ps1` の8項目も成功。

## 配布前の実行結果

ログは `Generated/Release121`、検証Projectは `C:/MyProject/NexusEngine/Generated/Release121Checks` に保存した。個人のEngine Registryや既存Projectの関連付けは変更していない。

| 検査 | 結果 |
|---|---|
| Release ProjectTool / RuntimeHost / SDK libraries / 配布Host | 成功。`NexusProjectTool.build.log`、`NexusRuntimeHost.build.log`、`Publish-Engine.retry.log` |
| 公式Mono Runtime / BCL / GAC / config / notices | SDKへ784ファイルを配置。システムインストール・PATH変更なし |
| Installed SDK inventory | 1,841ファイルを専用Registryへの登録時に照合成功 |
| Hybrid / C# / Nativeテンプレート | 新規作成し、各Editor / RuntimeのReleaseビルドとstamp検証が成功 |
| 同梱Monoのみでの起動 | `NEXUS_MONO_ROOT` / `NexusMonoRoot`を除去して上記6構成のCPU起動と各8 GPU frame・正常終了に成功 |
| Launcher回帰 | Windows PowerShell用のモジュール検索pathを設定し、62項目成功。`LauncherTests.clean-env.log` |
| Web配布テスト | 4項目成功。静的assets、HTML除去、配布URLと整合性を確認 |
| ZIP | 165,928,742 bytes。SHA-256: `099e37f3a00a1c654a39f6bdead9588554021b298dd07fae9d6e34876476ad14` |

SDK buildIdは `2581922f-55c2-49fb-a2ca-af54c3870e6d`。発行時にはMono GACの長い名前でWindows PowerShell 5.1のMAX_PATH制約が発生し、inventory hashと所有stagingのcleanupに拡張長pathを使う修正後、発行・Host起動が成功した。

既存公開4版と本番Launcher setupを公開URLから取得し、全て既存カタログのサイズとSHA-256に一致。Launcherはエンジンのみの更新のため再利用した。

## 公開結果

GitHub Releases: https://github.com/kase-haruto/NexusEngine/releases/tag/distribution-93667918dc43

配布タグは修正ソース `e81007f` を指す。新旧SDK5版と既存本番Launcher setupを添付し、GitHub APIのサイズ・SHA-256照合後に公開した。1.2.1は公開ダウンロードURLから実際に取得し、165,928,742 bytesと上記SHA-256に一致。実ZIPの展開と1,841ファイルのinventory登録も成功。既存のReleaseやファイルは削除・置換していない。

Cloudflareの本人認証を完了し、既存Workers Freeの静的サイトへ公開した。公開URLは https://nexus-engine.nexus-engine-website.workers.dev/ 。`/releases.json` と `/data/catalog.json` のSDK5版について、版・構成・URL・サイズ・SHA-256の一致を確認。SDK5版と本番Launcher setupの全6ファイルを公開URLから再取得し、サイズ・SHA-256を照合した。`Publish-Website.authenticated.log` に結果を保存した。既存の旧版Releaseを保持し、R2・有料プラン・従量課金サービスは追加していない。

全GPU・DPI・解像度・周辺機器での手動操作、開発SDKが一切ないクリーンPCでの検証は行っていない。
