# C++ / C# Hybrid実行検証

2026-11-10、ユーザーからビルド・テストとEngineで利用できる状態までの確認を依頼され、従来の静的監査に続いて実行検証を行った。C++ / C# / Hybridの生成・共通Runtime、共通Editor、両BackendのReload、配布物の起動までを確認した。以下に実行範囲と環境依存の制約を記録する。過去のREADMEの「未実行」は各実装時点の履歴である。

## 環境と確認範囲

- Windows x64、Visual Studio 18 Community / MSVC v145、Debug / Release。
- .NET SDK 10.0.402、Microsoft.NETCore.App 10.0.13。
- 公式Host packの10.0.13 / win-x64 / nativeを明示してnethost.dllを配置。
- Input等のBackend自体はユーザー確認で未実装。今回、架空のBackendや言語別の代替実装を追加しない。
- MSBuild FileTrackerはsandbox内でアクセス拒否になったため、承認されたsandbox外のBuildを使用した。

## 実行済みの結果

| 対象 | 結果・範囲 |
|---|---|
| Nexus.Managed / Bootstrap | dotnet build成功、警告0・エラー0 |
| NexusEditorTests | Debug Build成功、警告0・エラー0、実行exit 0。Native Module / System / API、Runtime Thread所有、共通Reflection・編集・Undo / Redo・保存など |
| NexusManagedGameplayTests | Debug Build成功、警告0・エラー0、実行exit 0。実Hosting、同Level再開始、共有ECS、Script lifecycle、Native Binding、Managed Reload・失敗隔離・状態復元・cleanup負例 |
| NexusManagedHostingTests | Debug Build成功、警告0・エラー0、実行exit 0。複数session、繰返し開始・停止、Logger、ABI / Thread / entry拒否 |
| NexusProjectTests | Debug Build成功、警告0・エラー0、実行exit 0。保存互換、生成rollback、Module依存・Build計画・Package inventory、Unicode配置。symlink作成ケースはOS権限不足によりSKIP |
| HybridGeneration.Tests.ps1 | Windows PowerShellで実行exit 0。Native System / Facade / Behaviour生成、Module追加、共通Host統合、Project ID・Level・Asset保持 |
| HybridPackaging.Tests.ps1 | Windows PowerShellで実行exit 0。Cpp / CSharp / Hybridの配布byte・hash・不正入力拒否。通常ファイルfixtureでありGame起動の証明ではない |
| NexusEditor / NexusRuntimeHost / NexusProjectTool | Debug Build成功、警告0・エラー0 |
| Standalone Launcher | dotnet Debug Build成功、警告0・エラー0。Browserの手操作は本記録の範囲外 |
| CppSample / CSharpSample | 実テンプレート生成・共通build-editor・Runtimeの--check-gameがexit 0。C#はPlayerControllerのCreate / Enable / Disable / DestroyもLoggerで確認 |
| HybridSample | 生成・共通build-editor・verify-editor・CPU起動がexit 0。RuntimeとEditorの--hidden --frames 8もexit 0、DirectX 12で8フレームと正常終了を確認 |
| 途中追加・再構築 | HybridSampleへExtraNative / ExtraManagedを追加し、2 Native / 2 Managedのmanifestで共通Build・CPU起動がexit 0。BulletSystem / EnemyMover追加後の-RebuildManagedとCPU再起動もexit 0 |
| 実Hybrid Package | private .NET込みでEngine外へ生成、verify-package / CPU起動 / GPU 8フレームがexit 0。Project / Engineの引数なし。Hosting traceで同梱hostfxr / CoreCLRのpathを確認 |
| NativeLivePatch profile | 実SDK 2.11.4でCppSampleのEditor / Runtime Buildが成功。RuntimeのAgent接続・DLL登録・GPU 8フレームと正常終了もexit 0 |
| Native実コードパッチ | NativeModuleLivePatchSmokeが実SDKでexit 0。同じWorld / Entityを保持し、Native移動先5から9への変更・Binding再登録・停止を確認。NativePatchSignals4/Passedを保存 |
| C#併用のNative実パッチ | 同じテストホストとNativePatch/ManagedProbe.csでexit 0。C#が保持済みのNativeFunctionをパッチ後も明示Refreshせず呼び、世代の再解決と成功回数・移動結果を確認。NativePatchSignals5/Passedを保存 |
| Native Compile Errorからの復帰 | 実SDKへ生成Main.cppの意図的なstatic_assert失敗を渡し、旧コード・World・C# Bindingの保持を確認。その後同Processで正常パッチがexit 0。NativePatchSignals6/ErrorPreserved / Passedを保存 |
| Release | Engine Editor / Runtime / ProjectTool、Managed Gameplay / HostingのBuildが警告0・エラー0。Managed Gameplay / Hostingの実行もexit 0 |
| Releaseの4 Moduleゲーム | HybridSampleの共通Release BuildとCPU起動がexit 0。private .NET込みのRelease Packageでinventory検証・GPU 8フレーム・終了がexit 0 |
| 言語追加の逆方向 | CSharpSampleへ初めてNativeAddedを追加し、共通Editor BuildとCPU起動がexit 0。元C#のCreate / Enable / Disable / Destroyも確認 |
| Native Project Browser | Debug / Release Buildが警告0・エラー0。125% DPIで表示を実画像確認。HybridBrowser.Tests.ps1のDebug / Release実行で、実入力・テンプレート選択・初期言語選択・C++ / C#作成・Registry更新・終了がexit 0 |

## 実行で発見・修正した問題

1. NativeGameModuleTestsのContext初期化でapi / worldの順序が逆だった。公開structと一致させた。
2. GameServicesTestsの同一scopeでworkerを二重宣言していた。呼出し検証用Threadの名前を分離した。
3. Windows PowerShellではFile.Replaceのbackup引数に$nullを渡すと空文字へ変換され、既存Source / Moduleの更新が失敗した。System.Management.Automation.Language.NullString.Valueで.NETのnull stringを明示する。Add-GameSource / Add-GameModule / Complete-GameDeployment / Managed-Deploymentの同じ境界を修正した。
4. 生成テストがPowerShellのread-only自動変数$Hostを上書きしていた。fixtureのHostソース値をhostSourceへ変更した。
5. DotNetHostがhostfxrをセッションごとにUnloadしており、CoreCLR / hostpolicyが残る二回目の初期化で0x80008081になった。hostfxrをProcess寿命へpinし、host contextとセッション参照は従来どおり解放する。Managed Gameplay / Hostingの繰返し開始テストで修正後の成功を確認した。
6. Managedテストの失敗時にInterop statusだけで原因が隠れたため、共通Loggerの末尾40記録を出力するようにした。
7. Browserは既定Window / 125% DPIでTemplateが一列に並び、初期言語などの入力欄が長い一覧の下へ隠れていた。2列表示の境界と一覧の独立scrollを調整した。
8. 非表示Browser入力の試験では、DPI非対応のPowerShellから送信した座標が1.25倍に変換されていた。送信ThreadだけDPI対応contextへ切り替え、必ず復元する試験adapterに修正した。Native側の入力処理を変更する案は不要と判明し、一時診断も除去した。

Hostingの判断では[Microsoft公式Native Hosting](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting)と[公式hostpolicy実装](https://github.com/dotnet/runtime/blob/v10.0.0/src/native/corehost/hostpolicy/hostpolicy.cpp)を参照した。CoreCLRを停止・再起動したとは扱わず、GameScriptsのcollectible AssemblyLoadContextと区別する。

## Native実パッチの追加検証

Tests/NativeModuleLivePatchSmoke.cppとBuild/NexusNativeModuleLivePatchSmoke.vcxprojは、既存GameRuntime / LiveCodingAgentを使うCPU専用テストである。新しいRuntimeやSystem Managerは作らず、公式SDK Backendを明示Buildした場合だけ実行する。新規signal directoryのReady後、生成NativeModuleのMain.cppのTargetRequestを5から9へ変更し、Requestファイルを作る。実パッチ成功・WorldとEntityの同一性・新しい移動先9・停止をすべて確認した場合だけPassedとexit 0を返す。

最初の試行では適用前の安全検査によりProcessがexit 1で終了した。診断を追加した結果、SDK hookのSOURCE通知がownerThread=no / attempting=yesであることを確認した。SDK hookがRuntime所有Threadで呼ばれるという想定が誤っていた。

hookからRuntimeへ再入する案は採用せず、Loaderと同じheader / 翻訳単位のsnapshotを値として共有する方式へ修正した。PrepareとWorld / Module契約の検査はownerが行い、準備済み状態をatomicで公開した後にSDKへ入る。hookでは変更CPPの範囲と固定入力・header集合を再検査し、World / Entity / System / DLL stateには触れない。適用後のABI検査とBinding再登録はownerで行う。Observer解除は従来のDLL側mutexで通知の終了を待ち、snapshotを最後に破棄する。

修正後は実パッチ、移動先9、World / Entity保持、終了をすべて確認してexit 0となった。固定入力・header競合と未準備の通知は引き続き拒否する。CPP内の任意layout変更を自動判定できる保証は追加していない。変更したSourceはGenerated内のサンプルだけで、既存ユーザーSourceは変更していない。

この検証ではRuntime/Gameplay/GameModuleLoader.h / .cppへ既存入力読取の値APIを追加し、Platform/Development/LiveCodingAgent.cppのSDK callbackからRuntime直接呼出しを除去した。Runtime/Application/GameRuntime.hのThread契約、Templates/NativeModule/NativePatchHooks.cpp.inの拒否診断も更新した。ScriptComponentの生成Reflectionは実Buildの既存生成器で更新し、GeneratedReflection.cpp / .h / ReflectionSchema.jsonへ登録情報が反映された。

続いてCppSampleへManagedPatchを途中追加し、Tests/Fixtures/NativePatch/ManagedProbe.csを生成Moduleへコピーして同じ専用profileでBuildした。テストホストの任意第4引数へHost出力Rootを渡すと、同一WorldへScriptComponentを追加して実Hostingする。ManagedProbeはOnCreateで取得したNativeFunctionを保持し、OnUpdateで呼び出す。Nativeパッチ後の呼出し成功を、Native Crowdが操作しないscale値の増分で確認する。結果はexit 0で、C#へDLL pointerを渡さず再登録後のBinding世代を解決できた。CppSampleはこの検証の過程でHybridへ拡張されたため、上表のCpp-only結果は追加前の記録である。

同じホストへRequestErrorを送る追加試験では、生成Main.cppへstatic_assert(false)を入れて実Compile Errorを起こした。旧target=5、同じWorld、稼働するC# Bindingを保持し、ErrorPreservedの後に正しいtarget=9のSourceとRequestを渡して正常パッチへ復帰した。Compile Errorログは意図した負例であり、成功時は最終Process exit 0とPassedの両方を確認した。

## 変更ファイルと責務

| ファイル（Project/基準） | 今回の変更 |
|---|---|
| Runtime/Scripting/DotNetHost.cpp / .h | hostfxrのProcess寿命とセッション参照の分離、繰返しPlayの修正 |
| Runtime/Gameplay/GameModuleLoader.cpp / .h | 既存の固定入力読取を値snapshotとしてBackendと共有 |
| Runtime/Application/GameRuntime.h | SDK前のowner-thread検査とhook側の入力検査を区別する契約 |
| Platform/Development/LiveCodingAgent.cpp | receiver Threadでは入力snapshotだけを照合し、ownerでWorld / 契約検査・Binding再登録を行う |
| Templates/NativeModule/NativePatchHooks.cpp.in | 不正なpre-patch入力を拒否した場合の診断 |
| Tools/Project/Add-GameModule.ps1 | 原子的な既存ファイル置換のnull string修正 |
| Tools/Project/Add-GameSource.ps1 | 同上、Native Source更新の失敗修正 |
| Tools/Project/Complete-GameDeployment.ps1 | 同上、配置manifestの繰返し更新修正 |
| Tools/Project/Managed-Deployment.ps1 | 同上、Managed依存物の置換修正 |
| Launcher/LauncherApplication.cpp | 初期言語・Project入力とTemplate一覧の表示・scroll調整 |
| Tests/NativeGameModuleTests.cpp | 公開Contextのapi / world初期化順修正 |
| Tests/GameServicesTests.cpp | Thread変数の重複宣言修正 |
| Tests/ManagedGameplayTests.cpp | 失敗時の共通Logger診断出力 |
| Tests/HybridGeneration.Tests.ps1 | PowerShell自動変数との衝突修正 |
| Tests/HybridBrowser.Tests.ps1 | 実BrowserからCpp / CSharpを作成するDPI対応回帰試験 |
| Tests/NativeModuleLivePatchSmoke.cpp | 実Nativeパッチ、Compile失敗、World / Entity / Managed Binding保持の試験 |
| Tests/Fixtures/NativePatch/ManagedProbe.cs | Nativeパッチをまたいで数値Bindingを保持するManaged fixture |
| Build/NexusNativeModuleLivePatchSmoke.vcxproj | 既存Runtimeと実SDKを使う試験のBuild対象 |
| Build/NexusManagedGameplayTests.vcxproj | 明示BuildでReload fixtureを生成する責務コメント |
| Runtime/Reflection/EngineGenerated/GeneratedReflection.cpp / .h / ReflectionSchema.json | 既存生成器の実行によるScriptComponentの共通Reflection登録 |
| Docs/CSharpRuntimeREADME.md | Process寿命と再PlayのHosting契約 |
| Docs/NativeModuleLivePPREADME.md | hook Threadの実測とowner / snapshotの分担 |
| Docs/HybridProjectREADME.md / IntegrationAuditREADME.md / HybridVerificationREADME.md | 実装履歴と最新検証結果・起動方法 |
| ../README.md | 最新の実行検証記録への案内 |

新しいECS / Level / Asset Manager / Inspectorは追加していない。RuntimeからEditor / ImGuiへの依存も追加していない。新しい公開Game ABIや保存形式の変更はなく、snapshotの値APIはEngine内部のC++境界に限定する。試験は既存の所有順序を使い、Runtime停止ではManaged、逆順Native、Bridge、Levelの順で解放する。CLR / hostfxrはProcess寿命、GameScriptsのcollectible世代はセッション / Reload寿命として区別する。

## Engineで利用する方法

共通CLIでDevelopment Engineを登録し、Generated/HybridVerification/HybridSampleへhybrid-gameplayテンプレートを実際に生成した。SourceやDLLをEngine本体へ埋め込まず、生成Projectの独立Game ModuleとGameScriptsを共通Build経路で検証した。成果物やログはGenerated/HybridVerificationに保持する。Engine登録は同directoryの専用EngineRegistry.jsonに限定し、利用者の既定Registryは置換していない。

Repository Rootから次のコマンドで確認済みProjectを開ける。EditorでScriptを選び、共通Detailsで公開値を編集し、既存Play / Reload操作を使う。Build時はNEXUS_DOTNET_HOST_PACK_DIRへ公式Host packのnative配置を指定する。Module / Source追加後は共通BuildとHost再起動を行う。

```powershell
$env:NEXUS_DOTNET_HOST_PACK_DIR = 'C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Host.win-x64/10.0.13/runtimes/win-x64/native'
& './Generated/HybridVerification/HybridSample/Build/Bin/x64/Debug/GameEditor.exe' --project './Generated/HybridVerification/HybridSample/HybridSample.nxproject' --engine './Project'
```

上記のHost packは確認に使用した配置であり、別のSDK packを使う場合は対応する公式native directoryを指定する。Process内の環境設定だけで、Projectの言語や共有Engine設定へ固定しない。

実配布物は承認された新規directory C:/MyProject/NexusEngine-HybridPackage-20261110へ作成した。開発Project / Engine指定をせず、Bin/GameRuntime.exeから起動する。Packageは2 Module時点の独立snapshotであり、後から開発側へ追加したModuleを自動で取り込むものではない。同梱.NETの探索はGenerated/HybridVerification/PackageHosting.trace.logに記録した。これは同一PC上の可搬起動検証であり、別PCや.NET未インストールOSの実証とは区別する。

4 ModuleのRelease配布物はC:/MyProject/NexusEngine-HybridReleasePackage-20261110/Bin/GameRuntime.exeである。ReleasePackageGpu.logとReleasePackageHosting.trace.logに、引数によるProject / Engine指定なしでの描画と同梱hostfxr / CoreCLRの探索結果を残した。SourceやSDK全体は配布の実行依存にしていない。

## 再実行する試験

生成・Packageの回帰はTests/HybridGeneration.Tests.ps1とHybridPackaging.Tests.ps1、実Browserの作成は次を使用する。Browser試験は現在の125% DPIのgolden座標を前提にし、条件外では中断する。Native / Managed / Editorの実行ログはGenerated/HybridVerificationの*.final.log / *.Release.logに保存した。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Project/Tests/HybridBrowser.Tests.ps1 -EngineRegistry Generated/HybridVerification/EngineRegistry.json -Configuration Debug
```

## 未確認・制約

別マシン配布、全configurationの実行、任意Native state移行、複数Native DLLの同時パッチはこの記録の成功範囲に含めない。汎用Native Extensionや自動Bindingは要求で後続へ残された機能である。8フレームの起動検証を長時間の安定動作や全UI操作の証明とは扱わない。
