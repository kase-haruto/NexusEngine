param(
    [string]$EngineManifest,
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(-not $EngineManifest) { $EngineManifest = Join-Path $workspace "NexusEngine.$Configuration.nxengine" }
$EngineManifest = [IO.Path]::GetFullPath($EngineManifest)
$tool = Join-Path $workspace "Generated/Modules/x64/$Configuration/NexusProjectTool/NexusProjectTool.exe"
$root = Join-Path $workspace ('Generated/GitCloneValidation/' + [guid]::NewGuid().ToString())
[IO.Directory]::CreateDirectory($root) | Out-Null
$hooks = Join-Path $root 'NoHooks'; [IO.Directory]::CreateDirectory($hooks) | Out-Null
$registry = Join-Path $root 'Engines.json'
function Invoke-Expected([string[]]$Arguments) {
    $output = & $tool @Arguments 2>&1 | Out-String
    if($LASTEXITCODE -ne 0) { throw "Project command failed: $Arguments`n$output" }; return $output
}
function Invoke-Git([string]$Directory,[string[]]$Arguments) {
    $output = & git -c core.autocrlf=false -c "core.hooksPath=$hooks" -C $Directory @Arguments 2>&1 | Out-String
    if($LASTEXITCODE -ne 0) { throw "Fixture Git failed: $Arguments`n$output" }; return $output.Trim()
}
$null = Invoke-Expected @('engine-register',$EngineManifest,$registry)
foreach($kind in @('Data Game','Cpp Game')) {
    $original = Join-Path $root $kind
    if($kind -eq 'Data Game') { $null = Invoke-Expected @('create',$original,'Data Game') }
    else { $null = Invoke-Expected @('create-template',$original,'Cpp Game',$EngineManifest,'blank') }
    $file = Join-Path $original ($kind + '.nxproject')
    $null = Invoke-Expected @('check',$file,$registry,$Configuration)
    $null = Invoke-Git $original @('init','--quiet')
    $null = Invoke-Git $original @('add','--','.')
    $tracked = Invoke-Git $original @('ls-files')
    if($tracked -notmatch 'Assets/\.gitkeep') { throw 'Empty Asset directory is not tracked.' }
    if($tracked -match '(?m)^(Build/|Intermediate/|Cache/|Nexus\.Engine\.local\.props)') { throw 'Clone would contain build/cache/machine-local files.' }
    $tree = Invoke-Git $original @('write-tree')
    if($tree -notmatch '^[a-f0-9]{40,64}$') { throw 'Fixture Git tree was not created.' }
    # commitやユーザーidentity設定を作らず、Gitが記録したtreeだけを新Rootへ展開する。
    $archive = Join-Path $root ($kind + '.zip')
    $null = Invoke-Git $original @('archive','--format=zip',('--output=' + $archive),$tree)
    $clone = Join-Path $root ('Clone 日本語 ' + $kind)
    Expand-Archive -LiteralPath $archive -DestinationPath $clone
    $cloneFile = Join-Path $clone ($kind + '.nxproject')
    foreach($directory in @('Assets','Levels','Source','Config')) { if(-not (Test-Path -LiteralPath (Join-Path $clone $directory) -PathType Container)) { throw "Git clone lost required Directory: $directory" } }
    if(Test-Path -LiteralPath (Join-Path $clone 'Build')) { throw 'Clone inherited build output.' }
    if(Test-Path -LiteralPath (Join-Path $clone 'Nexus.Engine.local.props')) { throw 'Clone inherited local Engine path.' }
    $null = Invoke-Expected @('inspect',$cloneFile)
    $null = Invoke-Expected @('check',$cloneFile,$registry,$Configuration)
    $before = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
    $after = Get-Content -LiteralPath $cloneFile -Raw | ConvertFrom-Json
    if($before.projectId -cne $after.projectId -or $before.engine.id -cne $after.engine.id -or $before.engine.version -cne $after.engine.version) { throw 'Git round-trip changed shared project identity/Engine metadata.' }
}
Write-Output "PASS: actual Git trees preserve empty project directories; archives exclude build/cache/local props; cloned Data/Cpp Games open and run after local Engine rebinding. Artifacts: $root"
