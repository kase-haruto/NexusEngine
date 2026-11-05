param([string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$engineRoot = Join-Path $workspace 'Project'
$binaries = Join-Path $workspace "Generated/Modules/x64/$Configuration"
$tool = Join-Path $binaries 'NexusProjectTool/NexusProjectTool.exe'
$runtime = Join-Path $binaries 'SampleGameRuntime/SampleGameRuntime.exe'
$editor = Join-Path $binaries 'SampleGameEditor/SampleGameEditor.exe'
$validationRoot = Join-Path $workspace ('Generated/Phase2Validation/CLI/' + [guid]::NewGuid().ToString())
[IO.Directory]::CreateDirectory($validationRoot) | Out-Null
function Invoke-Expected([string]$Executable, [string[]]$CommandArguments, [int]$Expected = 0) {
    $output = & $Executable @CommandArguments 2>&1 | Out-String
    if($LASTEXITCODE -ne $Expected) { throw "Unexpected exit code ($LASTEXITCODE): $Executable $CommandArguments`n$output" }
    return $output
}
$projectRoot = Join-Path $validationRoot '日本語 Game A'
$null = Invoke-Expected $tool @('create', $projectRoot, '日本語 Project')
$file = Join-Path $projectRoot '日本語 Game A.nxproject'
$null = Invoke-Expected $tool @('inspect', $file)
if(Test-Path -LiteralPath (Join-Path $projectRoot 'Config/Project.settings')) { throw 'Legacy shared settings were unexpectedly generated.' }
$registry = Join-Path $validationRoot 'User/Recent.json'
$null = Invoke-Expected $tool @('register', $file, $registry)
$null = Invoke-Expected $tool @('register', $file, $registry)
$records = Get-Content -LiteralPath $registry -Raw -Encoding UTF8 | ConvertFrom-Json
if($records.projects.Count -ne 1) { throw 'Registration duplicated a Project.' }
$null = Invoke-Expected $runtime @('--project', $file, '--engine', $engineRoot, '--check-game')
$null = Invoke-Expected $editor @('--game', '--project', $file, '--engine', $engineRoot, '--check-game')
$original = [IO.File]::ReadAllText($file)
$descriptor = $original | ConvertFrom-Json
$descriptor.engine.version = '999.0.0-incompatible'
[IO.File]::WriteAllText($file, ($descriptor | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
$beforeFailure = [IO.File]::ReadAllText($file)
$failure = Invoke-Expected $runtime @('--project', $file, '--engine', $engineRoot, '--check-game') 1
if($failure -notmatch 'requires Engine' -or [IO.File]::ReadAllText($file) -cne $beforeFailure) { throw 'Version mismatch did not preserve the Descriptor.' }
$descriptor.engine.association = 'installed'; $descriptor.engine.version = '1.0.0'
[IO.File]::WriteAllText($file, ($descriptor | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
$null = Invoke-Expected $runtime @('--project', $file, '--engine', $engineRoot, '--check-game') 1
[IO.File]::WriteAllText($file, $original, [Text.UTF8Encoding]::new($false))
$movedRoot = Join-Path $validationRoot 'Moved Game B'
# 今回作成した隔離Rootの直下だけを移動し、Engineや既存Projectを対象にしない。
foreach($moveTarget in @($projectRoot, $movedRoot)) {
    if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($moveTarget)) -ne $validationRoot) { throw 'Unsafe test move target.' }
}
Move-Item -LiteralPath $projectRoot -Destination $movedRoot
$movedFile = Join-Path $movedRoot '日本語 Game A.nxproject'
$null = Invoke-Expected $runtime @('--project', $movedFile, '--engine', $engineRoot, '--check-game')
$listing = Invoke-Expected $tool @('list', $registry)
if($listing -notmatch "`t1\s*$") { throw 'Moved Project was not marked Missing.' }
$null = Invoke-Expected $tool @('register', $movedFile, $registry)
$records = Get-Content -LiteralPath $registry -Raw -Encoding UTF8 | ConvertFrom-Json
if($records.projects.Count -ne 1 -or $records.projects[0].file -notmatch 'Moved Game B') { throw 'Relocated Project was not re-registered.' }
$null = Invoke-Expected $tool @('remove', $movedFile, $registry)
if(-not (Test-Path -LiteralPath $movedFile)) { throw 'Removing a recent entry deleted Project data.' }
$null = Invoke-Expected $runtime @('--project', (Join-Path $validationRoot 'Missing.nxproject'), '--engine', $engineRoot, '--check-game') 1
Write-Output 'PASS: Project CLI creation/inspection/registry, nxproject host arguments, Unicode/spaced/moved roots and Engine mismatch rejection without Descriptor changes.'
