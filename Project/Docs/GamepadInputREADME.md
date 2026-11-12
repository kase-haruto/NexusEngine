# 共通ゲームパッド入力

## 機能概要と調査結果

WindowsのXInput対応コントローラーを最大4台扱う。A/B/X/Y、D-pad、Start/Back、左右Shoulder、Stick押込み、左右Stick、左右Trigger、接続/切断、Down/Pressed/Releasedを提供する。1 slotはXInput user indexで、切断後に別deviceが同じslotを使う可能性がある。永続device IDとして保存しない。

既存入力は`ProjectWindow → InputFrame → NexusFramework → GameApplication/GameRuntime → Level.Input → GameServices`。Editorは同じ値を`EditorApplication → EditorSession.PlayRuntime`へGame Viewの許可判定後に渡す。C++の`Input.Read`、C#の`World.ReadInput`は800 byteのキーボード/マウスsnapshotを共有している。この経路を再利用し、別Input Manager、別ECS、別のManaged device objectは作らない。

公式仕様は[XInputGetState](https://learn.microsoft.com/en-us/windows/win32/api/xinput/nf-xinput-xinputgetstate)、[Getting Started with XInput](https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput)を参照した。EngineのWindows/DX12環境と整合し、追加の外部SDK配布を必要としないXInput 1.4を採用した。DirectInput/SDL/GameInputによる全device対応は今回導入しない。特にDirectInput専用のPlayStation等の機器が、そのまま対応するとは扱わない。

## 責務・依存関係・採用理由

| ファイル | 責務 |
|---|---|
| `Foundation/Input/GamepadFrame.h` | raw sample、radial dead zone、正規化、接続/入力edge。OSやEditorに依存しない |
| `Foundation/Input/InputFrame.h` | 既存入力に4 slotを追加。focus喪失とframe edge解除を統合 |
| `Platform/Input/XInputGamepads.h` | 所有threadでXInputを4回pollし、値だけを変換。状態Managerではない |
| `Platform/Window/ProjectWindow.h/.cpp` | frame採取の直前にpoll。終了時は入力状態を破棄 |
| `Editor/Core/EditorApplication.cpp` | Game View許可状態の前後でbutton edgeを再評価。非focus中もhot-plugを追跡 |
| `Runtime/Gameplay/GameApiBridge.cpp` | 既存servicesの所有thread/World/client検査を通した値コピー |
| `Public/Nexus/GameInput.h/.hpp` | 固定DTOとC++ ReadGamepads |
| `Managed/Nexus.Managed/GameInput.cs` | GamepadButton、GamepadState、GamepadInputState、World.ReadGamepads |

大きなdevice抽象化やaction mappingを先に作る案は採用しない。既存InputFrame内で値とedgeを一度確定することで、C++/C#ごとのpoll、同じbuttonの奪い合い、device pointerの寿命問題を避ける。正規化をFoundationへ分離したのは、Windows実機なしでも境界値・focus遷移・切断を決定的に検証するためである。

## データフローとライフサイクル

1. Window所有threadがOS messageを処理する。
2. `ConsumeInput`が各slotのXInputGetStateを呼ぶ。失敗時は空sampleとし、古いbutton/analogを再利用しない。
3. 接続状態、前frameとの差分、dead zone後の値を確定する。
4. 値コピーを全System/Scriptへ配布し、Window側のedgeだけを解除する。同じframeに何度読んでも消費しない。
5. Editorでは前のPlay入力と比較して許可/遮断を適用する。Game View外、Window非focus、テキスト入力中はbutton/analogを中立へ戻す。
6. Window終了時に入力値を消す。Level保存、Asset、Scene Clone、Reload stateへdevice状態を永続保存しない。

接続監視はfocusと分離する。非focus中もConnectedと接続/切断edgeは取得できる。focus喪失/切断では保持buttonのReleasedを返し、次frame以降へ繰り返さない。Game Viewへ復帰したときの保持buttonは、新しく許可された入力としてPressedを生成する。

## 正規化

- Left stick dead zoneは7849、Rightは8689。円形の範囲を除去して残りを0〜1へ展開し、対角方向でもvector長を1以内へ制限する。
- Stickは左右がX、上が+Y。値域は各軸-1〜1。
- Triggerは0〜30を0、31〜255を0〜1へ展開する。Triggerのbutton edgeは今回提供せず、必要な閾値をゲーム側で判定する。
- 公開button maskは`0xf3ff`。Guide等の未公開bitを落とす。

## Native / Managed ABIとエラー

既存`nexus.engine.input` protocol version 1を維持する。空requestは従来どおり800 byte。4 byteの`NexusGamepadRequestV1{operation=1}`は272 byteの`NexusGamepadsFrameV1`を返す。headerは16 byte、各slotは64 byte、4 slot。Booleanはuint32、buttonはuint32 mask、analogはfloat、reservedは0。C++ static_assertとManaged layout/範囲検査を行う。

不正operation/byte数/targetはINVALID_ARGUMENT、容量不足は必要byte数付きBUFFER_TOO_SMALL、別threadはWRONG_THREAD、失効client/WorldはINVALID_HANDLE。既存GameWorldの検査がManaged例外へ変換する。C#の範囲外slotや無効button maskはArgumentOutOfRangeException。

XInputの切断/errorは例外や毎frameのエラーログにせず、Connected=falseと中立値で表す。読取だけなのでManaged Reload freeze中も取得できる。新しい型はdevice pointer/delegate/GCHandleを所有しない。OSへのpollはWindow所有threadだけで行う。

## C#の使用例

```csharp
protected override void OnUpdate(float deltaTime)
{
    var pads = World.ReadGamepads(); // frame中は一回だけ取得して共有する
    var pad = pads[0];
    if (pad.Connected && pads.Focused) {
        var transform = Entity.Transform;
        transform.Translation += new System.Numerics.Vector3(
            pad.LeftStick.X, 0, pad.LeftStick.Y) * 5.0f * deltaTime;
        Entity.Transform = transform;
        if (pad.Pressed(GamepadButton.A)) World.Log("A pressed");
    }
    // 切断時にも解放処理を行えるよう、ReleasedをConnectedだけで遮断しない。
    if (pad.Released(GamepadButton.A)) World.Log("A released");
}
```

Buttonの組合せは、指定した全bitのDown/Pressed/Releasedを検査する。任意のbuttonが押されたか調べる場合は個別に判定する。

## C++の使用例

```cpp
#include "Public/Nexus/GameInput.hpp"
NexusGame::Input input(services); // servicesはModule所有者から同期借用する
NexusGamepadsFrameV1 pads{};
if(input.ReadGamepads(pads)==NEXUS_GAME_OK) {
    const auto& pad=pads.pads[0];
    if(pad.connected && (pad.pressed & NEXUS_PAD_A)) { /* jump */ }
    if(pad.released & NEXUS_PAD_A) { /* release */ }
}
```

## 変更対象・静的確認・検証

上表の実装に加え、`Tests/GameServicesTests.cpp`へGamepadOperations、`Tests/MonoGameplayTests.cpp`と`Tests/Fixtures/MonoGameplay/Probe.cs`へ共通snapshotのManaged確認を追加した。既存GameInput.cs内に収めたため、Managed csprojの明示CompileとPublisherの既存コピー項目を再利用する。追加Platform headerは既存Publisherのheader収録対象になる。xinput.libはPlatform headerのpragmaで各Host/testのlinkへ渡す。

include/namespace/宣言定義、固定layout、thread、所有権、focus、旧800 byte API互換、SDKコピー経路を確認する。実行結果は1.1.1の配布検証記録へ分けて記載する。テストは接続/切断/再接続、repeat、radial dead zone、極値、trigger、4 slot独立、Window/Game View focus、Native/Managed ABI、client寿命、誤thread、実XInput pollを含む。

## 制約と拡張

振動、battery、button remapping/action mapping、DirectInput/HIDの汎用対応、4台を超える入力、リモートIPCへのゲームパッド転送は今回対象外。XInputGetStateの短いpoll間に押して離したbuttonは検出できない。slotは永続IDではない。実機のボタン操作・USB/Bluetooth接続は接続された機器で別途確認が必要で、合成sampleのテスト成功を実機全機種の確認と扱わない。

将来の追加も同じInputFrame/値APIを利用する。振動を追加する場合は、出力の所有client、focus/Stop時の停止、Reload中の副作用を入力とは別に定義する。
