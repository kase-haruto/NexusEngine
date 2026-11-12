# 1.1.1 ゲームパッド実装・配布確認

2026-11-13。ゲームパッド入力を既存Inputへ追加し、1.1.1 Windows x64 Releaseとしてローカル配布・Launcherインストールまで行った。1.0.0/1.1.0の配布物と1.1.0インストールは維持する。

## 調査結果

既存`ProjectWindow.ConsumeInput → NexusFramework.SetFrameInput → GameRuntime/Level.Input → GameApiBridge`を再利用した。Editorは既存ViewportSessionのGame View許可判定とEditorSession.PlayRuntimeへ接続する。C++ Input facadeとManaged InputStateはキーボード/マウス用800 byte snapshotを持っていた。既存のゲームパッドBackend/APIは未実装だった。

配布は既存Publish-Engine/New-EngineRelease、Launcherのローカルカタログ、SHA-256 inventory、ProjectToolのRegistry登録を再利用した。新しいECS/Input Manager/Installerは作っていない。

## 設計判断・依存関係

Windows/DX12環境へXInput 1.4を導入し、4 user slotをWindow所有threadで一度ずつpollする。XInputGetStateの失敗は切断として中立値へ移行する。normalization/edgeはOS非依存のGamepadFrameで処理し、WindowとEditorの許可境界で解放を確定する。

`Game Logic → Public Input/Nexus.Managed → 既存GameServices → Level.Input`。OS依存はPlatformだけへ置く。RuntimeはEditor/ImGuiへ依存しない。既存800 byte応答を拡大せず、同じInput V1 commandへ4 byte operation=1 request/272 byte responseを追加した。Managed側は値だけを保持する。

SDL/DirectInput/GameInputの汎用device層やaction mappingを先に作る案は採用しない。今回の対象はXInput対応入力であり、全機種対応を意味しない。

## 変更ファイル

| ファイル | 変更内容 |
|---|---|
| `Foundation/Input/GamepadFrame.h` | raw sample、4種の接続/入力edge、radial dead zone、trigger正規化、focus解除 |
| `Foundation/Input/InputFrame.h` | 4台の値を追加し、既存focus/edge処理へ統合 |
| `Platform/Input/XInputGamepads.h` | XInputGetStateの値変換とxinput.libリンク |
| `Platform/Window/ProjectWindow.h` | ConsumeInputを実装側へ移動 |
| `Platform/Window/ProjectWindow.cpp` | frame直前にpoll、Window終了時の入力初期化 |
| `Editor/Core/EditorApplication.cpp` | Game Viewの許可/遮断に応じたedge生成と非focus中のhot-plug追跡 |
| `Runtime/Gameplay/GameApiBridge.cpp` | 新要求と4 slot応答。既存client/World/thread検査を再利用 |
| `Public/Nexus/GameInput.h` | button定義、4/64/272 byte DTOとstatic_assert |
| `Public/Nexus/GameInput.hpp` | C++ Input.ReadGamepads |
| `Managed/Nexus.Managed/GameInput.cs` | GamepadButton/GamepadState/GamepadInputState/World.ReadGamepads |
| `Tests/GameServicesTests.cpp` | 接続/切断/再接続、repeat、4台独立、極値/threshold、focus、API/寿命、実XInput poll |
| `Tests/MonoGameplayTests.cpp` | 共通frameへ合成gamepad入力を設定 |
| `Tests/Fixtures/MonoGameplay/Probe.cs` | 実Monoで4 slot/analog/button/slot範囲とReload後の読取 |
| `Docs/GamepadInputREADME.md` | 既存構造・理由・ABI・所有権・使用例・制約 |
| `Docs/GameApiReference.md` | 新APIと提供状況 |
| `Docs/RuntimeServicesREADME.md` | 共通Inputのゲームパッド契約 |
| `Docs/Release-1.1.1.md` | 配布版の変更点と互換性 |
| `README.md` | Gamepad資料への案内 |
| `Generated/Release111Gamepad` | 配布SDKから生成した検証用Hybrid。C++/C#双方にReadGamepads検査を追加 |
| `Generated/EngineReleases/v1.1.1` | 新規Installed SDK/ZIP/版別カタログ/検証記録 |
| `Generated/EngineReleases/releases.local.json` | 1.1.1を追加。1.1.0/1.0.0も維持 |

既存の明示Managed CompileとPublisherコピー項目を再利用できるため、新たなBuild targetsやSDK DLLは追加していない。

## ライフサイクル・所有権

Window生成 → message処理 → 所有threadの4 slot poll → GamepadFrame正規化/edge → frame値コピー → Windowのedge解除 → 各言語/Systemが非消費読取 → EditorのGame View許可/遮断 → Window終了時に状態破棄。

deviceをゲーム側へ所有させない。slotは永続device IDではない。切断/非focusではReleasedを返し、analogを中立化する。接続監視はfocusと分離する。Level保存・Scene Clone・Reload stateへ入力を永続化しない。読取はReload freeze中も許可し、旧client/Worldと誤threadは既存境界で拒否する。

## 実行・静的確認

ログRootは`Generated/Gamepad-20261113`。

| 確認 | 結果・ログ |
|---|---|
| EditorTests全回帰とGamepadOperations | exit 0 / EditorTests.build.log、EditorTests.run.log |
| 実Mono + 合成gamepadのC#読取とReload | exit 0 / MonoNative.build.log、MonoBootstrap.build.log、MonoGameplay.run.log |
| CoreCLR API/Gameplay回帰 | exit 0 / CoreCLR.build.log、CoreCLR.run.log |
| Release SDK発行・SDKのみでHostリンク/CPU検査 | exit 0 / Publish-Engine.log |
| SDKからHybrid生成・C++/C# Gamepad facadeのReleaseビルド | exit 0 / Installed.Hybrid.build.log、生成ProjectのBuild/Logs |
| Hybrid CPU起動、30 GPU frame | exit 0 / Installed.Hybrid.CPU.log、Installed.Hybrid.GPU.log |
| Hybrid Editor Releaseビルド/Play/Stop/Game View内外へのfocus | 成功 / Installed.Hybrid.Editor.build.log、Editor.UI.logと実画面 |
| Runtime依存DLL | XINPUT1_4.dllを確認 / Installed.Runtime.dependencies.log |
| Launcher配布一覧 | 実画面で1.1.1/1.1.0/1.0.0の3版を確認 |
| Launcherインストール | 実ボタンから完了。インストール済み一覧で1.1.1と1.1.0を確認 |
| インストール内容 | 1012ファイル全てSHA-256一致 |
| 配置後の汎用Runtime 10 GPU frame | exit 0 / Launcher.Installed.GPU.log |
| Hybrid Host + インストール先Dataの10 GPU frame | exit 0 / Launcher.Installed.Hybrid.GPU.log |
| 静的確認 | include/namespace/宣言定義/link、DTO layout、client寿命、thread、旧API/保存互換、MSBuild XML、git diff --checkを確認 |

実XInput pollは成功し、接続台数は0だった。合成sampleでの成功を実機操作の検証と置き換えない。

汎用HostへHybrid Projectを直接渡した経路は、既存契約どおり「explicit Native Game Module host required」で拒否された。ログはLauncher.Installed.HybridRejected.logへ保持した。汎用HostはModuleなしの検証Projectで、Hybridは生成済み専用Hostでそれぞれ正常終了を確認した。

## 配布先

- Version/ID/構成: `1.1.1 / nexus / Release x64`
- ZIP: `Generated/EngineReleases/v1.1.1/Packages/nexus-1.1.1-Release.zip`
- bytes: `107321912`
- SHA-256: `0a67419a1fa772126d6f19884d4297a16ec68246160af8ade336f55e67427e77`
- カタログ: `Generated/EngineReleases/releases.local.json`
- インストール先: `%LOCALAPPDATA%/NexusEngine/Versions/nexus-1.1.1-Release`
- デスクトップ/スタートメニューの既存Launcherから利用可能。現在はインストール済み画面を開いている。

インストール済み1.1.0と既存Projectの関連付けは維持する。新規用のRegistry既定Engineは1.1.1。既存Projectで新APIを使う場合は1.1.1へ移行し、Host/Game Module/Managedを対応SDKで再ビルドする。

## 未実装・未検証

実機button操作・USB/Bluetooth接続・全機種は未検証。振動、battery、action mapping、DirectInput/HID専用機器、4台超、リモートIPCへのgamepad転送は未提供。具体的な使い方とdead zone/focusの契約は[Gamepad Input](GamepadInputREADME.md)。

今回はこのPCのローカルZIP/カタログ配布。HTTPSサーバーへのアップロードは行っていない。公開後のZIP/Installed SDKは書き換えず、検証記録だけを外側へ追加する。
