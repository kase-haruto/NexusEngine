$ErrorActionPreference='Stop'
. "$PSScriptRoot/HybridGeneration.Tests.ps1"
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId CrowdSimulation -Name NamespacedActor -Namespace 'Gameplay.Actors' -RelativeDirectory 'Gameplay/Actors'
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId CrowdSimulation -Name ActorClone -RelativeDirectory 'Gameplay/Actors' -CloneFrom 'Gameplay/Actors/NamespacedActor.cpp' -CloneClass NamespacedActor
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId CrowdSimulation -Name Helper -Kind Class -Namespace 'Gameplay.Utility' -RelativeDirectory Utility
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId CrowdSimulation -Name HeaderOnly -Kind Header -RelativeDirectory Utility
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId CrowdSimulation -Name Functions -Kind Source -Namespace 'Gameplay.Utility' -RelativeDirectory Utility
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId Managed -Name NestedScript -Namespace 'Gameplay.Scripts' -RelativeDirectory Gameplay
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId Managed -Name ScriptClone -RelativeDirectory Gameplay -CloneFrom 'Gameplay/NestedScript.cs' -CloneClass NestedScript
& $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId Managed -Name PlainData -Kind Class -Namespace 'Gameplay.Data' -RelativeDirectory Gameplay
$nativeRoot=Join-Path $game 'Source/Native'
Require ([IO.File]::ReadAllText((Join-Path $nativeRoot 'Gameplay/Actors/NamespacedActor.h')).Contains('namespace Gameplay::Actors')) 'Native namespace was not generated.'
Require ([IO.File]::ReadAllText((Join-Path $nativeRoot 'Gameplay/Actors/ActorClone.cpp')).Contains('ActorClone::OnUpdate')) 'Native clone body was not renamed.'
Require ([IO.File]::ReadAllText((Join-Path $game 'Source/Managed/Gameplay/ScriptClone.cs')).Contains('Managed.behaviours.ScriptClone')) 'Managed clone retained a conflicting ID.'
$rejected=$false
try { & $sourceTool -ProjectFile $file -EngineManifest $manifest -ModuleId Managed -Name Escape -RelativeDirectory '../Outside' } catch { $rejected=$true }
Require $rejected 'Source generator allowed a Module escape.'
$headers=[IO.File]::ReadAllText((Join-Path $nativeRoot 'NexusGeneratedBehaviours.h'))
Require ($headers.Contains('Gameplay/Actors/NamespacedActor.h') -and $headers.Contains('Gameplay/Actors/ActorClone.cpp')) 'Native source registration did not use nested paths.'
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $workspace 'Generated/ContentBrowserPhase2/source-fixture.txt'),$game,$utf8)
Write-Output ('PASS: Namespace, nested source paths, Class/Header/Source creation, Native/C# cloning and escape rejection. Artifacts: '+$game)
