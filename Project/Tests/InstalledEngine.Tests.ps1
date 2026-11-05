param(
    [Parameter(Mandatory=$true)][string]$EngineManifest,
    [string]$OtherEngineManifest,
    [string]$DevelopmentManifest,
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tool = Join-Path $workspace "Generated/Modules/x64/$Configuration/NexusProjectTool/NexusProjectTool.exe"
$root = Join-Path $workspace ('Generated/InstalledValidation/Tests/' + [guid]::NewGuid().ToString())
[IO.Directory]::CreateDirectory($root) | Out-Null
$sourceFile = [IO.Path]::GetFullPath($EngineManifest)
$sourceRoot = [IO.Path]::GetDirectoryName($sourceFile)
$clone = Join-Path $root 'Installed Clone'
# 破損検証は専用コピーだけで行い、登録済みの安定版は一切変更しない。
foreach($entry in Get-ChildItem -LiteralPath $sourceRoot -Recurse -Force) {
    if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Installed test source contains a link.' }
}
Copy-Item -LiteralPath $sourceRoot -Destination $clone -Recurse
$file = Join-Path $clone ([IO.Path]::GetFileName($sourceFile))
$registry = Join-Path $root 'Engines.json'
$utf8 = [Text.UTF8Encoding]::new($false)
function Invoke-Expected([string[]]$Arguments,[int]$Expected = 0) {
    $output = & $tool @Arguments 2>&1 | Out-String
    if($LASTEXITCODE -ne $Expected) { throw "Exit $LASTEXITCODE (expected $Expected): $Arguments`n$output" }
    return $output
}
$null = Invoke-Expected @('engine-register',$file,$registry)
$null = Invoke-Expected @('engine-register',$file,$registry)
if(@(Get-ChildItem -LiteralPath (Join-Path $clone 'SDK') -Recurse -Filter '*.cpp').Count) { throw 'Installed SDK contains Engine implementation cpp files.' }
if(Test-Path -LiteralPath (Join-Path $clone 'SDK/Runtime/Reflection/Generated/GeneratedReflection.h')) { throw 'Installed SDK exported the legacy Game-dependent reflection header.' }
$gameIncludes = @(Get-ChildItem -LiteralPath (Join-Path $clone 'SDK') -Recurse -File | Where-Object { $_.Extension -in @('.h','.hpp','.inl') } | Select-String -Pattern '#\s*include\s*["<](Game|Samples)[/\\]')
if($gameIncludes.Count) { throw 'Installed SDK public headers depend on a specific Game/Sample.' }
if($OtherEngineManifest) {
    $null = Invoke-Expected @('engine-register',[IO.Path]::GetFullPath($OtherEngineManifest),$registry)
    $records = Get-Content -LiteralPath $registry -Raw -Encoding UTF8 | ConvertFrom-Json
    if($records.engines.Count -ne 2) { throw 'Installed versions did not coexist.' }
}
$header = Join-Path $clone 'SDK/Foundation/Error/Error.h'
$original = [IO.File]::ReadAllBytes($header)
try {
    [IO.File]::AppendAllText($header,"`n// Intentional corruption in the owned test clone.`n",$utf8)
    $null = Invoke-Expected @('engine-register',$file,$registry) 1
} finally { [IO.File]::WriteAllBytes($header,$original) }
$extra = Join-Path $clone 'SDK/Foundation/Injected.h'
try {
    [IO.File]::WriteAllText($extra,'#pragma once',$utf8)
    $null = Invoke-Expected @('engine-register',$file,$registry) 1
} finally {
    if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($extra)) -ne (Join-Path $clone 'SDK/Foundation')) { throw 'Unsafe test cleanup path.' }
    if(Test-Path -LiteralPath $extra) { Remove-Item -LiteralPath $extra }
}
$manifestBytes = [IO.File]::ReadAllText($file)
try {
    $manifest = $manifestBytes | ConvertFrom-Json
    $manifest.inventory.PSObject.Properties.Remove('SDK/Lib/x64/' + $Configuration + '/NexusRuntime.lib')
    [IO.File]::WriteAllText($file,($manifest | ConvertTo-Json -Depth 10),$utf8)
    $null = Invoke-Expected @('engine-register',$file,$registry) 1
} finally { [IO.File]::WriteAllText($file,$manifestBytes,$utf8) }
$null = Invoke-Expected @('engine-register',$file,$registry)
if(Test-Path -LiteralPath (Join-Path $clone 'SDK/Build/Nexus.Compatibility.json')) {
    $compatibilityFile = Join-Path $clone 'SDK/Build/Nexus.Compatibility.json'
    $compatibilityBytes = [IO.File]::ReadAllText($compatibilityFile)
    $projectRoot = Join-Path $root 'Compatibility Game'
    $null = Invoke-Expected @('create',$projectRoot,'Compatibility Game')
    $projectFile = Join-Path $projectRoot 'Compatibility Game.nxproject'
    $projectBytes = [IO.File]::ReadAllText($projectFile)
    try {
        foreach($pair in @(@('projectSchema',2),@('assetStorage','cooked-v2'),@('maximumLevelFormat',1))) {
            $compatibility = $compatibilityBytes | ConvertFrom-Json
            $compatibility.PSObject.Properties[$pair[0]].Value = $pair[1]
            [IO.File]::WriteAllText($compatibilityFile,($compatibility | ConvertTo-Json),$utf8)
            $manifest = $manifestBytes | ConvertFrom-Json
            $manifest.inventory.PSObject.Properties['SDK/Build/Nexus.Compatibility.json'].Value = (Get-FileHash -LiteralPath $compatibilityFile -Algorithm SHA256).Hash.ToLowerInvariant()
            [IO.File]::WriteAllText($file,($manifest | ConvertTo-Json -Depth 10),$utf8)
            # 正しいinventoryを持つ配置でも、宣言された保存形式が違えば移行を拒否する。
            $null = Invoke-Expected @('engine-register',$file,$registry)
            $failure = Invoke-Expected @('migration-plan',$projectFile,$file,(Join-Path $root 'UnsupportedPlan.json')) 1
            if($failure -match 'inventory mismatch') { throw 'Compatibility test was rejected by inventory instead of format validation.' }
            if([IO.File]::ReadAllText($projectFile) -cne $projectBytes) { throw 'Incompatible Engine review changed the Project.' }
        }
    } finally {
        [IO.File]::WriteAllText($compatibilityFile,$compatibilityBytes,$utf8)
        [IO.File]::WriteAllText($file,$manifestBytes,$utf8)
    }
    $null = Invoke-Expected @('engine-register',$file,$registry)
    $null = Invoke-Expected @('migration-plan',$projectFile,$file,(Join-Path $root 'SupportedPlan.json'))
}
if($DevelopmentManifest) {
    $null = Invoke-Expected @('engine-publish',[IO.Path]::GetFullPath($DevelopmentManifest),$clone,'must-not-replace','9.9.9') 1
    if([IO.File]::ReadAllText($file) -cne $manifestBytes) { throw 'Publishing changed an existing installation.' }
}
Write-Output "PASS: Installed versions coexist; copied SDK has no Engine cpp; changed/missing-inventory/untracked files and overwrite are rejected. Artifacts: $root"
