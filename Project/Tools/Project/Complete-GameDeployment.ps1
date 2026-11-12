param(
    [Parameter(Mandatory=$true)][string]$ProjectFile,
    [Parameter(Mandatory=$true)][string]$EngineManifest,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateSet('Debug','Develop','Release')][string]$Configuration='Debug',
    [ValidateSet('Standard','NativeLivePatch')][string]$BuildProfile='Standard',
    [string]$LivePPDirectory='',
    [switch]$NativeOnly,
    [switch]$Rebuild,
    [switch]$RebuildManaged,
    [switch]$SkipNativeBuild
)
$ErrorActionPreference='Stop'
if($NativeOnly -and $SkipNativeBuild) { throw 'NativeOnly cannot skip its Native Build.' }
. (Join-Path $PSScriptRoot 'Project-ModuleOrder.ps1')
. (Join-Path $PSScriptRoot 'Managed-Deployment.ps1')
. (Join-Path $PSScriptRoot 'Managed-Runtime.ps1')
$project=Get-Content -Raw -Encoding UTF8 -LiteralPath $ProjectFile | ConvertFrom-Json
$engine=Get-Content -Raw -Encoding UTF8 -LiteralPath $EngineManifest | ConvertFrom-Json
$root=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($ProjectFile))
# VSがEditor / Runtimeを並列Buildしても共有rosterと追加Module出力を同時更新しない。
$lock=$null; $deadline=[DateTime]::UtcNow.AddMinutes(5)
while($null -eq $lock) {
 try { $lock=[IO.File]::Open((Join-Path $root '.nexus.deployment.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
 catch [IO.IOException] { if(($_.Exception.HResult -band 0xffff) -notin @(32,33) -or [DateTime]::UtcNow -ge $deadline) { throw }; Start-Sleep -Milliseconds 50 }
}
try {
$engineRoot=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($EngineManifest))
$sdk=[IO.Path]::GetFullPath((Join-Path $engineRoot $engine.paths.sdk))
$targets=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $root ($project.paths.config+'/BuildTargets.json')) | ConvertFrom-Json
$managedRuntime=Get-NexusManagedRuntime $targets
$frameworkArguments=@(Get-NexusManagedArguments $managedRuntime)
$modulePlan=@(Get-NexusModuleBuildPlan $project $targets $root $Configuration $BuildProfile $engine)
$order=@(Get-NexusModuleOrder $project); Assert-NexusRuntimeModules $project
if($project.engine.id -cne $engine.id -or $project.engine.association -cne $engine.association -or ($project.engine.version -and $project.engine.version -cne $engine.version) -or $engine.configuration -cne $Configuration) { throw 'Module deployment Engine / configuration mismatch.' }
function Within([string]$Reference) {
    if([IO.Path]::IsPathRooted($Reference)) { throw 'Deployment reference must be relative.' }
    $path=[IO.Path]::GetFullPath((Join-Path $root $Reference))
    if(-not $path.StartsWith($root.TrimEnd('\','/')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Deployment reference escapes Project.' }
    $cursor=$path; while($cursor -and $cursor.Length -ge $root.Length) { if((Test-Path -LiteralPath $cursor) -and ((Get-Item -Force -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Deployment cannot traverse reparse points.' }; $cursor=[IO.Path]::GetDirectoryName($cursor) }
    return $path
}
function Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Escape([string]$Value) { $value=$Value.Replace('%','%25'); foreach($pair in @(@(';','%3B'),@('$','%24'),@('@','%40'),@("'",'%27'),@('(','%28'),@(')','%29'))) { $value=$value.Replace($pair[0],$pair[1]) }; return $value }
function Output-Path([string]$Reference) {
    $value=$Reference.Replace('{configuration}',$Configuration).Replace('\','/')
    if($BuildProfile -ceq 'NativeLivePatch') { if(-not $value.StartsWith('Build/Bin/x64/')) { throw 'Invalid profile layout.' }; $value='Build/Bin/LivePP/x64/'+$value.Substring('Build/Bin/x64/'.Length) }
    return Within $value
}
function Atomic-Json([string]$File,[object]$Value) {
    $temporary=$File+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    [IO.File]::WriteAllText($temporary,(ConvertTo-Json -InputObject $Value -Depth 20),[Text.UTF8Encoding]::new($false))
    # Windows PowerShellでもbackupなしをnull stringとして渡し、二回目以降の配置を失敗させない。
    try { if([IO.File]::Exists($File)) { [IO.File]::Replace($temporary,$File,[System.Management.Automation.Language.NullString]::Value) } else { [IO.File]::Move($temporary,$File) } } finally { if([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
}
$bin=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\','/')
if($bin -ine [IO.Path]::GetDirectoryName((Output-Path $targets.runtimeExecutable))) { throw 'Deployment output differs from host output.' }
$mappings=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
foreach($item in $targets.moduleProjects) {
    if(-not @($project.modules | Where-Object { $_.id -ceq $item.id }).Count -or $mappings.ContainsKey($item.id)) { throw 'Invalid Module mapping.' }
    $mappings.Add($item.id,$item)
}
$primaryNative=@($project.modules | Where-Object { $_.language -ceq 'Cpp' } | Select-Object -First 1)
$primaryManaged=@($project.modules | Where-Object { $_.language -ceq 'CSharp' } | Select-Object -First 1)
$native=@(); $staticNative=@(); $managed=@()
# Native先行生成やCpp-onlyでは旧Managed配置を読まない。破損した旧depsが再Build自体を妨げないようにする。
$managedFiles=if(-not $NativeOnly -and $targets.gameScripts) { New-NexusManagedFileMap (Join-Path $bin 'Managed') } else { $null }
foreach($id in $order) {
    $module=@($project.modules | Where-Object { $_.id -ceq $id })[0]
    $mapping=if($mappings.ContainsKey($id)) { $mappings[$id] } else { $null }
    if($module.language -ceq 'Cpp') {
        if($mapping) {
            if(-not $SkipNativeBuild) {
            $locator=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
            $msbuild=@(& $locator -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe')[0]
            if(-not $msbuild) { throw 'MSBuild is unavailable.' }
            $arguments=@((Within $mapping.project),'/m','/verbosity:minimal',"/p:Configuration=$Configuration",'/p:Platform=x64',('/p:NexusEngineRoot='+(Escape $sdk)),('/p:NexusHostEngineId='+(Escape $engine.id)),('/p:NexusHostEngineVersion='+(Escape $engine.version)))
            $arguments+='/p:NexusNativeModuleLivePP='+$(if($BuildProfile -ceq 'NativeLivePatch') { 'true' } else { 'false' })
            if($LivePPDirectory) { $arguments+='/p:NexusLivePPDir='+(Escape $LivePPDirectory) }
            $arguments+=$(if($Rebuild) { '/t:Rebuild' } else { '/t:Build' })
            & $msbuild @arguments; if($LASTEXITCODE -ne 0) { throw "Native Module Build failed: $id" }
            }
            $output=Output-Path $mapping.output
        } elseif($NativeOnly) { continue }
        elseif($primaryNative.Count -and $id -ceq $primaryNative[0].id -and $targets.nativeModule) { $output=Output-Path $targets.nativeModule }
        elseif($primaryNative.Count -and $id -ceq $primaryNative[0].id) { $staticNative+=$id; continue }
        else { throw "Missing Module mapping: $id" }
        if([IO.Path]::GetDirectoryName($output) -ine $bin -or -not [IO.File]::Exists($output)) { throw 'Native DLL must be beside its host.' }
        $native+=@{id=$id;path=[IO.Path]::GetFileName($output)}
    } elseif(-not $NativeOnly) {
        if($mapping) {
            $output=Output-Path $mapping.output
            if([IO.Path]::GetDirectoryName($output) -ine (Join-Path $bin 'Managed')) { throw 'Managed Module must share the deployment directory.' }
            # 同じ世代の全Projectを依存順でBuildし、安定APIは再Buildしない。
            $stage=Join-Path $root ('Intermediate/ManagedDeployment/'+[guid]::NewGuid().ToString('N'))
            Initialize-NexusManagedStage (Join-Path $bin 'Managed') $stage $managedFiles ([IO.Path]::GetFileName($output))
            $profileDirectory=if($BuildProfile -ceq 'NativeLivePatch') { 'LivePP/' } else { '' }
            # 追加Managed Moduleも主Moduleと同じ再構築契約を使い、古い中間生成物を残さない。
            $managedTarget=if($Rebuild -or $RebuildManaged) { '-t:Rebuild' } else { '-t:Build' }
            & dotnet build (Within $mapping.project) $managedTarget @frameworkArguments --configuration $Configuration --output $stage -p:BuildProjectReferences=false ('-p:NexusManagedApiPath='+(Escape (Join-Path $bin 'Managed/Nexus.Managed.dll'))) ('-p:NexusManagedIntermediateRoot='+(Escape (Join-Path $root ('Intermediate/'+$profileDirectory+'ManagedModules/'+$Configuration+'/'))))
            if($LASTEXITCODE -ne 0) { throw "Managed Module Build failed: $id" }
            if(-not [IO.File]::Exists((Join-Path $stage ([IO.Path]::GetFileName($output))))) { throw 'Managed AssemblyName differs from Module output.' }
            Merge-NexusManagedDeployment $stage (Join-Path $bin 'Managed') $managedFiles
            $managed+=[IO.Path]::GetFileName($output)
        } elseif($primaryManaged.Count -and $id -ceq $primaryManaged[0].id -and $targets.gameScripts) { $managed+=[IO.Path]::GetFileName((Output-Path $targets.gameScripts)) }
        elseif(-not $primaryManaged.Count -or $id -cne $primaryManaged[0].id) { throw "Missing Managed mapping: $id" }
    }
}
if($NativeOnly) { return }
foreach($dependency in @($targets.runtimeDependencies | Where-Object { $_ })) {
    $source=Within $dependency.Replace('{configuration}',$Configuration); $name=[IO.Path]::GetFileName($source)
    if([IO.Path]::GetExtension($name) -ine '.dll' -or $name -imatch '\A(nethost|Nexus\.|LPP_)' -or @($native | Where-Object { $_.path -ieq $name }).Count -or @($engine.runtimeFiles | Where-Object { [IO.Path]::GetFileName($_) -ieq $name }).Count) { throw 'Game runtime dependency conflicts with a Module / Engine / development DLL.' }
    $destination=Join-Path $bin $name
    if(-not [IO.File]::Exists($destination) -or (Hash $source) -cne (Hash $destination)) { [IO.File]::Copy($source,$destination,$true) }
}
if($managed.Count) {
    if(@($managed | Sort-Object -Unique).Count -ne $managed.Count) { throw 'Managed Module outputs collide.' }
    Atomic-Json (Join-Path $bin 'Managed/GameScripts.modules.json') @($managed)
    Invoke-NexusMetadataTool $managedRuntime (Join-Path $bin 'ManagedTools/Nexus.Managed.Metadata.dll') (Join-Path $bin 'Managed/GameScripts.dll') (Join-Path $bin 'Managed/GameScripts.metadata.json')
}
Atomic-Json (Join-Path $bin 'Nexus.modules.json') @{schemaVersion=1;projectId=$project.projectId;native=@($native);staticNative=@($staticNative);managedRuntime=$managedRuntime;managedAssembly=$(if($targets.gameScripts) { 'Managed/GameScripts.dll' } else { '' })}
} finally { $lock.Dispose() }
