# ゲーム固有Componentの追加と保存

`Project/Game`以下のヘッダーにReflection宣言を付けると、ビルド時に次の登録を生成する。

- Detailsの「Component追加」一覧
- 「その他」カテゴリのプロパティ編集
- Undo/Redo、Play時のWorld複製、Live++のComponent復元
- Levelファイルの保存・読込とゲーム単体起動での復元

## 定義例

`Game/Components/MoveComponent.h`が動作するサンプル。

```cpp
#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include <cmath>

NEXUS_REFLECT_COMPONENT(NexusGame::MoveComponent, "Game.Move", "component.gameMove", speed)

namespace NexusGame {
struct MoveComponent {
    float speed = 3.0f;
    bool IsValid() const noexcept { return std::isfinite(speed); }
};
}
```

宣言の引数は、完全修飾型名、安定した型ID、表示用の翻訳キー、公開・保存するメンバの順。
型IDは他のComponentと重複させず、保存済みLevelを使う間は変更しない。
メンバのC++名を変更する際は`newName = oldStableId`と指定して保存上のフィールドIDを維持できる。
生成器は制限された宣言形式だけを解析するため、マクロの引数を別のマクロで組み立てない。

`Resources/Editor/Localization/en.lang`と`ja.lang`へ表示名を追加する。

```text
component.gameMove "Movement"
speed "Movement Speed"
```

```text
component.gameMove "移動"
speed "移動速度"
```

翻訳がない場合はキー自体を表示する。`GeneratedReflection.h/.cpp`は編集しない。
通常の本体/Editor/ECSテストのビルド時に`Tools/Reflection/Generate-Reflection.ps1`が再生成する。
今回のComponentはヘッダーだけなので、生成コードからのincludeでコンパイルされる。
実装を.cppへ分ける場合は、その.cppも利用先のビルド構成へ追加する。

## Editorでの操作

1. Entityを選択し、Details上部の「Component追加」から「移動」を選ぶ。
2. 「その他」カテゴリの「移動速度」を編集する。
3. Levelを保存する。再度開くとComponentと値が復元される。
4. Playすると、`Game/SampleGame.cpp`のMoveSystemがその速度でX方向へ移動させる。

Componentの追加・値編集は既存の履歴サービスを通り、Undo/Redoできる。
再生中はComponent追加を無効化し、Play中の更新は編集Worldへ反映しない。
Componentはデータなので、独自の動作を実装する際は対応するSystemもGame側で登録する。
現在のMoveSystemはサンプル用であり、入力や衝突判定は行わない。

## 対応するデータ

汎用Inspectorはbool、int、uint32_t、float、string、float配列（2/3/4要素）を編集できる。
uint32_tのInspectorでの編集範囲は0〜INT32_MAX。
保存用Reflectionはこれらに加え、既存Codecが対応する算術型・enum・array・vector・optional・
`NEXUS_REFLECT_VALUE`で登録した構造体を扱える。複雑な型の専用Inspectorは別途必要。

保存するComponentは既定構築でき、ECSの要求するnoexcept move操作に対応すること。
`IsValid()`がある場合、読み込み・保存・Inspectorの値変更時に検証する。
GPU Resource、ポインタ、サービス参照などは保存メンバへ含めない。
別Entityへの永続参照にはPersistentIdを使う。独自Component内のEntityIdをロード時に自動再解決する機能はない。

## 保存形式と失敗時の動作

新しく保存するLevelは`NEXUS_LEVEL 12`。各Entityに`COMPONENTS`節を追加し、
Componentの型IDとReflectionの値ツリーを可読テキストで記録する。
既存の組み込みComponentは従来の専用レコードを維持し、version 1〜11も読める。
新しいv12ファイルは旧エンジンでは読めない。

追加されたフィールドがファイルにない場合はC++の既定値を維持し、削除されたフィールドは読み飛ばす。
型そのものが未登録の場合は、データを黙って捨てずに読込エラーを返す。
型の不一致、値域超過、重複Component、重複フィールド、破損データも拒否する。
読込失敗時は現在のLevelを交換しない。
保存時は独自Componentの検証とテキスト化をファイルを開く前に行う。
書込中のI/O失敗に対するファイル自体のatomic replacementは従来どおり今後の範囲。

## 検証

`Tests/ReflectedComponentTests.cpp`が、実際のDetails追加メニューの選択、「その他」表示、
追加/値編集のUndo/Redo、Level往復、未登録・重複・破損・型不一致・非有限値の拒否、
フィールドの追加/削除時の既定値、Live++用キャプチャ、保存した移動速度によるゲーム実行を検証する。
