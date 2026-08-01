# NexusEngine AI Development Guidelines

このドキュメントは、NexusEngine のコードを編集・追加する AI エージェント向けの開発指針です。

NexusEngine では、単に動作するコードを書くことではなく、責務分離・保守性・拡張性・可読性を重視します。

既存設計を尊重し、プロジェクト全体で統一された設計思想とコーディングスタイルを維持してください。

---

# 1. 基本方針

コードを変更する前に、必ず対象周辺の既存実装を確認してください。

以下を確認せずに実装を開始してはいけません。

* 対象クラスの責務
* 呼び出し元
* 依存しているクラス
* 所有権
* ライフタイム
* 既存の類似実装
* 命名規則
* フォルダ構成
* Editor / Runtime / Renderer / Resource / Scene / Game の境界

既存設計で解決可能な場合は、新しい仕組みを追加するより既存設計を利用してください。

ただし、既存設計に明確な責務違反や拡張性の問題が存在する場合は、問題点と変更理由を説明した上で改善してください。

---

# 2. 設計思想

NexusEngineでは以下を重視します。

## 単一責任

1つのクラスに複数の大きな責務を持たせないでください。

Manager クラスに処理を集中させることを避け、必要に応じて専用クラスへ分離してください。

例:

悪い例:

```cpp
class SceneManager {
    void LoadScene();
    void SaveScene();
    void RenderScene();
    void ImportAsset();
    void DrawEditor();
};
```

可能であれば以下のように責務を分離します。

```text
SceneManager
SceneSerializer
SceneRenderer
AssetImporter
SceneEditorPanel
```

---

# 3. 依存方向

依存関係はできるだけ一方向にしてください。

基本的な考え方:

```text
Game
 ↓
Engine Runtime
 ↓
Renderer / Resource / Scene
 ↓
Foundation
```

Editor は Runtime の機能を利用して構いませんが、Runtime が Editor に依存してはいけません。

ゲーム固有コードを Engine 側へ追加してはいけません。

---

# 4. Engine / Game の責務

Engine は汎用機能を提供します。

例:

* Renderer
* Resource管理
* Scene管理
* Serialization
* Input
* Audio
* Physics
* Editor基盤
* Asset管理

Game側はゲーム固有の処理を管理します。

例:

* Player
* Enemy
* Boss
* GameRule
* Stage固有処理
* ゲーム固有UI
* ゲーム固有イベント

ゲーム固有の都合による分岐を Engine に直接追加してはいけません。

---

# 5. Runtime / Editor の分離

Runtimeで必要のないEditor処理をRuntimeクラスに直接追加しないでください。

悪い例:

```cpp
class Transform {
public:
    void Update();
    void DrawImGui();
};
```

可能であれば、

```text
Transform
TransformInspector
```

のように分離します。

RuntimeデータをEditorから操作する構造にしてください。

---

# 6. Renderer 設計

DirectX12関連コードでは特に以下を明確にしてください。

* GPUリソースの所有者
* CPU側オブジェクトの所有者
* Descriptorの所有者
* Resourceの破棄タイミング
* Fenceとの関係
* CommandListの使用タイミング
* Frame単位のResource
* Upload Resourceの寿命

COMオブジェクトは原則として `Microsoft::WRL::ComPtr` を使用してください。

裸の `new` / `delete` による所有権管理は避けてください。

---

# 7. Resource管理

Resourceのロード処理と利用処理を分離してください。

例えばモデルの場合、

```text
ModelResource
ModelLoader
ModelInstance
ModelRenderer
```

の責務を混ぜないようにします。

Asset情報とGPU Resourceも可能な限り分離してください。

---

# 8. Scene設計

Sceneはゲームオブジェクトを管理しますが、すべての機能をSceneManagerへ集中させないでください。

シリアライズ、ロード、遷移、描画などは必要に応じて分離してください。

例:

```text
SceneManager
SceneSerializer
SceneTransitionService
SceneObjectRegistry
```

---

# 9. 拡張性

機能追加時には、

「同じ種類の機能が今後増えた場合どうなるか」

を考えて設計してください。

例えばポストエフェクトを追加するとき、

```cpp
if (effect == Bloom)
if (effect == Vignette)
if (effect == CRT)
```

のような巨大な分岐を増やし続ける設計は避けます。

可能であれば、

```text
IPostEffect
 ├ BloomEffect
 ├ VignetteEffect
 └ CRTEffect
```

など拡張可能な構造を検討してください。

ただし必要以上の抽象化は行わないでください。

---

# 10. データドリブン

設定値として扱えるものは、可能な限りコードへハードコードしないでください。

例:

* Editorレイアウト
* Rendering設定
* Effectパラメータ
* Scene設定
* Asset設定
* Camera設定

ただし、内部実装上固定すべき定数まで外部データ化する必要はありません。

---

# 11. 命名規則

既存コードの命名規則を最優先してください。

新規コードではC++標準ライブラリに近い自然な命名を使用します。

基本例:

```cpp
class GraphicsDevice;

struct WindowDetail;

void Initialize();
void Update();

int frameCount;
float deltaTime;
```

略語を過剰に使用しないでください。

意味が曖昧な名前は禁止します。

悪い例:

```cpp
int num;
float val;
auto data;
void Process();
```

可能であれば目的が分かる名前にします。

```cpp
int frameIndex;
float animationTime;
ModelResource modelResource;
```

---

# 12. クラスコメント

主要クラスには責務を説明するコメントを追加してください。

形式:

```cpp
/*-----------------------------------------------------------------------------------------
 * AnimationModel
 * - アニメーションモデルクラス
 * - スケルタルアニメーション付きモデルの再生・スキニング処理を担当
 *---------------------------------------------------------------------------------------*/
class AnimationModel {
};
```

クラスコメントでは、

* 何を担当するか
* 何を管理するか
* 必要であれば何を担当しないか

が分かるようにしてください。

---

# 13. 関数コメント

public関数や重要な内部関数にはコメントを付けてください。

ヘッダでは以下の形式を基本とします。

```cpp
/**
 * \brief シーン更新後に必要となる描画前処理を実行する
 * \param cmd 描画命令を記録するコマンドリスト
 * \param pso 描画パイプラインを管理するサービス
 */
void PostUpdate(
    ID3D12GraphicsCommandList* cmd,
    PipelineService* pso);
```

コメントには必要に応じて、

* 関数の目的
* 引数
* 戻り値
* 副作用
* 注意点

を記述してください。

明らかなGetterなど、コメントによって可読性が上がらない関数には無理に追加する必要はありません。

---

# 14. cpp関数コメント

cpp側では処理単位を分かりやすくするため、必要に応じて以下を使用します。

```cpp
/////////////////////////////////////////////////////////////////////////////////////////
// 描画リソースを初期化する
/////////////////////////////////////////////////////////////////////////////////////////
void Renderer::InitializeResources() {
}
```

すべての小さな関数につける必要はありません。

重要な処理や処理量が多い関数を中心に使用してください。

---

# 15. メンバ変数コメント

メンバ変数には可能な限り短い説明を追加してください。

```cpp
class Player {
private:
    float speed;        //< 移動速度
    int health;         //< 現在HP
    State currentState; //< 現在の状態
};
```

コメントは簡潔にしてください。

変数名をそのまま日本語にしただけのコメントは避けてください。

---

# 16. 処理コメント

処理の意図がコードだけでは分かりにくい場合、処理単位でコメントしてください。

特に以下にはコメントを推奨します。

* DirectX12 Resource Barrier
* Descriptor操作
* GPU同期
* Fence
* CommandQueue
* Upload処理
* Matrix変換
* 特殊な補間
* アルゴリズム
* 一見不要に見える処理
* バグ回避処理
* ライフタイム制御

例:

```cpp
// GPUがこのフレームのCommandListを使用し終えるまで待機する
WaitForFence(frameFenceValue);

// RenderTargetとして使用するためResource Stateを遷移する
TransitionResource(
    backBuffer,
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET);
```

コードを日本語に翻訳しただけのコメントは不要です。

悪い例:

```cpp
// iを1増やす
++i;
```

---

# 17. コメント量

コメントは比較的多めに記述してください。

特にAIが生成したコードでは、

「なぜこの処理が必要なのか」

が分かるコメントを優先してください。

ただし、コードそのものから明らかな処理を1行ずつ説明するような過剰コメントは禁止します。

処理のまとまり単位でコメントしてください。

---

# 18. const / constexpr / noexcept

適切な場合は、

```cpp
const
constexpr
noexcept
[[nodiscard]]
```

を利用してください。

ただし意味を理解せず機械的につけないでください。

特に `noexcept` は内部処理が例外を発生させる可能性を確認してください。

---

# 19. 所有権

所有権を明確にしてください。

所有する場合:

```cpp
std::unique_ptr<T>
```

共有所有が本当に必要な場合:

```cpp
std::shared_ptr<T>
```

所有しない参照の場合:

```cpp
T*
T&
std::reference_wrapper<T>
```

を用途に応じて使用します。

`shared_ptr` を便利だからという理由だけで使用しないでください。

---

# 20. raw pointer

raw pointerは禁止ではありません。

所有権を持たない参照として利用することは許可します。

ただし、

```cpp
T* ptr;
```

を見たときに所有権が不明確にならない設計にしてください。

---

# 21. include

不要なincludeを追加しないでください。

ヘッダでは可能であればforward declarationを使用してください。

ただし可読性やコンパイル安全性を犠牲にしてまでforward declarationを行う必要はありません。

include依存を不必要に増加させないことを重視してください。

---

# 22. エラー処理

失敗する可能性のある処理を無視しないでください。

DirectX12 APIではHRESULTを適切に確認してください。

エラー情報には可能な限り、

* 何が失敗したか
* 対象Resource
* ファイル
* API名

が分かる情報を含めてください。

---

# 23. assert

assertはプログラム内部の不変条件確認に使用してください。

ユーザー入力、ファイル読み込み、外部Resourceのエラー処理をassertだけで済ませないでください。

---

# 24. リファクタリング

既存コードを変更する場合、いきなり大規模な書き換えをしてはいけません。

まず以下を確認してください。

1. 現在の責務
2. 問題点
3. 依存関係
4. 変更による影響範囲
5. 最小限必要な変更

大規模変更が必要な場合は理由を説明してください。

---

# 25. 禁止事項

以下は禁止します。

* Managerクラスへ無制限に処理を追加する
* Game固有処理をEngineへ追加する
* RuntimeからEditorへ依存する
* 意味のないSingletonを増やす
* Global変数を安易に使用する
* raw new/deleteによる所有権管理
* 巨大なif/switchを機能追加のたびに増やす
* 同じ処理を複数箇所へコピーする
* 理由のないstatic
* 不要なshared_ptr
* 根拠のない抽象化
* 将来使うかもしれないという理由だけで機能を追加する
* 既存設計を確認せず独自方式を導入する

---

# 26. 実装前の確認

実装前に以下を確認してください。

```text
1. 関連ファイル
2. 呼び出し元
3. 依存先
4. 類似実装
5. 所有権
6. ライフタイム
7. Engine / Game / Editor の責務
8. 変更範囲
```

---

# 27. 実装時の進め方

基本的に以下の順番で進めてください。

```text
1. 現在の構造を確認
2. 問題点を整理
3. 変更方針を決定
4. 変更対象ファイルを整理
5. 実装
6. ビルド上の問題を確認
7. 設計上の問題を再確認
```

---

# 28. 実装後の確認

コード変更後は以下を確認してください。

* 責務分離できているか
* EngineとGameの責務が混ざっていないか
* RuntimeとEditorの依存が逆転していないか
* 所有権が明確か
* Resourceのライフタイムが明確か
* 将来拡張しやすいか
* 不要な依存を追加していないか
* コメントが不足していないか
* 過剰なコメントになっていないか
* 未初期化変数がないか
* nullの可能性を考慮しているか
* HRESULTを無視していないか
* ビルドエラーが発生しそうな箇所がないか
* 未解決シンボルが発生しそうな変更がないか

---

# 29. AIへの重要事項

不明な点を推測して実装しないでください。

既存コードから判断できない場合は、

```text
不明
確認が必要
```

と明記してください。

既存設計と異なる方式を採用する場合は、その理由を説明してください。

コード量を減らすことよりも、

* 責務が明確
* 読みやすい
* 修正しやすい
* 拡張しやすい

ことを優先してください。

ただし、必要以上に複雑な設計へしないでください。

NexusEngineは学習・研究目的だけではなく、継続的に機能追加していくゲームエンジンとして設計してください。
