# NexusEngine Launcher 1.3.0 配布検証

2026-11-16 / Windows x64 / Release / Visual Studio C++ v145 / .NET SDK 10。

## 配布物とソース

- ソース基準コミットは`450c7243d5c0209a7608a8727eece4dc00155ab8`。作業ツリーのTemplate整理・WinForms UI改修・C++ Launcher削除と、Launcher 1.3.0向け変更を含む。未コミット差分を保護し、配布タグの自動Source codeアーカイブとビルド入力が同一とは扱わない。
- 既存Release Runtimeライブラリの再利用ではTemplate読込時の異常終了を観測したため、現在のソースからRuntimeとCLIを別のビルドRootへ生成し直した。このRelease成果物では異常終了が再現せず、以下の検証を通過した。Engine実装の修正や既存SDKの上書きは行っていない。
- Runtime / CLIのビルドRootは今回専用の`work/launcher-release-1.3.0-native`。
- 配布出力は`Generated/LauncherDistribution/Launcher-1.3.0`。Windows x64自己完結型のSetup。
- SetupとLauncher DLLのProductVersionは1.3.0（ビルド元コミットを付記）、設定のlauncherVersionも1.3.0。
- Setupサイズ: **177,121,446 bytes**。
- Setup SHA-256: `da7245c5ef1adec87b606fd11624f43b04448dec2106f8ad4c7dfe64ff985c84`。
- 本番HTTPSの`/releases.json`と`/launcher-releases.json`を設定。PC内CatalogFileなし。
- 同梱Template資源はMetadata 2件、初期Level 2件、Preview PNG 2件。SampleやGameコードを同梱しない。
- エンジンSDKは再発行せず、既存6版のZIPとSHA-256・サイズを維持する。

## 配布前検証

| 検査 | 結果 |
| --- | --- |
| Runtime / CLI Release | 現在のソースから別Rootへのビルド成功 |
| WinForms / Setup Release | ビルド成功、Version・本番設定一致 |
| 配布回帰 | Releaseテスト69項目PASS |
| Templateカタログ | Release CLIで破損Metadata / Source / 初期Level / パス / 重複IDを局所化、作成拒否、配置欠落を確認 |
| 残したSamples | Native / Managed / Hybrid両初期言語の生成PASS |
| 新規ProjectとUIコントロール | 同梱Release CLIとInstalled SDK 1.3.0でPASS。Empty 0 Entity、Basic Level CameraとDirectional Lightのみ、Demoコードなし、日本語パス、表示名変更とID分離、画像デコード、カード全体選択、幅変更、Fallback、破損時Create禁止、Engine切替時の旧選択破棄 |
| 配布DLLの画面初期化 | 実LauncherFormを非表示で生成し、SDK選択後の同梱Template・Preview読込と正しい入力でのCreate有効化がPASS。画面表示・captureなし |
| 既存移行UI | 配布DLL / 同梱Release CLIでSDK 1.2.0→1.3.0のレビュー、了承前の適用禁止、了承後の有効化、移行先変更による計画無効化、レビュー時Descriptor不変がPASS |
| Project共通回帰 | NexusProjectTests Release PASS。atomic生成・復旧、カスタムDirectory、両初期Level、Hybrid / 単言語生成、Engine条件等。symlink作成ケースのみWindows権限不足でSKIP |
| Web配布回帰 | 5項目PASS。旧Setup保持・重複版拒否・公開URL整合性・静的配信等 |
| Level形式 | 公開SDK 6版の配布ZIP内Basic3D初期LevelはいずれもNEXUS_LEVEL 12。全6版のRuntime動作検証を意味しない |
| スクリーンショット確認 | 未実施。captureやDrawToBitmapの検証は呼び出していない |

スクリーンショットなしで生成ファイル・画像デコード・UIの操作状態を確認した。全DPI、任意のユーザープロジェクト、実SetupによるこのPCへのインストールは未検証。Emptyの単体Runtime実行にはCameraをユーザーが追加する必要がある。

## 公開後検証

- GitHub配布バッチ: https://github.com/kase-haruto/NexusEngine/releases/tag/distribution-909698a15545
- 更新用Setup: https://github.com/kase-haruto/NexusEngine/releases/download/distribution-909698a15545/launcherSetup.exe
- `/launcher-releases.json`の先頭に1.3.0を反映し、変更点・URL・サイズ・SHA-256が配布物と一致した。
- `/releases.json`と`/data/catalog.json`も生成した一覧と一致。公開URLからSDK 6版と新Setupの計7ファイルを取得し、全件のサイズ・SHA-256を照合した。
- 公開済みLauncher 1.2.0の実DLLで更新一覧と変更点を取得し、`LauncherUpdates.Download`による1.3.0 Setupの取得・サイズ・SHA-256・ProductVersion検査がPASS。
- 旧Launcher 3版は同じURL・サイズ・SHA-256を保持。SDK 6版も内容を変更せず、旧GitHub Releaseを保持した。
- 同梱Release CLIが生成したBasic LevelをInstalled SDK 1.3.0のRuntimeでCPU起動チェックし、exit 0を確認した。
- Cloudflare Workers FreeのStatic Assets + GitHub Releasesを継続。R2・有料プラン・従量課金契約の追加なし。
- このPCへのSetup実行・インストール、既存ユーザープロジェクトのEngine関連付け変更は行っていない。

GitHubにビルド元ローカルコミットが存在しないため、配布バッチタグの参照先は既存の公開コミット ba1f402f109c3c51d6bcafd676bb72755b358c31 とした。開発中ソースのpushは行わず、実ビルドの由来は上記のローカルコミットと作業ツリー差分の記録で区別する。
