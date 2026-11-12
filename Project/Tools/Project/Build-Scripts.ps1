param(
    [Parameter(Mandatory=$true)][string]$ScriptsProject,
    [Parameter(Mandatory=$true)][string]$ApiAssembly,
    [Parameter(Mandatory=$true)][string]$MetadataTool,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug',
    [string]$ProjectFile=''
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Managed-Runtime.ps1')
$managedRuntime=if([IO.Path]::GetExtension($MetadataTool) -ieq '.exe') { 'Mono' } else { 'CoreCLR' }
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
# GameのCompileだけを実行する。稼働中のBootstrap / API / Host成果物は書き換えない。
foreach($file in @($ScriptsProject,$ApiAssembly,$MetadataTool)) {
    if(-not [IO.Path]::IsPathRooted($file) -or -not [IO.File]::Exists($file)) { throw "Missing explicit input: $file" }
}
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
if([IO.Directory]::Exists($taskOutput) -or -not [IO.Path]::IsPathRooted($OutputDirectory)) { throw 'Reload requires a new absolute output directory.' }
function Escape-Property([string]$Value) {
    $result = $Value.Replace('%','%25')
    foreach($pair in @(@(';','%3B'),@('$','%24'),@('@','%40'),@("'",'%27'),@('(','%28'),@(')','%29'),@('*','%2A'),@('?','%3F'))) { $result = $result.Replace($pair[0],$pair[1]) }
    return $result
}
$taskIntermediate = Join-Path ([IO.Path]::GetDirectoryName($taskOutput)) 'obj/'
if($ProjectFile) {
    . (Join-Path $PSScriptRoot 'Project-ModuleOrder.ps1')
    . (Join-Path $PSScriptRoot 'Managed-Deployment.ps1')
    $project=Get-Content -Raw -Encoding UTF8 -LiteralPath $ProjectFile | ConvertFrom-Json
    $projectRoot=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($ProjectFile))
    $config=Resolve-NexusProjectReference $projectRoot ($project.paths.config+'/BuildTargets.json')
    $targets=Get-Content -Raw -Encoding UTF8 -LiteralPath $config | ConvertFrom-Json
    $managedRuntime=Get-NexusManagedRuntime $targets
    $modulePlan=@(Get-NexusModuleBuildPlan $project $targets $projectRoot $Configuration)
    $primaryPlan=@($modulePlan | Where-Object { $_.language -ceq 'CSharp' -and $_.primary })
    if($primaryPlan.Count -ne 1 -or -not $primaryPlan[0].output -or [IO.Path]::GetFullPath($ScriptsProject) -ine $primaryPlan[0].project) { throw 'Reload Project differs from the declared primary Managed Module.' }
}
$frameworkArguments=@(Get-NexusManagedArguments $managedRuntime)
& dotnet build $ScriptsProject @frameworkArguments --configuration $Configuration --output $taskOutput "-p:NexusManagedIntermediateRoot=$(Escape-Property $taskIntermediate)" "-p:NexusManagedApiPath=$(Escape-Property $ApiAssembly)"
if($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if($ProjectFile) {
    $managedFiles=New-NexusManagedFileMap $taskOutput
    $roots=@(); $primary=@($project.modules | Where-Object { $_.language -ceq 'CSharp' } | Select-Object -First 1)[0]
    foreach($id in @(Get-NexusModuleOrder $project)) {
        $module=@($project.modules | Where-Object { $_.id -ceq $id })[0]; if($module.language -cne 'CSharp') { continue }
        if($id -ceq $primary.id) { $roots+='GameScripts.dll'; continue }
        $mapping=@($targets.moduleProjects | Where-Object { $_.id -ceq $id })[0]
        if(-not $mapping -or [IO.Path]::IsPathRooted($mapping.project)) { throw 'Missing additional Managed Project.' }
        $file=[IO.Path]::GetFullPath((Join-Path $projectRoot $mapping.project))
        if(-not $file.StartsWith($projectRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Managed Project escapes Root.' }
        $stage=Join-Path ([IO.Path]::GetDirectoryName($taskOutput)) ('module-'+[guid]::NewGuid().ToString('N'))
        Initialize-NexusManagedStage $taskOutput $stage $managedFiles ([IO.Path]::GetFileName($mapping.output))
        & dotnet build $file @frameworkArguments --configuration $Configuration --output $stage -p:BuildProjectReferences=false "-p:NexusManagedIntermediateRoot=$(Escape-Property $taskIntermediate)" "-p:NexusManagedApiPath=$(Escape-Property $ApiAssembly)"
        if($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        if(-not [IO.File]::Exists((Join-Path $stage ([IO.Path]::GetFileName($mapping.output))))) { throw 'Managed AssemblyName differs from Module output.' }
        Merge-NexusManagedDeployment $stage $taskOutput $managedFiles
        $roots+=[IO.Path]::GetFileName($mapping.output)
    }
    if(@($roots | Sort-Object -Unique).Count -ne $roots.Count) { throw 'Managed Module outputs collide.' }
    [IO.File]::WriteAllText((Join-Path $taskOutput 'GameScripts.modules.json'),(ConvertTo-Json -InputObject @($roots)),[Text.UTF8Encoding]::new($false))
}
Invoke-NexusMetadataTool $managedRuntime $MetadataTool (Join-Path $taskOutput 'GameScripts.dll') (Join-Path $taskOutput 'GameScripts.metadata.json')
if(-not [IO.File]::Exists((Join-Path $taskOutput 'GameScripts.dll')) -or -not [IO.File]::Exists((Join-Path $taskOutput 'GameScripts.metadata.json'))) { throw 'Reload outputs are incomplete.' }
exit 0
