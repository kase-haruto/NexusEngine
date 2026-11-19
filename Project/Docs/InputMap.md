# Input Map

Input Mapは、物理入力をJumpやMoveなどの操作名へ対応付けるProject共通設定です。既存のキーボード・マウス・XInput snapshotを評価し、C++とC#で同じ操作値を使います。

## Editorで設定する

1. ツール（Tools）→ Input Mapを開きます。Windowメニューにも登録されます。
2. 操作を追加し、名前・Context・値の種類を指定します。
3. 割り当てを追加して入力元、キーまたはボタン、Pad番号、寄与X/Yを選択します。
4. 保存を押します。次のPlayから読み込まれます。Play中の編集・保存は無効です。

設定先はProjectのConfigディレクトリ内の`InputMap.settings`です。独自のConfigパスもEditor、Runtime、Level遷移で引き継ぎます。設定ファイルがなければ既定の割り当てをメモリ上で使い、Editorの保存でファイルを作成します。再読込は未保存編集を破棄します。既定設定ボタンは下書きだけを置き換えるので、保存までディスクの値は変わりません。

## 既定の割り当て

| 操作 | 種類 | キーボード・マウス | Pad 0 | Context |
|---|---|---|---|---|
| Jump | Button | Space | A | Gameplay |
| Attack | Button | 左クリック | X | Gameplay |
| Move | Axis2D | WASD | 左スティック | Gameplay |
| Look | Axis2D | 未割り当て | 右スティック | Gameplay |
| Pause | Button | Escape | Start | 常時 |
| Confirm | Button | Enter | A | Menu |
| Cancel | Button | Escape | B | Menu |

Moveは右が+X、前が+Yです。複数方向・複数deviceを加算した後、ベクトルの長さを1へ制限し、斜め移動の速度増加を防ぎます。Buttonは複数bindingのORで判定します。最後のbindingが解放されるまで操作は保持されます。

## C#

ScriptBehaviourのOnUpdateなど、Worldが使えるcallbackで読み取ります。移動・ジャンプ自体の処理はゲーム側で実装します。

```csharp
var actions = World.ReadActions();
var movement = actions.Vector2("Move");
if (actions.Pressed("Jump"))
{
    // ジャンプ処理
}
if (actions.Down("Attack"))
{
    // 攻撃ボタンの保持中
}
```

`Down`は保持、`Pressed`は押したframe、`Released`は解放したframeです。`Value`は1D値、`Vector2`は2D値です。`Contains`は操作名の登録を確認します。未登録の操作はfalse/zeroになります。1回取得したsnapshotをそのcallback内で使い回してください。

```csharp
World.SetInputContext("Menu");
// 次の入力frameからMenuが有効になる。
// Gameplayへ戻る場合: World.SetInputContext("Gameplay");
```

## C++

公開SDKの`Nexus/GameInput.hpp`をincludeし、ModuleのGame Services contextからInputを構築します。

```cpp
NexusGame::Input input(servicesContext);
NexusGame::ActionFrame actions;
if(input.ReadActions(actions) == NEXUS_GAME_OK) {
    const auto movement = actions.Vector2("Move");
    if(actions.Pressed("Jump")) {
        // ジャンプ処理
    }
}
```

Context切り替えは`input.SetContext("Menu")`です。Facadeの実装は`Public/Nexus/GameInputActions.cpp`にあり、標準の`Build/Nexus.GameModule.props`が各Game DLLへ組み込みます。独自のビルドシステムではこのcppもコンパイルしてください。Engine内部libへのリンクは必要ありません。

## Contextとframeの契約

- 開始ContextはGameplay。Context空欄の操作は常に有効です。
- Gameplay以外へ切り替えるには、そのContextを持つ操作が必要です。
- Contextの要求は次の入力frameで適用します。同じframeのScript間で操作結果が変わりません。同一frameの複数要求は最後の要求が有効です。
- Context切替時に押し続けていたキーを、新しく有効になった操作のPressedとして再発火しません。切替後frameの新しい押下は受け付けます。
- 同じsnapshotを複数回読んでもedgeは消費しません。短いキーの押下・解放が同一frame内に起きた場合、ButtonのPressed/Releasedを両方返します。
- Game Viewの入力許可、focus喪失、Pad切断時は既存の入力抑止に従い、保持を解放します。
- Worldごとに評価状態を所有します。入力状態はLevel保存やScene Cloneへ混ぜません。
- Managed Reloadのfreeze中はContext変更を拒否します。

## 値と上限

- 最大32操作、1操作あたり最大32割り当て。名前とContextは最大63 UTF-8 byte。操作名は一意で空欄・空白・制御文字を許可しません。
- 種類はButton、Axis1D、Axis2D。1D値は-1〜1、2D値は単位円内です。
- 判定しきい値は0以上1未満。しきい値以下は0になります。Padの基本dead zone処理後に適用します。
- キーボード・マウスボタンはWindows Virtual-Key番号。UIの一覧にないキーもVK番号を直接入力できます。
- 対応入力元はKey、Pad Button、左右StickのX/Y、左右Trigger、Mouse Delta X/Y、Wheel。Pad slotは0〜3です。
- 寄与X/Yは入力値に乗算します。例: W=(0,1)、S=(0,-1)、右Stick X=(1,0)。Axis1DとButtonではYは使いません。
- Mouse Deltaも操作値の範囲へ制限します。pixel単位の生の差分が必要な視点操作では、既存の`World.ReadInput().MouseDelta`を使えます。
- ファイルはversion 1のJSON。未来version、不正値、重複名、未知fieldを拒否します。失敗時に現在の定義を置換しません。保存は一時ファイルからの原子的な置換です。
- プレイヤー別の再割り当て保存、複数Contextの優先順位・入力消費、長押し・ダブルタップ判定、マウスの相対ロックは今回のAPIに含みません。

## 関連実装と検証

Runtime/Input/InputMap.h/.cppが定義・保存・評価を担当し、LevelがWorldごとの状態を所有します。GameInput.hの固定DTOを既存Game ServicesのInput command経由で一括取得します。従来のInput.Read/ReadGamepadsと800-byte ABIは維持します。

EditorTestsのInputMapOperationsとInputMapPanelIntegrationが、保存・不正値・複数bindingの引継ぎ・tap・focus・切断・Context・軸・World分離・公開C++ API・Editor操作・外部編集保護・Play開始時の設定読込を検証します。
