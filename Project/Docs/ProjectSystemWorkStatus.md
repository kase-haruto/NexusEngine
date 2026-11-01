# Project System 実装・検証の進捗

最終更新：2026-11-01。依頼された制作環境の実装と、この環境での受け入れ確認を完了。実行済み範囲・検証限界・将来機能を以下と[要求監査](ProjectSystemAcceptance.md)に記録する。

## 実装した範囲と証拠

| 要求 | 現在の状態 | 確認手段 |
| --- | --- | --- |
| Engine/Game分離・静的ライブラリ・型登録 | 実装済み | `EngineGameSeparation.md`、Reflection分離テスト、Game.Healthを含む生成GameのCPU起動 |
| Descriptor・Context・Root基準パス・作成・Recent | 実装済み | `NexusProjectTests`、既存のProject CLIテスト、原子保存・移動・Unicode・Registry並行更新のテスト |
| Launcher Recent/New/Engine Settings | 実装済み、主要導線の実入力を確認 | `LauncherSmoke.Tests.ps1`、3画面のPNG、作成・日本語検索・Recent Open・子Editorの正常終了 |
| データ駆動Blank/Basic 3D | 実装済み | `ProjectWorkflow.Tests.ps1`、両テンプレートのGameRuntime/GameEditor個別ビルド |
| Project別Editor/Runtimeと起動引数 | CPUと有限GPU実行で確認済み | Development Debug/Develop/Release、Installed Debug/ReleaseでGame.Healthを含む両ホストの3frameと正常終了 |
| プロセス引数・Unicode・CWD・Job所有 | 実装済み | `NexusProcessTests`、空引数/引用符/末尾backslash/日本語、終了259、子孫停止と無関係Job生存 |
| Development/Installedの厳密な解決 | 実装済み | Engine未配置・型/版/構成・buildId/hostKind/hash不一致を拒否 |
| Installed SDK発行と複数版共存 | Debug/Release x64で発行済み | `InstalledEngine.Tests.ps1`、SDK内cpp無し、破損・未追跡・inventory欠落・上書き拒否。0.1.5はLauncherを同梱 |
| Installed SDKだけのGameビルド | Debug/Release x64で確認済み | 両テンプレートのCPU検証に加え、Game固有Runtime/EditorのGPU実行を確認 |
| Editorのstandalone起動サービス | 実装・実行済み | 二重起動拒否、別Runtimeの3GPUframe、非表示を含む通常終了、所有者破棄後の独立実行 |
| Launcherのリサイズ | 600x600で実行済み | Native WindowをリサイズしてPNG寸法を確認。横並び操作の折返し、一覧と親画面のスクロールを目視確認 |
| 明示的なEngine変更・復旧 | 実装済み、Debug/x64で確認済み | ProjectMigration、互換性メタデータ、古い計画の拒否、旧Engine未配置・Project移動後の復旧、共有パスとの衝突回避 |
| Launcherでの作成・Engine変更・復旧の実入力 | 96 DPIの非表示Windowで確認済み | LauncherInteraction.Tests.ps1。Descriptor/Registryの実変更と元bytesへの復旧を確認 |
| Git分離・local props/成果物の除外 | 生成処理と資料を追加済み | 生成.gitignore、Descriptorにマシン絶対パスを含めない、Installed時のライブラリ参照 |
| Gitが記録したファイルだけでのclone | 実行済み | Git index/treeをarchive展開し、空Assets/Config/Sourceを保持。Data/Cpp Projectの再バインド・起動 |
| SDKとGameの移動・clone | 実行済み | 旧SDK登録の失敗・明示再登録・別版fallback拒否、日本語Shader Rootで3GPUframe |
| 将来のパッケージ化 | 設計方針とBuildTargetsを用意 | `ProjectWorkflow.md`。完成パッケージ機能ではない |

直近の検証データはすべて新規`Generated`以下に保持する。主な成果物：

- Developmentの最新版統合テスト：`Generated/WorkflowValidation/e9a5a8cf-838c-4138-89a2-802134d906c3`。
- Installed 0.1.1の統合テスト：`Generated/WorkflowValidation/53cdc7ff-da5c-4163-8e2a-58c50d89739b`。
- Installed 0.1.2の破損拒否・共存・上書き拒否：`Generated/InstalledValidation/Tests/382570f6-3abe-4755-a1e6-e71773466d7f`。
- Installedを登録した最新版Launcher描画：`Generated/LauncherValidation/9508f1d8-8d96-41fc-9d2f-6ded7dd5c3b4`。
- Engine移行レビューの描画：`Generated/LauncherValidation/26ced5b3-bdd6-4b83-9f52-eaeb501823fa/migration.png`。
- 移行・復旧と共有パスの検証：`Generated/MigrationValidation/d9da9143-bcb6-44aa-8696-e4ed031b3597`。
- Asset/Source/Config保持を含む直近の移行検証：`Generated/MigrationValidation/f13aac4e-09be-4836-bbb5-46373f3ead62`。
- Launcher実入力による作成・適用・復旧：`Generated/LauncherInteractionValidation/907f7fef-2403-4fb9-9b0c-d9909370ee4e`。
- 正しいinventoryでも非対応のDescriptor/Level/Asset契約を拒否：`Generated/InstalledValidation/Tests/5dec1e2a-f5fc-49a6-b8e2-36b8f8ac35f8`。
- GPU構成別ログ：`ProjectGpuValidation.md`に記録。3構成のDevelopment、Debug/Release Installedを確認。
- 600x600のLauncher：`Generated/LauncherValidation/f46da254-819e-4712-a5c8-989ab085382f`。移行popupの狭幅描画は`976c81c2-1e5b-417d-8ed0-f8dfa77a2394`。
- SDK同梱Launcherの直接起動：`Generated/LauncherValidation/beb15548-1bd9-4c1e-a3d8-df13ef07a7a7`。Source Data Rootを指定せずSDK/Dataを使う。
- 移動・clone・日本語Shader Root：`Generated/RelocationValidation/fc4a2513-5430-47f9-ba8a-e8cc201166a1`。
- 実Git tree/archiveの復元：`Generated/GitCloneValidation/3e126420-050f-498b-8179-5c5df2af4d2a`。新規作成の空必須Directoryに.gitkeepを配置し、Asset一覧とSourceコード検出から除外する。
- 既存Editorの隔離回帰：`Generated/FinalValidation/EditorRegression/5dbde2d2-a320-4ce0-b59d-dc62a6c1b82f`。Details/保存/Undo/Play/Reflection/Asset等が通過。
- 既存SampleGameの個別ビルドとStartupIsolation/ProjectSystemCliも通過。
- SDK 0.1.6の公開範囲・破損・互換性：`Generated/InstalledValidation/Tests/a7e52bec-cae4-4535-b112-f7990114f7ba`。旧Game依存Reflection headerを共有SDKへ含めない。

Development再登録でbuildIdを更新したため、以前のテストRegistry/stampが旧buildIdを持つことがある。過去のGeneratedを稼働環境として使い回す前に、明示的な再登録・再ビルドが必要。既存Installedのコピーは変更しない。

検証中に単独ビルドの完了待ちより先にテストを起動した一度の競合があり、ProjectToolのリンクと共有Runtime objの書込みが失敗した。関連プロセスの終了を確認してから個別に再ビルドし、クリーンアップは行っていない。CLIと既定Engine Rootの末尾区切りも揃え、同じEngineのinclude設定が表記差で揺れないようにした。今後も同じRuntime出力を使うビルド・テストは先行プロセスの終了後に順に行う。

## 最終照合と検証限界

1. Engine変更の基盤・UI・復旧、GPUの有限実行は確認済み。形式コンバータや自動API修正は行わず、現在の契約を事前検証してDescriptorだけを変更する。Runtimeのコンパイル入力に特定Game/Editor/Toolsがなく、LauncherのProjectReferenceがRuntimeだけであることをビルド定義で再確認した。
2. LauncherからEditorを開く操作、日本語検索、Tabでの入力欄移動も実入力で確認した。`Generated/LauncherInteractionValidation/ddf91d0c-999a-47ce-ae21-dec6b6e96c77`に作成・Recent登録・子Editorの3GPUframe・Engine適用・復旧の結果を保持する。OSファイルDialogの全操作と物理モニタ間のDPI切替は未検証。Engine未配置は解決拒否と明示再登録を専用移動テストで確認している。
3. Game型のDetails編集・保存・Undo・Play/Stopは既存Editorの隔離回帰で確認した。workspace初期化とGPU frame、同じstandaloneサービスによる別Runtimeの実起動・通常終了・独立実行も確認済み。メニューへの接続はProjectHostのCommand登録とRun/Stopの有効条件をコード照合した。全EditorメニューのGUI実入力を網羅した結果ではない。
4. 最新SourceのDevelop/Release個別ビルドとGPU実行も再確認した。既存サンプルの個別ビルドと起動互換、既存Editor回帰、3構成のDevelopment GPU実行とDebug/Release Installedは確認済み。Solution全体のビルドや既存成果物のCleanは禁止を維持する。
5. 別マシンの物理環境以外は、SDK移動の明示再登録、Game移動/clone/再バインド、日本語Shader Root GPU、Git treeだけからの復元を確認済み。Source Control Providerと最終パッケージ機能は元の指定どおり将来の拡張とする。
6. 元の要求をProjectSystemAcceptance.mdで項目ごとに照合した。調査・分離・Descriptor・Launcher・Template・Editor/Runtime・Engine管理・Git・配布拡張点について、実装ファイル、ビルド定義、実行ログ、GUI入力テスト、専用コピーの異常系検証を根拠とする。全Windows/GPU/物理モニタ環境を保証するものではない。

GPU検証はCore/HostingのBoundedFrameClientで実装し、Project管理へDX12 readback等は混入させていない。LOCALAPPDATAとProjectを専用データへ隔離した非表示Windowで実行した。画像を検査していないため、GPU初期化・frame loop・正常終了だけを確認結果として報告する。最新のStandaloneSessionを含むSDKは0.1.5 Debug。以前の0.1.4 SDKは更新せず保持している。

SDK 0.1.6は旧一括Reflection headerの除外までを含む固定snapshot。Sourceにはその後の空Directory用.gitkeep生成とLauncherのPerMonitorV2 manifestを追加したため、最終SDK発行ではそれらを含めて再確認する。過去の固定SDKを上書きしない。

0.1.7 Debugを新規発行し、上記修正を含むSDK同梱Launcherの実ウィンドウがPerMonitorV2であること、600x600へのリサイズと3画面描画を確認した（`Generated/LauncherValidation/269570b6-b471-4b98-966e-05dad21cfede`）。Blank ProjectもGame.Healthを含む両ホストで3 GPU frameと正常終了を確認した（`Generated/ProjectGpuValidation/f6a94865-4eb8-4a13-9324-cdd9cb0243e9`）。

その後のSource Launcher変更はsmoke時の子Editorを非表示・3frame・専用logにする検証経路で、通常起動のoptionsは既定値を維持する。0.1.7 snapshotへ遡及反映しない。最新SourceのDevelop/Releaseについても個別Toolと生成Gameを順番にビルドし、両ホストの3GPUframeと正常終了を再確認した。証拠はそれぞれ`Generated/ProjectGpuValidation/03b5e3a4-d5f4-44ae-8cee-5861b651e696`と`Generated/ProjectGpuValidation/40f662d3-532f-4a03-9da7-87a6ac0fffff`。

## 維持する制約

Engineを入口にする追加導線として、Editorの`Tools → Open game in Visual Studio`、Game Solutionの一意解決とIDE検出、Launcher shortcut作成スクリプトを追加した。検証結果と、実装済みのプロセス連携・未実装の双方向IPCの区別は[開発フロー](DevelopmentWorkflow.md)へ記録する。IDEはEditorSupportから起動し、RuntimeへIDE依存を追加しない。起動中の通常Launcherは終了させず、追加のLauncher確認は別出力で行った。

Runtime実行はMenu/Toolbarの同じCommandから非同期の個別ビルド・検証・起動へ接続した。未ビルドの新規Project、停止、取消、重複要求拒否、ビルド失敗とログを`Generated/RuntimeLaunchValidation/3780de50-938c-487d-b559-60ecbbb47324`で確認し、新しいToolbarを持つGameEditorもGPU 3frameと正常終了を確認した。起動済みの独立Runtimeの寿命はEditor終了で変更しない。

その後、fastTestの実ログからEditor使用中の同一DXC DLLを上書きして失敗する欠陥を確認した。Build-Game.ps1は同一SHA-256のDLL配置を省き、実Editorを常駐させた回帰でビルド・起動・停止とDLL更新時刻の維持を確認した。実行ボタンは一組へ統合し、実行メニューのモード選択と共通F5/Shift+F5で振り分ける。詳細とSearchBarの既存テスト不一致は[開発フロー](DevelopmentWorkflow.md)へ記録した。既存Installed SDKの固定snapshotは変更せず、修正はDevelopmentと次回発行へ反映する。

Solution全体のビルド、既存成果物のクリーンアップ、個人Registryのテスト利用は行っていない。
作業前から変更されていた`Config/Editor.settings`、`Resources/Assets/Levels/DemoLevel.json`、旧`Runtime/Reflection/Generated/*`は今回の実装・検証で上書きしない。

Developmentの直接登録は選択先`Project/Build/Nexus.EngineIdentity.props`の単一literal propertyから識別情報を読む。Launcher自身の版を別checkoutへ転記しない。複数条件やMSBuild式を使う独自identity定義は解釈せず、明示manifestの登録を求める。ホストをビルドしてから登録する必要があり、古い実行ファイルとの版不一致は起動時にも拒否する。
