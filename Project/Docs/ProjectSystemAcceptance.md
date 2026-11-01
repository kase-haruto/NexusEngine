# Project System 要求監査

監査日：2026-11-01。元のPhase 0〜8の範囲を維持し、実装・実行・設計のみの範囲を分ける。元の指示で将来機能とされたものを、完成済みの機能として扱わない。

| 要求 | 現在の実装と根拠 | 検証範囲 |
| --- | --- | --- |
| 既存構造の調査と変更計画 | ProjectSystemSurvey.mdに元のSolution/Project、依存、起動、型登録、保存、パス、移行順序を記録 | 実装前の調査記録。歴史的な提案と現在の実装は区別 |
| 現在のビルド構造の尊重 | MSBuildを継続。Runtime/EditorSupport/SampleGame/各ホスト/Launcher/Toolを個別Targetへ分離 | Debug/Develop/Releaseの個別ビルド。Solution全体とCleanは実行しない |
| Engineが特定Gameへ依存しない | NexusRuntimeのClCompileにGame/Samples/Editor/Toolsを含めない。型登録はReflectionRegistrationとホストの明示注入 | XML入力の検査、ReflectionSeparation、Game固有型を汎用ホストが拒否 |
| GameがEngineを利用する | GameRuntime/GameEditorを生成し、DevelopmentではProjectReference、Installedでは固定libをリンク | 両テンプレートの独立Gameビルド、Source/SDK双方のCPU/GPU実行 |
| ライフサイクル・所有権 | 既存Framework/GameRuntime/EditorSession/LevelDocumentを再利用。Contextは不変データ、GPUは既存Renderer所有 | Editor回帰、GPU frameと通常shutdown、standaloneの所有者破棄後の独立実行 |
| Gameの型とEditor | Game専用Reflection生成とEngine schema衝突チェック。Editorにも同じ登録を渡す | Game component編集/保存/Undo/Playの回帰、Game.Healthを含むEditor workspace/GPU初期化 |
| Public API整理 | Runtimeのデータ・ライフサイクル・型登録を境界にする。旧Game依存Reflection headerはSDKから除外 | SDK実装cpp不在とGame/Samples include不在を検査。ヘッダ全体の公開縮小は段階的整理 |
| .nxproject | Descriptor/Serializer/Contextを分離。schema/UUID/型/UTF-8/未知項目/重複キーを検証し原子保存 | ProjectSystemTests、CLI。無効データで元ファイルを置換しない |
| 作成・一覧・登録・起動 | ProjectServiceとProjectRegistry。作成は新規stagingで検証後に公開。Recentはマシン固有 | 作成失敗時のrollback、既存Root拒否、並行更新、欠損/移動/clone/削除のテスト |
| Rootとパス | Descriptorの親をRootにし、Assets/Levels/Source/Configと起動Levelを正規化。逸脱を拒否 | 日本語/空白/別CWD/移動/相対パス/リンク逸脱/無効Root。日本語Shader RootでGPU実行 |
| Launcher | EditorSupport非依存、Win32/ImGui/D3D11 adapter。Recentカード、検索、New/Open、Engine設定、エラー表示 | 3画面と移行popupのPNG、作成/Recent登録/日本語検索/Open/移行/復旧のWin32入力。Openした子Editorの3GPUframeと正常終了 |
| キーボード・resize・DPI | ImGui keyboard navとCtrl+N/O/F。600x420基準の最小サイズ、折返し、スクロール、PerMonitorV2 manifest/API | 実リサイズとPNG寸法、Win32 Window DPI context。異なる実モニタ間の移動は未実施 |
| ファイル選択・プロセス | FileDialogのCOM wide pathとcancel処理。ProcessのCreateProcessW、argv引用、個別handle/Job所有 | ProcessTestsの空/引用符/末尾slash/日本語/終了259/Job停止範囲。FileDialogは実装レビュー、OS Dialog全操作は未自動化 |
| Blank/Basic 3D | .nxtemplateのデータ定義から列挙し、ファイル生成・置換・Level検証を分離 | BlankはCamera、Basic 3DはCamera/Directional Light/Cube。両方のGame固有CPU起動、GPU frameも確認 |
| Editor/Runtime連携 | DescriptorとEngine Data Rootをargvで渡す。StandaloneSessionが起動/状態/通常終了を担当 | 実Runtimeの3GPUframe、二重起動拒否、WM_CLOSE、独立実行。未保存WorldのIPC転送は将来機能 |
| Engine登録と版解決 | Projectの要求とマシンRegistry/配置manifestを分離。id/type/version/configurationを厳密解決 | 欠損・曖昧・版/構成違い・変更済みmanifest・build stamp/依存DLL違いを拒否。別版へfallbackしない |
| Installed固定配置 | 固定buildId、SDK/Data/Templates/Binと全ファイルinventory。既存発行先へ置換しない | 版共存、hash破損、未追跡ファイル、inventory欠落、上書き拒否、SDKだけのGameビルド |
| Development/Installed切替 | ProjectMigrationのレビューと明示適用。Descriptor/Level/Asset契約確認とC++再ビルド確認 | 古い計画拒否、旧Engine未配置、正しいinventoryでも非対応形式を拒否 |
| 変更失敗時の復旧 | 旧Descriptorの正確なbytes、提案内容、UUID/hashのRecoveryを保存してから変更 | 後の編集/壊れたbackup/Root外の復旧を拒否、移動後・冪等復旧、共有パスとbackupの分離 |
| 独立Git管理 | EngineをGameへコピーせず、共有DescriptorとGameコードを記録。local props/cache/buildを除外 | 実Git index/tree/archiveでData/Cpp Gameを再構成。空必須Dirを.gitkeepで保持。Sourceリポジトリをcommitしない |
| SDK/Game移動とチーム共有 | Engine Registryのみ明示再登録。Descriptorは共有id/versionを維持しlocal propsを再生成 | SDK移動、旧登録拒否、別版代用拒否、Game移動・複製・再バインド・GPUを専用コピーで確認 |
| 将来の配布 | BuildTargetsでGame executableを識別し、Context/Engine manifestにAsset/Level/Config/Shader/DLLの収集元を持つ | 実行可能Game生成は検証済み。最終packager/cookは元の指定どおり今回は設計と拡張点のみ |
| 資料・エラー処理 | Survey/Separation/System/Workflow/Launcher/Installed/Migration/Git/GPU各資料 | 目的、責務、依存、方式比較、制約、エラー、拡張、コマンドと証拠を記録 |

## 最終照合

- LauncherのRecent Open、Tabによる入力欄移動、日本語検索、子Editorの3GPUframe完了を`Generated/LauncherInteractionValidation/ddf91d0c-999a-47ce-ae21-dec6b6e96c77`で確認した。通常操作では表示・実行時間を制限せず、smoke時だけ非表示・有限frameにする。
- SDK 0.1.7はGit保持MarkerとLauncher PerMonitorV2までの固定snapshot。その後のSource変更はLauncher smoke時の子Editor実行オプションで、通常起動の動作は維持する。旧SDKを上書きしない。最新SourceのDevelop/Release両ホストは別途GPU再検証済み（ProjectGpuValidation.md）。
- ProjectHostの起動・共有設定・Context受け渡し、EditorSettingsのConfig参照、EditorWorkspaceのLevel参照、standaloneコマンド登録、FileDialogのUnicode/cancel/HRESULT処理、Runtime/Launcherのビルド入力をコード照合した。初期分離資料の未実装記述は歴史的な記録と明記し、現在の資料へ案内した。

依頼された制作環境の実装と、この環境で行う受け入れ確認を完了した。以下の検証限界と、元の依頼で将来拡張とされた機能は残る。

別マシンの物理環境、全GPU機能、全Dialog操作、別DPIの物理モニタは、この環境の自動実行結果から証明していない。これらの限界を隠して全環境での成功とは報告しない。元の依頼で完成を要求されていないGame Module DLL、Source Control GUI、完全なpackagerは将来の拡張とする。
