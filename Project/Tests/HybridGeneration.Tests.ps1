# ユーザー実行用の生成回帰検証。Build / dotnet / Game / Runtimeは呼び出さない。
$ErrorActionPreference='Stop'
$engineSource=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workspace=[IO.Path]::GetDirectoryName($engineSource)
$artifacts=Join-Path $workspace ('Generated/Phase8Validation/Generation/'+[guid]::NewGuid().ToString('N'))
$game=Join-Path $artifacts 'Game 日本語'
[IO.Directory]::CreateDirectory($game) | Out-Null
function Put([string]$Path,[string]$Text) {
 [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
 [IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))
}
function Require([bool]$Condition,[string]$Message) { if(-not $Condition) { throw $Message } }
function Template([string]$Name) { return [IO.File]::ReadAllText((Join-Path $engineSource ('Templates/'+$Name))) }
. (Join-Path $engineSource 'Tools/Project/Project-ModuleOrder.ps1')

# punctuation・数字始まり・underscore始まりでも、保存IDを変更せず一意な識別子を作る。
$identifiers=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach($id in @('Native-a','Native_a','0Native','_Native','Native.a','Native')) {
 $identifier=Get-NexusModuleCodeIdentifier $id
 Require ($identifier -cmatch '\A[A-Za-z_][A-Za-z0-9_]*\z' -and $identifiers.Add($identifier)) 'Generated C# Module identifiers collided.'
}
$projectId=[guid]::NewGuid().ToString()
$project=[pscustomobject]@{schemaVersion=1;projectId=$projectId;name='Generation';projectVersion='0.1.0';engine=@{association='development';id='fixture';version='0.1.0'};paths=@{source='Source';assets='Assets';levels='Levels';config='Config'};startupLevel='Levels/Main.level';modules=@(
 @{id='CrowdSimulation';name='Native';language='Cpp';type='Game';path='Source/Native';dependencies=@()},
 @{id='Managed';name='Managed';language='CSharp';type='Game';path='Source/Managed';dependencies=@('CrowdSimulation')}
)}
$file=Join-Path $game 'Game.nxproject'; Put $file ($project | ConvertTo-Json -Depth 20)
$baseUri=[Uri]($artifacts.TrimEnd('\')+'\')
$templatesUri=[Uri](Join-Path $engineSource 'Templates')
$templatePath=[Uri]::UnescapeDataString($baseUri.MakeRelativeUri($templatesUri).ToString())
$manifest=Join-Path $artifacts 'Engine.nxengine'
Put $manifest (@{schemaVersion=1;id='fixture';association='development';version='0.1.0';configuration='Debug';paths=@{templates=$templatePath;sdk='.'};runtimeFiles=@()} | ConvertTo-Json -Depth 10)
$targets=@{schemaVersion=1;runtimeProject='GameRuntime.vcxproj';editorProject='GameEditor.vcxproj';runtimeExecutable='Build/Bin/x64/{configuration}/GameRuntime.exe';editorExecutable='Build/Bin/x64/{configuration}/GameEditor.exe';nativeModule='Build/Bin/x64/{configuration}/Game.dll';managedBootstrap='Build/Bin/x64/{configuration}/Managed/Nexus.Managed.Bootstrap.dll';gameScripts='Build/Bin/x64/{configuration}/Managed/GameScripts.dll'}
Put (Join-Path $game 'Config/BuildTargets.json') ($targets | ConvertTo-Json -Depth 10)
Put (Join-Path $game 'Levels/Main.level') 'unchanged Level bytes'
Put (Join-Path $game 'Assets/Keep.asset') 'unchanged Asset bytes'
foreach($name in @('Main.cpp','CrowdSimulationSystem.h','CrowdSimulationSystem.cpp','CrowdBinding.h','NativeModuleState.h','NativePatchHooks.cpp','NativeMover.h','NativeMover.cpp','GameComponents.h')) {
 Put (Join-Path $game ('Source/Native/'+$name)) ((Template ('NativeModule/'+$name+'.in')).Replace('@PROJECT_GUID@',$projectId).Replace('{"Game", 4}','{"CrowdSimulation", 15}'))
}
Put (Join-Path $game 'Source/Managed/Keep.cs') '// Existing source must remain unchanged.'
Put (Join-Path $game 'GameModule.vcxproj') ((Template 'NativeModule/GameModule.vcxproj.in').Replace('@MODULE_GUID@',[guid]::NewGuid().ToString()).Replace('@SOURCE@\Game','Source\Native'))
Put (Join-Path $game 'GameScripts.csproj') '<Project Sdk="Microsoft.NET.Sdk" />'
Put (Join-Path $game 'ManagedBootstrap.vcxproj') ((Template 'Hybrid/ManagedBootstrap.vcxproj.in').Replace('@MODULE_GUID@',[guid]::NewGuid().ToString()).Replace('</Project>','<!-- preserved utility customization --></Project>'))
foreach($hostName in @('GameRuntime','GameEditor')) {
 # 部分一致する別Project名では、本来のGameModule参照を省略してはならない。
 Put (Join-Path $game ($hostName+'.vcxproj')) '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemGroup><ProjectReference Include="CustomGameModule.vcxproj" /></ItemGroup></Project>'
}
Put (Join-Path $game 'Source/Host/Main.cpp') ('// NEXUS_PROJECT_MODULE_MANIFEST is mentioned only in a comment.'+"`r`n"+'#include "Core/Hosting/ProjectHost.inl"')
Put (Join-Path $game 'Game.slnx') '<Solution />'

$sourceTool=Join-Path $engineSource 'Tools/Project/Add-GameSource.ps1'
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId CrowdSimulation -Name CrowdAgent -Kind System
$native=[IO.File]::ReadAllText((Join-Path $game 'Source/Native/CrowdAgent.h'))
Require ($native.Contains('class CrowdAgent final') -and -not $native.Contains('CrowdAgentAgent')) 'Native template replacement processed the new name twice.'
$binding=[IO.File]::ReadAllText((Join-Path $game 'Source/Native/CrowdAgentBinding.h'))
Require ($binding.Contains('{"CrowdAgent", 10}')) 'Native System ID length does not match its generated name.'
$facade=[IO.File]::ReadAllText((Join-Path $game 'Source/Managed/CrowdAgent.cs'))
Require ($facade.Contains('BindNativeFunction("CrowdSimulation", "CrowdAgent"')) 'Module ID was replaced by the System name.'
Require ($facade.Contains('global::Nexus.Managed.Entity')) 'Facade can shadow its SDK parameter types.'
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId Managed -Name ScriptBehaviour
$behaviour=[IO.File]::ReadAllText((Join-Path $game 'Source/Managed/ScriptBehaviour.cs'))
Require ($behaviour.Contains('class ScriptBehaviour : global::Nexus.Managed.ScriptBehaviour')) 'Generated Behaviour inherited from itself.'
foreach($callback in @('OnCreate','OnEnable','OnUpdate','OnFixedUpdate','OnDisable','OnDestroy')) {
 Require ($behaviour.Contains('protected override void '+$callback+'(')) ('Missing C# lifecycle stub: '+$callback)
}
Require (-not $behaviour.Contains('transform.Translation.X +=')) 'A new Behaviour still moves an Entity automatically.'
Require ($behaviour.Contains('deltaTime') -and $behaviour.Contains('秒') -and $behaviour.Contains('ここに')) 'C# guidance comments are missing.'
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId Managed -Name MoveSpeed

# Entity単位のC++ Behaviourは、全Lifecycleと明示登録をまとめて生成する。
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId CrowdSimulation -Name PlayerController
$controller=Join-Path $game 'Source/Native/PlayerController.cpp'
$controllerText=[IO.File]::ReadAllText($controller)
foreach($callback in @('OnCreate','OnEnable','OnUpdate','OnFixedUpdate','OnDisable','OnDestroy')) {
 Require ($controllerText.Contains('PlayerController::'+$callback+'(')) ('Missing C++ lifecycle stub: '+$callback)
}
Require (-not $controllerText.Contains('inline ') -and $controllerText.Contains('ここに')) 'C++ guidance or definition style is incorrect.'
Put $controller ($controllerText+"`n// User gameplay code must survive adding another Behaviour.`n")
$edited=[IO.File]::ReadAllText($controller)
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId CrowdSimulation -Name EnemyController
Require ([IO.File]::ReadAllText($controller) -ceq $edited) 'Adding a Behaviour overwrote user gameplay code.'
$registryHeader=Join-Path $game 'Source/Native/NexusGeneratedBehaviours.h'
$header=[IO.File]::ReadAllText($registryHeader)
$literal=[regex]::Match($header,'(?m)^#define NEXUS_GENERATED_BEHAVIOUR_METADATA (".*")\r?$').Groups[1].Value
$json=ConvertFrom-Json $literal
$metadata=ConvertFrom-Json ('['+$json.Substring(1)+']')
Require ($metadata.Count -eq 2 -and $metadata[0].id -ceq 'CrowdSimulation.behaviours.PlayerController') 'Native Behaviour type IDs were not retained.'
Require ($metadata[0].sourceFiles.Count -eq 2 -and $metadata[0].sourceFiles[0] -ceq 'PlayerController.h' -and $metadata[0].sourceFiles[1] -ceq 'PlayerController.cpp') 'Native source declarations do not identify the editable files.'
$main=[IO.File]::ReadAllText((Join-Path $game 'Source/Native/Main.cpp'))
Require ([regex]::Matches($main,'RegisterGeneratedBehaviours\(state.systems\)').Count -eq 1 -and $main.Contains('ConnectGeneratedBehaviours(systems, *context)')) 'Native lifecycle registration was missing or duplicated.'
$rejected=$false
try { & $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId CrowdSimulation -Name PlayerController } catch { $rejected=$true }
Require ($rejected -and [IO.File]::ReadAllText($controller) -ceq $edited) 'Duplicate generation overwrote an existing Behaviour.'
Put $registryHeader ($header+"`n// Customized registration must not be overwritten.`n")
$rejected=$false
try { & $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId CrowdSimulation -Name ThirdController } catch { $rejected=$true }
Require ($rejected -and -not (Test-Path -LiteralPath (Join-Path $game 'Source/Native/ThirdController.cpp'))) 'Customized registration caused partial generation.'
Put $registryHeader $header
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId CrowdSimulation -Name PostBehaviourSystem -Kind System
Require ([IO.File]::ReadAllText((Join-Path $game 'Source/Native/Main.cpp')).Contains('RegisterGeneratedBehaviours(state.systems)')) 'Adding a batch System removed Behaviour registration.'
& (Join-Path $engineSource 'Tools/Project/Add-GameModule.ps1') -ProjectFile $file -EngineManifest $manifest -ModuleId Effects -Language CSharp
$hostSource=[IO.File]::ReadAllText((Join-Path $game 'Source/Host/Main.cpp'))
Require ([regex]::IsMatch($hostSource,'(?m)^#define NEXUS_PROJECT_MODULE_MANIFEST 1')) 'A comment was mistaken for a Host definition.'
foreach($hostName in @('GameRuntime','GameEditor')) {
 [xml]$xml=[IO.File]::ReadAllText((Join-Path $game ($hostName+'.vcxproj')))
 Require ($null -ne $xml.SelectSingleNode('//*[local-name()="ProjectReference"][@Include="$(ProjectDir)GameModule.vcxproj"]')) 'A different Project name suppressed the required Module reference.'
}
Require ([IO.File]::ReadAllText((Join-Path $game 'ManagedBootstrap.vcxproj')).Contains('preserved utility customization')) 'Existing utility customization was lost.'
$after=Get-Content -Raw -Encoding UTF8 -LiteralPath $file | ConvertFrom-Json
Require ($after.projectId -ceq $projectId -and $after.modules.Count -eq 3) 'Module addition replaced Project identity or declarations.'
Require ([IO.File]::ReadAllText((Join-Path $game 'Levels/Main.level')) -ceq 'unchanged Level bytes') 'Module addition changed Level data.'
Require ([IO.File]::ReadAllText((Join-Path $game 'Assets/Keep.asset')) -ceq 'unchanged Asset bytes') 'Module addition changed Asset data.'
Write-Output ('PASS: source generation, identifier isolation and shared Host integration. Artifacts: '+$artifacts)
