# C++ / C#共通ゲーム開発基盤

本資料はPhase 1（Project System / Project Browser）の実装記録。
後続の[改訂Phase 8](HybridProjectREADME.md)では任意のModule依存、複数Native / Managed Moduleの共通実行、途中追加UI、Build / Packageを実装した。依存のない既存Projectの保存形式を維持し、実行可能な構成は共通検証で区別する。ビルド・実行は未検証。
続く[Phase 2：Game API / Native Module](GameModulesREADME.md)で独立C++ DLLの経路を追加した。
[Phase 3：.NET Runtime Hosting](CSharpRuntimeREADME.md)ではManaged bootstrap専用テンプレートを追加した。
[Phase 4：C# Gameplay / 共通ECS](ManagedGameplayREADME.md)ではGameScriptsと既存ScriptComponentを接続した。
以下のPhase番号は今回の
C++ / C#導入計画の番号であり、既存ProjectSystem.md等の過去のPhase番号とは別である。
ビルド、Game起動、Launcher起動、生成CLIやテストバイナリの実行は行っていない。

## 調査結果（変更前の構造）

| 領域 | 既存構造・関連クラス |
|---|---|
| Project Browser | `Launcher/LauncherApplication`（ImGui）と`Launcher/Standalone/Program.cs`（配布用WinForms） |
| Project定義 | `.nxproject` schema 1、`Runtime/Project/ProjectDescriptor`、`ProjectSerializer`。Project単一language設定は存在しない |
| Project管理 | `ProjectContext`がRootと共通パスを解決、`ProjectService`が検証・staging生成、`ProjectRegistry`が最近使ったProjectを管理 |
| 初期生成 | `Tools/Project/ProjectTemplates`、`Templates/*.nxtemplate`。UIとCLIが同じ生成サービスを呼ぶ |
| データ配置 | Assets / Levels / Source / Config。Build / Intermediate / Cache等は生成物としてGit除外。共通パスは変更可能 |
| Engine選択 | `.nxengine`、`EngineManifest`、`EngineRegistry`、`CheckProjectEngine`。Engine ID / version / associationとbuild stampで照合 |
| Engine / Game分離 | `Build/NexusRuntime.vcxproj`と`NexusEditorSupport.vcxproj`。SampleGame.libとGame専用Editor / Runtimeホスト |
| VS生成・Build | `.vcxproj.in` / `.slnx.in`、`Config/BuildTargets.json`、`ProjectBuild`、`Build-Game.ps1`、`SourceCodeEditor` |
| 起動・Runtime所有 | `Core/Hosting/ProjectHost.inl` → `GameApplication` / `EditorApplication` → `GameRuntime`。実行用LevelはGameRuntime所有 |
| ゲーム登録 | 生成した`GameModule.h`のReflection / Setup、`ReflectionRegistration` / `GameLevelSetup`。DLLローダーではなく静的な登録境界 |
| ECS | `Runtime/Scene/Scene` / `Entity`、`EntityId`（index + generation）。永続化は`PersistentIdComponent`。`SystemScheduler`はSimulationの後にTransformを実行 |
| Level保存 | `Level` / `LevelDocument` / `LevelSerializer`。編集Worldと実行Worldは別所有。読込成功後に置換 |
| Asset・描画 | Editorの`AssetCatalog`、`Graphics/Model/ModelAssetLibrary` / `ModelAssetReference`、既存RenderScene / PrimitiveRenderer / DirectX12 |
| Runtime Reflection | `ComponentReflection`、Engine / Game別の生成Reflection、`TypeRegistry` / `TypeDescriptor` / `PropertyDescriptor` |
| 共通編集 | `PropertyHandle`（PersistentId + type / property ID）→ `PropertyEditService` → `PropertyDrawerRegistry` / `ReflectedPropertyEditor` → `DetailsPanel` |
| Reload | `Platform/Development/LiveCodingAgent`、`Foundation/Reflection/ReinstancingService`。Live++はメインスレッド制約を持つ既存Native方式 |
| Log・IPC | `Logger`とConsole / Buffered Log Sink、`Foundation/IPC/RuntimeProtocol`、EditorのStandalone / Viewport Session |
| Input / Audio / Physics | 調査したRuntime / Platform / Core / Foundationには独立した共通ゲームAPI実装を確認できなかった。Window / Editorの入力経路をゲームAPIが完成済みと見なさない |

既存の生成サービス、ECS、Level、Reflection、DetailsPanel、Asset配置を再利用する。
新しいProject Managerや言語別のWorld / Inspector / Asset Managerは作成しない。

## Project構造とModuleモデル

```text
MyGame/
  MyGame.nxproject
  Assets/
  Levels/Startup.level
  Config/
  Source/Game/
  Intermediate/        # 必要な開発処理で作成される生成物。必須Directoryではない
```

C++の場合は従来のGameRuntime.vcxproj / GameEditor.vcxproj / Game.slnxと
Config/BuildTargets.json、GameのReflection登録用コードも生成する。
C#の場合はSource/Game/README.mdとModule宣言までを生成し、C++ファイルやNative BuildTargetsを生成しない。
両言語の初期Levelは同じテンプレートのファイルを利用する。

共通Project情報は既存のまま、schema 1に省略可能な`modules`配列を加える。

```json
"modules": [
  { "id": "Game", "name": "Game", "language": "CSharp", "path": "Source/Game", "type": "Game" }
]
```

これは既存Descriptorへ加える項目の例であり、単独のJSONファイルではない。
`id`はProject内で一意な安定識別子（ASCII英数字と`.` / `-` / `_`、最大128文字）。
nameやpathを変更してもidを維持する。nameはUTF-8表示名、pathはProject Root相対のSource Directory。
languageは`Cpp` / `CSharp`、typeは現段階では`Game`のみ。型・用途の将来拡張は明示的に実装する。
最大256 Moduleとし、重複ID、未知項目、未知言語・用途、不正パス、Root外へのsymlinkを拒否する。
ModuleのSource実体の存在やコードの実行可能性は、Descriptor読込の条件にしない。

旧Descriptorの`modules`省略は空配列として扱い、自動的にModuleを推測して保存しない。
空配列は保存時に省略する。旧テンプレートによるCpp生成も旧形式を維持し、旧Installed SDKの
ホストが未知キーを拒否する問題を避ける。拡張済みDescriptorは新しいReaderが必要であり、
古いEngineで読めるという前方互換性は保証しない。共有データのパスやEngine版指定は変更しない。

## Project生成フローと言語選択

1. BrowserでProject Name / Location / Template / Engine / Initial Game Languageを指定する。
2. UIは選択した値をworkerへコピーする。配布Launcherは同等のCLI引数を渡す。
3. `ProjectTemplates::Create`がEngine整合性とテンプレートの対応言語を確認する。
4. 新テンプレートでは安定ID `Game`、path `paths.source + /Game`の初期Moduleを一つ宣言する。
5. `ProjectService::Create`が新規staging、共通Directory、Project IDを用意する。
6. ProjectTemplatesが共通ファイルと選択言語のファイルだけを展開し、既存LevelロードとCamera検証を行う。
7. Descriptorを保存・再読込し、`.gitkeep` / `.gitignore`を生成して完成したDirectoryを公開する。
8. Browserが既存ProjectRegistryへ登録する。失敗時は今回所有するstagingだけを既存RAII処理で破棄する。

生成処理は同期サービスであり、呼出元UIへの参照を保持しない。返されるProjectContextは
Descriptorとパスを値として所有する。Module情報にRuntime / Assembly / Nativeポインタを保持しない。
Editorに.NET Runtime所有権を追加せず、Runtime → Editor / ImGuiの依存も追加しない。

CLIは`create-template <new-root> <name> <engine.nxengine> <template-id> [Cpp|CSharp]`。
省略はCppで既存呼出しを維持する。新しいテンプレートの`moduleLanguages`は対応する初期言語の一覧で、
`files[].language`があるファイルだけを言語別に展開する。指定のないLevel等は共通。
旧テンプレートはCppのみとして扱い、CSharp選択時は更新済みテンプレートが必要なことをエラーで返す。

**Initial Game Languageは最初に生成するModuleの種類であり、Projectの固定言語ではない。**
Phase 1ではC#はModule宣言までで、csproj / C#コード / .NET Hostingを生成しない。
現在の静的ホストによるBuild / Launch / 直接起動はManagedまたは複数Moduleの宣言を明示的に拒否し、
Gameコードを無視したLevel実行を成功として扱わない。

## 将来のHybrid構成

同じ`modules`へ次の宣言を追加できる。共通Level / Asset / Entity / Componentの再作成は不要。

```json
"modules": [
  { "id": "gameplay", "name": "Gameplay", "language": "CSharp", "path": "Source/Gameplay", "type": "Game" },
  { "id": "combat", "name": "NativeCombat", "language": "Cpp", "path": "Source/NativeCombat", "type": "Game" }
]
```

Phase 1で可能なのは宣言・検証・保存。Module追加UI、任意のModule群からのBuild生成、
Hybridコード実行は未対応である。ModuleごとにWorldを設けず、後続のPublic Game APIと
共通Entity / Asset Handleで同じWorldを操作する。

## Phase 1の変更ファイルと依存関係

| ファイル | 変更 |
|---|---|
| `Runtime/Project/ProjectDescriptor.h` | Module値型、言語・用途enum、modules配列 |
| `Runtime/Project/ProjectSerializer.cpp` | 拡張項目の読書き、ID・言語・用途・パス検証、旧形式維持 |
| `Runtime/Project/ProjectContext.cpp` | Module配置のRoot内解決を検証 |
| `Tools/Project/ProjectTemplates.h` / `.cpp` | 初期Module生成とテンプレート言語フィルター。既存呼出しはCpp既定値 |
| `Templates/blank.nxtemplate` / `basic-3d.nxtemplate` | 共通LevelとCpp / CSharp別ファイルの指定 |
| `Templates/Shared/ManagedModule.README.md.in` | C# Module生成範囲を説明 |
| `Launcher/LauncherApplication.h` / `.cpp` | Native Browserの初期言語UIとworker値渡し |
| `Launcher/Standalone/Program.cs` | 配布Browserの初期言語UIとCLI接続 |
| `Tools/Project/ProjectTool.cpp` | create-templateの省略可能な言語引数 |
| `Tools/Project/ProjectBuild.cpp` | 未実装のManaged / 複数ModuleでのBuild / Launchを明示的に拒否 |
| `Tools/Project/Build-Game.ps1` | スクリプト直接呼出し時にも未対応構成を拒否 |
| `Core/Hosting/ProjectHost.inl` | ホスト直接起動でも未対応Moduleを黙って無視しない |
| `Tests/ProjectSystemTests.cpp` | Hybrid保存、ID維持、旧形式、重複・不正宣言の回帰ケースを追加（未実行） |
| `README.md` / 本README | 現状、設計、生成方法、残作業の説明 |

依存方向は`Browser / CLI → ProjectTemplates → ProjectService / ProjectContext / ProjectSerializer`を維持する。
ホストとBuild処理はProject値型を参照する。ProjectモデルからEditor、ImGui、.NETへの依存はない。
追加cppや外部ライブラリはなく、VSのコンパイル対象追加や依存取得は不要。

## 未対応と次Phase

| Phase | 状態・次に必要な作業 |
|---|---|
| 1 | 共通Project・Moduleモデル・初期言語選択を実装。静的確認のみ |
| 2 | 独立Native Moduleと共通Game APIを追加。詳細と静的確認の限界は[Phase 2](GameModulesREADME.md)を参照 |
| 3 | 公式Hosting・Bootstrapを追加。[Phase 3](CSharpRuntimeREADME.md)参照。LauncherのC#実装とは独立 |
| 4 | 基本Gameplayを共通Scene / EntityId / SystemSchedulerへ接続。[Phase 4](ManagedGameplayREADME.md)参照 |
| 5 | 未実装。既存Reflection / Property編集 / Level保存へManaged metadataを接続する |
| 6 | 未実装。Live++と分離したManaged Reload、状態復元とUnload確認を実装する |
| 7 | 宣言モデルのみ対応。複数Module実行と共通API・イベントによるHybrid連携は未実装 |
| 8 | 既存Native Build / SDK配布を維持。Managed Build、配置、Package統合は未実装 |

## 静的確認と確認限界

- include / namespace / symbol：既存NexusEngine名前空間へ値型を追加し、Create宣言・定義・呼出しの引数を照合。
- ownership / lifecycle：UI値のコピー、同期生成、既存staging公開・RAII cleanupを維持。Runtime所有物は追加しない。
- API consistency：Cpp既定引数と旧CLI引数数を維持。旧Descriptorのmodules省略を許容し、未知項目は拒否。
- 共通テンプレートのJSON構文・参照先と、言語別ファイルの分岐を静的に確認。
- blank / basic-3dの各定義でCppは8ファイル、CSharpは2ファイルの参照先と出力先重複を確認。
- 元のGit HEADと比較し、Cppの全8ファイルのsource / target / replaceが両テンプレートで一致することを確認。
- Build-Game.ps1はPowerShell Parserで構文エラーなしを確認（スクリプト自体は未実行）。
- `git diff --check`で空白不整合を確認。

ビルド・Game・Launcher・CLI・回帰テストは実行していないため、コンパイル・リンク成功、
Project生成成功、既存C++ Gameの実動作、C#ゲーム動作を検証済みとは扱わない。
