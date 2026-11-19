# NexusEngine 1.4.0 配布検証

2026-11-17 / Windows x64 / Visual Studio C++ v145。

## ソース

HEAD `0aa3b04`と今回のAnimation再設計・Curve Editorの作業ツリーをビルド入力とする。公開1.3.0の記録は`f8e94d1`と当時の未コミットCollider表示変更であり、Gitタグだけから完全なビルド元を推定しない。未配布の`450c724`（Collision Layers）と`0aa3b04`（Animation）も含む。旧1.3.0 SDKにはCollisionLayerSettings.hがないことを確認した。

GitHub distributionタグの自動生成Source codeと、今回のSDKのビルド作業ツリーが一致するとは扱わない。新規ファイルを含む変更一覧・hashは発行時の検証記録に残す。

## 実施済み

| 検査 | 結果 |
| --- | --- |
| Debug ECS --curves | exit 0。評価/積分、NaN、重複X、境界、保存往復、不変共有、二Entity独立再生、無効track、未保存再評価、削除時復元、独立速度fade/割込を検証 |
| Debug ECS --animation-system | exit 0。旧Animator/Timeline、events、復元、永続化の回帰 |
| Debug ECS --animation | exit 0。旧player、crossfade、pause、asset参照、Level永続化 |
| Debug EditorTests | exit 0。Curve履歴/保存、未保存速度preview/Undo/Seek/切断、編集Worldとdiskの保護、Curve/Clip/Controller/Timeline panels、ほか既存回帰 |
| Graphics animation assets | 実glTFの14 clipsを読み込み・sampleしexit 0。複数clip/morph metadata、binding、blend、同階層でrest poseの異なるモデルの拒否を検証 |
| GPU animation | RTX 3060 Laptop GPU / D3D12 validationで非skin・skinのinstance別pixels、repeat、shadow pose、削除がexit 0 |
| Preview camera | CameraなしのEditor camera fallbackとprimary Camera/FOV保持の回帰が成功。実機で単色Viewportの解消を確認 |
| 実Editor | 新componentのDetails、preview、階層timeline、Curveの使用先接続表示を確認。キー/接線drag、drag一操作Undo、timeline scrub、未保存CurveとViewportの連動を実機確認。CurveファイルSHA-256は編集前後で一致 |

ECSテストはEditor/ImGuiをリンクしない構成でCurveと共通clockを実行した。既存テンプレートを空にした変更に対する旧ECS全件のSky/Camera期待値は対象外であり、全ECS suite成功とは記載しない。

## 発行・公開

Installed Release SDKを新規`Generated/EngineReleases/v1.4.0/Engine-r2`に発行した。初回の`Engine`は配布せず、接線の数値操作を修正した再発行版を使用する。

- Engine: `nexus / 1.4.0 / Release / MT / v145`
- buildId: `17607cae-6ca7-43fe-a593-e56dff9de566`
- manifest SHA-256: `ade65a28bb6f12ac0397706ecb87d5f156ca41fd2ca226e0a8e66361a933a493`
- inventory: 1,889ファイルのSHA-256一致、余分なファイルなし。
- ZIP: `nexus-1.4.0-Release.zip` / 176,833,433 bytes
- ZIP SHA-256: `e479fad124a50b15e82aa3af034ff41bd8bf6d2990d19afb91191206a93d3da4`
- 入力記録: `Generated/Release140Checks/source-inputs-r2.json`。記録した変更ファイルは発行後の再照合でも不一致0件。
- SDKのEditor/Runtime単独Host CPUチェック成功。AnimationCurveDemoのCPUチェック、各Hostの8 GPU frames、Editor workspaceの8 GPU framesが成功。
- SDKから日本語パスのC++ Game.Healthを生成し、Runtime/Editorをコンパイル・リンク。異なるCWDから各3 GPU framesと正常終了を確認。

補助ProjectToolは現行Releaseで再ビルドした。初回は旧ビルドがcreate-projectを拒否した。Windows PowerShell子プロセスには標準ModulesのPSModulePathを補い、Game build stampのGet-FileHash探索失敗を解消して再検証した。SDKのソース変更は不要だった。

深いInstalled検証コピーでは既存のWindows MAX_PATH制限によりinventoryが読めなかった。検査本体を変えず、専用の短いパス（C:\nx140-validation-6ef4e1f1）で再実行し成功した。旧1.3.0との共存、Engine cpp非配布、改変・inventory欠落・未登録ファイル・上書き拒否、互換format検査を確認した。長い配置パス全般への対応を保証したものではない。公開URL検証と最終結果は末尾に記録した。

SDK内の文書は発行時点のsnapshot。公開後に確定する配布結果は、このリポジトリとWebの検証記録を参照する。

## 未検証・制約

全GPU/DPI/解像度/任意ユーザーProjectは未検証。Retargeting、root motion、layering、別process Curve IPC、Curve P2は未実装。旧v1 duration省略、authored skeleton metadata、fade割込規則などはRelease notesを参照。

Mono Runtimeは既存1.3.0 SDKのinventoryと一致する784ファイルを再利用する。既存SDKへ書込みは行わない。



## GitHub公開・サイト反映状況

SDK 1.4.0を[distribution-3c1098d89213](https://github.com/kase-haruto/NexusEngine/releases/tag/distribution-3c1098d89213)へ公開した。公開URLからZIPを取得し、176,833,433 bytesとSHA-256 `e479fad124a50b15e82aa3af034ff41bd8bf6d2990d19afb91191206a93d3da4`の一致を確認した。

[公開サイト](https://nexus-engine.nexus-engine-website.workers.dev/)とWeb/Launcher catalogに1.4.0を反映した。Cloudflareへの初回反映IDは`bee713ae-da4a-45ed-ae87-18fcd35ed4f1`。認証情報が見つからず停止した後、ユーザーの認証完了を受けてCLI loginを再実行し成功。同じGitHub配布batchを再利用してdeployした。

- 実サイトのトップと`/releases.json`、`/data/catalog.json`にSDK 7版と1.4.0を確認。
- Web配布検証scriptがSDK 7版とLauncher 1.3.0 setup、計8ファイルを公開URLから取得し、サイズとSHA-256を全件照合。
- Launcher実コードの`--verify-public-download`が公開7版を読み、HTTPS転送を経由して1.4.0を取得・SHA-256照合し成功。
- 旧6 SDKのidentity、サイズ、SHA-256を発行前記録と再照合し一致。旧GitHub Releasesと旧Launcherを削除・上書きしていない。
- 最終ソースを再ビルドしたDebug EditorTestsとECS `--curves` / `--animation-system` / `--animation`は全てexit 0。接線数値欄の最終修正を含む。

Launcher本体は正常な公開済み1.3.0 setupを再利用した。Cloudflare Workers FreeのStatic AssetsとGitHub Releasesを維持し、R2・Git LFS・有料プラン・従量課金サービスは追加していない。既存Projectの関連付けと旧SDKへの書込みは行っていない。

ログは`Generated/Release140Checks`、入力記録は`source-inputs-r2.json`。公開済みSDK内の文書は発行時のsnapshotであり、公開後に確定した結果はこの文書とWeb版が正式な記録。最終の文書反映deployment IDは`Project/Website/deployment-status.json`に記録する。


