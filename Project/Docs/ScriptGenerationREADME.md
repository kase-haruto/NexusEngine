# エディタから生成するC#・C++ Behaviour

参照保存・時計・タイマー・固定更新・Collider／衝突レイヤーの追加APIは[利用ガイド](GameReferencesTimePhysicsREADME.md)を参照してください。

Tools → Project Modules / Build / Packageを開き、Source ModuleとSource nameを指定してCreate Behaviourを押します。C++ではSource typeをEntity Behaviourにします。C#・C++ともに、ゲーム処理を書く5つの関数と、呼ばれるタイミング・書く場所を説明する日本語コメントを生成します。

| 関数 | 書く内容 |
|---|---|
| `OnCreate` | 関連付けの実行インスタンスを作る際の初期化。無効状態でも呼ばれる |
| `OnEnable` | 有効化時の動作再開、イベント購読。再有効化でも呼ばれる |
| `OnUpdate(float deltaTime)` | 有効な間の入力・移動・ゲームルール。deltaTimeは秒。Pause中は呼ばれない |
| `OnDisable` | 無効化・解除・停止時、動作中だった処理を止める。再有効化できる状態を残す |
| `OnDestroy` | 自分が所有する資源、購読、Task/Threadの最終的な後片付け |

Unityの`Start`や`Update`という名前をそのまま追加しても自動実行されません。NexusEngineで実際に呼ばれる上記の関数を生成します。FixedUpdate等の未提供の関数は用意しません。

## C#

`名前.cs`へ`ScriptBehaviour`を継承するclassを生成します。関数本体は空です。従来のPlayerControllerサンプルの移動処理や自動ログ出力は複製しません。`System.Numerics`もimport済みです。

必要な変数・公開プロパティはclassの先頭へ追加します。`[Expose]`の書き方はコメントで示します。Transformのコピーと書き戻し、所有thread、リロードでの再初期化、終了処理でEntityが破棄済みの場合があることも説明します。

作成後はBuildまたはReload C#を行い、Detailsで生成した型をScriptとして関連付けます。型IDは`ModuleId.behaviours.ClassName`です。保存済みシーンとの関連付けを保つため、クラスを改名する場合もIDを無断で変えないでください。

## C++

`名前.h`へ宣言と状態、`名前.cpp`へコンストラクターと5つの関数定義を生成します。ゲーム処理は.cpp内の「ここに」のコメント位置へ書きます。空の本体は`NEXUS_GAME_OK`を返し、Entityを勝手に動かしません。

自分のEntityのTransformや公開値は借用した`context_`で操作します。APIの失敗statusは確認して返します。`noexcept`関数から例外を出さないようにし、終了処理ではEntity/Worldを変更せず所有資源の解放を行います。

C++ Behaviourの作成は独立Native Game Moduleの標準scaffoldに対応します。旧静的C++ Moduleでは、ProjectパネルのAdd ModuleでC++ Moduleを追加して、そのModuleを作成先に選んでください。独自に登録処理を変更したModuleは、内容を勝手に上書きせず作成を拒否します。

型情報、既存Schedulerへの登録、services接続も生成し、通常Buildとホスト再起動後にDetailsからComponentとして追加できます。型IDは`ModuleId.behaviours.ClassName`です。ScriptComponentへ登録するC#とは追加方法が異なります。

`NexusGeneratedBehaviours.h/.cpp/.json`は登録用ファイルです。ゲーム処理はそこへ書かず、生成した各Behaviourの.h/.cppへ書いてください。別のBehaviourを追加しても、既存のBehaviour本体は変更しません。同名ファイルの上書きは拒否します。生成済み登録ファイルを手で変更した場合も、内容を保護して追加生成を拒否します。

既存の一括処理用Native SystemはSource typeのNative System (advanced)から作成できます。CLIでは`Add-GameSource.ps1 -Kind System`を指定します。指定なしの既定はBehaviourです。

CLIもエディタと同じ生成処理を使います。

```powershell
./Project/Tools/Project/Add-GameSource.ps1 `
  -ProjectFile C:/Games/MyGame/MyGame.nxproject `
  -EngineManifest C:/Engines/NexusEngine/NexusEngine.nxengine `
  -ModuleId Gameplay -Name PlayerController -Kind Behaviour
```

既に作成済みのスクリプトは書き換えません。新しいテンプレートを利用するには、新しいエンジンのソース生成ツールとTemplatesが必要です。
