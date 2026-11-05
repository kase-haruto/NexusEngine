param(
    [Parameter(Mandatory=$true)][string]$EngineManifest,
    [string]$OtherManifest,
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tool = Join-Path $workspace "Generated/Modules/x64/$Configuration/NexusProjectTool/NexusProjectTool.exe"
$root = Join-Path $workspace ('Generated/RelocationValidation/' + [guid]::NewGuid().ToString())
[IO.Directory]::CreateDirectory($root) | Out-Null
$originalRoot = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($EngineManifest))
foreach($item in Get-ChildItem -LiteralPath $originalRoot -Recurse -Force) { if($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'SDK relocation fixture must not contain links.' } }
$sdk = Join-Path $root 'Original Engine'; $movedSdk = Join-Path $root '移動した Engine'
Copy-Item -LiteralPath $originalRoot -Destination $sdk -Recurse
$manifest = Join-Path $sdk ([IO.Path]::GetFileName($EngineManifest))
$registry = Join-Path $root 'Engines.json'; $recent = Join-Path $root 'Recent.json'
function Invoke-Expected([string[]]$Arguments,[int]$Expected = 0) {
    $output = & $tool @Arguments 2>&1 | Out-String
    if($LASTEXITCODE -ne $Expected) { throw "Exit $LASTEXITCODE (expected $Expected): $Arguments`n$output" }
    return $output
}
function Move-Owned([string]$From,[string]$To) {
    foreach($path in @($From,$To)) {
        if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($path)) -cne $root) { throw 'Unsafe relocation-test move target.' }
    }
    Move-Item -LiteralPath $From -Destination $To
}
$null = Invoke-Expected @('engine-register',$manifest,$registry)
if($OtherManifest) { $null = Invoke-Expected @('engine-register',[IO.Path]::GetFullPath($OtherManifest),$registry) }
$null = Invoke-Expected @('engine-default',$manifest,$registry)
$game = Join-Path $root 'Original Game'
$null = Invoke-Expected @('create-template',$game,'Portable Game',$manifest,'basic-3d')
$file = Join-Path $game 'Original Game.nxproject'; $descriptor = [IO.File]::ReadAllText($file)
$null = Invoke-Expected @('check',$file,$registry,$Configuration)
$null = Invoke-Expected @('register',$file,$recent)
Move-Owned $sdk $movedSdk
$movedManifest = Join-Path $movedSdk ([IO.Path]::GetFileName($manifest))
$failure = Invoke-Expected @('verify-runtime',$file,$registry,$Configuration) 1
if($failure -notmatch 'Original Engine') { throw 'Missing selected Engine was not reported.' }
$null = Invoke-Expected @('engine-register',$movedManifest,$registry) 1
$null = Invoke-Expected @('engine-default',$manifest,$registry) 1
if([IO.File]::ReadAllText($file) -cne $descriptor) { throw 'Missing Engine resolution rewrote the Project.' }
$null = Invoke-Expected @('engine-remove',$manifest,$registry)
if(-not (Test-Path -LiteralPath $movedManifest)) { throw 'Removing Engine registration deleted its files.' }
$null = Invoke-Expected @('engine-register',$movedManifest,$registry)
$null = Invoke-Expected @('engine-default',$movedManifest,$registry)
$null = Invoke-Expected @('check',$file,$registry,$Configuration)
$movedGame = Join-Path $root '移動した Game'; Move-Owned $game $movedGame
$movedFile = Join-Path $movedGame 'Original Game.nxproject'
$null = Invoke-Expected @('check',$movedFile,$registry,$Configuration)
$null = Invoke-Expected @('register',$movedFile,$recent)
$records = Get-Content -LiteralPath $recent -Raw | ConvertFrom-Json
if($records.projects.Count -ne 1) { throw 'Moving the Game duplicated its missing recent entry.' }
# Git clone相当として共有データだけをコピーし、古いlocal propsや成果物は引き継がない。
$clone = Join-Path $root 'Clone Game'; [IO.Directory]::CreateDirectory($clone) | Out-Null
foreach($directory in @('Assets','Levels','Source','Config')) { Copy-Item -LiteralPath (Join-Path $movedGame $directory) -Destination (Join-Path $clone $directory) -Recurse }
foreach($name in @('Original Game.nxproject','GameRuntime.vcxproj','GameEditor.vcxproj','Game.slnx','.gitignore')) { Copy-Item -LiteralPath (Join-Path $movedGame $name) -Destination (Join-Path $clone $name) }
$cloneFile = Join-Path $clone 'Original Game.nxproject'
if(Test-Path -LiteralPath (Join-Path $clone 'Nexus.Engine.local.props')) { throw 'Clone inherited machine-local binding.' }
$null = Invoke-Expected @('check',$cloneFile,$registry,$Configuration)
$null = Invoke-Expected @('register',$cloneFile,$recent)
$records = Get-Content -LiteralPath $recent -Raw | ConvertFrom-Json
if($records.projects.Count -ne 2) { throw 'Registering a live clone removed the other live working copy.' }
if([IO.File]::ReadAllText($cloneFile) -cne $descriptor) { throw 'Clone/build changed shared Project metadata.' }
# 移動先Shader Rootは日本語を含む。実際のGPU処理でsource側への暗黙fallbackがないことを確認する。
$foreign = Join-Path $root 'Foreign cwd'; [IO.Directory]::CreateDirectory($foreign) | Out-Null
$start = [Diagnostics.ProcessStartInfo]::new((Join-Path $clone "Build/Bin/x64/$Configuration/GameRuntime.exe"))
$start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WorkingDirectory = $foreign
$start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true; $start.Environment['LOCALAPPDATA'] = Join-Path $root 'User'
foreach($argument in @('--project',$cloneFile,'--engine',(Join-Path $movedSdk 'Data'),'--hidden','--frames','3')) { $start.ArgumentList.Add($argument) }
$child = [Diagnostics.Process]::Start($start)
try {
    $outputTask = $child.StandardOutput.ReadToEndAsync(); $errorTask = $child.StandardError.ReadToEndAsync()
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    while(-not $child.WaitForExit(1000)) { if([DateTime]::UtcNow -gt $deadline) { $child.Kill($true); throw 'Relocated GPU Runtime timed out.' } }
    $output = $outputTask.GetAwaiter().GetResult(); $errorText = $errorTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $root 'RelocatedGpu.log'),$output + $errorText,[Text.UTF8Encoding]::new($false))
    if($child.ExitCode -ne 0 -or $output -notmatch 'PASS: completed 3 GPU frames') { throw "Relocated SDK GPU run failed: $errorText" }
} finally { $child.Dispose() }
Write-Output "PASS: SDK move requires explicit re-registration, no version fallback, Project move/clone/local rebind and GPU startup with Unicode Shader Root. Artifacts: $root"
