# Template Samples

Project Browserから作るTemplateは親DirectoryのEmpty / Basic Levelだけです。このDirectoryはCLIで明示生成するSampleと、そのコード資源を所有します。

| ID | 内容 | 初期Module |
| --- | --- | --- |
| native-module | 独立Game.dll、NativeMover、Crowd Simulation等のC++利用例 | Cpp 1個 |
| managed-gameplay | C# PlayerControllerと共通ECSの利用例 | CSharp 1個 |
| hybrid-gameplay | NativeCombatとGameplayの依存・共通World連携例 | Cpp / CSharp 各1個 |

いずれもDemoコードを含むため、空のProjectを作る用途には使用しません。初期Levelには従来のSample用Cube・Sky等を保持しています。新規Project用のBasic Levelとは別のLevelファイルです。

旧Managed BootstrapはGameplay以前の起動検証用Templateでした。現在のGameplayと役割が重なり、他Template・現行テストから参照されていなかったため、Metadataと専用資源を削除しました。`ManagedBootstrap/GameRuntime.vcxproj.in`と`GameEditor.vcxproj.in`はManaged Gameplayが使用する共有ホストとして残しています。

`Shared/Directory.Build.props.in`はSampleのManagedビルド用共通設定です。その他の旧Sharedコードは配布Templateから外し、型付きホスト検証用の`Tests/Fixtures/StaticProjectTemplates/`へ移しています。

Module / Source追加ツールもこのDirectory内の雛形を利用します。共通Behaviour雛形は親の`Behaviour/`にあります。
