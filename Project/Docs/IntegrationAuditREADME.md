# C++ / C#統合監査

全体目標はPhase 1〜8のHybrid開発環境である。機能のソース経路が存在することと、その動作を実証したことを区別する。第12回で実ビルド・実行検証を追加した。対応構成の結果と残る制約は末尾および実行検証記録を参照する。
第11回まではビルド・実行禁止の条件で静的監査を行った。その後、ユーザーの「すべて実装しテストまで」「エンジンで使用できるまで」という指示により、実ビルド・テスト・Runtime検証へ移行した。以下の過去の未検証記録は当時の状態であり、最新の実行結果は[実行検証記録](HybridVerificationREADME.md)で区別する。Input等のEngine Backend自体は未実装とのユーザー確認があり、存在する機能のBinding漏れと混同しない。

## 第1回時点の要求と根拠（履歴）

| Phase / 要求 | 確認する実装・成果物 | 判定と不足する根拠 |
|---|---|---|
| 1：共通Project、初期言語、複数Module、途中追加、旧データ互換 | ProjectDescriptor / ProjectSerializer / ProjectTemplates、Launcher、Add-GameModule、各nxtemplate | ソース経路あり。実際のBrowser操作、既存Projectからの追加transactionの実証は未検証 |
| 2：Game / Engine境界、C++ Game Logic、DLL ABI・所有権 | Public/Nexus/GameModule.h、GameModuleLoader、GameRuntime、NativeModule template | C ABIと生成元DLLでの解放経路あり。コンパイル・link・ロード・停止は未検証 |
| 3：Project固有System、既存Scheduler、登録と公開APIの分離 | NativeGameSystemScope、SystemScheduler、NativeGameApiMetadata、CrowdBinding sample | 共通Schedulerと明示登録の経路あり。複数DLLの実行順序・逆順解放は未検証 |
| 4：Runtime所有の.NET、共有Entity / Component、Lifecycle、C#生成 | DotNetHost、CSharpRuntime、NativeManagedBridge、ScriptSystem、Nexus.Managed、Managed templates | Hosting・共通World・値DTOの経路あり。CLR初期化、Lifecycle、例外経路は未検証 |
| 5：明示Native API、型付きFacade、世代・寿命・Thread検査 | NativeGameBindings.h、NativeGameSystemScope、GameServices、Managed/NativeBindings、Hybrid sample | 取得・呼出し・失効の経路あり。実Interop、誤Thread、Unload後の拒否は未検証 |
| 6：共通Reflection / Details / 保存 | ScriptMetadata、ManagedScriptMetadata、PropertyDescriptor / Handle / EditService / DrawerRegistry、LevelDocument | 共通Metadata・編集・保存の経路あり。Undo / Redo、型変更・Alias・再読込の動作は未検証 |
| 7：Live++ / Managed Reloadの分離、停止・参照解除・失敗通知 | LiveCodingAgent、NativeGamePatch、CSharpRuntime、GameScriptCatalog、ReloadEntryPoints、ManagedReloadCommands | SDK静的照合記録と制御経路あり。SDK接続・patch・Unload完了・状態復元の実証は未検証。複数Native DLL同時patchと任意state移行は非対応 |
| 8：Cpp / CSharp / Hybrid、追加、共通Build / IDE / Package | ProjectDevelopmentCommands、ProjectBuild、共通Script群、GameRuntime、ModuleDeployment、templates | 経路あり。監査でBuild / Reload / Packageの検証差を修正中。生成・ビルド・配布先起動は未検証 |
| 共通：単一ECS / Level / Asset / Reflection、依存方向 | Runtime / Public / Managed / Editorのincludeと所有関係、各README | ソース上の依存を確認。実コンパイルでの依存閉包、実行時所有権の実証は未検証 |
| 共通：コメント・README・既存変更の保持 | 各変更ファイル、各Phase README、git diff | 日本語コメントと変更説明を追加。Git変更の破棄は行っていない |

この表は完了証明ではない。ソースだけでは証明できない要求は未検証のまま残し、限定されたParser / hash確認で実行系全体の成功を主張しない。

## 今回の監査で修正した問題

1. C++のBuild計画とPowerShellの直接呼出しで検証範囲が異なっていた。既存Project-ModuleOrderへ値計画を追加し、Module追加・Build・Reload・Packageで再利用する。未知 / 重複mapping、主Moduleの再指定、Hostへの再帰参照、Project / 出力重複、言語・拡張子・配置不一致を事前拒否する。
2. nethost / Engine依存 / Live++ / 安定Managed APIと同名のDLLを生成できる余地があった。生成計画の予約名を検査し、Module追加のcommit前とコンパイル開始前に拒否する。C++側でも共通予約名・主Project・Host再参照を拒否する。
3. BuildTargetsだけ変更しても旧stampに結び付けられた。targetsHashを記録し、ProjectBuild.VerifyとPackageで照合する。Packageは追加ModuleのID・配置とruntime依存の対応も検査する。
4. Build中にProject / Engine / BuildTargetsが変わった場合、終了時の定義へ古い成果物のstampを付け得た。開始時の読み取ったbytesを保持し、stamp確定前に再比較する。別作業者の変更は元に戻さず、Buildを失敗させる。
5. Managed依存の検査後、コピーまでに入力が変わる場合を検査していなかった。入力hashを固定し、一時copyのhash確認後に同じDirectoryで公開する。.deps参照とcopy先も既存Root検査へ通す。
6. Rebuild要求が追加Native ModuleやManaged側の通常Buildへ落ちていた。共通CLIのRebuildを全対象へ伝える。先行生成済みの追加NativeはHost生成後に重ねてBuildせず、配置の存在を検査する。
7. Native先行生成でも旧Managed depsを読み取っていた。Cpp-only / NativeOnly / Bootstrap-onlyでは読み取らず、不要な旧Managed配置の破損で再Buildの入口を塞がない。

## 設計・依存関係

新しいManager / ECS / Registryは追加していない。Get-NexusModuleBuildPlanは既存の依存順検査とRoot解決を使う値計画で、DLL・Assembly・Worldを所有しない。
Scriptの候補生成と稼働中Assemblyは分離したまま。共有配置の検証成功後に既存のMetadata / Reload経路へ渡す。Native / ManagedのABI、Entity Handle、Levelの保存形式は変更しない。
targetsHashは開発用nxbuildの追加項目であり、nxproject / Levelのschema変更ではない。新しいPackage処理はtargetsHashのない旧stampを拒否して再Buildを要求する。通常Verifyは旧stampの読込互換性を維持するが、targetsHashのあるstampでは内容差を拒否する。

## 変更ファイル

Project/を基準とする。

| ファイル | 内容 |
|---|---|
| Tools/Project/Project-ModuleOrder.ps1 | 共通のRoot検査・Module Build計画・予約名・runtime依存の検証 |
| Tools/Project/Add-GameModule.ps1 | commit前に共通計画を検証 |
| Tools/Project/Build-Game.ps1 | 事前計画、設定snapshot、Rebuild伝搬、先行Nativeの重複Build回避 |
| Tools/Project/Build-Scripts.ps1 | compile前に現在のProject計画と主Managed入力を検証 |
| Tools/Project/Complete-GameDeployment.ps1 | 共通計画、全対象Rebuild、先行Native成果物の確認 |
| Tools/Project/Managed-Deployment.ps1 | Root内copy、検査時hashの保持、一時copyの検証・公開 |
| Tools/Project/Write-GameBuildStamp.ps1 | BuildTargetsのhash記録 |
| Tools/Project/Package-Game.ps1 | 共通計画・設定hash・Module / runtime stampの対応検査 |
| Tools/Project/ProjectBuild.cpp | 共通配置・予約名・主Project / Host再参照・設定hashの検証 |
| Build/Nexus.ManagedBootstrap.targets | 共通Rebuild要求をBootstrap / APIへ伝搬 |
| Build/Nexus.ManagedGameplay.targets | 同要求をGameScripts / Metadataへ伝搬 |
| Tests/ProjectSystemTests.cpp | BuildTargetsだけ変更された旧stampを拒否する検証ソース |
| Docs/HybridProjectREADME.md | 監査結果と新しいstamp互換性の案内 |
| Docs/IntegrationAuditREADME.md | 要求・根拠・不足する証拠・今回の修正 |

## ライフサイクル・静的確認

Project値検査 → 全Module計画検査 → 生成 / compile → Managed依存copy検査 → 配置Metadata確定 → 設定snapshot再比較 → stamp確定の順とする。実行Worldの生成・停止順序は既存GameRuntimeに残す。
PowerShellはParserのAST解析のみを行い、対象Script本体を呼び出さない。XML、include、namespace、宣言・定義、compile / link参照、値型の所有権、ABI・保存互換性を静的に確認する。
BuildTargetsStampTestsを追加したが実行していない。fixtureは不活性な通常ファイルで、VerifyがDLLをロードしない経路を対象とする。
今回の14ファイルについてUTF-8・競合marker・local includeを確認し、PowerShell 8ファイルのASTとtargets XML 2ファイルの構文解析でエラーはなかった。git diff --checkもエラーなし。既存のLF / CRLF警告に対する無関係な変更はしていない。

## 継続確認事項

全体目標は未完了扱いのまま維持する。特に、実際のMSBuild / SDK property伝搬、複数AssemblyのProjectReferenceと配置、途中追加の失敗復旧、Runtimeのロード・停止、Editor編集、Native / Managed Reload、配布先でのprivate .NET起動には実証がない。実行禁止を勝手に解除せず、ソースから確認できる残差を引き続き調査する。

## 生成経路の監査・第2回

前回の監査はソース・設定計画を対象とした。今回はAddon生成からHost接続までを読み、次を修正した。

- CrowdSimulationSystem → 新名 → Crowd置換という連続置換が、CrowdAgent等の新名を再加工していた。元の三つのtokenを一度だけ置換する。
- Module IDがCrowdSimulationの場合、FacadeのModule IDまでSystem名へ変わり得た。元tokenを先に分離してから値を注入する。
- Native-a / Native_aのようなIDから同じC#識別子を生成していた。通常の英字開始IDは維持し、それ以外は専用prefixとUTF-8 hexへ写す。保存ID・APIで使うModule IDは変更しない。
- ScriptBehaviour / Entity / NativeValue等の生成名がSDK型を隠し得た。生成templateの基底型・Attribute・Facadeの境界型をglobal::で明示した。生成メンバやscaffoldのstate型・既存System IDに衝突する名前は事前拒否する。
- 新しいBehaviourのIDはModule.behaviours.ClassNameへ統一し、旧生成のModule.Behaviour等との標準的な衝突を避ける。既存SourceのAttributeや保存値は書き換えない。
- コメントのNEXUS_PROJECT_MODULE_MANIFESTやCustomGameModule.vcxprojという別Project名を接続済みと誤認していた。実際のdefineと正規化したProject参照を確認し、条件付き参照は常時接続の根拠にしない。
- Native生成だけで保持していたProject lockを、C#を含めて読取から公開まで保持する。新規sourceは一時ファイルから新規名へmoveし、途中のcppや一時ファイルを残さない。独自に変更された登録文は自動挿入の対象にしない。
- Native System追加は新規登録・header変更を伴うため、CLIにも通常BuildとHost再起動が必要なことを表示する。

新しいRegistry / ECS / Binderは追加せず、既存Project helperとscaffoldだけを拡張した。所有権・Interop ABI・Level / Asset形式・Runtimeの開始終了順序は変更しない。

| ファイル | 内容 |
|---|---|
| Tools/Project/Project-ModuleOrder.ps1 | 保存IDから衝突しないC#識別子への値変換 |
| Tools/Project/Add-GameModule.ps1 | 識別子・Script ID、Host参照・define判定、Engine Version照合 |
| Tools/Project/Add-GameSource.ps1 | 一回の置換、token分離、名前検査、共有lock、一時copyからの公開 |
| Templates/ManagedGameplay/PlayerController.cs.in | SDK基底型とAttributeの完全修飾 |
| Templates/Hybrid/CrowdSimulation.cs.in | Facadeの境界型とcodecの完全修飾 |
| Tests/HybridGeneration.Tests.ps1 | 実生成物の型名・System ID長・Facade ID・Host接続・既存データ保持を検査するソース |
| Docs/HybridProjectREADME.md | 生成命名とNative System追加時の手順 |
| Docs/IntegrationAuditREADME.md | 第2回の根拠・修正・残る証拠不足 |

HybridGeneration.Tests.ps1はユーザー実行用に追加しただけで、実行していない。既存の他のテストと同じGenerated配下へ独立fixtureを作り、コンパイラ・SDK Agent・Game・CLR Hostを起動しない構成である。
今回の8ファイルをUTF-8・競合marker・末尾空白で確認し、4つのPowerShellをAST解析した。6つのtemplate manifestの参照先と出力先重複も確認し、エラーはなかった。templateのSDK型名・namespace・所有権・新規公開とrollbackの流れを静的に照合した。git diff --checkもエラーなし。実際の生成回帰、C++ / C#コンパイル、共通Hostへのロードは未検証である。

## Runtime Lifecycle監査・第3回

GameRuntime / Level / SystemScheduler、CSharpRuntime / NativeManagedBridge / ScriptSystem、Managed SessionのPrepare・Commit・Abort・Completeを確認した。別の実行基盤は作らず、既存のframe guardとSessionへ以下を反映した。

1. SetupのStop要求を主Native Module開始前に検出する。取消済みWorldへDLL / CLRの初期化を進めない。
2. C++のみのSystemでもUpdate失敗をRuntime停止へ接続する。標準 / 非標準例外をResultへ変換し、Schedulerとcallbackのstackを抜けてからSystem / Worldを解放する。
3. ReloadのPrepare取消とCommit開始後のAbortをSessionのReloadCommittingで区別する。前者は候補だけを破棄し、現行Gameplay・World capability・instanceを維持する。後者は世代を停止して保留Worldへ戻す。
4. Cloneの失敗・例外でもPrepare候補を明示取消する。Scene交換後の失敗も、Commit前は旧instance IDを維持して再接続し、Commit後は再利用しない。
5. Complete前は初期化Tickだけを許し、通常Updateを拒否する。停止中はManaged / Native両方のTick入口で拒否する。順序外のCommit / Completeや適用中の重複Prepareを拒否する。
6. Managed世代確定後にNativeサービスの再開が失敗した場合、Reload成功とは通知しない。新Worldを保持して停止し、根拠のない旧世代rollbackは行わない。
7. C++ ComponentのCloneが非標準例外を送出してもGameRuntimeのReload境界で捕捉する。再接続に失敗したWorldは通常Updateへ戻さない。
8. 障害による停止をユーザーPauseへ書き込んでいたため、Reload復旧後も実行が再開しなかった。IsPausedで停止状態を合成し、正常復旧時には実行を再開する。明示されたユーザーPauseは維持する。

### 所有権・互換性

候補の取消責務はPrepare直後のscopeから、Scene交換時にRestore scopeへ渡す。Level / Assetの所有者はGameRuntimeのまま、Managedの参照解除はBootstrapが担当する。Sessionの追加boolは内部状態であり、公開DTOのlayout・関数署名・Levelの保存形式は変えない。
ScriptSystem::RebindSceneは内部C++メソッドにbool引数を追加した。既定値は従来のinstance失効で、Prepare取消だけfalseを指定する。更新したheader / lib / Bootstrapを同じ通常Buildで揃える必要があり、この変更自体をLive++適用する契約ではない。

| ファイル | 内容 |
|---|---|
| Runtime/Application/GameRuntime.cpp | Setup取消、Updateエラー停止、標準 / 非標準例外のResult化 |
| Runtime/Application/GameRuntime.h | ユーザーPauseとManaged障害停止の区別 |
| Runtime/Scripting/CSharpRuntime.cpp | Prepare取消guard、Commit段階別復元、再接続とサービス再開失敗の停止 |
| Runtime/Scripting/ScriptSystem.h | 旧世代instanceを維持する再接続引数の宣言 |
| Runtime/Scripting/ScriptSystem.cpp | 宣言に対応する再接続・instance保持 |
| Runtime/Scripting/NativeManagedBridge.cpp | 停止中のTick拒否 |
| Managed/Nexus.Managed.Bootstrap/EntryPoints.cs | SessionのCommit開始状態 |
| Managed/Nexus.Managed.Bootstrap/ReloadEntryPoints.cs | 取消とAbortの分離、段階順序の検査 |
| Managed/Nexus.Managed.Bootstrap/GameplayEntryPoints.cs | 停止中・Complete前の通常Tick拒否 |
| Public/Nexus/ManagedBootstrap.h | Abortの段階別契約の説明 |
| Tests/GameRuntimeTests.cpp | Setup停止、SystemのResult失敗・例外、停止後の再開始の検証ソース |
| Tests/ManagedGameplayTests.cpp | Prepare取消のinstance / lifecycle保持、Clone失敗・非標準例外の検証ソース |
| Docs/ManagedHotReloadREADME.md | 最新の取消・復元・停止契約への案内 |
| Docs/IntegrationAuditREADME.md | 調査・判断・所有権・変更内容・未検証事項 |

テストは追加のみで実行していない。Clone失敗には既存ComponentStorageのコピー不可契約を使い、実装へテスト専用の障害注入APIは増やしていない。Managedの取消検証も既存Bridge / ScriptSystemの公開境界を通す。実際のCLR参照解除、callback終了順序、複数DLL停止、Editorの状態通知の実証は引き続き不足している。
第3回の14ファイルについてUTF-8・競合marker、54件のlocal include、4つの利用側Project XMLとcompile入力を静的に確認し、参照エラーはなかった。RebindSceneの宣言・定義・利用側、Sessionの状態遷移、公開ABIのlayoutを変更しないことを照合した。git diff --checkもエラーなし。C++ / C#の構文・型・linkの成功をコンパイラで実証した確認ではない。

## Editor操作調整監査・第4回

### 調査結果と設計判断

ProjectDevelopmentJobのfuture、StandaloneSessionのBuild / child、ManagedReloadBuildのProcessは既存の所有機構を再利用した。ただしProject panelだけが自身のJobを検査しており、Playや別Runtimeの直接command、Native Reloadから同じ出力へ別操作を開始できた。JobとStandaloneのPollもToolbar描画内にあり、非表示時には完了回収が進まなかった。StandaloneSessionはHost起動時のProject snapshotを再利用し、Module追加後の定義を次回Buildへ渡していなかった。

新しいJob Managerや共通Reload Backendは追加しない。既存CommandのisCheckedに継続状態を接続し、WorkspaceのIsProjectOperationActiveで照会する。canExecuteとJob開始直前で競合を拒否し、Stopは維持する。状態照会はisCheckedだけを使用するため、canExecute同士の循環参照を作らない。

既存SetReloadPollerを残し、Workspaceの所有Threadで複数拡張を監視するAddFramePollerを追加した。これは描画から完了回収を分離するために必要な最小拡張であり、SystemやCLRを管理しない。初期化時のみ登録し、callback内の登録・Workspace破棄・再入は禁止する。

### 変更ファイル

| ファイル | 内容 |
|---|---|
| Editor/Core/EditorWorkspace.h | 継続操作の照会、frame監視callbackの登録・所有契約 |
| Editor/Core/EditorWorkspace.cpp | Native状態の公開、Play / mode / Reload開始条件、PrepareFrame監視、終了時解放 |
| Editor/Core/EditorWorkspaceReinstancing.cpp | patch前の新callback解除 |
| Editor/Services/ProjectDevelopmentCommands.cpp | Job状態の公開、開始再検査、非描画Poll、例外のConsole通知 |
| Editor/Services/StandaloneCommands.cpp | 直接Runtime commandの競合拒否、非描画Poll |
| Editor/Services/StandaloneSession.cpp | 起動要求時のProject再読込、BuildからLaunchまでのsnapshot共有 |
| Editor/Services/ManagedReloadCommands.cpp | Managed操作状態の公開、他操作との競合拒否 |
| Tests/EditorServicesTests.cpp | Job / Managed状態でのPlay拒否、frame監視、Shutdown参照解除の検証ソース |
| Tests/StandaloneSessionTests.cpp | Project Job中の直接Runtime command拒否の検証ソース |
| Docs/HybridProjectREADME.md | 共通操作・完了回収・Host再起動の範囲 |
| Docs/IntegrationAuditREADME.md | 第4回の調査・判断・変更・未検証事項 |

### 依存関係・ライフサイクル

依存方向はEditorから既存Runtime ProjectService / Platform Processのままとし、RuntimeからEditorへの参照は追加しない。公開Native / Managed ABI、Entity Handle、Levelの保存形式も変更しない。

拡張初期化でCommandと監視callbackを登録し、所有ThreadのPrepareFrameで完了を回収してから操作を許可する。Job workerは値snapshotだけを保持し、Workspace / Worldを参照しない。終了・Reinstancing時には監視callback、Toolbar、Command、Panelを解除し、最後のJob所有者が自身のworkerを取消・待機する。短いModule / Sourceのcommitは従来どおり途中強制終了しない。Managed子processの取消とCLR所有権も従来どおりである。

### 未実装・未検証

Editor操作の競合拒否と監視のテストはソース追加のみで、実行していない。実際の非同期完了、非表示Toolbar、Stopとの競合、Live++ patch、Editor終了時待機と複数Module追加後の別Runtime起動は未検証である。外部CLI / IDEやLive++の外部操作をこのEditor開始条件だけで排他制御する保証はしない。既存のprocess lock・Runtime停止契約を併用する。Module追加後の既存Host設定とCatalog更新は引き続き通常BuildとHost再起動を必要とする。

### 静的確認

今回の11ファイルのUTF-8・競合marker・末尾空白、104件のlocal include、利用側の3つのProject XMLとcompile入力を確認し、参照エラーはなかった。Command ID、宣言と使用箇所、isChecked照会の非再帰性、callbackとworkerの所有権・終了順序をソースで照合した。公開ABI / Handle / Serialization形式は変更していない。Workspaceの内部レイアウト変更自体は通常BuildとHost再起動で反映する対象である。git diff --checkもエラーなし。コンパイル・link・テスト・Game / Runtime / Editor実行は行っていない。

## Package入力整合性監査・第5回

### 調査結果と採用理由

既存Package-Game、Write-GameBuildStamp、ProjectBuild::Verify、Complete-GameDeployment、RuntimeのModuleDeployment / PackageStartupを照合した。既存の共通Module値計画・stamp・staging・inventoryは再利用する。ただしPackageは検証後の入力をcopy時に新しくhashしていたため、同時Buildで入れ替わった別世代を新しいinventoryで受け入れ得た。Managed directoryへBuild後に追加したファイルもstamp外のままcopyしていた。

Package内の値dictionaryに入力hashを固定する方式を採用した。独立したPackage Manager、全期間の排他lock、別のModule形式は追加しない。外部IDEは全Build期間を既存deployment lockで保護しないため、そのlockだけでは検証とcopyの同一性を保証できない。定義は一回のbytes読取からparseとhashを行い、実行コードはstampで検証したhashをcopy時にも要求する。copy先hashの従来検査と、公開直前の入力再検査を併用する。

件数一致だけでは誤ったDLLを選べるため、Native deploymentのID・filenameを共通Module計画と照合した。静的Nativeの宣言、Managed起動ファイルのstamp収録、Engine association、Standard profileも確認する。Runtimeが受け付けないNative entry型・未定義fieldはPackage側でも拒否する。

### 変更ファイル

| ファイル | 内容 |
|---|---|
| Tools/Project/Package-Game.ps1 | 入力snapshotのhash固定、stamp / roster / 起動ファイル照合、copy・公開時の再確認 |
| Tests/HybridPackaging.Tests.ps1 | Cpp / CSharp / Hybridの配布inventoryと、未収録Managed DLL・誤Native配置・起動stamp欠落・古いDLL・Engine association不一致を検査するソース |
| Docs/HybridProjectREADME.md | Package入力の固定と検証範囲 |
| Docs/IntegrationAuditREADME.md | 第5回の根拠・設計・変更・未検証事項 |

### 依存関係・ライフサイクル

Toolsから既存Project helper・ファイルAPIへの依存を維持する。Runtime / Editor / Managedへの新しい依存はない。入力・Build成果物は借用のままとし、自分のstagingだけを所有する。検証、staging copy、copy先hash検査、入力再確認、新規Directoryの公開の順に処理する。失敗時には既存Project・Build出力・Packageを変更せず、自分のstagingだけを従来の絶対path検査後に削除する。Native / Managed ABI、Handle、Level保存形式、Module lifecycle、CLR所有者は変更しない。

### 静的確認・未検証

HybridPackaging.Tests.ps1はユーザー実行用ソースであり、実行していない。PE読取に必要なheaderだけを持つ通常ファイルをfixtureに使い、コードsection・entry point・実Assemblyを生成しない。Build、dotnet、Game、DLLロードは呼び出さない。これはPackage処理の回帰用であり、配布先でのEngine機能、実Managed依存解決、公式Runtime同梱の成功を証明しない。同時Buildの実競合も未検証である。

今回の4ファイルをUTF-8・競合marker・末尾空白で確認し、2つのPowerShellをAST解析してエラーはなかった。Windows PowerShell用BOM、fixture内にcompiler / dotnet / Start-Process呼出しがないこと、Publish-Engineが変更したPackage scriptと既存helperをSDKへ収録することもソースで確認した。stamp writer・共通Module計画・Runtime loaderとのfield / path / hash形式を照合し、公開API・link対象・Serialization形式の変更はない。git diff --checkもエラーなし。Parserの成功を実際のPackage生成やRuntime動作の成功とは扱わない。

## Managed cleanup監査・第6回

### 調査結果・設計判断

NativeGameSystemScopeのscope / 関数世代、GameApiBridgeのWorld / Entity世代・所有Thread、ScriptSystemのattachment再検証、ScriptRuntimeの停止とSessionのRetire / Confirmを照合した。既存のHandle解決、Scheduler、World capability、collectible世代と弱参照監査を再利用した。

ScriptRuntimeはLifetime取消、OnDisable / OnDestroy、Unbind内のresource解除失敗をログだけに留め、StopからSessionへ失敗を返していなかった。外部resourceの解除失敗とGC参照の解放は別条件であり、弱参照が消えただけでReload成功を通知できない。

Runtime世代とSessionに解除失敗のboolを保持し、既存のTick / Stop / Reload結果へ伝える方式を採用した。別のLifecycle Managerや例外Registryは追加しない。例外objectを保存すると旧Assemblyをrootするため、診断はその場でログへ変換し、永続する状態はboolだけとする。解除失敗後も残りのBehaviour・resource・World capability・Catalogの解除を続け、GCで解放できても同Sessionの成功へ戻さない。

通常Tick中の解除失敗は既存GameRuntimeのframe末停止へ返す。Reload中は既存Suspendを使い、自動復旧を許可しない。Preflight候補の解除失敗もSessionを停止し、Native側ではStateRestoreErrorとする。通常の型・Property互換性エラーや、cleanupが正常で参照解放だけを待つ再試行は従来の契約を維持する。

[公式Unloadガイド](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability)の協調的解放・外部参照条件と、[.NET 10.0.0のAssemblyLoadContext実装](https://raw.githubusercontent.com/dotnet/runtime/v10.0.0/src/libraries/System.Private.CoreLib/src/System/Runtime/Loader/AssemblyLoadContext.cs)のUnloading呼出し順序を確認した。同実装ではUnloading eventが内部解放の準備より先に呼ばれ、callback例外がその後の処理を中断し得る。そのため既存RetiredGameのUnloadErrorもSessionの解除失敗として扱い、候補のUnload失敗から旧世代を無条件に再開しない。

### 変更ファイル

| ファイル | 内容 |
|---|---|
| Managed/Nexus.Managed.Bootstrap/ScriptRuntime.cs | cleanup失敗の保持、後続通常実行の停止、Stop結果の返却、snapshot拒否 |
| Managed/Nexus.Managed.Bootstrap/EntryPoints.cs | Sessionの解除失敗状態 |
| Managed/Nexus.Managed.Bootstrap/ReloadEntryPoints.cs | 世代終了・候補cleanup・World失効の結果伝達、GC確認との分離、再開拒否 |
| Managed/Nexus.Managed.Bootstrap/GameplayEntryPoints.cs | 解除失敗SessionのTick拒否 |
| Runtime/Scripting/CSharpRuntime.cpp | Prepare段階で停止したSessionをStateRestoreErrorへ反映 |
| Tests/Fixtures/ManagedGameplay/Behaviours.cs | Dispose / Disable / Destroy / cancellation / Preflight / Unloading hook失敗の負例 |
| Tests/ManagedGameplayTests.cpp | 残りの解除・失敗保持・再試行拒否・Scene未交換・Native結果分類の検証ソース |
| Docs/ManagedHotReloadREADME.md | cleanupとGCの成功条件・停止契約 |
| Docs/IntegrationAuditREADME.md | 第6回の調査・設計・変更・未検証事項 |

### 依存関係・所有権・ライフサイクル

Bootstrapから安定Managed API、Runtimeから既存Bridgeへの依存方向は維持する。公開DTO・C ABI・数値Handle・Module ID・Level保存形式を変更せず、Editorの直接Managed所有も追加しない。

停止はLifetime取消、必要なOnDisable、OnDestroy、逆登録順resource解除、Managed参照解除、World capability失効、CatalogのUnload要求、弱参照確認の順に試行する。一つの例外で後続resourceを残さず、停止結果をSessionへ引き継ぐ。NativeのStop失敗は既存quarantineとpatch禁止へ接続する。Preflight失敗時もNative Sceneを交換せず、cleanup失敗による停止と互換性エラーによる継続を区別する。今回の変更は通常BuildでEngineとBootstrapを揃える対象である。

### 未検証

追加したfixtureと統合テストは実行していない。実際のCLR解放、callback解除、Native World保持、quarantine、例外ログ、複数Moduleの同時終了は未検証である。外部resourceが実際に正常解放されたという証明や、自動rollback保証を追加したものではない。

今回の9ファイルのUTF-8・競合marker・末尾空白、17件のlocal include、5つのProject XMLと明示compile入力を静的に確認し、参照エラーはなかった。ScriptRuntime.Stopの内部bool結果と唯一の呼出元、Sessionの状態共有、Nativeのsuspended結果からエラー分類への接続を照合した。Publisherに変更したBootstrap sourceが収録されることも確認した。公開layout・entry名・Cdecl・Serialization形式・link対象は変更していない。git diff --checkもエラーなし。C++ / C#のコンパイル・link・テスト・Runtime実行の成功を証明した確認ではない。

## Native Loader再入監査・第7回

### 調査結果・採用理由

Phase 1〜8の現行READMEと未対応一覧を再確認し、共通Module実行、生成、Build、Packageの経路を照合した。引き続き実動作の証拠は不足しており、歴史的READMEにある旧Phase時点の未実装表は、冒頭で案内した最新READMEと区別する。

GameModuleLoaderの直接利用では、create / Update / destroyや完了ログからStopが再入できた。GameRuntimeのframe guardだけではLoader単体の再入を保護できず、callbackのstack上でstate・DLLが解放される可能性があった。LoggerはSink配送を同期lock内で行うため、Stopからdestroyのログへ再入する経路にも注意が必要である。

Loader内部へ操作scopeのguardと停止要求・終了中の状態を追加した。別のLoader Manager、Scheduler、Module Registryは作らない。既存GameRuntimeのframe guardは維持し、Loader自身の資源所有を局所的に保護する。操作中の停止は要求だけを記録し、外側のscope終了時に既存Stopを実行する。Start / Updateが停止要求を受けた場合は成功を通知しない。終了中の重複Stopは無視し、再入Update / patchは拒否する。

あわせてLevel / Worldの対応と、System exportに必要なLevelの有無をcreateより前に確認する。入力不備による失敗前に別WorldへEntityを生成させない。誤ThreadのStopは所有状態を書き換えず拒否し、所有Threadへ終了責務を残す。Loaderそのものを操作中に破棄する契約は設けず、所有者はcallback終了まで生存させる。

### 変更ファイル

| ファイル | 内容 |
|---|---|
| Runtime/Gameplay/GameModuleLoader.h | guard・停止状態、所有ThreadとLoader自身の寿命契約 |
| Runtime/Gameplay/GameModuleLoader.cpp | callback後の停止遅延、再入拒否、開始取消、Level検査、patch操作へのguard接続 |
| Tests/Fixtures/NativeModule/Module.cpp | callback内と戻り直前のログ、停止要求後のAPI利用を観測するfixture |
| Tests/NativeGameModuleTests.cpp | create / Update / destroy / 完了通知からの停止、二重Stop、再利用、誤World・Level省略・誤Thread拒否の検証ソース |
| Docs/GameModulesREADME.md | 現在の再入・所有・停止契約 |
| Docs/IntegrationAuditREADME.md | 第7回の調査・設計・変更・未検証事項 |

### 依存関係・ライフサイクル

Runtimeは既存GameApiBridge / System scope / Platform DLL APIへ依存するままで、Editor依存は追加しない。DLLへ渡すC ABI、state allocator、公開Handle、Level保存形式は変更しない。Loader内部のlayout変更自体は通常BuildとHost再起動で反映する。

操作開始で再入guardを取得し、DLL callbackとその同期通知が戻ってから停止を回収する。停止時はPublic API失効、Systemの逆順停止、Module stateの生成元DLL内破棄、関数表解除、FreeLibraryという従来順を維持する。既存の不完全patch・destroy失敗時のDLL pinも維持する。停止要求の取消や、新旧DLLの自動rollback保証は追加していない。

### 未検証

fixtureと回帰テストはソース追加のみで実行していない。実際のDLLロード・Unload、callbackの再入、Logger配送、複数Module停止、Live++との連携は未検証である。操作中のLoader所有者破棄や、所有Thread以外からの同時操作を安全にする汎用的な並行所有モデルは提供しない。

今回の6ファイルのUTF-8・競合marker・末尾空白、16件のlocal includeと5件のPublic include、4つのProject XMLと明示compile入力を静的に確認し、参照エラーはなかった。OperationGuardの宣言・定義、開始と停止の再入条件、Start / Update / patchの各scope終了時の停止回収、既存GameRuntimeの呼出元がLevelを渡すことを照合した。Game DLLの公開layout・entry名・Cdecl・allocator・保存形式・link対象は変更していない。git diff --checkもエラーなし。C++のコンパイル・link・DLLの実動作を証明した確認ではない。

## Managed複数Module依存監査・第8回

### 調査結果・採用理由

GameScriptCatalog、Metadata exporterの同sourceリンク、Managed-Deployment、Build / Reloadの生成順序を照合した。配置時の相対pathとhash衝突検査は再利用するが、RuntimeのResolverは最初の候補pathだけを選んでいた。Moduleごとの有効候補が異なるpathへ解決される場合、内容の相違を確認していなかった。rootもfilenameの一意性だけを見ており、実Assembly identityの重複はCLRロード後まで確認していなかった。

[公式のALC説明](https://learn.microsoft.com/en-us/dotnet/core/dependency-loading/understanding-assemblyloadcontext)では、通常のAssemblyは一つのcontext内でsimple nameごとに一つのversionだけをロードする。さらに[.NET 10のResolver実装](https://raw.githubusercontent.com/dotnet/runtime/v10.0.0/src/libraries/System.Private.CoreLib/src/System/Runtime/Loader/AssemblyDependencyResolver.cs)はsimple nameを大文字小文字を区別せず解決する。この契約に合わせ、安定API共有・Bootstrap / Metadata内部参照の判定もOrdinalIgnoreCaseへ揃えた。

既存Catalogへ限定的な検査を加え、独立したAssembly Registryや言語別Loaderは作らない。rootは[AssemblyName.GetAssemblyName](https://learn.microsoft.com/en-us/dotnet/api/system.reflection.assemblyname.getassemblyname?view=net-10.0)でmetadataだけを読み、CLRへ追加する前に実identityの重複と予約名を検査する。依存物は各Resolverの有効候補を列挙し、pathが異なる場合だけSHA-256を比較する。同一内容は共有できるが、異なる内容は登録順で選ばない。解決先は同じshadow世代へ限定する。

ディレクトリ内の全DLLを単純にbasenameで禁止する案は採用しなかった。異なるRIDや未使用copyまで排除し、有効なNuGet配置を壊すためである。宣言rootと有効候補を対象にする。通常のframework検索へのnull返却は維持し、Game codeのsandboxを提供したとは扱わない。

### 前回追加したfixtureの修正

第6回のCleanupPreflight / CleanupUnloadはconstructorで負例を有効にしていた。Exporterも全Scriptの既定値取得にconstructorを呼ぶため、そのままではMetadata生成を失敗させる。追加時の見落としを修正し、NEXUS_TEST_CLEANUP_CONSTRUCTORSを明示した負例scopeだけで副作用を有効にした。C++テストは元のprocess環境を保持し、例外経路でも復元する。Exporterや通常Project向けAPIへ特別な回避処理は追加しない。

### 変更ファイル

| ファイル | 内容 |
|---|---|
| Managed/Nexus.Managed.Bootstrap/GameScriptCatalog.cs | root実identity、複数Resolverの内容照合、shadow範囲、名前case照合、constructor失敗時の所有shadow cleanup |
| Tests/Fixtures/ManagedGameplay/Behaviours.cs | constructor負例の実行scope化、安定API共有とBootstrap名caseの検証用Behaviour |
| Tests/ManagedGameplayTests.cpp | 実Assembly copyとroot重複、ゲーム起動前の拒否、未使用copy許容、失敗後の再開始、shadow cleanup、環境復元の検証ソース |
| Docs/HybridProjectREADME.md | 複数ModuleのAssembly解決契約 |
| Docs/IntegrationAuditREADME.md | 第8回の根拠・設計・修正・未検証事項 |

### 依存関係・ライフサイクル

BootstrapとMetadata exporterは同じCatalog sourceを使う。System.ReflectionとSystem.Security.Cryptographyは既存.NET 10 frameworkを利用し、追加NuGetやRuntimeからEditorへの依存は加えない。公開ABI・関数表・Handle・Script ID・Level保存形式は変更しない。

所有shadowへcopyし、roster・root metadataを検査した後だけcollectible contextを作る。context作成後の依存解決では、同世代の有効候補を検証してロードする。constructorでの失敗は、まだGame Assemblyをロードしていないため、自分のGUID shadowを既存の所有path検査後に削除する。ロード後の世代終了は引き続きRelease / RetiredGameによる参照解除・Unload確認を使う。

### 未検証

追加・修正したテストは実行していない。root重複とcase判定の検証ソースはあるが、複数Resolverが実際に異なるManaged / Native依存pathを返すケース、NuGetの複数RID・satellite配置、Metadata exporterの実生成と配布先ロードは未検証である。静的な参照確認だけで依存解決全体の成功は主張しない。

今回の5ファイルのUTF-8・競合marker・末尾空白、10件のlocal include、Bootstrap / Metadata / fixture / test / Runtimeの6つのProject XMLと明示compile入力を確認し、参照エラーはなかった。Metadata exporterのCatalog sourceリンク、Publisher収録、CreateUuidの既存Runtime内定義とtest側参照も確認した。環境flagの2つのconstructorとNative側設定名、元値の復元、公開ABIと保存形式を変更しないことを照合した。git diff --checkもエラーなし。C++ / C#のコンパイル・link・Metadata生成・テスト・Runtime実行の成功を実証した確認ではない。

## 共通Property transaction監査・第9回

### 調査結果・設計判断

ManagedScriptMetadataの値accessor、ScriptComponentの型名と型別保存値、共通PropertyEditService / EditorHistory、TypeRegistry、共通Drawerと既存ManagedReflectionTestsを照合した。Script型の切替、Alias、未知値の保持、Version保護は既存MetadataとLevelDocumentを再利用する。

一方、PropertyEditServiceはsetter群の成功だけで適用成功とし、その直後にReadのoptionalを無条件に参照していた。例えばC++の値、旧Managed型の値、Script型名を一括変更した場合、旧Managedのsetterまでは成功しても、最後の型名変更で旧Property Handleが解決不能になる。逆順でsetterが拒否したケースだけでは、この経路を検証できない。

共通ApplyEditsで最終状態の全Handleを再検証する方式を採用した。解決不能なら既存の依存順序付きrollbackを使う。成功時には読取済みの最終値copyをPreviewへ返し、正規化後の値を再読取なしで履歴候補へ渡す。新しいHistory、C#専用Inspector、Component snapshot基盤は追加しない。Script型を含むbatchを一律禁止する案は、同型Alias等の有効な編集も拒否するため採用しなかった。

### 変更ファイル

| ファイル | 内容 |
|---|---|
| Editor/Services/PropertyEditService.h | 内部ApplyEditsの検証済み値出力と責務コメント |
| Editor/Services/PropertyEditService.cpp | 最終Handle再検証、失敗時rollback、Previewのunchecked optional参照除去 |
| Tests/ManagedReflectionTests.cpp | C++ / C#混在batchでの型変更拒否、両順序の復元、履歴保持、同型Alias許容の検証ソース |
| Docs/GameLogicReflectionREADME.md | 共通transactionと既存編集フローの関係 |
| Docs/IntegrationAuditREADME.md | 第9回の調査・判断・変更・未検証事項 |

### 依存関係・ライフサイクル

Editorから既存Runtime Reflection / Levelへの依存を維持する。Managed object、CLR、ECS内部pointerへの依存は追加しない。PropertyHandleは従来どおりPersistentIdとType / Property IDを保持し、Componentを所有しない。ApplyEditsの変更はEditor内部C++メソッドであり、Native / Managed ABI、Metadata JSON、Level保存形式は変更しない。

編集前の値を取得し、setterを順序解決して適用し、全Handleの最終値を取得した後だけ成功を返す。失敗時は直前の値へ戻し、未確定履歴へ要求値を記録しない。Undo / Redo / Cancelも同じApplyEditsを使用する。既存のsetterが失敗時に元を変更せず、getterが副作用のない値copyを返す契約は維持する。操作はEditor所有Threadで行い、Metadataの置換時には既存フレーム境界で履歴を解除する。

### 未検証

回帰テストはソース追加のみで実行していない。実際の共通Details操作、正規化するC++ setter、Undo / Redo、Level保存、Metadata更新との組合せは未検証である。任意の破壊的setter・副作用getterに対するrollback保証を追加したものではない。

今回の5ファイルのUTF-8・競合marker・末尾空白、12件のlocal include、3つのProject XMLと明示compile入力を静的に確認し、参照エラーはなかった。ApplyEditsの宣言・定義・全呼出し、検証済み値の件数と寿命、ScriptComponentのReflection markerとProperty ID、履歴へ失敗値を追加しない経路を照合した。private C++メソッドの署名変更はEditorSupportとHostを通常Buildで揃える対象であり、公開Native / Managed ABIやSerializationの変更ではない。git diff --checkもエラーなし。コンパイル・link・テスト・Runtime実行の成功を実証した確認ではない。

## Runtime所有Thread・総合参照監査・第10回

### 調査結果・設計判断

Project / Template / SDKの総合参照確認後、GameRuntimeの入口とNative / Managed内部のThread検査を照合した。内部APIは誤Threadを拒否するが、RuntimeのUpdateは内部の拒否を通常の更新失敗として扱い、その誤Thread上でStopへ進み得た。Interopを使わないCpp-only Worldには内部APIの検査もなく、Systemを別Threadで実行できた。Pauseやpatch放棄もRuntimeの状態を直接変更していた。

Worldを開始する最初の有効なStart要求から所有Threadを記録し、共通Runtime入口で状態参照・変更より先に検査する方式を採用した。Stop後も所有Threadを維持し、Loader / CLR / Schedulerを異なるThreadで再利用しない。開始前の設定値の逐次handoffは許可し、構築Threadへ固定して既存の準備処理を狭めない。開始前の同時操作や任意Threadからの照会・所有者破棄を安全にする並行所有モデルは追加しない。

新しいDispatcher / System Managerは作らず、既存ホスト側command経路の所有Threadへ要求を渡す契約を維持する。Stop等のvoid操作は誤Threadで何も変更せず戻り、Result操作はFramework code 45で拒否する。誤Threadでの診断callback再入を増やさないため、拒否したStopからLoggerを呼ばない。

### 変更ファイル

| ファイル | 内容 |
|---|---|
| Runtime/Application/GameRuntime.h | 所有Thread、入口検査、照会・所有者破棄・初期化の契約 |
| Runtime/Application/GameRuntime.cpp | 設定・Start / Stop / Update / Pause / Reload / patchの誤Thread拒否 |
| Tests/GameRuntimeTests.cpp | Cpp-onlyでの誤Thread停止・更新拒否、状態保持、開始前handoff、停止後の同Thread再開の検証ソース |
| Docs/GameRuntime.md | 共通RuntimeのThread制約 |
| Docs/IntegrationAuditREADME.md | 第10回の調査・設計・参照監査・残る根拠不足 |

### ライフサイクル・依存関係

設定値を逐次準備し、最初の有効なStart要求でThreadを確定した後、既存のSetup、Native Module、Managed、System / Transform初期化を同じThreadで行う。通常停止、callbackからの停止遅延、quarantineの順序は変更しない。誤Threadの失敗はpendingStopへ変換せず、WorldとModuleの所有者へ制御を残す。公開Game ABI、Handle、Project / Level保存形式と依存方向は変更しない。Runtime内部layoutの変更はEngine / Hostの通常Buildで揃える対象である。

### 総合参照監査

XML / template XML 67件、明示compile入力599件、Project参照25件、PowerShell 35件、template 6件の73入力を確認した。最初の存在検査ではIndependentGame fixtureのGeneratedGameReflection.cppが未生成だったが、同ProjectのBeforeTargets=ClCompileとGenerate-Reflection.ps1の出力先・stemが一致することを確認した。Buildを実行せず、生成待ちとして分類する。placeholderのcppを作って参照検査だけを通す処理は行わない。

Windows PowerShellのParseFileではUTF-8 BOMなしのLauncherInteraction.Tests.ps1が既定encodingで誤読された。UTF-8として明示読取したParseInputでは構文エラーはなかった。同テストはProcessStartInfoの.NET側APIとLauncherのPowerShell 7向け検証経路を使うため、この結果をWindows PowerShell 5での実行互換性とは扱わない。既存の別作業のテストsourceを変更してはいない。

今回の5ファイルはUTF-8、競合marker、末尾空白、18件のlocal includeを確認した。4件のProject XMLと361件の明示compile入力に参照エラーはなく、LauncherのUTF-8明示読取によるASTエラーも0件だった。git diff --checkにエラーはない。これはコンパイル・linkやThread動作の成功を示すものではない。

### 未検証・完了判定

追加したThread検証も含め、ビルド・テスト・CLR / DLL / Editor / Game、生成器、Publish / Packageは実行していない。各Phaseの実動作、SDK接続、配布先での依存解決と共有Worldの更新を証明する結果は引き続き不足している。ユーザー側の最新ビルド・動作確認結果を依頼しているが、その結果を受け取るまでは動作完了と判定しない。

## Rebuild Scripts要求の監査・第11回

### 調査結果・設計判断

Phase 8の共通Editor操作を照合したところ、ProjectDevelopmentJobはRebuildScriptsを通常Buildと同じrebuild=falseで処理していた。既存Managed targetsはNexusManagedRebuild=trueを処理できるが、UIからの要求が届かず、追加Managed Moduleにも通常Buildしか渡らなかった。

ProjectBuildOptionsへ既定falseのrebuildManagedを追加し、既存ProjectBuildとBuild-Gameへ値として渡す。全体rebuildとの論理和をManaged targetへ使用し、Native targetの選択には全体rebuildだけを使う。新しいBuild ManagerやScript専用の配布経路は追加しない。単独dotnet呼出しへの切替案は、追加Module・Metadata・stampの共通確定を迂回するため採用しない。

### 変更ファイル

| ファイル | 内容 |
|---|---|
| Editor/Services/ProjectDevelopmentCommands.cpp | Rebuild ScriptsをManaged再構築要求としてworkerへ設定 |
| Tools/Project/ProjectBuild.h | 既定falseのrebuildManaged値と責務コメント |
| Tools/Project/ProjectBuild.cpp | 明示要求を-RebuildManagedへ変換 |
| Tools/Project/Build-Game.ps1 | 主Managed targetsと追加Moduleの配置工程へ要求を伝搬 |
| Tools/Project/Complete-GameDeployment.ps1 | 追加Managed ModuleのBuild / Rebuild選択 |
| Build/Nexus.GameDeployment.targets | IDEのNexusManagedRebuildを追加Moduleへ伝搬 |
| Docs/HybridProjectREADME.md | Build / Rebuild / Rebuild Scriptsと直接Script / IDEの使用契約 |
| Docs/IntegrationAuditREADME.md | 第11回の調査・判断・確認・残作業 |

### 依存関係・ライフサイクル

Editorの所有Threadで開始条件を確認し、workerへBuild optionsのcopyを渡す。主Moduleの既存targets、追加Moduleの依存順生成、集約Metadata、配置、全体成功後のstamp確定を維持する。Play / 別Runtime / Reloadとの競合拒否と子processの取消も変更しない。通常Buildでは両flagがfalse、全体Rebuildではrebuild=true、Rebuild ScriptsではrebuildManaged=trueとなる。Managed再構築はBootstrap / 安定API / Metadata toolも対象で、Native / Hostは差分Buildを継続する。稼働中Hostのファイル置換が拒否された場合は従来どおりHostを閉じてBuildする。

新しいRuntime依存、公開Game ABI、Project / Level保存項目は増やさない。ProjectBuildOptionsは開発ツール内部のC++値であり、利用側の通常再Buildが必要となる。旧呼出しは既定falseを使い、従来の差分Build動作を保つ。

### 静的確認

変更8ファイルのUTF-8・競合marker・末尾空白、14件のlocal include、2つのPowerShellのAST、関連6件のXMLを確認し、エラーはなかった。要求名と各parameter / propertyの伝搬、NativeとManagedのtarget選択、既存Publish-EngineのScript / targets収録経路を照合した。git diff --checkにもエラーはない。既存2ファイルの改行警告を解消するための変更は行っていない。

### 未検証

変更はソース・設定の静的照合のみで、Build、テスト、Editor、dotnet、Packageは実行していない。実際のMSBuild target展開、全Managed ProjectへのRebuild適用、共有出力の更新、stamp確定と再起動後の読込はユーザー側の確認が必要である。これらの結果なしにPhase 8全体の動作完了とは判定しない。

## 第12回：実行検証とEngine利用の完了監査（2026-11-10〜11）

### 調査・再利用と設計判断

ユーザーの実行許可後、既存GameRuntime、GameModuleLoader、SystemScheduler、共通Reflection、ProjectBuildとPackage経路を実際にBuild・実行した。別のECSや試験専用のRuntimeを追加せず、生成したGame Projectと既存テストを使った。調査で判明したhostfxr寿命、Windows PowerShellのnull変換、Live++ receiver Thread、Browser表示領域の問題を修正した。

hostfxrはProcess寿命へpinし、Host contextとセッション参照を従来どおり解放する。CLR再起動による解決は採用しない。Live++ hookは不変の入力snapshotだけを参照し、Runtime / World / APIの検査・再登録は所有Threadで行う。hookからWorldを直接呼び出す案はThread契約を破るため採用しない。Browserは既存生成サービスを維持し、Template一覧の独立scrollで入力欄を確保した。

### 要求と実証した経路

| Phase | 今回の根拠 |
|---|---|
| 1 | ProjectTests、生成回帰、Native BrowserのDebug / Release実操作でC++ / C#作成・初期Module・Registry更新・正常終了を確認 |
| 2 | EditorTestsのModule ABI / lifecycleと、実Cpp Project生成・共通Build・Runtime起動を確認 |
| 3 | 既存SchedulerへのNative System登録・公開API分離のテスト、2 Native Moduleを含むHybrid ProjectのBuild・起動を確認 |
| 4 | Managed Hosting / GameplayのDebug / Releaseテスト、実CSharp Project生成・RuntimeのCreate / Enable / Disable / Destroyを確認 |
| 5 | Native API寿命・世代・Thread検査のテスト、C#で保持したBindingを実Nativeパッチ前後に呼び出し成功を確認 |
| 6 | 共通Property編集・Undo / Redo・Level保存のEditorTestsとManaged Reflection経路、共通EditorのGPU起動を確認 |
| 7 | collectible AssemblyのReload・失敗処理・状態復元テスト、公式Live++ 2.11.4の実body patch、実Compile Error時の旧World / Binding保持と同Processでの復帰を確認 |
| 8 | 両方向の言語途中追加、2 Native / 2 Managedの共通Build / Rebuild Scripts / 起動、Release Packageの検証・同梱.NETでのGPU起動を確認 |

EditorTests / ManagedGameplayTests / ManagedHostingTests / ProjectTests、生成・Package回帰、Browser回帰の実行はexit 0。Release Browserの最終記録はGenerated/HybridVerification/HybridBrowser.Release.log、生成出力はBrowserCreation/4a50adfa3a5346db9497e3e6dd71eee6である。Live++の実Compile Errorは意図した負例で、NativePatchSignals6のErrorPreservedとPassedを確認した。

### 変更ファイル・依存関係・ライフサイクル

ファイルごとの変更は[実行検証記録の変更表](HybridVerificationREADME.md#変更ファイルと責務)に記載した。Runtime / Managed / Editorの依存方向、公開Game ABI、Project / Levelの保存形式は維持する。ScriptComponentの共通Reflection生成物は既存生成器で更新した。

初期化は既存RuntimeのLevel / Bridge / Native Module / Managedセッションを利用する。終了はManaged参照の解放、Native Moduleの逆順解除、Bridge、Levelの順で行う。GameScriptsのcollectible世代とProcess寿命のCLR / hostfxrを区別する。SDK hookの解除は通知終了を待ち、その後snapshotを破棄する。試験用Win32入力は自分のBrowser Windowだけを借用し、DPI contextを復元して正常終了させる。

### 静的確認

関連実装のinclude、namespace、宣言と定義、既存Runtimeへのlink、Handle / Binding世代、owner Thread、保存互換と終了順序を照合した。最終差分の13ファイルについて厳密UTF-8・競合marker・末尾空白、Browser試験のPowerShell AST、Native実パッチ試験のProject XMLを確認した。git diff --checkはエラーなし。既存ファイルの改行警告を解消するための変更や、他作業者の変更破棄は行っていない。

### 完了範囲と未検証

Phase 1〜8の対応構成について、実装・実テスト・Engine内利用・Release配布物起動の経路を確認した。確認済みProjectと起動手順は[実行検証記録](HybridVerificationREADME.md#engineで利用する方法)を参照する。

Input等の未実装Engine Backend、汎用Native Extension、自動Binding、任意C++ layout / state移行、複数Native DLLの同時Live++ patchは実装完了と主張しない。前二者のうちInput等はユーザー確認済みの別作業、Native Extension / 自動Bindingは各Phaseの明示的な対象外である。Develop構成の実行、別PC / 全GPU / 長時間動作は未検証。ProjectTestsのsymlink作成ケースはOS権限不足でSKIPした。GPU起動の8フレーム成功を全UI操作の保証とは扱わない。
