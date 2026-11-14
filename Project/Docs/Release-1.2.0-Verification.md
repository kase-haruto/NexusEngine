# NexusEngine 1.2.0 配布検証

2026-11-13 / Windows x64 / Release / C++ v145。実装ソースは `7be5a8197bf557bf7bc064fdf85f00a70035197d`（fix/title）。SDK buildIdは `77814ea4-9f9f-48c3-9c02-269ddf104862`。公開に伴いリリースノート、利用ガイド、Web説明、配布版一覧の自動生成・公開ダウンロード検証コードを更新した。

## SDKとテンプレート

- 新規出力先 `Generated/EngineReleases/v1.2.0/Engine` にInstalled SDKを発行。SDKのみから汎用Editor / Runtime Hostをリンクし、双方のCPU起動検査がexit 0。
- `nexus / 1.2.0 / Release` のmanifest inventory 1,056ファイルを全件SHA-256照合。アイコンPNGはソースとの全件一致を確認。
- SDKのBasic 3D / C++テンプレートから検証用プロジェクトを新規生成。SDK内のBuild-Game.ps1でEditor / RuntimeのReleaseビルドがexit 0。
- 生成したEditor / RuntimeはそれぞれCPU起動検査と30 GPUフレームがexit 0。NVIDIA GeForce RTX 3060 Laptop GPUで正常終了。

## 回帰テスト

| 検査 | 結果 |
| --- | --- |
| Debug EditorTests | exit 0。タイトルバーメニュー、ツールバー配置、Details、Viewport操作、空の作成・保存・Undo、ゲームサービスを含む |
| Debug ImGuiTests | exit 0。実GPU / WARP、分離ウィンドウ、アイコン読取、サイズ変更、GPU資源寿命 |
| Debug WindowTests | exit 0。メニュー領域、ドラッグ、サイズ変更、閉じる操作の制御 |
| Debug EcsTests | exit 0 |
| Debug GraphicsTests --atmosphere | exit 0。空の背景・強度、IBL、通常描画とMesh Shaderのフォグ統合 |
| LauncherDistribution | 62項目成功。プロジェクト削除と安全なアンインストールを含む |
| Webテスト | 4項目成功。配布物照合、パス検査、HTML除去、静的配信構成 |

ImGuiのinvalid.png警告は不正画像を拒否するテストの想定結果。Launcherテストは親PowerShellのモジュール探索パスの影響で初回失敗し、Windows PowerShell標準のPSModulePathで再実行して成功した。SDK発行は初回に未生成のProjectTool用DLLで停止し、ProjectToolとsetupを先に生成して再発行した。いずれも失敗した出力を公開していない。

ログは作業チャットの `work` ディレクトリに保存。検証用Projectは `Generated/Release120Smoke`。既存Projectの関連付けやRegistryの既定Engineを変更しない。

## 配布物

| ファイル | bytes | SHA-256 |
| --- | ---: | --- |
| nexus-1.2.0-Release.zip | 109079692 | `865cc5fb58199067518c61541447325cfa1bb64a831ec35ea1651e438ec46416` |
| launcherSetup.exe | 175081638 | `dffe159ef2b582bbe1d4bfbe0ad227d395c1f722499b0f02962c49ed275b9d2f` |

ランチャー本体とProjectToolに更新があるためsetupを新規生成。埋め込むカタログは本番HTTPS `/releases.json`、ローカルcatalogFileは空。1.0.0 / 1.1.0 / 1.1.1は公開済みZIPを回収して既存のSHA-256とサイズを確認し、統合カタログに保持した。

## 検証の範囲

今回の新規環境でC# / Hybrid / Mono / CoreCLRの独立した統合試験は実施していない。既存APIの回帰はEditorTestsで確認した。実コントローラーの操作、全GPU・DPI・解像度、手動による全メニュー操作は未検証。リリースノートの既知の制約も引き継ぐ。

## 実公開の確認

- [GitHub Releases](https://github.com/kase-haruto/NexusEngine/releases/tag/distribution-2f2d369c9882)へ新しい配布バッチを公開。旧Releaseと旧ファイルは削除・上書きしていない。
- [公開サイト](https://nexus-engine.nexus-engine-website.workers.dev/)と `/releases.json`、`/data/catalog.json`で1.2.0 / 1.1.1 / 1.1.0 / 1.0.0の4版を確認。
- 今回生成したカタログと実公開の全版・setupのURL、サイズ、SHA-256が一致することを確認。公開URLからSDK 4本と新setupの計5ファイルを実際に取得し、全ファイルのサイズとSHA-256が一致した。Verify-Public-1.2.0.log / exit 0。
- デプロイ直後の初回検査は旧カタログを取得したため、新版の成功証拠には採用していない。検証スクリプトへ今回の生成カタログとの一致検査を追加し、1.2.0と新setupを含む5ファイルで再検証した。Launcher用カタログにも再検証用のCache-Controlを設定。
- Cloudflare Workers FreeのStatic Assetsのみ。ファイル本体はGitHub Releases。R2 / Git LFS / 従量課金契約 / 有料プランを追加していない。
- ユーザー指定どおり公開までを実施し、このPCへの新SDK・setupのインストール、既存プロジェクトの移行は行っていない。
