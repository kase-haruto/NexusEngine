# NexusEngine Launcher

## 目的と構成

2026-11-01のUI改修では、[Unreal Project Browser](https://dev.epicgames.com/documentation/en-us/unreal-engine/create-your-first-project-in-unreal-engine)のナビゲーション・テンプレートと作成設定の分離、[Unity Hub Projects](https://docs.unity.com/en-us/hub/projects)のプロジェクトとEngine配置を管理する構成を参考にした。Projects / Create project / Engine libraryを左ナビゲーションに置き、Projectsの作成とOpenを先頭に表示する。狭幅ではナビゲーションを縮小し、作成画面を縦配置にする。

色だけを合わせるのではなく、Editorで使っていたスタイル定義を`Tools/UI/ImGuiStyle.h`へ移した。Editorの`ApplyEditorStyle`はこの共通定義へ委譲し、Launcherも同じ`ApplyStyle`を使う。黒・グレーとOrange Accent、padding・rounding・各状態色を一箇所で維持する。Launcherは引き続きEditorSupportへリンクせず、Editorの設定サービスも所有しない。新規フォームの余白やカード寸法はLauncher側のレイアウトとする。

カードのワイヤーフレーム表紙は共通の装飾であり、実際のLevelのthumbnailではない。長いProjectパスは一行のclip表示とhover全文で確認する。Engine変更とRecent削除はカードの`...`から操作する。Engineの登録フォームはEngine libraryの`Add an engine`で展開する。

複数のGameとEngine配置を選択する独立したProject Browser。EditorSupportやGame/Samplesへリンクしない。既存`ProjectWindow`とDear ImGuiを再利用し、Launcher専用D3D11 adapterで描画する。DX12 Editor全体を起動するより初期化と所有関係が小さく、Editorのレイアウト・Sceneへ依存しない。ネイティブWin32コントロールだけの構成も可能だが、既存ImGuiと同じ操作・テーマ調整・DPI対応を利用するため採用しなかった。

`LauncherWindow`はWindow・D3D11・ImGui contextを所有する。`LauncherApplication`はUI状態と一つの非同期操作を所有する。Projectの読み書き、Engineの解決、テンプレート生成、ビルド、プロセス起動は既存Tools/Runtime/Platformへ委譲する。CPU Level検証はRuntimeライブラリを利用するため、現時点ではDXC DLLも実行ファイル付近へ配置する。Launcher自身がDX12 GraphicsSystemを初期化することはない。

## ビルドと起動

`Build/NexusLauncher.vcxproj`を個別にビルドする。Debug/x64では`Generated/Modules/x64/Debug/NexusLauncher/NexusLauncher.exe`を起動する。Solution全体のビルドは不要。Development登録前に、対応configurationの汎用`NexusEditor`・`NexusRuntimeHost`の個別ビルドが必要。

SDK 0.1.5以降の今回の発行処理は`Bin/NexusLauncher.exe`も配置する。SDK/Dataから内蔵資源を探索するため、EngineソースのフォルダにLauncherを置く必要はない。初回はEngine Settingsで使用するmanifestを登録し、新規Project用の既定値を明示できる。

引数なしではユーザーのLOCALAPPDATA以下のProject/Engine Registryを使用する。`--recent-registry <file>`、`--engine-registry <file>`で専用Registryを選べる。`--data <root>`は内蔵フォントの探索Root。省略時は実行ファイルの祖先から探索し、CWDをEngine Data Rootにしない。フォントがない配置でもデフォルトフォントで起動できるが、日本語表示にはEngineのNotoSerifJP.ttfを配置する。

Developmentの直接登録は選択先の`Build/Nexus.EngineIdentity.props`からliteral identityを読み、Launcher自身の版を流用しない。条件ごとに異なる値やMSBuild式を使う独自定義の場合は、明示manifestを登録する。

起動中のロゴ表示とデスクトップショートカットについては[起動体験の設計](StartupExperience.md)を参照。

## 操作

- Projects：名前・パス検索、Project名、使用Engine、最終使用UTC、欠落・不正状態を表示する。OpenはDescriptorの要求Engineを厳密に解決してGame固有Editorをビルド・起動する。`...`のRemove from recentは一覧の記録のみを削除する。
- Create project：名前、親Directory、登録済みEngine、データから列挙したテンプレートを選ぶ。Create Projectは新しいDirectoryへ生成してRecentに登録する。既存Directoryを上書きしない。作成後はProjectsからOpenできる。
- Engine library：Add an engineで`.nxengine`の登録、Development Source Repositoryとconfigurationの明示登録を行う。既定値の選択、登録の削除にも対応する。削除でEngineのファイルを消さない。既定値は新規Project用であり、既存ProjectのEngineを置き換えない。
- Ctrl+NはNew Project、Ctrl+OはProject選択、Ctrl+FはRecent検索。Tab/方向キー/EnterはImGuiのキーボードナビゲーションを利用する。

Windowのサイズ変更とモニタDPI変更を既存Platformで受け、D3D11 RenderTargetとImGuiのサイズ・フォントスケールを更新する。日本語や空白を含むファイル選択はCOMのUnicodeパスを使う。ファイル選択キャンセルはエラーにしない。

最小client領域は600×420を基準にDPIへ合わせる。狭幅ではBrowseやconfigurationの操作を次の行へ送り、親画面と一覧にスクロール領域を残す。移行popupは現在のviewportへ収まるサイズ制約を持つ。600×600への実リサイズとPNG寸法、Recent/New/Engine Settings/移行popupの描画を確認済み。異なるDPIの実モニタ間移動の確認とは区別する。

LauncherのmanifestにもPerMonitorV2を宣言し、COM初期化前からDPIの扱いを固定する。非表示検証では実際のWindow DPI awareness contextがPerMonitorV2であることをWin32 APIで照合する。

## 所有権・エラー

UI表示用snapshotをworkerと共有しない。Registry読込、Engine inventory照合、作成、ビルドは一つのworkerで実行し、完了した結果だけUIへ渡す。失敗は画面下へ表示し、不正Registryを空データで上書きしない。既定Engineが欠落してもEngine一覧を残し、修復操作を可能にする。外部で変更したRegistryはRefreshで取り込む。

キャンセルとLauncher終了はstop tokenでビルドworkerへ伝える。Process Jobが停止するのは自分が起動したビルドツリーだけ。起動済みEditor/Runtimeは独立し、Launcher終了で強制停止しない。Window終了時はworkerを先に待ち、ImGui/backend、GPU、Window、COMの順で終了する。ファイル読込やテンプレートコピー途中のキャンセルは即時停止を保証しない。

## 検証と制限

`Tests/LauncherSmoke.Tests.ps1`をPowerShell 7で実行すると、専用Registryと非表示WindowでRecent/New/Engine Settingsを描画し、PNGとプロセス終了コードを確認する。`-EngineRegistry`と`-ProjectFile`で読み込み対象を渡せる。個人Registryを変更せず、captureは新規`Generated/LauncherValidation/<uuid>`に残す。`--smoke <new.png> --page recent|new|engines`はこの用途の起動方法。

2026-10-31 Debug/x64で個別ビルド、3画面の非表示描画、PNG保存、正常終了、日本語Project名・パス・テンプレート説明、既定Engine選択を確認した。描画成功だけで全クリック操作・ファイル選択Dialog・モニタ間DPI変更・GameのGPU描画成功を証明するものではない。Game生成と型登録・ビルド・CPU起動は別の`ProjectWorkflow.Tests.ps1`で検証する。

Installed SDKは[専用CLI](InstalledEngine.md)から発行し、Engine Settingsで登録できる。RecentのChange Engineから[移行レビューと復旧](ProjectMigration.md)を開く。解決失敗時に別Engineを勝手に選ばない。将来Source Control Provider、形式変換器、パッケージ処理を追加する際もRuntimeからLauncherへ依存させない。

2026-11-01には96 DPIの非表示WindowでWin32入力を送り、新規Game作成・Recent登録・Engine変更の明示確認・適用・復旧を実行して、ファイル内容の変更と復元を確認した。`Tests/LauncherInteraction.Tests.ps1`は現在の96 DPIレイアウト用で、別DPIや全Dialogの操作確認を代替しない。`--smoke-frames <5..3000>`でcaptureまでの最低frame数を指定できる。通常起動での自動適用・自動終了は行わない。

同テストはTabで保存先へ移動し、日本語検索後にRecentからOpenする経路も実行する。OpenしたGame固有Editorの3GPUframeと正常終了を専用logで確認する。smoke時だけ子Editorを非表示・有限frameにし、通常起動は表示と実行時間を制限しない。証拠：`Generated/LauncherInteractionValidation/ddf91d0c-999a-47ce-ae21-dec6b6e96c77`。

改修後の3画面と移行popupは`Generated/LauncherValidation/8d8e3858-c358-4c43-8a6e-1f370fdd6e36`、600x600は`Generated/LauncherValidation/f4654eb6-74bf-468f-93f6-b9c8f3d5171b`で描画とPNGを確認した。新配置での日本語作成・Tab移動・検索・EditorのGPU起動・Engine適用・復旧は`Generated/LauncherInteractionValidation/ae30a76e-0621-4c9f-9b64-cd5cc4b0c86c`で通過。既存の固定SDKは上書きせず、Source Launcherへ反映する。

スタイル共通化後の既存`NexusEditorTests`も個別ビルドと実行が通過。出力と個人設定は`Generated/LauncherUiValidation/EditorRegression`へ隔離し、テーマ切替・倍率再適用、Details、Undo/Redo、保存、Play、Asset操作等を確認した。Solution全体のビルドとCleanは実行しない。
