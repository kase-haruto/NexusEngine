param(
    [Parameter(Mandatory=$true)][string]$TargetManifest,
    [string]$SourceManifest,
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(-not $SourceManifest) { $SourceManifest = Join-Path $workspace "NexusEngine.$Configuration.nxengine" }
$SourceManifest = [IO.Path]::GetFullPath($SourceManifest); $TargetManifest = [IO.Path]::GetFullPath($TargetManifest)
$tool = Join-Path $workspace "Generated/Modules/x64/$Configuration/NexusProjectTool/NexusProjectTool.exe"
$root = Join-Path $workspace ('Generated/MigrationValidation/' + [guid]::NewGuid().ToString())
[IO.Directory]::CreateDirectory($root) | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
function Invoke-Expected([string[]]$Arguments,[int]$Expected = 0) {
    $output = & $tool @Arguments 2>&1 | Out-String
    if($LASTEXITCODE -ne $Expected) { throw "Exit $LASTEXITCODE (expected $Expected): $Arguments`n$output" }
    return $output
}
function Write-Json([string]$File,$Value) { [IO.File]::WriteAllText($File,($Value | ConvertTo-Json -Depth 12),$utf8) }
$game = Join-Path $root '日本語 Migration Game'
$null = Invoke-Expected @('create-template',$game,'Migration Game',$SourceManifest,'blank')
$file = Join-Path $game '日本語 Migration Game.nxproject'
$original = [IO.File]::ReadAllText($file)
$asset = Join-Path $game 'Assets/Keep.txt'; [IO.File]::WriteAllText($asset,'Project-owned asset bytes',$utf8)
$sourceFile = Join-Path $game 'Source/Game/HealthComponent.h'; $sourceBytes = [IO.File]::ReadAllText($sourceFile)
$configFile = Join-Path $game 'Config/BuildTargets.json'; $configBytes = [IO.File]::ReadAllText($configFile)
$level = Join-Path $game 'Levels/Startup.level'; $originalLevel = [IO.File]::ReadAllText($level)
$plan = Join-Path $root 'Review.json'
$null = Invoke-Expected @('migration-plan',$file,$TargetManifest,$plan)
if([IO.File]::ReadAllText($file) -cne $original) { throw 'Review modified the Project.' }
$null = Invoke-Expected @('migration-apply',$plan) 1
try {
    $descriptor = $original | ConvertFrom-Json; $descriptor.name = 'Externally edited'
    Write-Json $file $descriptor
    $changed = [IO.File]::ReadAllText($file)
    $null = Invoke-Expected @('migration-apply',$plan,'--accept-api-risk') 1
    if([IO.File]::ReadAllText($file) -cne $changed) { throw 'Stale plan overwrote an external Descriptor edit.' }
} finally { [IO.File]::WriteAllText($file,$original,$utf8) }
try {
    [IO.File]::AppendAllText($level,"`n",$utf8)
    $null = Invoke-Expected @('migration-apply',$plan,'--accept-api-risk') 1
    [IO.File]::WriteAllText($level,$originalLevel.Replace('NEXUS_LEVEL 12','NEXUS_LEVEL 999'),$utf8)
    $null = Invoke-Expected @('migration-plan',$file,$TargetManifest,(Join-Path $root 'Unsupported.json')) 1
} finally { [IO.File]::WriteAllText($level,$originalLevel,$utf8) }
if([IO.File]::ReadAllText($file) -cne $original) { throw 'Failed review/apply changed the Project.' }
# before Engineが未配置でも、明示した移行と復旧はDescriptorだけで行える。
try {
    $descriptor = $original | ConvertFrom-Json
    $descriptor.engine.association = 'installed'; $descriptor.engine.id = 'missing-old-engine'; $descriptor.engine.version = '999.0.0'
    Write-Json $file $descriptor; $missing = [IO.File]::ReadAllText($file)
    $missingPlan = Join-Path $root 'MissingOld.json'
    $null = Invoke-Expected @('migration-plan',$file,$TargetManifest,$missingPlan)
    $output = Invoke-Expected @('migration-apply',$missingPlan,'--accept-api-risk')
    if($output -notmatch 'Recovery: (.+)') { throw 'Apply did not return a recovery receipt.' }
    $recovery = $matches[1].Trim()
    $null = Invoke-Expected @('migration-restore',$file,$recovery)
    if([IO.File]::ReadAllText($file) -cne $missing) { throw 'Unavailable previous Engine Descriptor was not restored exactly.' }
} finally { [IO.File]::WriteAllText($file,$original,$utf8) }
$output = Invoke-Expected @('migration-apply',$plan,'--accept-api-risk')
if($output -notmatch 'Recovery: (.+)') { throw 'Apply did not return a recovery receipt.' }
$recovery = $matches[1].Trim()
$updated = [IO.File]::ReadAllText($file)
$target = Get-Content -LiteralPath $TargetManifest -Raw -Encoding UTF8 | ConvertFrom-Json
$descriptor = $updated | ConvertFrom-Json
if($descriptor.engine.id -cne $target.id -or $descriptor.engine.version -cne $target.version) { throw 'Migration selected another Engine.' }
if([IO.File]::ReadAllText($level) -cne $originalLevel) { throw 'Migration rewrote Level data.' }
if([IO.File]::ReadAllText($asset) -cne 'Project-owned asset bytes' -or [IO.File]::ReadAllText($sourceFile) -cne $sourceBytes -or [IO.File]::ReadAllText($configFile) -cne $configBytes) { throw 'Migration rewrote Asset/Source/Config data.' }
$registry = Join-Path $root 'Engines.json'
$null = Invoke-Expected @('engine-register',$SourceManifest,$registry)
$null = Invoke-Expected @('engine-register',$TargetManifest,$registry)
$null = Invoke-Expected @('check',$file,$registry,$Configuration)
try {
    $descriptor.name = 'Edited after migration'; Write-Json $file $descriptor
    $changed = [IO.File]::ReadAllText($file)
    $null = Invoke-Expected @('migration-restore',$file,$recovery) 1
    if([IO.File]::ReadAllText($file) -cne $changed) { throw 'Recovery overwrote a later Descriptor edit.' }
} finally { [IO.File]::WriteAllText($file,$updated,$utf8) }
$backup = Join-Path ([IO.Path]::GetDirectoryName($recovery)) 'Original.nxproject'
$backupBytes = [IO.File]::ReadAllBytes($backup)
try {
    [IO.File]::AppendAllText($backup,"`n",$utf8)
    $null = Invoke-Expected @('migration-restore',$file,$recovery) 1
} finally { [IO.File]::WriteAllBytes($backup,$backupBytes) }
# 今回作成した隔離Rootの直下だけを移動し、既存GameやEngineを対象にしない。
$moved = Join-Path $root '移動後 Game'
foreach($path in @($game,$moved)) { if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($path)) -cne $root) { throw 'Unsafe migration-test move target.' } }
$receiptRelative = [IO.Path]::GetRelativePath($game,$recovery)
Move-Item -LiteralPath $game -Destination $moved
$file = Join-Path $moved '日本語 Migration Game.nxproject'; $recovery = Join-Path $moved $receiptRelative
$null = Invoke-Expected @('migration-restore',$file,(Join-Path $root 'Outside/Recovery.json')) 1
$null = Invoke-Expected @('migration-restore',$file,$recovery)
$null = Invoke-Expected @('migration-restore',$file,$recovery)
if([IO.File]::ReadAllText($file) -cne $original) { throw 'Moved Project did not restore exact original Descriptor bytes.' }
$null = Invoke-Expected @('check',$file,$registry,$Configuration)
# 成果物名を共有データに使うProjectでは、復旧ファイルを別の除外Directoryへ退避する。
$aliasRoot = Join-Path $root 'Shared Paths'
$null = Invoke-Expected @('create-template',$aliasRoot,'Shared Paths',$SourceManifest,'blank')
$aliasFile = Join-Path $aliasRoot 'Shared Paths.nxproject'
foreach($pair in @(@('Assets','Build'),@('Levels','Intermediate'),@('Source','Cache'),@('Config','Generated'))) {
    $from = Join-Path $aliasRoot $pair[0]; $to = Join-Path $aliasRoot $pair[1]
    foreach($movePath in @($from,$to)) { if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($movePath)) -cne $aliasRoot) { throw 'Unsafe shared-path test move target.' } }
    Move-Item -LiteralPath $from -Destination $to
}
$alias = Get-Content -LiteralPath $aliasFile -Raw | ConvertFrom-Json
$alias.paths.assets = 'Build'; $alias.paths.levels = 'Intermediate'; $alias.paths.source = 'Cache'; $alias.paths.config = 'Generated'; $alias.startupLevel = 'Intermediate/Startup.level'
Write-Json $aliasFile $alias; $aliasBytes = [IO.File]::ReadAllText($aliasFile)
$aliasPlan = Join-Path $root 'SharedPathsReview.json'
$null = Invoke-Expected @('migration-plan',$aliasFile,$TargetManifest,$aliasPlan)
$output = Invoke-Expected @('migration-apply',$aliasPlan,'--accept-api-risk')
if($output -notmatch 'Recovery: (.+)') { throw 'Shared-path migration did not return a receipt.' }
$aliasReceipt = $matches[1].Trim()
if($aliasReceipt.Replace('\','/') -notmatch '/\.vs/NexusMigrations/') { throw 'Backup files mixed with shared Project data.' }
$backPlan = Join-Path $root 'SharedPathsBack.json'
$null = Invoke-Expected @('migration-plan',$aliasFile,$SourceManifest,$backPlan)
$null = Invoke-Expected @('migration-restore',$aliasFile,$aliasReceipt)
if([IO.File]::ReadAllText($aliasFile) -cne $aliasBytes) { throw 'Shared-path Project did not restore exact bytes.' }
Write-Output "PASS: explicit review/apply, stale Descriptor/Level rejection, format rejection, unavailable old Engine, target build, edited/corrupt recovery rejection, moved Project and exact/idempotent restore. Artifacts: $root"
