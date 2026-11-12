param(
    [Parameter(Mandatory=$true)][string]$ProjectFile,
    [Parameter(Mandatory=$true)][string]$EngineManifest,
    [Parameter(Mandatory=$true)][string]$Executable,
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug',
    [ValidateSet('Editor','Runtime')][string]$HostKind = 'Runtime',
    [ValidateSet('','true','false')][string]$NativeModuleLivePP = 'false'
)
$ErrorActionPreference = 'Stop'
# 共通Build coordinatorが最後に全Moduleを検証してstampを書く。旧Host templateも同じ順序へ統合する。
if($env:NEXUS_DEFER_BUILD_STAMP -ceq '1') { return }
$project = Get-Content -LiteralPath $ProjectFile -Raw -Encoding UTF8 | ConvertFrom-Json
$engine = Get-Content -LiteralPath $EngineManifest -Raw -Encoding UTF8 | ConvertFrom-Json
if($project.schemaVersion -ne 1 -or $engine.schemaVersion -ne 1 -or $engine.configuration -ne $Configuration) { throw 'Build identity/configuration mismatch.' }
if($project.engine.id -cne $engine.id -or $project.engine.association -cne $engine.association -or ($project.engine.version -and $project.engine.version -cne $engine.version)) { throw 'Project requires a different Engine. No fallback was used.' }
# Windows PowerShell側のモジュール自動読込設定に依存せず成果物を識別する。
$hasher = [Security.Cryptography.SHA256]::Create()
$inputFile = [IO.File]::OpenRead($Executable)
try { $digest = [BitConverter]::ToString($hasher.ComputeHash($inputFile)).Replace('-','').ToLowerInvariant() }
finally { $inputFile.Dispose(); $hasher.Dispose() }
$stamp = [ordered]@{ schemaVersion=1;projectId=$project.projectId;engineId=$engine.id;engineVersion=$engine.version;association=$engine.association;buildId=$engine.buildId;configuration=$Configuration;hostKind=$HostKind;engineManifest=[IO.Path]::GetFullPath($EngineManifest);sha256=$digest }
$stamp.projectHash=(Get-FileHash -LiteralPath $ProjectFile -Algorithm SHA256).Hash.ToLowerInvariant()
# 通常stampは旧形式を維持する。開発専用の成果物を通常版と誤認しないためにだけ明示する。
if($NativeModuleLivePP -eq 'true') { $stamp.buildProfile = 'NativeLivePatch' }
. (Join-Path $PSScriptRoot 'Project-ModuleOrder.ps1')
$null = @(Get-NexusModuleOrder $project)
Assert-NexusRuntimeModules $project
# DLLを差し替えただけの古いホストを起動しないよう、同じstampへModule内容を記録する。
# 旧静的Projectのstamp形式は維持する。ScriptはMSBuildのProjectReference完了後に呼ばれる。
$projectRoot = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($ProjectFile))
$targetsFile = Join-Path (Join-Path $projectRoot $project.paths.config) 'BuildTargets.json'
# 専用profileは元のBuildTargetsを書き換えない。生成テンプレートの成果物だけを別配置へ解決する。
function Resolve-BuildReference([string]$Reference) {
    $resolved = $Reference.Replace('{configuration}', $Configuration)
    if($NativeModuleLivePP -eq 'true') {
        $resolved = $resolved.Replace('\','/')
        if(-not $resolved.StartsWith('Build/Bin/x64/', [StringComparison]::Ordinal)) { throw 'Module-only profile requires the generated Build/Bin/x64 layout.' }
        $resolved = 'Build/Bin/LivePP/x64/' + $resolved.Substring('Build/Bin/x64/'.Length)
    }
    return $resolved
}
if([IO.File]::Exists($targetsFile)) {
    # Module mapping / 配布依存だけの変更も、古い成果物へ結び付けない。
    $stamp.targetsHash=(Get-FileHash -LiteralPath $targetsFile -Algorithm SHA256).Hash.ToLowerInvariant()
    $targets = Get-Content -LiteralPath $targetsFile -Raw -Encoding UTF8 | ConvertFrom-Json
    if($targets.runtimeDependencies) {
        $stamp.runtimeFiles=@()
        foreach($reference in $targets.runtimeDependencies) {
            $reference=$reference.Replace('{configuration}',$Configuration)
            $file=[IO.Path]::GetFullPath((Join-Path $projectRoot $reference))
            if([IO.Path]::IsPathRooted($reference) -or -not $file.StartsWith($projectRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Runtime dependency escapes Project.' }
            $stamp.runtimeFiles+=@{path=$reference;sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()}
        }
    }
    if($targets.moduleProjects) {
        $stamp.moduleFiles=@()
        foreach($module in $targets.moduleProjects) {
            $reference=Resolve-BuildReference $module.output
            if([IO.Path]::IsPathRooted($reference)) { throw 'Module output must be relative.' }
            $file=[IO.Path]::GetFullPath((Join-Path $projectRoot $reference))
            if(-not $file.StartsWith($projectRoot.TrimEnd('\','/')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Module output escapes Project.' }
            $stamp.moduleFiles+=@{id=$module.id;path=$reference;sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()}
        }
    }
    $deployment=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Executable))) 'Nexus.modules.json'
    if([IO.File]::Exists($deployment)) { $stamp.deploymentHash=(Get-FileHash -LiteralPath $deployment -Algorithm SHA256).Hash.ToLowerInvariant() }
    if($targets.managedBootstrap) {
        $directory=[IO.Path]::GetDirectoryName((Join-Path $projectRoot (Resolve-BuildReference $targets.managedBootstrap)))
        $stamp.managedFiles=@()
        foreach($file in Get-ChildItem -LiteralPath $directory -File -Recurse) {
            $reference=$file.FullName.Substring($projectRoot.Length+1).Replace('\','/')
            $stamp.managedFiles+=@{path=$reference;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
        }
    }
    if($NativeModuleLivePP -eq 'true') {
        if($Configuration -eq 'Release' -or $engine.association -cne 'development' -or -not $targets.nativeModule) { throw 'Module-only profile requires a development Native / Hybrid project in Debug or Develop.' }
        $hostReference = if($HostKind -eq 'Editor') { $targets.editorExecutable } else { $targets.runtimeExecutable }
        $expectedHost = [IO.Path]::GetFullPath((Join-Path $projectRoot (Resolve-BuildReference $hostReference)))
        if($expectedHost -ine [IO.Path]::GetFullPath($Executable)) { throw 'Module-only host is outside its generated profile output.' }
    }
    if($targets.nativeModule) {
        $reference = Resolve-BuildReference $targets.nativeModule
        if([IO.Path]::IsPathRooted($reference)) { throw 'Native Module path must be relative.' }
        $moduleFile = [IO.Path]::GetFullPath((Join-Path $projectRoot $reference))
        if(-not $moduleFile.StartsWith($projectRoot.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Native Module path escapes the Project.' }
        if($moduleFile -ine (Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Executable))) 'Game.dll')) { throw 'Native Module must be Game.dll beside its host.' }
        $moduleHasher = [Security.Cryptography.SHA256]::Create()
        $moduleInput = [IO.File]::OpenRead($moduleFile)
        try { $moduleDigest = [BitConverter]::ToString($moduleHasher.ComputeHash($moduleInput)).Replace('-','').ToLowerInvariant() }
        finally { $moduleInput.Dispose(); $moduleHasher.Dispose() }
        $stamp.nativeModule = @{ path=$reference; sha256=$moduleDigest }
    }
    if($targets.managedBootstrap) {
        $assemblyReference = Resolve-BuildReference $targets.managedBootstrap
        $assemblyFile = [IO.Path]::GetFullPath((Join-Path $projectRoot $assemblyReference))
        $expectedBootstrap = Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Executable))) 'Managed/Nexus.Managed.Bootstrap.dll'
        if($assemblyFile -ine [IO.Path]::GetFullPath($expectedBootstrap) -or [IO.Path]::IsPathRooted($assemblyReference)) { throw 'Managed Bootstrap must be deployed beside its host in Managed/.' }
        $hostDirectory = [IO.Path]::GetDirectoryName($assemblyReference).Replace('\','/')
        $nethostReference = ($hostDirectory.Substring(0, $hostDirectory.LastIndexOf('/')) + '/nethost.dll')
        $records = @()
        $bootstrapFiles = @($assemblyReference, [IO.Path]::ChangeExtension($assemblyReference, '.runtimeconfig.json'), [IO.Path]::ChangeExtension($assemblyReference, '.deps.json'), $nethostReference)
        if($targets.managedRuntime -ceq 'Mono') {
            $monoReference=$hostDirectory.Substring(0,$hostDirectory.LastIndexOf('/'))+'/Mono'
            $bootstrapFiles=@($assemblyReference,[IO.Path]::ChangeExtension($assemblyReference,'.deps.json'),($monoReference+'/bin/mono-2.0-sgen.dll'),($monoReference+'/lib/mono/4.5/mscorlib.dll'),($monoReference+'/etc/mono/config'))
            # 同じmanagedFiles群でBCL・native runtime・configも固定し、Packageへ検証済み内容を渡す。
            foreach($file in Get-ChildItem -LiteralPath (Join-Path $projectRoot $monoReference) -File -Recurse) {
                $relativeMono=$file.FullName.Substring($projectRoot.Length+1).Replace('\','/')
                $stamp.managedFiles+=@{path=$relativeMono;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
            }
        }
        $apiReference = $hostDirectory + '/Nexus.Managed.dll'
        if([IO.File]::Exists((Join-Path $projectRoot $apiReference))) { $bootstrapFiles += $apiReference }
        if($targets.gameScripts -and $bootstrapFiles.Count -ne $(if($targets.managedRuntime -ceq 'Mono') {6} else {5})) { throw 'Gameplay requires Nexus.Managed.dll.' }
        foreach($relative in $bootstrapFiles) {
            $managedFile = [IO.Path]::GetFullPath((Join-Path $projectRoot $relative))
            if(-not $managedFile.StartsWith($projectRoot.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Managed Bootstrap path escapes the Project.' }
            $managedHasher = [Security.Cryptography.SHA256]::Create()
            $managedInput = [IO.File]::OpenRead($managedFile)
            try { $managedDigest = [BitConverter]::ToString($managedHasher.ComputeHash($managedInput)).Replace('-','').ToLowerInvariant() }
            finally { $managedInput.Dispose(); $managedHasher.Dispose() }
            $records += @{ path=$relative; sha256=$managedDigest }
        }
        $stamp.managedBootstrap = $records
        if($targets.gameScripts) {
            $gameReference = Resolve-BuildReference $targets.gameScripts
            $gameFile = [IO.Path]::GetFullPath((Join-Path $projectRoot $gameReference))
            if([IO.Path]::IsPathRooted($gameReference) -or $gameFile -ine (Join-Path ([IO.Path]::GetDirectoryName($assemblyFile)) 'GameScripts.dll')) { throw 'GameScripts must be beside the Managed API.' }
            $gameRecords = @()
            foreach($relativeScript in @($gameReference, [IO.Path]::ChangeExtension($gameReference, '.deps.json'), [IO.Path]::ChangeExtension($gameReference, '.metadata.json'))) {
                $scriptHasher = [Security.Cryptography.SHA256]::Create()
                $scriptInput = [IO.File]::OpenRead((Join-Path $projectRoot $relativeScript))
                try { $scriptDigest = [BitConverter]::ToString($scriptHasher.ComputeHash($scriptInput)).Replace('-','').ToLowerInvariant() }
                finally { $scriptInput.Dispose(); $scriptHasher.Dispose() }
                $gameRecords += @{ path=$relativeScript; sha256=$scriptDigest }
            }
            $stamp.gameScripts = $gameRecords
        }
    }
}
$output = $Executable + '.nxbuild'
$temporary = $output + '.' + [guid]::NewGuid().ToString() + '.tmp'
$backup = $temporary + '.bak'
try {
    $stampText=$stamp | ConvertTo-Json -Depth 5
    if([Text.Encoding]::UTF8.GetByteCount($stampText) -gt 1MB) { throw 'Build stamp exceeds the Project JSON limit.' }
    [IO.File]::WriteAllText($temporary, $stampText, [Text.UTF8Encoding]::new($false))
    if([IO.File]::Exists($output)) { [IO.File]::Replace($temporary, $output, $backup) } else { [IO.File]::Move($temporary, $output) }
} finally {
    if([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    if([IO.File]::Exists($backup)) { [IO.File]::Delete($backup) }
}
