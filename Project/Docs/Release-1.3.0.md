# NexusEngine 1.3.0

Windows x64 / Release。C#ゲーム開発API、スクリプト生成、コライダーの可視化と衝突レイヤー、ランチャーの導入・更新を追加した版です。1.2.1のMono Runtime同梱・初回ビルド修正も引き継ぎます。

## ゲーム開発とスクリプト

- C#の数学API：Vector2/3/4・Quaternion・Matrix4x4と、変換・補間・座標変換などの補助関数。
- DebugのInfo/Warning/Error、Assert、例外のstack trace、線・球・箱・軸の描画。ゲーム画面とエディタの描画経路に対応します。
- `[Expose] EntityReference` / `ComponentReference`の保存とDetailsでの選択。実行中のHandleを保存せず、Entityの永続IDとComponentの安定Type IDを解決します。
- ゲーム時間／実時間、TimeScale、固定更新の時計とC#の取消可能なタイマー。
- C# / C++の`OnFixedUpdate`。通常のゲーム実行を既定60Hzの固定物理stepで更新し、通常OnUpdateはframeごとに実行します。
- エディタから生成するC# / C++ Behaviourに、初期化・有効化・通常更新・固定更新・無効化・破棄の関数と日本語コメントを配置します。

## コライダーとエディタ

- Box / Sphere / Capsule / Mesh / Convex Meshを形状名で選択。C#の`GameCollider`からも設定できます。
- 32個の所属レイヤーと相互衝突mask。レイヤーを指定したRaycastに対応します。
- 選択したコライダーのワイヤーフレームを標準表示。ビューポート右上の「表示」の詳細メニューから、選択のみ／全体／非表示と、無効化された形状の表示を切り替えられます。
- Mesh / Convex MeshはJoltでcookingした実際の衝突形状を表示し、node transformとConvexの重心offsetに対応。Play中の実行Worldにも追従します。
- Viewport右上の設定を一列に保つ配置、利用できないReloadの無効表示、不要なReload状態テキストを整理しました。
- 新規テンプレートを空のプロジェクトと、Camera・Sky・Cube・照明を持つBasic 3Dとして明確にしました。

## ランチャー

- プロジェクトを開く際の不要なビルド処理と画面の停止を改善しました。
- ランチャー自身も一覧から版を選び、サイズ・SHA-256を検証したsetupで更新できます。新しいLauncher setupを同時公開します。

## 移行と制約

既存の公開版とプロジェクトの関連付けを保持します。移行するHost / Native Game DLL / C#スクリプトは1.3.0のSDKで再ビルドしてください。C#の新API・固定更新には対応するBootstrapが必要です。実行可能なC#プロジェクトにはManaged Gameplay、C++併用にはHybrid Gameplayテンプレートを使用してください。Blank / Basic 3DでC#を選ぶ経路は初期Module宣言のみです。従来のNative Module/Systemの通常Updateはframeごとのままです。物理に関係する処理はOnFixedUpdateへ置くことを推奨します。

Triangle MeshはStatic / Kinematic用で、DynamicにはConvex Meshを使います。親付き物理Entity、1 Entity内の複数Collider、Constraint、Character Controllerは未提供です。コライダー表示はエディタ用overlayで、全体表示は最大32,768線です。ComponentReferenceはEntity上の現在の指定型を指し、同型Script attachment個別の参照ではありません。PrefabやEntity複製時の参照IDの自動置換は行いません。タイマーのcallbackや残り時間は保存・Reloadに引き継がず、OnCreateで登録し直します。

開発にはWindows x64、DirectX 12対応GPU、Visual Studio C++ v145、Windows SDK、.NET SDKと.NET Framework 4.8参照アセンブリが必要です。Mono RuntimeはSDKに同梱し、別途Mono SDKをインストールする必要はありません。CoreCLRプロジェクトには従来どおり対応する.NET環境が必要です。全GPU・DPI・解像度・周辺機器の実機操作は未検証です。

利用方法は[参照・時間・物理](GameReferencesTimePhysicsREADME.md)、[数学・Debug API](GameMathDebugREADME.md)、[スクリプト生成](ScriptGenerationREADME.md)を参照してください。今回の検証記録は[Release-1.3.0-Verification](Release-1.3.0-Verification.md)に記載します。
