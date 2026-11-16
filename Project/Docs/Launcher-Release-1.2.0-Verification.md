# NexusEngine Launcher 1.2.0 配布検証

2026-11-16 / Windows x64 / Release / .NET SDK 10.0.402 / Visual Studio C++ v145。

## ソースと配布物

- ソース基準は`f8e94d16e6f142bec8bee7ef05b9f6cf143c9713`と、作業ツリーのランチャー移行機能・バージョン更新・変更点配信の追加。既存の未コミット変更を維持し、GitHubの自動Source codeアーカイブとビルド時の作業ツリーが同一とは扱わない。
- ランチャーと同梱NexusProjectToolをReleaseでビルド。自己完結型Windows x64のsetupを新規生成した。
- 出力先: `Generated/LauncherDistribution/Launcher-1.2.0`。
- SetupのProductVersionとランチャーDLLのProductVersion、`launcher.json`の版数はいずれも1.2.0。
- Setup: 177,051,814 bytes。
- SHA-256: `73fdfd33950ac096ec433b05ff26bfb371acd6c253a069144e8fe1c70ae04e0c`。
- カタログは本番HTTPSの`/releases.json`と`/launcher-releases.json`。PC内のCatalogFileは設定していない。
- SDK自体は再発行しない。公開済み6版のZIPを既存サイズ・SHA-256に照合して再利用する。

## 配布前の検証結果

| 検査 | 結果 |
| --- | --- |
| ランチャー・Setup・同梱Tool | Releaseビルド成功。SetupとDLLのバージョン一致 |
| 配布用DLLと同梱ToolでのUI検証 | 明示的な移行先選択、実Toolによる互換性確認、確認前の適用無効化、了承後の有効化、移行先変更時の再確認、レビュー時のDescriptor無変更が成功 |
| Native移行回帰 | SDK 1.2.0→1.3.0の移行、Game再ビルド、旧設定への復旧と旧版再ビルドが成功 |
| Native移行の拒否・保護 | レビュー後のDescriptor / Level変更、非対応Level、旧Engine未配置、復旧後の編集・破損バックアップ・Root外の復旧記録、移動済みProjectの正確かつ冪等な復旧、共有データとバックアップの分離が成功 |
| ランチャー配布回帰 | 69項目成功 |
| Web配布回帰 | 5項目成功。旧Setup保持、重複版拒否、未公開Setup除外、変更点の履歴保持、公開URLとhash照合を確認 |
| ローカル公開物生成 | SDK 6版の実ZIPを照合し、1.2.0 Setupの版数・サイズ・SHA-256・変更点付きupload manifestを生成 |

配布DLLのUI検証: `Generated/StandaloneMigrationValidation/a2686d18-44a1-4a98-b2d5-270ac61fcaa3`。
Native移行検証: `Generated/MigrationValidation/0fc9a050-506d-473c-952d-5a8be44c1336`。

PowerShell 7からWindows PowerShellを呼ぶ検査では、テストプロセスの`PSModulePath`をWindows PowerShellのModulesへ設定した。個人の環境設定を永続変更していない。

任意のユーザープロジェクト、全DPIでの表示、実Setupを用いたこのPCへのインストールは未検証。互換性と操作手順は[変更点](Launcher-Release-1.2.0.md)を参照。

## 実公開の照合結果

- GitHub配布バッチ: https://github.com/kase-haruto/NexusEngine/releases/tag/distribution-bce4d31df585
- 1.2.0 Setup: https://github.com/kase-haruto/NexusEngine/releases/download/distribution-bce4d31df585/launcherSetup.exe
- サイトの`/launcher-releases.json`に1.2.0が先頭として表示され、変更点・URL・サイズ・SHA-256がローカルの配布物と一致した。
- `/releases.json`と`/data/catalog.json`も今回生成した公開一覧に一致した。公開URLからSDK 6版と新Setupの計7ファイルを実際に取得し、すべてのサイズとSHA-256を照合した。
- 公開済みLauncher 1.1.0の実DLLを使い、新しい更新一覧と変更点の取得、および`LauncherUpdates.Download`による1.2.0 Setupのダウンロード・SHA-256検査が成功した。取得したSetupのProductVersionも1.2.0だった。
- 旧ランチャー2版は同じURL・サイズ・SHA-256のまま保持。SDK 6版のサイズ・SHA-256も公開前と一致した。旧GitHub Releaseの削除・配布ファイルの差し替えは行っていない。
- Cloudflare Workers FreeのStatic Assets + GitHub Releasesを維持。R2・Git LFS・有料プラン・従量課金契約の追加なし。
- このPCへのセットアップの実行・インストール、既存ユーザープロジェクトのエンジン関連付け変更は実施していない。
