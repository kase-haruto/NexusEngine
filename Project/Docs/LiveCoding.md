# Live++・Reflection・Reinstancing

## 操作

公式 Windows SDK **2.11.4** を `Project/ThirdParty/LivePP` に展開済み。
SDK、ローカル設定、ライセンス情報は Git 管理対象外。
配布元と ZIP のハッシュは `ThirdParty/LivePP.UPSTREAM.md` を参照する。

1. Debug または Develop / x64 を通常ビルドしてエディタを起動する。
2. 必要なら Broker 側で所有済みライセンスを有効化する。
3. ソースを変更して保存し、`Ctrl+Alt+F11` を押す。
4. Broker のコンパイル結果と、`Live++` カテゴリの復元成功ログを確認する。

Live++ の pre-build step でも型情報を生成するため、登録宣言の変更を通常ビルドなしで反映できる。
安全な破棄順を保証するため、コンパイル開始前から同期し、コンパイル中はフレーム更新を待機する。
新規 CPP やビルド依存の追加は通常ビルドが必要。Broker の Hot-Restart は処理しない。
Visual Studio のネイティブ Hot Reload / Edit and Continue は競合を避けるため無効化する。

## 型登録

CalyxEngine のマーカーから登録コードを生成する方式を参考に、NexusEngine の ECS に合わせて実装した。
生成器は C++ 全体を解析せず、明示的な宣言マクロの引数を読み取る。

```cpp
#include "Foundation/Reflection/ReflectionMarkers.h"

NEXUS_REFLECT_COMPONENT(HealthComponent, "Health", "component.health", current, maximum)
namespace NexusEngine {
struct HealthComponent {
    float current = 100;
    float maximum = 100;
};
}
```

引数は型名、安定型 ID、Inspector の表示キー、保存するメンバの一覧。
マーカーは対応ヘッダへ記載し、型は `NexusEngine` 名前空間内に定義する。
型 ID は互換性のため維持する。メンバ名と保存 ID を分ける場合は `member=stableId` と書く。

```cpp
NEXUS_REFLECT_COMPONENT(CameraComponent, "Camera", "component.camera",
    verticalFieldOfViewRadians=fov, nearClip=near, farClip=far, primary, enabled, smallFrustum=frustumSize)
```

ネストした値型は `NEXUS_REFLECT_VALUE(TypeName, "StableTypeId", member1, member2)` で登録する。
内部 Component は表示キーを空文字列にして Inspector から隠せる。
マーカーにないメンバは移行せず、新しいコンストラクタの既定値から再構築する。
Transform の派生 Matrix は保存せず、復元後の Update で再計算する。
新しく保持したいメンバを追加した場合は一覧にも追加する。

`Tools/Reflection/Generate-Reflection.ps1` が Foundation / Runtime / Graphics / Editor / Samples のヘッダを走査する。
通常の MSBuild と Live++ の pre-build step で同じ生成器を実行し、変更のある生成物だけ更新する。
重複 ID、不正なメンバ指定、未対応の引数は生成エラーにする。

- `Runtime/Reflection/Generated/GeneratedReflection.h`: 型 ID とメンバアクセサ。
- `Runtime/Reflection/Generated/GeneratedReflection.cpp`: Component と Inspector の登録関数。

生成ファイルは手で編集しない。

## Inspector と状態保存

既存の `TypeRegistry` の専用 getter/setter と検証を維持し、生成情報で未登録項目を補う。
新規 Component の bool / int / uint32 / float / string / float 配列（2〜4要素）は既定 Inspector を生成する。
uint32 の既定編集は int の範囲に限定する。ネスト値型やコンテナの専用 UI は自動生成しない。

Reinstancing は表示項目だけでなく、マーカーに指定した全メンバを保存する。
bool、符号付き／符号なし整数、浮動小数、文字列、std::array、std::vector、std::optional、
登録済み値型に対応する。ポインタ、GPU ハンドル、std::function は保存しない。
既存 `LevelSerializer` の Level ファイル形式は維持し、ライブ移行の値ツリーとは分けている。

## パッチ時の処理

1. Framework のフレーム境界で、投入済み Direct Queue の GPU 完了を待つ。
2. `ReinstancingService::Prepare` が全参加者の状態を保存し、回復ファイルを書き出す。
3. Live++ の pre-patch hook で Workspace と旧 Component Storage を破棄する。
4. Live++ が新コードを適用する。
5. post-patch hook が新 Registry とファクトリで Storage と Workspace を再生成する。
6. 両 World の Transform を再計算し、登録と参照を再接続して次フレームへ戻る。

編集用／Play 用の `Scene` と `Level` ホストは維持し、新しい `ComponentStorage<T>` と配列へ値を復元する。
Entity slot、世代、free list を保存するため、EntityId と親子参照を維持する。
PersistentId、選択、ドキュメントパス、Level 設定、Play/Paused 状態も維持する。
Workspace は Panel、型アクセサ、Command、編集サービスごと再生成し、カメラ、Panel 表示、DPI、
個人設定、Docking 配置、未保存状態を復元する。ImGui context と GPU 描画基盤は維持する。
編集中の値は保存前に確定し、アクティブ Widget を解除する。
Undo/Redo、開いている確認・ファイルダイアログ、Panel 独自の一時状態は引き継がない。

毎回対象全体を再生成し、`modifiedClassLayouts` による型変更検出は前提にしない。
関数本体だけの変更でも再生成する。Scheduler は現状逐次実行のため、ワーカー導入時には
同じ境界で停止・合流する処理を追加する。

## 対応範囲

| 変更 | 動作 |
| --- | --- |
| Component のメンバ追加 | 新しい既定値を使い、既存項目を復元 |
| メンバ削除・並べ替え | ID で復元し、削除項目は無視 |
| Workspace・Panel・所有サービスのメンバ変更 | Workspace 全体を再生成 |
| 保存プロパティの互換性がない型変更 | 復元エラー。明示的な変換が必要 |
| 使用中 Component 型自体の削除 | 復元エラー。インスタンスの事前削除か通常再起動が必要 |
| 未登録 Component が存在する World | 保存時に更新を拒否し、旧 World を維持 |
| 固定ホストのヘッダ変更 | 保存・通常ビルド・再起動が必要 |

固定ホストは main のスタック上にある EditorApplication と PrimitiveRenderer、Framework、
Scene/Level のホスト、System、GPU 管理、ImGuiRenderer、UI Context、移行管理、snapshot 自身の型。
共有数学型や Graphics 値型も固定ホストから値で参照されるため、ヘッダ変更時は通常再起動する。
これらを実行中に移行する実装は含めていない。

起動時に固定領域のヘッダを記録し、変更時はパッチ前に保守的に拒否する。
そのため固定ヘッダ内のインライン関数だけの編集も再起動が必要。CPP 内の関数変更は反映できる。
拡張登録コールバックは、再生成対象のポインタや可変レイアウトの状態を長期間捕捉しないこと。
独自の外部所有オブジェクトは `ReinstancingService::Participant` として登録し、
非所有参照も release / restore で切断・再接続する。

## 失敗時と回復

snapshot 保存に失敗した場合は SDK の Reload を呼ばず、旧データを維持してログへ原因を出す。
コンパイル失敗でフックが走らなかった場合もインスタンスを維持する。
パッチ後の復元失敗は旧コードへ戻そうとせず、Framework の通常エラー処理で終了する。
準備済み snapshot がない状態で pre-patch フックが走った場合は、不正レイアウトの利用を防ぐため即時終了する。

直前の状態は `Generated/LiveCoding/Recovery_<PID>.nxreload` に残し、複数プロセス間の衝突を避ける。
`ReadReflectionArchive` で読み取れる値ツリーで、uint64 の永続 ID も精度を失わない。
Component と Entity 配置を含み、空 Scene への `ComponentReflection::Restore` にも利用できる。
起動時の自動回復 UI や既存 Level JSON への変換は実装していない。
ファイルは Windows x64 のローカル回復用であり、一般 Asset の交換形式には使わない。

## ビルド設定

SDK があれば Debug / Develop で自動有効化し、Release では無効化する。
`LivePP.props` が必要なコンパイラ・リンカ設定を適用する。
SDK 未配置でも通常ビルドできるが、明示有効化して SDK がない場合はエラーになる。

```powershell
MSBuild.exe Project/NexusEngine.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:NexusLivePPEnabled=false
MSBuild.exe Project/NexusEngine.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:NexusLivePPEnabled=true /p:NexusLivePPDir=C:/SDK/LivePP
```

VS から設定する場合は Git 管理対象外の `Project/LivePP.local.props` を作成する。

```xml
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <NexusLivePPDir>C:/SDK/LivePP</NexusLivePPDir>
    <NexusLivePPEnabled>true</NexusLivePPEnabled>
  </PropertyGroup>
</Project>
```

## 検証

`NexusEditorTests` の `ReinstancingTests.cpp` が、サイズの異なる旧／新 Component を使い、
追加・削除・並べ替え・既定値・Entity 世代・親子参照・永続 ID・連続更新を検証する。
回復ファイルの往復、型不一致、未登録型、保存／復元失敗、コンパイル失敗相当の経路、
Workspace の再生成、選択・dirty・Play/Paused・カメラ・Panel 表示の維持も検証する。

`Tests/NexusLiveCodingSmoke.vcxproj` は GPU を使わず、実際の SDK と生成された型情報で検証する任意のプロジェクト。
リポジトリのルートを作業ディレクトリとして起動し、`Generated/LiveCodingSmoke/Request` を作ると
`ScheduleReload` を呼ぶ。同ディレクトリの `Stop` で終了する。成功時はログへ `PATCH VERIFIED` と型サイズを出す。

Live++ 2.11.4 の実パッチで NameComponent の先頭へのメンバ追加と削除（40 → 48 → 40 byte）、
追加メンバの既定値、および既存文字列の維持を確認済み。
検証ログは `Generated/LiveCodingSmoke/Startup.log` に残す。検証用のメンバは実装へ残さない。

## 参考

- CalyxEngine の `Tools/Reflection`、`LivePPService`、`LevelEditor` の snapshot フック。
- [Live++ 公式ドキュメント](https://www.liveplusplus.tech/docs/documentation_jpn.html)
- [公式リリース](https://www.liveplusplus.tech/releases.html)
