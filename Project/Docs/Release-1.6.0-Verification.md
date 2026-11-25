# NexusEngine 1.6.0 配布検証

2026-11-26 / Windows x64 / Release / MT / MSVC v145。

## ビルドと動作

- ソースコミットは `d613be1250df0acb288587bc7133e24d36fd41cc`。配布タグも同じコミットを指す。
- 最終 SDK の Build ID は `ad54fe92-4d24-4b18-a1c4-3c2806b0dddf`。ProjectTool で 2,062 ファイルの inventory を持つ SDK を登録できた。
- Debug x64 の NexusEditorTests で、Raycast の除外、剛体の減衰、旧データの読み込み、固定ステップの回帰を確認した。
- Release の Mono 統合テストで、Jolt に対する C# の除外 Raycast と地面判定、接触・Input・Audio、Domain 再生成を確認した。
- Release の C++ テンプレートから型付き Editor／Runtime を生成・ビルドし、RTX 3060 Ti でそれぞれ 3 フレーム描画して正常終了した。この試験には最終版と同じネイティブ実装を持つ SDK の第2候補を使用した。
- 最終 SDK の C# テンプレートを新規生成し、Managed Bootstrap と GameScripts をビルドした。実 Mono で Behaviour を起動し、同 GPU で 3 フレーム描画して正常終了した。
- 配布前の試験で Windows PowerShell のモジュール読込に依存していたビルド記録と配置処理を検出し、共通の SHA-256 計算へ変更した。失敗した候補は公開しなかった。

## 公開物

- ファイル名 `nexus-1.6.0-Release.zip`
- サイズ 200,749,499 バイト
- SHA-256 `7b187dcdcb6998d0ebe3fd10fe508a940c5c56a03443fc101f7a39c751f2478a`
- [GitHub Releases](https://github.com/kase-haruto/NexusEngine/releases/tag/distribution-7b187dcdcb69) に公開。公開 URL から ZIP を取得し、サイズと SHA-256 がローカルと一致することを確認した。
- 既存の Launcher 1.3.2 は公開セットアップのサイズ・SHA-256 を照合した。従来の SDK とセットアップは引き続き取得できる。
- 新版と既存16版を含む Web／Launcher カタログをローカルで生成した。Cloudflare のログインが必要なため、公式サイトへの反映と反映後の Launcher 経由の取得確認は保留。
