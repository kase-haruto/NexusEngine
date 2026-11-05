param([string]$OutputRoot = (Join-Path $PSScriptRoot '../../Generated/Phase1Validation/Reflection'))
$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$generator = Join-Path $engineRoot 'Tools/Reflection/Generate-Reflection.ps1'
$gameRoot = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) '日本語 Game'
$gameSources = Join-Path $gameRoot 'Game'
[IO.Directory]::CreateDirectory($gameSources) | Out-Null
$legacy = Join-Path $engineRoot 'Runtime/Reflection/Generated/GeneratedReflection.h'
$legacyHash = (Get-FileHash -LiteralPath $legacy).Hash
& $generator -ProjectRoot $engineRoot -Module Engine
$engineHeader = Join-Path $engineRoot 'Runtime/Reflection/EngineGenerated/GeneratedReflection.h'
if([IO.File]::ReadAllText($engineHeader) -match '#include "(Game|Samples|Editor)/') { throw 'Engine reflection depends on an upper layer.' }
$fixture = @'
#include "Foundation/Reflection/ReflectionMarkers.h"
namespace IndependentGame { struct Speed { float value = 1; }; }
NEXUS_REFLECT_COMPONENT(IndependentGame::Speed, "Independent.Speed", "component.speed", value)
'@
$header = Join-Path $gameSources 'Speed.h'
[IO.File]::WriteAllText($header, $fixture, [Text.UTF8Encoding]::new($false))
$generated = Join-Path $gameRoot 'Intermediate/Reflection'
& $generator -ProjectRoot $engineRoot -GameRoot $gameRoot -Module Game -OutputDirectory $generated
$generatedHeader = Join-Path $generated 'GeneratedGameReflection.h'
if([IO.File]::ReadAllText($generatedHeader) -notmatch 'IndependentGame::Speed') { throw 'External game type was not generated.' }
$before = (Get-Item -LiteralPath $generatedHeader).LastWriteTimeUtc
& $generator -ProjectRoot $engineRoot -GameRoot $gameRoot -Module Game -OutputDirectory $generated
if((Get-Item -LiteralPath $generatedHeader).LastWriteTimeUtc -ne $before) { throw 'Unchanged reflection was rewritten.' }
# Engineの型IDをGameが再定義したとき、別集合でも生成を拒否する。
[IO.File]::WriteAllText($header, $fixture.Replace('Independent.Speed', 'Camera'), [Text.UTF8Encoding]::new($false))
$rejected = $false
try { & $generator -ProjectRoot $engineRoot -GameRoot $gameRoot -Module Game -OutputDirectory $generated }
catch {
    if($_.Exception.Message -notmatch 'Duplicate reflected type') { throw }
    $rejected = $true
}
if(-not $rejected) { throw 'Engine/Game duplicate ID was accepted.' }
if((Get-FileHash -LiteralPath $legacy).Hash -ne $legacyHash) { throw 'Legacy generated file was modified.' }
[IO.File]::WriteAllText($header, $fixture, [Text.UTF8Encoding]::new($false))
Write-Output 'PASS: separate Engine/Game reflection, external Unicode root, stable outputs, duplicate IDs and legacy preservation.'
