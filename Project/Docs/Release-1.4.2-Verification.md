# NexusEngine 1.4.2 検証記録

2026-11-18。Windows x64 / Release / MT / v145。

## ソースと保持方針

HEAD 0aa3b048cf8ae7010ba122762938e964f9dd7f2bからの未commit変更を含む開発treeを使用します。既存変更をreset / commit / pushせず、旧SDK / ZIP / Launcherを保持します。配布batchタグを各SDKの正確なsource commitとは説明しません。

1.4.1のsource-inputs-r2.jsonと公開済みSDKのheaders/shadersを比較しました。差分と今回の入力一覧・hashはGenerated/Release142Checksへ保存します。実装済みと検証済みを区別します。SDKに含まれるこの文書は発行時点のsnapshotです。公開後の結果は作業treeと公開サイトへ追記します。

## 発行前の実施済み検証

- Debug NexusEngineビルド成功。
- EditorTests全体成功。Input Map、Source作成UI、Component Browser検索／分類／cache／お気に入り／型追加、Engine/Native/C#共通解除、instance指定、Undo/Redo、複数選択、失敗時復元、必須型保護、Play抑止を含みます。
- C# APIのCoreCLR / Mono向けビルド成功。ComponentEditor Attributeの実Metadata Export成功。
- Native SDKが生成したInput Actions snapshotのManaged読込成功。
- C# / C++ source generatorの実生成・登録・既存コード保護テスト成功。
- UVのGPU pixel変化、倍率、offset、恒等変換への復帰、Raster/Mesh一致、DXR shader、Bake署名を検証済み。今回のRelease向け描画検証は以下へ追記します。
- Live++ SDK未配置のため実接続は未検証。Smoke targetのcompile/linkはLive++無効指定で成功。

## 配布用SDK・公開後検証

発行とInstalled SDKでの動作確認、inventory、ZIP hash、公開URL、公開catalogと実取得の検証結果を、実施後に追記します。この段階では公開完了としません。

## 配布前の追加検証結果

- Release NexusRuntime / NexusEditorSupportとInstalled Editor / Runtimeのビルド成功。SDK inventory全1,989ファイルのSHA-256一致。初回SDKは互換性JSONのLevel上限14を検出したため配布せず、15へ修正したEngine-r2を採用しました。
- Installed SDKの改ざん・欠落・未登録ファイル・上書き拒否、旧1.4.1との共存、移行互換性検査が成功。公開FacadeのGameInputActions.cppだけを許可し、Engine実装cppの混入拒否を維持しています。
- 配布SDKだけから日本語パスのNative C++ / Managed C# / Hybridテンプレートを生成し、typed Editor / RuntimeをReleaseでビルド。全6 hostのCPU startup / systems / assets / render extractionチェック成功。Monoは旧SDKの同梱Runtimeから検証済みのコピーを使用しました。
- Installed汎用Editor / RuntimeはAnimationCurveDemoを開き、GPU初期化・3frame・正常終了を両方で確認。
- Release GraphicsTests --uv-transform成功（Raster / Mesh、DXR、恒等への復帰、Bake署名）。--shadow-grazingはHardware / WARP両方で、浅い太陽角3方向の受光面の縞がなく、遮蔽物の影が残ることを確認しました。
- Websiteの5テスト成功。Launcher 1.3.2を再利用。同梱ProjectToolと検査に用いたRelease ProjectToolは同一SHA-256（db9edf272a01232dc065c9ede13d8e9f49fda77886113544c0ba65a8cbe9ae55）で、新SDKの登録・生成・ビルドに成功しました。
- 初回template buildは子Windows PowerShellのGet-FileHash探索で失敗しました。テストプロセス内のPSModulePathをWindows標準Modulesへ設定し、成功。個人環境設定は永続変更していません。CPUテスト用scriptのRuntimeへのEditor専用引数も取り除いて再検証しました。
- r2ビルド前後のfirst-party source hashが一致。resource / compatibility JSONは別一覧とSDK inventoryに記録。公開用文書の結果追記はSDK発行後に行うため、ZIP内文書は発行時点snapshotです。

Build ID: `4aa89a4c-4ee8-42d0-9573-3ead027231a1`。Manifest SHA-256: `e0804f9f1e00eb5ac0f9dea09897ad8c1a44980d4d18310b5d6604075c1264c0`。

ZIP: `189113177` bytes / SHA-256 `11124dbe67bc8f2bf2f9aff58d714496f64a4c5e9fa7b19ef36e7a449f62c0b5`。

Launcher setup 1.3.2: 177588390 bytes / SHA-256 `0e13aa2cc5c5b8b29965d77db95361984baec588250551758710ec2711fe5a77`。

## 公開完了（2026-11-19 JST）

[配布サイト](https://nexus-engine.nexus-engine-website.workers.dev/)と[GitHub Releases](https://github.com/kase-haruto/NexusEngine/releases/tag/distribution-45f2b2eceb38)へ公開しました。

公開URLからSDK全9版（1.4.2 / 1.4.1 / 1.4.0 / 1.3.0 / 1.2.1 / 1.2.0 / 1.1.1 / 1.1.0 / 1.0.0）とLauncher 1.3.2の計10ファイルを取得し、すべてサイズ・SHA-256一致。二つの公開SDK catalogが一致し、旧8版のサイズ・hashは公開前と同一です。

本番Launcher 1.3.2の実assemblyから公開catalog読込とHTTPS転送を含むSDK 1.4.2の取得を実行し、189113177 bytesとSHA-256一致を確認しました。setupは再生成・置換せず1.3.2を再利用。今回、Launcher更新インストール自体の再実行はしていません。

トップページ・変更点・Component追加／アイコン／解除ガイドの公開内容も確認しました。旧SDK・旧Releaseを保持し、既存Projectの関連付けは変更していません。無料Static Assets + GitHub Releases構成を継続し、R2・有料プラン・従量課金を追加していません。

証跡: Generated/Release142Checks/publish-website.log、public-releases.json、public-data-catalog.json、public-site.json、launcher-public.log。公開後の文書追記は追加の静的サイト更新で反映し、配布ZIPは同一内容を保持します。
