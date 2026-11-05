# ProjectホストのGPU起動とstandalone連携

## 目的と実装

Project固有の型を持つRuntimeとEditorを、実際のFramework・Graphics初期化・フレーム描画・通常終了まで確認する。CPU検証だけを描画成功として扱わない。

`Core/Hosting/BoundedFrameClient`は既存`IFrameworkUpdateClient`への委譲adapter。更新と描画抽出をそのまま呼び、指定回数の描画準備後に同じUIスレッドへWM_QUITを送る。FrameworkはそのframeのRenderを終え、次のイベント処理で通常終了する。Graphics実装やProject管理へ特別な描画経路を追加しない。

`ProjectHost.inl`は`--frames <1..10000>`と`--hidden`を受ける。引数がない通常実行は従来どおり。CPU用`--check-game`と有限GPU frame指定の混在は拒否する。指定frameより前の終了、描画失敗、初期化失敗を成功表示しない。Frameworkの正常終了codeと描画準備回数の両方を確認する。

```powershell
GameRuntime.exe --project "D:/Games/MyGame/MyGame.nxproject" --engine "D:/Engines/0.1.5/Data" --hidden --frames 3
GameEditor.exe --project "D:/Games/MyGame/MyGame.nxproject" --engine "D:/Engines/0.1.5/Data" --engine-manifest "D:/Engines/0.1.5/NexusEngine.nxengine" --hidden --frames 3
```

## Editorからの別プロセスRuntime

`Editor/Services/StandaloneSession`は選択済みProject ContextとEngineを保持する。`RequestStart`でRuntimeだけを非同期に増分ビルドし、UIからの`Poll`で完了を受け取り、`ProjectBuild::Launch`でGame自身のRuntimeを起動する。EditorのWorldを直接渡したり操作したりしない。`StandaloneCommands`がMenuとToolbarの実行・停止を同じサービスへ接続する。検証済み成果物を直接起動する`Start`も維持する。

起動済みなら二重起動を拒否する。Stopは自分が起動したPIDのWindowへWM_CLOSEを送り、Runtime自身の通常終了を待つ。非表示Runtimeも対象に含める。Editorやsessionの破棄でRuntimeをkillしない。Processが所有するのはhandleであり、通常の独立Runtimeの寿命ではない。ビルドJobの所有とは別の扱いを維持する。

`ProjectLaunchOptions`は既存bool版のLaunchを維持したうえで、非表示、有限frame、専用ログを明示指定できる。通常のメニュー操作では制限しない。既存ログの上書き、相対ログパス、不正frame値は拒否する。

## 検証手順

`Tests/ProjectGpuSmoke.Tests.ps1`は専用ProjectとRegistryを作り、Basic 3DのCameraへGame.Healthを保存する。両Gameホストを個別ビルドした後、別CWD、専用LOCALAPPDATA、非表示Windowで各3frameを実行する。Editor workspace初期化、GraphicsSystem初期化、frame完了、正常shutdownをログで確認する。

```powershell
./Project/Tests/ProjectGpuSmoke.Tests.ps1
./Project/Tests/ProjectGpuSmoke.Tests.ps1 -Configuration Develop
./Project/Tests/ProjectGpuSmoke.Tests.ps1 -Configuration Release
./Project/Tests/ProjectGpuSmoke.Tests.ps1 -EngineManifest './Generated/InstalledValidation/nexus-0.1.5-debug/NexusEngine.nxengine'
```

前提は対応configurationのProjectToolと汎用Editor/Runtimeホストの個別ビルド・Development登録。Installedは有限frame起動を持つSDKを使う。古い配置を自動更新しない。

`Build/NexusStandaloneTests.vcxproj`はEditorが使う同じstandaloneサービスを呼び、検証済みGameRuntimeの有限GPU実行、二重起動拒否、WM_CLOSE、繰り返しStop、所有者破棄後の独立GPU完了を確認する。テスト失敗時だけ、テスト自身が保持する子プロセスを終了する。任意の既存Runtimeやユーザーのプロセスを対象にしない。

## 2026-11-01の結果と範囲

Windows x64 / v145、NVIDIA GeForce RTX 3060 Tiで確認した。

| 配置 | 構成 | 結果 | データRoot（Generated/ProjectGpuValidation以下） |
| --- | --- | --- | --- |
| Development | Debug / MTd | 両ホストで3frameと正常終了 | 13994c89-7b31-40f2-a1b6-ac8baed0b54a |
| Development | Develop / MTd | 両ホストで3frameと正常終了 | ab435722-24bc-4283-b92f-12b196e9f3cb |
| Development | Release / MT | 両ホストで3frameと正常終了 | a9d003a7-e3ac-45dc-8051-8bb2dd936adb |
| Development / 最新standalone・Git保持変更 | Develop / MTd | 両ホストで3frameと正常終了 | 03b5e3a4-d5f4-44ae-8cee-5861b651e696 |
| Development / 最新standalone・Git保持変更 | Release / MT | 両ホストで3frameと正常終了 | 40f662d3-532f-4a03-9da7-87a6ac0fffff |
| Installed 0.1.4 | Debug / MTd | 両ホストで3frameと正常終了 | 8f519360-6f1d-43b3-8071-8f2cde6e0435 |
| Installed 0.1.4 | Release / MT | 両ホストで3frameと正常終了 | ef489663-e610-4d50-ba98-784fe7565c2f |
| Installed 0.1.5 | Debug / MTd | 新standaloneサービスを持つEditorを含め3frameと正常終了 | b5c16cfe-6285-4b20-8c5f-e9fcaaae0eb6 |
| Installed 0.1.7 / Blank | Debug / MTd | 空Levelの両ホストで3frameと正常終了 | f6a94865-4eb8-4a13-9324-cdd9cb0243e9 |

standaloneサービスのログは0.1.4のGPU Project内`Build/StandaloneTests/d105c379-df6c-4642-a394-f09d8f0f24a6`に保持する。未起動/二重起動等の状態と、子Runtimeの3frame完了・通常終了・独立実行を確認した。

これはframe処理の確認であり、描画画像の画素比較・全GPU機能・全Editor操作の成功を意味しない。Detailsでの実編集、メニュークリック、複雑な既存Levelは別の確認対象。Solution全体のビルド、既存成果物のClean、個人Registryの変更は行っていない。
