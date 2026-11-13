# Mono RuntimeとScriptの編集

## 概要

新規Managed / Hybrid ProjectはMono 6.12.0.206 x64 / SGenを使用する。C#コンパイルは既存の.NET SDK / Roslyn、実行はMono embedding APIが担当する。UnityのMono Backendを参考にした構成だが、Unity独自のMono forkやUnity APIを組み込むものではない。

`Config/BuildTargets.json` の `managedRuntime` は `Mono` または `CoreCLR`。省略した既存Projectは従来のCoreCLRとして扱い、既存Level / Asset / Module定義を勝手に変換しない。新規テンプレート、C++ Projectへ最初に追加するC# ModuleはMonoを選ぶ。Project全体の言語を固定する項目ではない。同一RuntimeでMonoとCoreCLRのScriptを混在させる構成は対象外。

## 既存機能との関係と採用理由

World、EntityHandle、Component、LevelDocument、Asset、Property編集は従来の共通基盤をそのまま使う。Mono用ECS / Asset Manager / Inspectorは存在しない。Game RuntimeのCSharpRuntimeがDotNetHost経由でMonoHostを所有し、EditorはMetadataと値だけを所有する。

- `MonoHost`: 公式C APIの動的解決、root / child Domain、Thread制約、unload確認、shadowの寿命を担当。CoreCLR専用hostfxrで代用できないため追加した。
- `MonoInterop`: Mono向け固定レイアウトIntPtrとCdecl delegate。C ABIを変えず、古いMonoが扱えないfunction-pointer fieldを回避する。
- `MonoEntryPoints` / `MonoGameScriptCatalog`: Domain内のdelegate root、Game Assemblyのshadow copy、値snapshotを管理する。CoreCLRのcollectible AssemblyLoadContextとは分離する。
- `NativeManagedBridge`: 安定C ABIと既存Native Game APIを接続し、Domainを跨ぐ際に古いdelegateを破棄する。
- `Managed-Runtime.ps1`: Build / Reload / Metadataが同じBackendとTargetFrameworkを選ぶための小さな共通処理。Worldの所有やSystem登録は行わない。
- `Deploy-MonoRuntime.ps1`: 明示SDKのRuntime / BCL / Config / noticesを配置する。Packageも同じ配置を引き継ぐ。

CoreCLRを全面削除する案は既存Projectのtargetと配布契約を破壊するため採用しない。Monoへ切り替えるProjectは全Managed Moduleをnet48へ再コンパイルする。net10専用BCLを使う既存ゲームコードの自動変換は行わない。

## Scriptのアタッチ

後続変更で、同じScriptComponent内へ異なる型を最大32件配置する[複数Script対応](MultipleScriptsREADME.md)を追加した。個別の有効状態・公開値・Lifecycleを扱う。この追加変更は静的確認のみで、以下の過去の実行記録が複数Scriptの検証を保証するものではない。

1. C#コードをBuild / Rebuild Scriptsし、登録済みScriptTypeとPortable PDBのソース対応を生成する。
2. Content BrowserのSourceからC#ファイルをDetailsまたはOutlinerのEntityへdropする。複数型を含むファイルは右クリックから型を選ぶ。
3. DetailsのScript選択欄でも登録済み型を検索してアタッチ・置換できる。
4. Open ScriptまたはSourceの右クリックで関連付け済みテキストエディタを開く。
5. Detach ScriptでScriptComponentを解除する。既存PropertyEditServiceの履歴で値を含めUndo / Redoできる。Play中の構造編集は禁止する。

型はファイル名から推測しない。C#はPDBの宣言位置、C++はReflection生成済みsourceFilesを利用する。未Buildの任意ファイルを実行可能なComponentとして扱わない。C++は登録済みEntity Componentのみアタッチ可能。Native Game Systemは引き続きGame ModuleからSystemSchedulerへ登録し、Entityへファイルをアタッチするものではない。

## Buildと配布

前提はWindows x64、Visual Studio C++環境、.NET SDK、.NET Framework 4.8参照Assembly、Mono x64 SDK。Mono RuntimeはC#言語のcompilerではないためSDK / Roslynを併用する。

Installed SDKを発行する場合は、発行者が `Publish-Engine.ps1 -MonoRoot <Mono x64 SDKのルート>` または `NEXUS_MONO_ROOT` でMonoを指定する。発行前にRuntime / BCL / GAC / Configを検証し、`SDK/ThirdParty/Mono/Runtime` へライセンスとともに同梱する。この配置もEngine inventoryの対象となる。Monoを欠くSDKはリリースZIPの生成時にも拒否する。

修正版のInstalled SDKでは利用者によるMono SDKの別途設定は不要。新規ProjectのBuildは明示した `NexusMonoRoot`、`NEXUS_MONO_ROOT`、Engine同梱Runtime、既存Project出力の `Mono` の順に解決する。.NET SDKと.NET Framework 4.8参照Assemblyは引き続き必要。

1.2.0の配布版にはMono Runtimeが同梱されていないため、`Set NexusMonoRoot or NEXUS_MONO_ROOT` で停止する。暫定的には公式Mono x64 SDKの配置先を `NEXUS_MONO_ROOT` に設定し、ランチャーを再起動して再ビルドする。同梱修正の適用にはInstalled SDKと配布ZIPの再発行が必要で、既存インストールはソース修正だけでは更新されない。

```powershell
$env:NEXUS_MONO_ROOT='C:/SDK/Mono'
powershell -NoProfile -ExecutionPolicy Bypass -File Project/Tools/Project/Build-Game.ps1 `
  -ProjectFile C:/Games/MyGame/MyGame.nxproject -EngineManifest NexusEngine.Debug.nxengine `
  -Kind Editor -Configuration Debug
```

新規C# ProjectのTargetFrameworkはnet48、LangVersionはlatest。新しいC#構文のすべてが旧BCLで利用できるとは限らない。System.Text.Json / System.Reflection.Metadataは明示packageを使う。

配置は `Managed/` に安定API / Bootstrap / Game Assembly / Metadata、`Mono/` にbin / lib/mono/4.5 / lib/mono/gac / etc/mono / Notices。Windows版Monoのstrong-name BCL解決にGACも必要になる。`Nexus.modules.json` がBackendを指定する。Monoの配布時はCoreCLRのnethost / runtimeconfig / DotNetRootを必要としない。既存CoreCLRの配布経路は維持する。

Build stampはMono Runtimeファイルも既存managedFilesとしてhash検証する。Module依存順序、Asset / Level / Configコピー、Engine互換性検査は既存Project Build / Package処理に従う。SDK取得をPackage処理へ暗黙に組み込まない。

## 所有権とLifecycle

1. RuntimeがLevelと共通GameApiBridgeを用意し、Native Game Moduleを登録する。
2. CSharpRuntimeがMonoHostを初期化する。root Domain / SGen DLLはProcess寿命で保持し、再起動しない。
3. 所有Threadで子Domainを作り、安定API / Bootstrapを読み込む。Game Assemblyは固有shadowからロードする。
4. 子DomainがCdecl delegateをrootする。NativeはそのDomain存続期間だけthunkを借用し、ScriptSystemが既存WorldのScriptComponentを列挙する。
5. Entityの世代付きHandleを毎回共通Game APIで検査する。ManagedにEntity* / Component*を保持させない。Systemも明示登録された公開APIだけから呼ぶ。
6. StopはScript lifecycleを終了し、catalog / event / instance参照とNative delegateを解除してから子Domainをunloadする。成功を確認してshadowを削除する。
7. 解除失敗はquarantineとして失敗を返し、未確認のDomain / shadowを解放済み扱いにしない。

Interopは固定レイアウトDTO、UTF-8長さ付き文字列、Handle、statusを使用する。Managed例外はstatusと既存Loggerへ変換しC++境界へ伝搬しない。Domain APIは所有Thread限定。Engine更新中に任意ThreadからScriptを呼ぶAPIは提供しない。

## Reload

Rebuild Scriptsの候補を別Domainでロードし、公開Property snapshotの適用とcleanupを検証する。そのDomainのunload成功を確認してから現行Scriptを停止する。旧Native delegateを解除し、旧Domainのunloadを確認して新Domainを作成する。instance counterを引き継ぎ、Entity / Level / Native Systemの同一性は維持する。

コンパイル・事前検証失敗は現行コードを保持する。commit後のactivation / state restore失敗には完全自動rollbackを保証せず、既存CSharpRuntimeの停止・復元・再試行契約を使う。Thread / Taskを独自に残したGameコードの無条件unload成功は保証しない。Mono経路のReloadはGameRuntimeのbound V5 contextを対象とし、旧context形式のReloadは対象外。Native側Live++のpatchとは別Backendのまま扱う。

## 公式資料

- [Mono embedding](https://www.mono-project.com/docs/advanced/embedding/)
- [Mono公式配布](https://www.mono-project.com/download/stable/)
- [Unity scripting backends](https://docs.unity3d.com/6000.0/Documentation/Manual/scripting-backends.html)
- [Godot Script attachment](https://docs.godotengine.org/en/stable/getting_started/step_by_step/scripting_first_script.html)

## 検証

2026-11-11、Windows x64 / Debugで確認。ログはRepositoryの `Generated/MonoMigration/` に保存した。過去CoreCLR試験をMono試験の成功として流用していない。

| 対象 | 結果 / 証跡 |
|---|---|
| Mono実SDK | 公式MSIの署名Valid、SHA256照合、6.12.0 / amd64 / SGenを確認。システムへインストールせずGenerated配下に展開 |
| Managed API / Bootstrap / Metadata | net48 Build成功、警告・エラー0。Mono実行によるMetadata / PDB sourceFiles生成成功 |
| 共通Hybrid Build | 新規MonoHybridSampleのEditor / Runtime / Native DLLを生成・Build。`MonoHybrid.Editor.updated.log` |
| 実Mono CPU統合 | `MonoTests.run.log`。所有Thread拒否、別Assemblyのentry拒否、session再起動、子Domain unload、既存ECS Transform、instance ID更新、公開Speed復元、V2適用、候補不在でも現行更新維持がexit 0 |
| 共通Runtime | `MonoHybrid.Runtime.cpu.log` / `MonoHybrid.Runtime.gpu.log`。Native + Mono起動、Native API / Event、8 GPU frameと正常終了がexit 0 |
| Editor | 最終UIは`MonoHybrid.Editor.final.gpu.log`で8 frameの起動・描画・終了を確認。`EditorTests.final.run.log`でSource drop、対象Entity transaction、解除・複数選択・公開値Undo / Redoを検証 |
| Reload候補Build | `MonoHybrid.ReloadBuild.log`。既存Assemblyを上書きしない別配置へnet48 candidate / Metadataを生成、exit 0 |
| 既存CoreCLR | `CoreClrRegression.build.log` / `CoreClrRegression.run.log`。従来のManaged Gameplay / Reload / fault isolation回帰がexit 0 |
| Project生成 / Package回帰 | `Generation.tests.log` / `Packaging.tests.log`がexit 0。既存Level / Asset保持と不正構成拒否を検証 |
| Mono同梱Package | `C:/MyProject/NexusEngine-MonoPackage-20261111`。inventory検証成功、Project / Engine引数なしの`--game --check-game`がexit 0。`MonoHybrid.Package.cpu.log` |
| 静的確認 | PowerShell AST、XML / C++のBuild対象、宣言・定義、既存C ABIの同一layout、所有Thread、Domain / delegate解放順、安定型ID / Level形式、`git diff --check`を確認 |

確認中に見つけたnet48のSystem.Memory参照不足、配布Mono BCLのcompiler参照混入、Windows Mono GAC不足を修正した。新規生成・Module追加時はC#のCandidateAssemblyFiles探索を無効化し、Runtime BCLを参照Assemblyとして使わない。

未検証はMono版Release、SDK / VC RuntimeのないクリーンPC、長時間実行、任意の第三者Managed library、unload不能なThreadを残す負例、外部エディタ別の手動操作全通し、更新SDKをPublishしたInstalled Engineの実Build、および後続追加の複数Script動作。任意C++クラス自動Binding、IL2CPP、Input Backendの新規実装は今回の範囲外。

操作確認用Projectは `Generated/MonoMigration/MonoHybridSample/MonoHybridSample.nxproject`。同Projectの `Build/Bin/x64/Debug/GameEditor.exe` を起動済み。Cubeを選び、DetailsのScript / Sourceの右クリック・dropで設定し、PlayでMono上のgame.playerを実行できる。旧CoreCLRサンプルの実行中ファイルを上書きしていない。

## 変更ファイル

以下はMono移行とScript選択操作の変更対象。以前のPhaseや並行作業のファイルは戻していない。全パスはProject/基準。

| ファイル | 変更内容 |
|---|---|
| Runtime/Scripting/MonoHost.h | Mono Domainの所有・Reload・unload確認の契約 |
| Runtime/Scripting/MonoHost.cpp | 公式embedding API、GCHandle scope、shadow、Thread検査とquarantine |
| Runtime/Scripting/DotNetHost.h | optional Mono backendの所有 |
| Runtime/Scripting/DotNetHost.cpp | backend選択とBootstrap限定entry解決 |
| Runtime/Scripting/CSharpRuntimeSettings.h | 明示Mono root追加 |
| Runtime/Scripting/CSharpRuntime.cpp | Host停止失敗のquarantine反映 |
| Runtime/Scripting/NativeManagedBridge.h | Domainを跨がないcallbackとV5 contextの値保存 |
| Runtime/Scripting/NativeManagedBridge.cpp | Mono delegate解決、事前検証・commit・abort・complete |
| Runtime/Project/ModuleDeployment.h | 配置manifestのBackendをRuntime設定へ変換 |
| Managed/Directory.Build.props | Mono / net48とcompiler define |
| Managed/Nexus.Managed/Nexus.Managed.csproj | Mono依存packageと互換source |
| Managed/Nexus.Managed/MonoCompatibility.cs | net48でrecordに必要なIsExternalInit |
| Managed/Nexus.Managed/MonoInterop.cs | 固定IntPtr fieldとCdecl delegate |
| Managed/Nexus.Managed/Interop.cs | Mono用Handle / string / bytes表現、CoreCLR layout維持 |
| Managed/Nexus.Managed/NativeBindings.cs | Mono delegateとnet48互換変換 |
| Managed/Nexus.Managed/GameWorld.cs | 旧BCLでも同じ共通World APIを利用 |
| Managed/Nexus.Managed/ScriptMetadata.cs | net48互換finite判定、共通Metadataを維持 |
| Managed/Nexus.Managed.Bootstrap/Nexus.Managed.Bootstrap.csproj | Backend別source / framework選択 |
| Managed/Nexus.Managed.Bootstrap/EntryPoints.cs | Mono delegate loggerと旧BCL互換 |
| Managed/Nexus.Managed.Bootstrap/GameplayEntryPoints.cs | CoreCLR UCOとMono entryの条件分離 |
| Managed/Nexus.Managed.Bootstrap/ScriptRuntime.cs | net48互換文字列処理 |
| Managed/Nexus.Managed.Bootstrap/MonoGameScriptCatalog.cs | Domain内Assembly / shadow / Resolve購読 |
| Managed/Nexus.Managed.Bootstrap/MonoEntryPoints.cs | delegate root、公開値snapshot、候補Domain契約 |
| Managed/Nexus.Managed.Metadata/Nexus.Managed.Metadata.csproj | Mono exporterのnet48選択 |
| Managed/Nexus.Managed.Metadata/Program.cs | 別DomainのExporter、unload後のshadow削除と出力確定 |
| Build/NexusRuntime.vcxproj | MonoHostのlink対象追加 |
| Build/Nexus.ManagedBootstrap.targets | Mono Build / SDK指定 / 配置 |
| Build/Nexus.ManagedGameplay.targets | Backend別Game Build / Metadata実行 |
| Build/NexusMonoGameplayTests.vcxproj | 実Mono CPU統合テストtarget |
| Editor/Services/SceneEditService.h | 共通解除transactionの宣言 |
| Editor/Services/SceneEditService.cpp | ScriptComponent解除とUndo / rollback |
| Editor/Services/ManagedReloadCommands.cpp | Mono Metadata.exeの選択 |
| Editor/Gui/Panels/DetailsPanel.h | 型検索文字列だけを保持 |
| Editor/Gui/Panels/DetailsPanel.cpp | Script型選択、Open、Detach、構造変更の遅延適用 |
| Editor/Gui/Panels/OutlinerPanel.cpp | 右クリックから登録Script型を選択 |
| Editor/Gui/ReflectedPropertyEditor.cpp | raw scriptType手入力を共通選択UIへ移行 |
| Resources/Editor/Localization/ja.lang | Script操作の日本語表示 |
| Resources/Editor/Localization/en.lang | Script操作の英語表示 |
| Tools/Project/Managed-Runtime.ps1 | BackendとBuild / Metadata実行の共通化 |
| Tools/Project/Deploy-MonoRuntime.ps1 | 明示SDKのRuntime / BCL / GAC / 通知配置 |
| Tools/Project/Build-Game.ps1 | BuildTargetsのBackendをMSBuild / local propsへ伝達 |
| Tools/Project/Complete-GameDeployment.ps1 | 複数Moduleと配置manifestのBackend整合 |
| Tools/Project/Build-Scripts.ps1 | Mono Reload候補生成 |
| Tools/Project/Managed-Deployment.ps1 | 安定API / Bootstrap依存を追加Moduleへ共有 |
| Tools/Project/ProjectBuild.h | managedRuntime設定 |
| Tools/Project/ProjectBuild.cpp | Mono成果物とstampの検証 |
| Tools/Project/Write-GameBuildStamp.ps1 | Mono Runtime全体を既存hash群へ追加 |
| Tools/Project/Package-Game.ps1 | Mono同梱 / Backend整合検査、CoreCLR互換維持 |
| Tools/Project/Add-GameModule.ps1 | 最初のC#追加時にMono / net48、既存CoreCLRは維持 |
| Tools/Engine/Publish-Engine.ps1 | Installed SDKへMono source / helper / licenseを配置 |
| Templates/Shared/Directory.Build.props.in | Backend既定値、参照Assembly探索、C#言語version |
| Templates/ManagedGameplay/GameScripts.csproj.in | net48 / System.Memory参照 |
| Templates/ManagedGameplay/BuildTargets.json.in | 新規Mono選択 |
| Templates/ManagedBootstrap/BuildTargets.json.in | 新規Mono選択 |
| Templates/Hybrid/BuildTargets.json.in | 新規Mono選択 |
| Templates/ManagedGameplay/README.md.in | Mono SDKとアタッチ手順 |
| Templates/ManagedBootstrap/README.md.in | Mono配置とDomain寿命 |
| Templates/Hybrid/README.md.in | Mono / Native共存と操作手順 |
| Tests/NexusEditorTests.vcxproj | MonoHostを含めた静的link |
| Tests/EditorServicesTests.cpp | Script解除・公開値Undo / RedoとPlay guard |
| Tests/MonoGameplayTests.cpp | 実Mono hosting / reload / ECS統合 |
| Tests/Fixtures/MonoGameplay/MonoGameplay.csproj | net48 V1 / V2 fixture |
| Tests/Fixtures/MonoGameplay/Probe.cs | 共通Transformと公開Speedの検証Script |
| ThirdParty/Mono/LICENSE.txt | 公式license原文 |
| ThirdParty/Mono/PATENTS.txt | 公式patent grant原文 |
| ThirdParty/Mono/UPSTREAM.md | 配布元、version、MSI hash、配置方針 |
| Docs/SourceContentBrowserREADME.md | 共通Script選択・解除の追記 |
| Docs/HybridProjectREADME.md | 新規Mono / 旧CoreCLRの適用範囲 |
| Docs/MonoRuntimeREADME.md | 本書、設計比較、所有権、使用方法、検証範囲 |

Repository root README.mdにも本書への入口を追加した。
