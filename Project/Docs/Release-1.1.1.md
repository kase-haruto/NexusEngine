# NexusEngine 1.1.1

1.1.0のC++/C#/Hybrid基盤にゲームパッド入力を追加したWindows x64 Release版。

- XInput対応コントローラーを最大4台取得。
- A/B/X/Y、D-pad、Start/Back、Shoulder、Stick押込みのDown/Pressed/Released。
- 左右Stickのradial dead zoneと正規化、左右Triggerのthresholdと0〜1値。
- 接続/切断と、切断・Window/Game View非focus時の入力解除。
- C++ `Input.ReadGamepads`、C# `World.ReadGamepads`による同じframeの一括snapshot。
- 既存キーボード/マウスの800 byte APIとGameServices V1は維持。

新しいECS、Input Manager、Managed device所有は追加していない。旧1.0.0/1.1.0の配布物と既存Projectは残す。新SDKを利用するHostとGame Moduleは対応版で再ビルドする。1.1.0の機能・制約も引き継ぐ。

対応機器はXInput対象。DirectInput専用機器、振動、battery、action mapping、4台超、リモートIPC転送は未提供。実機操作とUSB/Bluetoothの全機種保証は行わない。詳細とサンプルは[GamepadInputREADME](GamepadInputREADME.md)。

配布はこのPCのローカルZIP/カタログ。HTTPSサーバーへのアップロードとは別。実行・インストール結果は配布先の`Verification-1.1.1.md`へ記録する。検証ログは`Generated/Gamepad-20261113`。

## 配布前の確認

Debug EditorTestsの全回帰と実Mono統合はexit 0。4 slot、接続/切断、button edge、radial dead zone、trigger、focus、旧800 byte応答、誤thread/client寿命、実Monoの272 byte読取とReloadを確認した。実XInput pollは成功し、検出された実機は0台だった。実機操作は未検証。Release SDK/Launcherインストール結果は外部検証記録へ記載する。
