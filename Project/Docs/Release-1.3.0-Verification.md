# NexusEngine 1.3.0 配布検証

2026-11-15 / Windows x64 / Release / Visual Studio C++ v145 / MT。

ソース基準は`f8e94d1`と作業ツリーのCollider表示追加。実公開1.2.1の`e81007f`からMono同梱・自動検出・配布検査を引き継いだ。GitHub配布バッチのSource codeアーカイブとSDK生成時の作業ツリーは一致を保証しない。

## 配布前の検証結果

| 検査 | 結果 |
| --- | --- |
| Installed SDK発行 | Release libraries、汎用Runtime / Editor Host、両HostのCPU起動検査が成功 |
| Mono同梱 | 公開済み1.2.1 ZIPのSHA-256を照合し、そのinventoryと一致する公式Mono 6.12.0.206 x64の784ファイルを引き継ぎ。GAC / BCL / config / licenseを含む |
| SDK単独ビルド | Basic 3D C++、Managed Gameplay C#、HybridのEditor / RuntimeをReleaseビルド。個人のEngine Registryや既存Projectを変更せず、各build stampを検証 |
| 配布Host起動 | 上記3プロジェクト、計6 HostのCPU起動と各8 GPU frame・正常終了が成功。外部Mono環境変数を空にしてSDK同梱Runtimeを使用 |
| C#新APIの実Mono統合 | Installed SDK内のAPI / Bootstrapを新規ビルドし、参照・Collider API・タイマー取消と例外・60Hz固定更新・TimeScale・Pause・Reload時計とcallback寿命・数学・Debugを検証 |
| 実CoreCLR統合 | 上記API、参照MetadataのexportとNative側の型解決も成功 |
| Debug EditorTests | Collider表示、スケール、Mesh / Convexの重心offset、disabled / Trigger、線数制限、物理を変更しない抽出、実ImGui overlay、Play World追従と旧snapshot互換を含めて成功 |
| GPU Debug描画 | RTX 3060 Laptop GPU / D3D12 validationで線のraster・depth遮蔽・overlay・frame終了を確認 |
| 生成した実C++ DLL | lifecycle、空Behaviourと60Hz OnFixedUpdateを確認 |
| Launcher回帰 | 69項目成功。Launcher 1.1.0の本番HTTPS setupを新規生成 |
| Mono同梱・Release検査 | InstalledMono.TestsとEngineReleaseの8項目が成功 |
| 生成テスト / Webテスト | Hybrid生成、PowerShell 7 / 5.1の生成回帰、Web配布テスト5項目が成功 |

SDK発行の最初の試行では、完全に空にしたBlankテンプレートをCPUゲーム起動の検査に使用したため、primary Camera必須検査で停止した。発行ツールの検証用fixtureをBasic 3Dへ修正し、新規stagingで発行をやり直した。失敗したSDKは公開していない。

C#のビルド検証は実行可能なManaged Gameplayを使用した。Blank / Basic 3DのC#初期Module宣言だけを、実行可能なManaged Hostの成功として扱っていない。

検証用ProjectとHostログは`Generated/R130Checks`、Mono API統合は`Generated/Release130Checks`、発行ログは作業チャットの`work/Publish-Engine-1.3.0-Retry.log`。個人のEngine Registry、システムのMonoインストール、既存ProjectのEngine関連付けは変更していない。

## 配布識別と公開の確認

このファイルを含むSDK自体のZIP hashは循環するため、ZIPのサイズ・SHA-256・公開URLはWebの最新検証記録と`/releases.json`、Launcher setupは`/launcher-releases.json`を参照する。公開後の照合結果はWeb側の本資料へ追記する。

全GPU・DPI・解像度・周辺機器の実機操作、任意のユーザープロジェクトや全てのlifecycle組合せは未検証。既知の制約は[Release-1.3.0](Release-1.3.0.md)を参照する。

## 実公開の照合結果

- GitHub配布バッチ: https://github.com/kase-haruto/NexusEngine/releases/tag/distribution-10943520599d
- SDK buildId: `e957d469-ef8e-4d33-a67d-1397b52df416`。Inventory 1,866ファイルを再照合。manifest SHA-256: `c3a9de40ddc6e15ce47ce2ea3b72c26508800e9275aa13e61dfbea4ce620f7a1`。
- SDK 1.3.0 ZIP: 168,098,050 bytes、SHA-256 `eaf8123234fd44c0adfe308758f41cd49b14f9f19be4cc8c8749cf1e30bec916`。
- Launcher 1.1.0 setup: 177,043,622 bytes、SHA-256 `bc33138ac35b5fd77186acc264b076d71deaa732be56e48937e2247942fb01b6`。
- 実サイトの `/releases.json`、`/data/catalog.json`、`/launcher-releases.json` が今回生成した一覧・URL・サイズ・SHA-256と一致。
- 公開URLからSDK 6版と新setupの計7ファイルを取得し、全ファイルのサイズとSHA-256が一致。以前のSDK・Release・旧Launcher setupも保持した。
- Cloudflare Workers FreeのStatic Assets + GitHub Releasesを維持。R2 / Git LFS / 有料プラン・従量課金契約の追加なし。
- このPCへのsetupインストールや、既存ユーザーProjectのEngine関連付け変更は実施していない。

最新のSDKダウンロード: https://github.com/kase-haruto/NexusEngine/releases/download/distribution-10943520599d/nexus-1.3.0-Release.zip

Launcher setup: https://github.com/kase-haruto/NexusEngine/releases/download/distribution-10943520599d/launcherSetup.exe
ランチャー実コードでも公開6版の一覧取得と、GitHub HTTPS転送を経由した最新SDK 1.3.0のダウンロード・SHA-256照合が成功した。
