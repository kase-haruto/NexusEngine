# モデルアニメーションの再生

アニメーションを含むglTF / GLBをEntityのメッシュに設定し、Detailsの「Component追加」から「アニメーション再生」を追加する。Playを開始すると、そのEntityだけのアニメーションが進む。同じモデルを複数Entityで使う場合も、再生時刻とクリップは独立する。

- アニメーション番号：モデル内のクリップ番号。0から始まる。
- 再生速度：1が通常速度、2が倍速。0で時刻を保持する。
- ループ：オフの場合は末尾の姿勢を保持する。
- 再生中：オフで一時停止する。
- 再生時刻（秒）：開始位置の指定や、一時停止中のシークに使う。

設定はLevelに保存される。編集時は指定時刻の姿勢を表示し、Play中だけ時刻を進める。PIEの一時停止・停止は既存のWorld分離に従い、編集Worldの設定を保持する。コンポーネントがないEntity、クリップが存在しないモデル、範囲外の番号はbind poseで描画する。

```cpp
#include "Runtime/Scene/Components/AnimationPlayerComponent.h"

auto* player = entity.AddComponent<NexusEngine::AnimationPlayerComponent>();
player->Play(0);       // 先頭から再生
player->Pause();       // 現在の姿勢で一時停止
player->Resume();
player->Stop();        // 先頭へ戻して停止
player->time = 0.5f;   // 秒単位のシーク
```

TRS補間とスキニングには既存のModelInstanceを使用する。通常描画・影・選択輪郭は同じ姿勢を使用する。アニメーション付きEntityは静的ライトベイクから除外する。負の速度、クリップ間ブレンド、ルートモーション、モーフアニメーションには対応しない。
