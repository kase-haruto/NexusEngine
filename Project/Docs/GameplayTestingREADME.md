# C++ / C#追加機能のテスト手順

再実行用の手順。作成時点では今回の変更後のBuild・テストは未実行だった。以前の実行記録と、今回追加したNative値Component/複数Scriptの確認結果を区別する。

後続のユーザー指示により、2026-11-12にエージェントがBuild/実行を行った。Mono複数Scriptと既存CoreCLR回帰の成功・確認範囲は[最新実行記録](MultipleScriptsVerificationREADME.md)を参照。

## 1. 準備

編集中のLevelを保存し、起動中のGame Editor/Runtimeを終了する。Visual StudioのDeveloper PowerShellを開き、Repository Rootで実行する。C++ Projectはv145/x64、テストはDebug構成を使う。通常のPowerShellでmsbuildが見つからない場合はDeveloper PowerShellへ切り替える。

```powershell
Set-Location C:\MyProject\NexusEngine
$ErrorActionPreference = 'Stop'
$testOutput = Join-Path $PWD ('Generated/ManualGameplay-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testOutput | Out-Null
function Confirm-Stage([string]$stage) {
    if ($LASTEXITCODE -ne 0) { throw "$stage failed: exit=$LASTEXITCODE. Stop here and inspect the log." }
}
```

実行には既存ProjectのC++/Windows SDK依存物が必要。以下はデータの破棄やGitの巻き戻しを行わないが、Buildは通常どおり生成物を更新し、テストはGenerated内に検証用Level等を作る。利用中のゲーム本体で最初の確認をせず、テスト/検証用Projectを使う。

## 2. 共通ECS・Reflection・Editorサービス

まずこの段階だけを実行する。Native fixture DLLはProjectReferenceからBuildされる。作業ディレクトリはRootのままとする。

```powershell
msbuild .\Project\Tests\NexusEditorTests.vcxproj /m /t:Build /p:Configuration=Debug /p:Platform=x64 `
    /fl "/flp:logfile=$testOutput/EditorTests.build.log;verbosity=normal"
Confirm-Stage 'EditorTests build'

# Windows PowerShellのstderr変換で、意図した負例ログを途中終了と扱わない。
$ErrorActionPreference = 'Continue'
& .\Generated\Outputs\EditorTests\NexusEditorTests.exe 2>&1 |
    Tee-Object -FilePath "$testOutput/EditorTests.run.log"
Confirm-Stage 'EditorTests run'
$ErrorActionPreference = 'Stop'
```

成功判定はexit 0。assert/FAIL/コンパイルエラーが出た場合はその段階で停止する。途中のPASS行だけで全成功としない。

主に確認する追加経路：

- Native DLL由来の値Metadata、共通Property編集、追加/個別解除のUndo、Level保存復元。
- Scriptの追加で既存Scriptを置換しないこと、追加Scriptの独立したenabled、個別解除とUndo。
- 保存した追加Scriptの値/順序の復元と、instance/attachmentを永続化しないこと。
- 同じEntityのtoken状態分離、主Script解除時の昇格、旧tokenによる書込拒否、旧APIの全解除。

この段階は実Monoでの実行やDomain Reload成功の証明にはならない。テストには多数の既存Editorサービス回帰も含まれる。

## 3. Mono上で複数ScriptとReload

段階2が成功してから進む。前提は.NET SDK、.NET Framework 4.8参照Assembly、Mono x64/SGen SDK。コンパイルには.NET SDKを使い、実行にはMonoを使う。

同じDeveloper PowerShellと変数を使う。NEXUS_MONO_ROOTは実際のSDKまたは配布Runtime Rootへ置き換える。bin/mono-2.0-sgen.dllだけでなくlib/mono/4.5、GAC、etc/monoも必要。

```powershell
$env:NEXUS_MONO_ROOT = 'C:/SDK/Mono' # 実際の配置に変更する
if (-not (Test-Path -LiteralPath "$env:NEXUS_MONO_ROOT/bin/mono-2.0-sgen.dll")) {
    throw 'Set NEXUS_MONO_ROOT to the actual Mono x64 SDK root.'
}

msbuild .\Project\Build\NexusMonoGameplayTests.vcxproj /m /t:Build /p:Configuration=Debug /p:Platform=x64 `
    /fl "/flp:logfile=$testOutput/MonoTests.build.log;verbosity=normal"
Confirm-Stage 'Mono native test build'

dotnet build .\Project\Managed\Nexus.Managed.Bootstrap\Nexus.Managed.Bootstrap.csproj `
    -c Debug -o "$testOutput/Managed" `
    -p:NexusManagedRuntime=Mono -p:TargetFramework=net48 -p:GenerateDependencyFile=true `
    "-p:NexusManagedIntermediateRoot=$testOutput/obj/Managed/" 2>&1 |
    Tee-Object -FilePath "$testOutput/MonoBootstrap.build.log"
Confirm-Stage 'Mono API and Bootstrap build'

dotnet build .\Project\Tests\Fixtures\MonoGameplay\MonoGameplay.csproj `
    -c Debug -o "$testOutput/V1" -p:NexusMonoProbeVersion=1 `
    "-p:NexusManagedApiPath=$testOutput/Managed/Nexus.Managed.dll" `
    "-p:BaseIntermediateOutputPath=$testOutput/obj/V1/" 2>&1 |
    Tee-Object -FilePath "$testOutput/MonoFixtureV1.build.log"
Confirm-Stage 'Mono fixture V1 build'

dotnet build .\Project\Tests\Fixtures\MonoGameplay\MonoGameplay.csproj `
    -c Debug -o "$testOutput/V2" -p:NexusMonoProbeVersion=2 `
    "-p:NexusManagedApiPath=$testOutput/Managed/Nexus.Managed.dll" `
    "-p:BaseIntermediateOutputPath=$testOutput/obj/V2/" 2>&1 |
    Tee-Object -FilePath "$testOutput/MonoFixtureV2.build.log"
Confirm-Stage 'Mono fixture V2 build'

$ErrorActionPreference = 'Continue'
& .\Generated\Modules\x64\Debug\NexusMonoGameplayTests\NexusMonoGameplayTests.exe `
    $env:NEXUS_MONO_ROOT "$testOutput/Managed/Nexus.Managed.Bootstrap.dll" `
    "$testOutput/V1/MonoGameScripts.dll" "$testOutput/V2/MonoGameScripts.dll" 2>&1 |
    Tee-Object -FilePath "$testOutput/MonoTests.run.log"
Confirm-Stage 'Mono gameplay run'
$ErrorActionPreference = 'Stop'
```

古いBootstrap/API/V1/V2 DLLを混ぜない。NexusMonoGameplayTests.vcxprojはNativeテスト本体とRuntimeをBuildするだけなので、上のManaged Buildを省略しない。

成功判定はexit 0と最後の`PASS: actual Mono hosting, Domain unload/restart, shared ECS, lifecycle, reload and state restoration.`。このテストには意図的な不在Assembly/誤thread等の負例があり、その診断ログ自体を最終失敗と誤認しない。最終exitとFAIL/例外を確認する。

追加した期待結果：

| 時点 | 期待 |
|---|---|
| 初回Update(1) | 同じEntityのX=3、第二ScriptによるY=4 |
| 第二Scriptの更新 | 自身だけを無効化し、主Scriptは有効なまま |
| 再アタッチAPI | 同じ型へのAddScriptは既存wrapperを返し、Script数は2のまま |
| Domain Reload | 両instance IDが更新、公開Speed/Liftと第二Scriptの無効状態を維持 |
| V2 Update(1) | X=9、Y=4。新コード適用と無効Scriptの非実行 |
| 不在候補のReload後 | 現行コードを維持し、次のUpdateでX=15 |
| Stop/再Start | ScriptSystemを解除し、再起動可能 |

## 4. Editorからの手動確認

上の自動テストが通った後、検証用Managed/Hybrid Projectを現在のEngine/API/Bootstrapで通常Buildし、Editorを再起動する。既存の古いGameEditor.exeをそのまま起動しても追加変更の確認にはならない。Projectの`Config/BuildTargets.json`でMonoを選んでいることも確認する。

異なるScriptType IDを持つ二つのScriptBehaviourを用意し、それぞれExpose付きfloat Propertyを定義する。同じEntityへSourceから順に追加する。

1. 最初のScriptが消えず、二つとも表示されることを確認する。
2. 共通Property画面で別々の公開値とEnabledを編集する。
3. 一方だけを解除し、他方の公開値が維持されることを確認する。
4. Undo/Redo、Level保存、Editor再起動/Level読込を確認する。
5. 各Script行から正しいソースを開けることを確認する。
6. Playで同じEntityを操作すること、無効なScriptだけが更新されないことを確認する。
7. 関数本体の変更後にReload C#し、公開値と有効状態の保持を確認する。
8. Stop/Playを繰り返し、Consoleに旧token・cleanup・Domain解放の失敗がないことを確認する。

Native側は検証用Native/HybridテンプレートのNativeMoverをEntityへ追加し、enabled/speedの編集、解除Undo、保存/読込、Play時のX移動を確認する。CrowdSettingsは一つの設定Entityに配置する。任意のC++ファイルを置いただけでは自動登録されない。詳細は[Native Entity Component](NativeEntityComponentsREADME.md)を参照。

同じ型の複数instance配置、任意Native state移行、Input等のBackendはこのテストの期待項目に含めない。

## エラー時に共有するもの

`$testOutput`のパス、止まった段階、build.logの最初のerrorと直前後、run.logの末尾を共有する。Monoの場合はMono Rootと最終exit codeも必要。失敗後に次の段階を進めたり、無関係なSource/Git変更を戻したりしない。

参照：[複数Script実装](MultipleScriptsREADME.md)、[Native Component実装](NativeEntityComponentsREADME.md)、[共通Component API](GameComponentApiREADME.md)。
