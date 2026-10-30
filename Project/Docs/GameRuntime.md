# ゲーム実行基盤

EditorのPlayと単体起動は、同じ`GameRuntime`とGame側の構築関数を使用する。
引数なしでは従来どおりEditorが起動し、`--game`ではEditor UIを生成せずゲームを起動する。
現段階では同じNexusEngine.exeに両方の起動経路を収めている。

## 起動

リポジトリのルートから実行する。

```powershell
# EditorでサンプルLevelを開き、ToolbarのPlayで回転を開始する
./Generated/Outputs/Debug/NexusEngine.exe --project ./Project --level Resources/Assets/Levels/RuntimeDemoLevel.json

# 同じLevelとゲームSystemをゲームウィンドウで起動する
./Generated/Outputs/Debug/NexusEngine.exe --game --project ./Project --level Resources/Assets/Levels/RuntimeDemoLevel.json

# GPU/Windowを作らず、起動・1フレームの更新・Asset解決・描画抽出を検査する
./Generated/Outputs/Debug/NexusEngine.exe --game --project ./Project --level Resources/Assets/Levels/RuntimeDemoLevel.json --check-game
```

`--project`はProjectルートを指定する。`--level`の相対パスはProjectルートを基準とする。
省略時は`Config/Project.settings`の`startupLevel`と`assetPath`を使用する。
Project指定がなければ、作業ディレクトリ、次にexeの配置先からProjectルートを探索する。
未知の引数、読込失敗、単体起動での有効primary Camera不在は終了コード1で報告する。
`--help`で引数の一覧を表示する。

通常ビルドでは既存のResources/Shaderコピーに加え、`Config/Project.settings`も出力先へコピーする。
これは起動に必要な共有設定のコピーであり、Assetの依存解析や配布用cook処理ではない。

## ゲームSystemの追加

ゲーム固有のComponent/System/ルールは`Game`側に置く。
`Game/SampleGame.cpp`は`RuntimeSpinner`という名前のEntityを毎秒1 radian回転させる最小例。
名前での対象選択はこのサンプルだけの規約であり、通常のゲームでは専用Componentで対象を選ぶ。

```cpp
NexusEngine::Result<void> SetupMyGame(NexusEngine::Level& level) {
    level.GetSystemScheduler().AddSystem<PlayerMovementSystem>();
    level.GetSystemScheduler().AddSystem<EnemySystem>();
    return {};
}
```

`main.cpp`の両起動経路から同じ構築関数を接続する。

```cpp
if(!editor.SetGameLevelSetup(SetupMyGame)) { /* 再生中などの設定拒否を処理する */ }
if(!game.SetGameLevelSetup(SetupMyGame)) { /* 設定拒否を処理する */ }
```

構築関数は新しい実行用Levelに一度だけ呼ばれる。SystemはSchedulerが所有する。
変換Matrixは構築完了時に計算するが、この時点でゲームSystemの`Update(0)`は呼ばない。
通常の更新順は登録順のSimulation System、その後Transform更新。
構築中の失敗・例外は`Result`へ変換し、半構築のLevelとSystemを破棄する。
Setup関数からは、そのLevelを指す参照を外部に保存したり、取り消せない副作用を実行しない。
捕捉したサービスは、構築関数と生成Systemより長く生存させる。

## Editorの再生状態

- Play: 編集WorldをCloneし、新しいLevelにGame側のSystemを構築する。
- Update: `EditorSession::UpdatePreview`から共通Runtimeを更新する。
- Pause: 描画は継続し、ゲームSystemのUpdateは呼ばない。負数/非有限deltaはPause中も拒否する。
- Stop: 実行用LevelとSystemを破棄し、編集Worldへ戻す。
- 再Play: 編集Worldから再び構築し、以前のSystem内部状態は引き継がない。

編集World、選択、Undo履歴、dirty状態はPlayによるゲーム更新では変更しない。
Play開始失敗の詳細はToolbar操作時の通知へ表示する。
Live++のComponent復元ではTransformだけを再計算し、ゲーム更新を追加実行しない。
Systemのメンバ配置変更と内部状態の移行は今回のReinstancing対象外。

## 単体起動の描画と現在の範囲

`GameApplication`がLevel、CPU Asset Library、ModelInstance、RenderSceneを保持する。
Frameworkは従来の`IFrameworkUpdateClient`からUpdateとPrepareRenderを呼び、
PrimitiveRendererが抽出済みの値snapshotを描画する。RuntimeからEditorへの依存はない。
旧Levelの`:startup`参照はGame側から渡したLoaderで解決し、その参照がなければSponzaをロードしない。

GPU Assetは起動時に使用中のMesh/Texture/組み込み形状を準備する。
同じ準備済みAssetのEntity生成・Transform更新は可能だが、実行中に新しいAssetや材質Variantへ
切り替えるとエラーを返す。動的GPUアップロードは今後の範囲。
Animationは既存の統合モデル姿勢を使う。Entityごとの独立したClip再生は未対応。
Level遷移、ゲーム進行の保存、Input、固定時間刻み、Physics、Audioは含めない。
独自Componentの一覧・編集・Level保存は、[ゲーム固有Componentの追加と保存](CustomComponents.md)を参照する。

## 検証

`NexusEditorTests`の`GameRuntimeTests.cpp`で次を検査する。

- 共通構築関数の呼出し回数、Systemの寿命、停止と再開、再入/構築変更拒否
- 編集Worldの保護、Pause中の更新抑止、Simulation後のMatrix反映
- Setupの失敗・例外・LevelのUnload時のrollbackと再試行
- サンプルのEditor/単体起動でのWorld Matrix一致、Asset範囲、Camera抽出
- 不要なstartupモデル読込の回避、未準備Assetの拒否、読込失敗後の再試行

`--check-game`はCPU側の統合確認用で、GPU描画やウィンドウ操作の検証は含まない。
