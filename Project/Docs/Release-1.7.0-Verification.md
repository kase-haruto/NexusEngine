# NexusEngine 1.7.0 配布検証

2026-11-27 / Windows x64 / Release / MT / MSVC v145。

- ビルド入力は `feature/physics` の `155bed724b5a7233dac48f2bb1cf10e8872cb799`。配布 SDK の Build ID は `2e0077f1-7583-4dd2-9a80-110a4f07650d`。ProjectTool で 2,067 ファイルの inventory を検証して登録した。
- 最初の配布候補で共通のソース識別子検証スクリプトの欠落を検出した。修正後に別ディレクトリへ SDK と ZIP を発行した。
- 最終 SDK から Hybrid プロジェクトを生成した。C# と C++ の Behaviour をそれぞれモジュール下の `Controllers` へ作成し、コンパイル、成果物の検証、ソースパスのメタデータ生成を確認した。
- 最終 SDK を使った Hybrid Editor と Runtime は D3D12 で各 3 フレーム描画して正常終了した。Runtime は Mono で Managed Gameplay を起動した。別の検証では Editor を起動したまま新規 C#／C++ Behaviour の生成・ビルドに成功した。
- HybridGeneration テストと、旧版との共存・ファイル改変検出を含む InstalledEngine テストが通過した。
- Debug x64 の NexusEditorTests が終了コード 0 で完了した。Content Browser の両言語の作成ダイアログ、ソース照合、アタッチの Undo／Redo とロールバックを確認した。
- 利用者がエディタを再起動すると新規 Behaviour の型を読み込む。エディタ上での長時間の手動操作による見た目の確認は要確認。

## 配布物

- `nexus-1.7.0-Release.zip`（197,545,598 バイト）
- SHA-256 `c8609cab951fe85f5e1823b1364ece5ed35a42ab4ef91c09228763e32d2baa70`
- [GitHub Releases](https://github.com/kase-haruto/NexusEngine/releases/tag/distribution-e851c4eac1a0) に最新版として公開した。公開 ZIP をダウンロードし、サイズと SHA-256 を照合した。
- Launcher 1.3.2 の公開済みセットアップはローカルファイルのサイズと SHA-256 を公開カタログと照合した。
- 既存版を含む Web／Launcher 用カタログを生成した。SDK 18 版とセットアップ 6 版を含み、Cloudflare の静的アセットのドライランに成功した。本番サイトは 1.5.6 を返しており、Cloudflare の認証後に配備と公開カタログの再検証が必要。
