# 共通Input / Audio / Asset / Level API

## 構成

既存のGameServices C ABIへ予約commandを追加し、C++ Game ModuleとNexus.Managedが同じLevelを操作する。別ECS・Asset Manager・Inspectorは作らない。EditorはPlay Runtimeへ値の入力を渡し、Managed objectやAudio deviceを所有しない。

| 領域 | C++ | C# | Native実装 |
| --- | --- | --- | --- |
| Input | `NexusGame::Input::Read` | `World.ReadInput()` | ProjectWindow → InputFrame → Level |
| Audio | `NexusGame::Audio` | `World.PlayAudio()`、`AudioVoice` | Level所有AudioWorld → XAudio2 |
| Physics | `NexusGame::Physics` | `World.Raycast()`、EntityのForce/Impulse | 共通ECS → Jolt |
| Asset照会 | `WorldControl::AssetExists` | `World.AssetExists()` | 既存Project Root/path解決 |
| Level遷移 | `WorldControl::RequestLevel` | `World.RequestLevel()` | GameRuntime → 既存LevelSerializer |

全commandは既存のWorld token、Entity世代、所有thread、client寿命、byte数と上限を検査する。関数表の既存V1 layoutは変えない。C++例外、STL、Native pointer、GCHandleを転送しない。`Public/Nexus/GameInput.h`、`GameAudio.h`、`GameWorldControl.h`とManagedの明示Compile/SDK収録が対応する。

## Input

Window messageから保持/押下/解除を採取する。OS repeatを押下edgeとして再生成せず、focus喪失で保持を解除する。InputFrameはLevelの保存・Scene Clone対象にしない。複数Scriptが同じedgeを読んでも消費しない。

EditorではGame Viewのfocus、テキスト入力、popup、描画領域外クリックを検査する。ゲームへ渡すmouse位置はView相対へ変換する。非表示・UI入力時はPlay Worldの保持状態を解除する。`BoundedFrameClient`も入力を転送し、通常Hostとframe制限Hostで動作を分けない。

`nexus.engine.input`は空request/空targetに対して800 byteを返す。256 keyのdown/pressed/released、mouse位置/差分/wheel、uint32 focusとreservedが内容で、key番号はWindows Virtual-Key。高頻度ループでは一回取得したsnapshotを再利用する。

```csharp
var input = World.ReadInput();
if (input.Down('W')) { /* 同じWorldのTransformを変更する */ }
```

1.1.1ではWindows keyboard/mouseに加えてXInput gamepadを4台まで扱う。同じInput commandへoperation=1の明示requestを追加し、800 byteの既存snapshotを維持した。`World.ReadGamepads()` / `Input.ReadGamepads()`、button edge、analog、focusと接続の契約は[ゲームパッド入力](GamepadInputREADME.md)を参照。Action mapping、relative mouse lockは未提供。

## Audio

AudioWorldは初回Playまでdefault deviceを作らない。音声未使用・headless起動をdevice不在だけで失敗させない。WAVは既存Project path解決を通し、Root外やlinkによる逸脱を拒否する。PCM 8/16/24/32 bitとIEEE float 32 bit、mono/stereo、8〜192 kHzに対応し、RIFF/chunk長・padding・format整合・非有限sampleを検証する。

sampleはSource voiceのDestroyVoice完了まで保持する。VoiceはProcess非再利用tokenとservices client所有者で識別する。別clientの停止/音量変更は拒否し、client終了でその全voiceを停止する。Level終了は全voice、sample cache、mastering voice、XAudio2、取得済みCOM参照の順に解放する。APIと破棄は所有threadに限定し、Audio callbackからGame/Managedを呼ばない。

`nexus.engine.audio`は24 byte headerとUTF-8 pathを使う。Playはuint64 token、IsPlayingはuint64の0/1、Stop/SetVolumeは空応答。1file 32 MiB、cache 64 MiB、同時128 voice、音量0〜4に制限する。World pauseはengine処理を停止して再生位置を保つ。

```csharp
private AudioVoice? voice;
protected override void OnUpdate(float dt) {
    voice ??= World.PlayAudio("Assets/Audio/music.wav", 0.5f, loop: true);
}
protected override void OnDestroy() { voice?.Dispose(); }
```

AudioはScene snapshotで巻き戻せないため、Managed Reloadのfreeze中は再生操作を拒否する。OnCreateで再生を必須にせず、実行再開後のUpdateから開始する。旧世代cleanupのDisposeは新Worldへの書込を避け、client解放が旧voiceを止める。コンパイル失敗で現行Domainを維持する場合、現行voiceも維持する。

形式追加、streaming、3D spatializationやmixerは既存AudioWorldの拡張点である。別のManaged Audio Managerを作らない。APIのbuffer寿命とvoice破棄は[Microsoft公式の再生手順](https://learn.microsoft.com/en-us/windows/win32/xaudio2/how-to--play-a-sound-with-xaudio2)、OS依存は[XAudio2 versions](https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-versions)に従う。

## Assetと動的描画資源

AssetExistsはProject相対fileの照会であり、新しいasset所有システムではない。モデル・テクスチャは従来どおりPrimitiveRender等の共通Componentの参照で指定する。

単体GameApplicationでも、実行中の追加・変更を既存ModelAssetLibraryの共有source cacheとcopy-on-write候補へ解決する。CPU候補が完成したらHostへ同期公開を委譲する。Hostは既存GraphicsSystem/PrimitiveRendererのPrepareModelResources、GPU完了待ち、CommitPreparedModelを利用し、CPU snapshot交換とRenderer pointer更新を同じframe境界で行う。旧snapshotは公開完了まで保持する。

IFrameworkUpdateClientのAttachGraphics/DetachGraphicsはこの借用寿命を定義するために追加した。BoundedFrameClientはこれも転送する。GameApplication自身は具体Rendererの実装へリンクせず、CPU回帰/headless経路を維持する。HostのAssetPublisherは同期callbackで、commit関数を保存したり二重に呼んだりしない。Editorの既存非同期asset準備経路は維持する。

## Level遷移

RequestLevelはProject内のfile参照を検証して要求する。同一frameの同じ要求は冪等、競合する別Level要求は拒否する。要求受理とロード完了は異なる。

1. 現行のNative/Managed callbackとSchedulerのstackが戻る。
2. 次Levelを既存Serializerで候補として読み込む。
3. 読込に失敗した場合はConsoleへ通知し、現行Worldを維持する。
4. Native DLLのコードimageを一時leaseで保持する。Live++のパッチをUnloadで失わないために必要で、Game stateを保持するものではない。
5. 既存順序でManaged、Native System/Module、Bridge、Levelを停止する。
6. 同じProjectの設定・資源Root・Reflectionを使って新しいLevelとGame codeを開始する。

停止後のゲーム初期化失敗に自動rollback保証はない。破棄失敗は既存Quarantineを保つ。Editorの編集documentは交換せず、Play Worldだけを変更する。保存Level/Assetの再作成は必要ない。Level遷移の要求はReload freeze中には拒否する。

## 実行確認

Inputのrepeat/edge、focus喪失、同じsnapshotの複数取得、誤thread・client失効、実XAudio2の無音WAV、Pause/Volume、Root逸脱、不正WAV、Reload freeze、owner解放をEditorTestsで実行し、終了コード0を確認した。Jolt基本物理も同じ回帰で成功した。

同型Script、Native値移行、動的asset公開、Level遷移を含む最新EditorTestsと、Mono/CoreCLR統合のexit 0を確認した。Release SDKの発行とGPU smoke、実Editorの追加/保存/Play/Stopも成功した。記録は`Generated/GameplayCompletion-1.1.0-13d4bcbd47824bd884f0331df9d7f703`へ保存し、Release発行前に更新する。
