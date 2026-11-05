# 起動画面とデスクトップからの起動

デスクトップの `NexusEngine.lnk` は、引数なしの NexusLauncher.exe を開く。Project Browserでゲームを選び、Game固有のEditorを起動する。特定のProjectやEngine版はショートカットに固定しない。

## 表示と寿命

LauncherはRegistry読込と最初の画面描画が終わるまで、NXロゴ、NexusEngine名、バージョン、処理状況を表示する。EditorはProject、Level、Graphics、Renderer、Editorツールの準備状況を表示し、最初のフレーム描画後にメインWindowへ切り替える。表示時間を延ばすための待機は入れない。バーは残り時間や進捗率を保証しない処理中表示である。

`Platform/Window/StartupSplash` は専用threadとWin32/GDIのWindowを所有する。GPUやImGuiを初期化する前に表示でき、描画初期化中も自身のメッセージを処理する。Unicode文字列、モニタ作業領域、DPI変更に対応する。Closeとデストラクタは自身のWindowを閉じてthreadをjoinする。呼出元が通常終了・初期化失敗のどちらで抜けても解放される。表示失敗は警告に留め、エンジン起動を継続する。

Frameworkは `FrameworkStartupStage` の通知と `deferWindowUntilReady` のみ提供する。ロゴ表示やLauncherに依存しない。Editor用ProjectHostが通知を表示内容に変換する。Game Runtimeと非表示の自動検証では起動画面を出さない。Game固有の起動画面は将来Game側で指定する。

ImGuiによる表示も比較したが、起動前にGraphicsの初期化が必要になるため、今回はPlatform層の軽量なGDI表示を採用した。ネイティブ描画のため、ImGuiテーマとの色・文字配置の整合は個別に維持する必要がある。

## ブランド素材と再作成

`Resources/Editor/Branding/NexusEngine.svg` がNXマークのベクター素材、同Directoryの `.ico` がWindows用の複数サイズのアイコン。`Tools/Project/Generate-EngineIcon.ps1` でアイコンを再生成できる。起動画面のマークはStartupSplashで同じ形をGDI描画する。Launcherのresourceにアイコンを埋め込み、ショートカットもそのresourceを参照する。

`Tools/Project/Create-LauncherShortcut.ps1` は既定でWindowsのDesktop既知フォルダへ作成する。既存ファイルを上書きしない。実行ファイルの移動後は、新しい場所を指定してショートカットを作成し直す。Installed Engineは既存配布物を書き換えず、新しいSDK発行時に今回の起動処理と素材を取り込む。

## 検証

2026-11-01 Debug/x64でLauncher、Editor、NexusStartupTestsを個別ビルドした。Solution全体のビルドとCleanは実行していない。

- StartupSplashTests：表示、重複表示拒否、日本語status更新、GDI画像取得、二重Close、再表示、呼出元threadへWM_QUITが漏れないことを確認。
- LauncherSmoke：非表示でRecent/New/Enginesの3画面描画と正常終了を確認。
- LauncherShortcut.Tests：リンク先、引数、作業Directory、上書き拒否を確認。
- ProjectGpuSmoke：日本語パスの独立Gameを生成し、RuntimeとEditorでGPU初期化、3フレーム描画、正常終了を確認。EditorのFirstFrame通知もログで確認。

非表示GPU検証は物理モニタでの切り替え、異なるDPIのモニタ間移動の実機確認を代替しない。将来、起動診断やGame固有ブランドを追加する場合も、処理の所有権と表示を分離して通知を拡張する。
