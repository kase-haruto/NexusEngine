param(
    [Parameter(Mandatory=$true)][string]$ProjectFile,
    [Parameter(Mandatory=$true)][string]$EngineManifest,
    [ValidateSet('Editor','Runtime')][string]$Kind = 'Editor',
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug',
    [ValidateSet('Standard','NativeLivePatch')][string]$BuildProfile = 'Standard',
    [string]$LivePPDirectory = '',
    [switch]$Rebuild,
    [switch]$RebuildManaged
)
$ErrorActionPreference = 'Stop'
if($BuildProfile -cnotin @('Standard','NativeLivePatch')) { throw 'BuildProfile must use its canonical name.' }
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$projectFilePath = [IO.Path]::GetFullPath($ProjectFile)
$engineFilePath = [IO.Path]::GetFullPath($EngineManifest)
$projectRoot = [IO.Path]::GetDirectoryName($projectFilePath)
$engineRoot = [IO.Path]::GetDirectoryName($engineFilePath)
$projectSnapshot=[IO.File]::ReadAllBytes($projectFilePath)
$engineSnapshot=[IO.File]::ReadAllBytes($engineFilePath)
$project = [Text.Encoding]::UTF8.GetString($projectSnapshot).TrimStart([char]0xfeff) | ConvertFrom-Json
$engine = [Text.Encoding]::UTF8.GetString($engineSnapshot).TrimStart([char]0xfeff) | ConvertFrom-Json
if($project.schemaVersion -ne 1 -or $engine.schemaVersion -ne 1 -or $engine.configuration -cne $Configuration) { throw 'Project/Engine build schema or configuration mismatch.' }
. (Join-Path $PSScriptRoot 'Project-ModuleOrder.ps1')
. (Join-Path $PSScriptRoot 'Managed-Runtime.ps1')
$moduleOrder = @(Get-NexusModuleOrder $project)
Write-Output ('Module dependency order: ' + ($moduleOrder -join ' -> '))
# 直接CLIから呼び出した場合も、未対応Moduleを無視して既存Nativeホストを生成しない。
$nativeCount = @($project.modules | Where-Object { $_ -and $_.language -ceq 'Cpp' }).Count
$managedCount = @($project.modules | Where-Object { $_ -and $_.language -ceq 'CSharp' }).Count
Assert-NexusRuntimeModules $project
if($BuildProfile -ceq 'NativeLivePatch') {
    if($engine.association -cne 'development' -or $Configuration -ceq 'Release' -or $nativeCount -ne 1 -or
        -not [IO.Path]::IsPathRooted($LivePPDirectory) -or -not [IO.File]::Exists((Join-Path $LivePPDirectory 'API/x64/LPP_API_x64_CPP.h')) -or
        -not [IO.File]::Exists((Join-Path $LivePPDirectory 'Agent/x64/LPP_Agent_x64_CPP.dll'))) { throw 'NativeLivePatch requires development Debug/Develop, one Native Module and an explicit absolute Live++ SDK directory.' }
} elseif($LivePPDirectory) { throw 'LivePPDirectory is only valid for NativeLivePatch.' }
if($project.engine.id -cne $engine.id -or $project.engine.association -cne $engine.association -or ($project.engine.version -and $project.engine.version -cne $engine.version)) { throw 'Project requires another Engine. No fallback was used.' }
function Within([string]$Root, [string]$Relative, [switch]$AllowRoot) {
    if([IO.Path]::IsPathRooted($Relative)) { throw 'Build path must be relative.' }
    $resolved = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if($AllowRoot -and $resolved.TrimEnd('\','/') -eq $Root.TrimEnd('\','/')) { return $resolved }
    $prefix = $Root.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    if(-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Build path escapes its Root.' }
    return $resolved
}
$sdk = Within $engineRoot $engine.paths.sdk -AllowRoot
$dataRoot = Within $engineRoot $engine.paths.data -AllowRoot
$targetsFile = Join-Path (Within $projectRoot $project.paths.config) 'BuildTargets.json'
$targetsSnapshot=[IO.File]::ReadAllBytes($targetsFile)
$targets = [Text.Encoding]::UTF8.GetString($targetsSnapshot).TrimStart([char]0xfeff) | ConvertFrom-Json
$managedRuntime = Get-NexusManagedRuntime $targets
if($targets.schemaVersion -ne 1) { throw 'Unknown build targets schema.' }
$modulePlan=@(Get-NexusModuleBuildPlan $project $targets $projectRoot $Configuration $BuildProfile $engine)
$isManagedProject = @($project.modules | Where-Object { $_ -and $_.language -ceq 'CSharp' }).Count -gt 0
if($isManagedProject -ne [bool]$targets.managedBootstrap -or ($targets.nativeModule -and $nativeCount -eq 0) -or
    ($managedCount -gt 1 -and -not $targets.gameScripts)) { throw 'Module language and BuildTargets backend differ.' }
if($targets.gameScripts -and -not $isManagedProject) { throw 'GameScripts requires a CSharp Module.' }
if($BuildProfile -ceq 'NativeLivePatch' -and -not $targets.nativeModule) { throw 'NativeLivePatch requires an independent Native Game DLL.' }
function Resolve-OutputReference([string]$Reference) {
    $resolved = $Reference.Replace('{configuration}', $Configuration)
    if($BuildProfile -ceq 'NativeLivePatch') {
        $resolved = $resolved.Replace('\','/')
        if(-not $resolved.StartsWith('Build/Bin/x64/', [StringComparison]::Ordinal)) { throw 'NativeLivePatch requires the generated output layout.' }
        $resolved = 'Build/Bin/LivePP/x64/' + $resolved.Substring('Build/Bin/x64/'.Length)
    }
    return $resolved
}
$locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = @(& $locator -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe')[0]
if(-not $msbuild -or -not [IO.File]::Exists($msbuild)) { throw 'MSBuild was not found. Install the C++ v145 workload.' }
function Escape-Property([string]$Value) {
    $result = $Value.Replace('%','%25')
    foreach($pair in @(@(';','%3B'),@('$','%24'),@('@','%40'),@("'",'%27'),@('(','%28'),@(')','%29'),@('*','%2A'),@('?','%3F'))) { $result = $result.Replace($pair[0],$pair[1]) }
    return $result
}
function Quote-Argument([string]$Value) {
    $text = [Text.StringBuilder]::new(); [void]$text.Append('"'); $slashes = 0
    foreach($character in $Value.ToCharArray()) {
        if($character -eq '\') { $slashes++; continue }
        if($character -eq '"') { [void]$text.Append('\', $slashes * 2 + 1) } else { [void]$text.Append('\', $slashes) }
        [void]$text.Append($character); $slashes = 0
    }
    [void]$text.Append('\', $slashes * 2); [void]$text.Append('"'); return $text.ToString()
}
# GUI起動時もVSから開けるように、マシン固有の参照はGit除外されたlocal propsへ限定する。
function Xml([string]$Value) { return [Security.SecurityElement]::Escape($Value) }
function Content-Hash([string]$Path) {
    # Editorがロード中のDLLも読み取り比較できる。ファイル時刻だけでは同一内容と判定しない。
    $stream = [IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [Convert]::ToBase64String($algorithm.ComputeHash($stream)) }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}
$localProps = '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup><NexusEngineRoot>' + (Xml (Escape-Property $sdk)) + '</NexusEngineRoot><NexusEngineDataRoot>' + (Xml (Escape-Property $dataRoot)) + '</NexusEngineDataRoot><NexusEngineManifest>' + (Xml (Escape-Property $engineFilePath)) + '</NexusEngineManifest><NexusEngineAssociation>' + (Xml $engine.association) + '</NexusEngineAssociation><NexusManagedRuntime>'+ (Xml $managedRuntime) +'</NexusManagedRuntime></PropertyGroup></Project>'
$localFile = Join-Path $projectRoot 'Nexus.Engine.local.props'
$temporary = $localFile + '.' + [guid]::NewGuid().ToString() + '.tmp'
$backup = $temporary + '.bak'
try {
    [IO.File]::WriteAllText($temporary,$localProps,[Text.UTF8Encoding]::new($false))
    if([IO.File]::Exists($localFile)) { [IO.File]::Replace($temporary,$localFile,$backup) } else { [IO.File]::Move($temporary,$localFile) }
} finally {
    if([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    if([IO.File]::Exists($backup)) { [IO.File]::Delete($backup) }
}
$projects = @($targets.runtimeProject)
if($Kind -eq 'Editor') { $projects += $targets.editorProject }
$runtimeOutput = Within $projectRoot (Resolve-OutputReference $targets.runtimeExecutable)
# 追加Native Moduleは依存順に先行Buildする。主DLLとHostは既存ProjectReferenceを使う。
& (Join-Path $PSScriptRoot 'Complete-GameDeployment.ps1') -ProjectFile $projectFilePath -EngineManifest $engineFilePath -OutputDirectory ([IO.Path]::GetDirectoryName($runtimeOutput)) -Configuration $Configuration -BuildProfile $BuildProfile -LivePPDirectory $LivePPDirectory -NativeOnly -Rebuild:$Rebuild
foreach($relative in $projects) {
    $path = Within $projectRoot $relative
    $arguments = @($path, '/m', '/verbosity:minimal', "/p:Configuration=$Configuration", '/p:Platform=x64',
        ('/p:NexusEngineRoot=' + (Escape-Property $sdk)), ('/p:NexusEngineManifest=' + (Escape-Property $engineFilePath)),
        ('/p:NexusEngineAssociation=' + $engine.association), ('/p:NexusHostEngineId=' + (Escape-Property $engine.id)),
        ('/p:NexusHostEngineVersion=' + (Escape-Property $engine.version)), ('/p:NexusHostEngineAssociation=' + $engine.association))
    # profileをglobal propertyで全ProjectReferenceへ渡す。通常版・専用版の成果物を混在させない。
    $arguments += '/p:NexusNativeModuleLivePP=' + $(if($BuildProfile -ceq 'NativeLivePatch') { 'true' } else { 'false' })
    $arguments += '/p:NexusDeferredModuleDeployment=true'
    $arguments += '/p:NexusManagedRuntime=' + $managedRuntime
    $arguments += '/p:NexusSkipDeploymentTarget=true'
    # Scriptsだけの再構築も既存Managed targetsへ伝え、NativeのtargetはBuildのまま維持する。
    $arguments += '/p:NexusManagedRebuild=' + $(if($Rebuild -or $RebuildManaged) { 'true' } else { 'false' })
    if($BuildProfile -ceq 'NativeLivePatch') { $arguments += '/p:NexusLivePPDir=' + (Escape-Property $LivePPDirectory) }
    $arguments += $(if($Rebuild) { '/t:Rebuild' } else { '/t:Build' })
    $start = [Diagnostics.ProcessStartInfo]::new(); $start.FileName = $msbuild; $start.WorkingDirectory = $projectRoot; $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $start.Arguments = ($arguments | ForEach-Object { Quote-Argument $_ }) -join ' '
    $environmentValues = @{}
    foreach($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) { $environmentValues[$entry.Key] = $entry.Value }
    $start.EnvironmentVariables.Clear(); foreach($key in $environmentValues.Keys) { $start.EnvironmentVariables[$key] = $environmentValues[$key] }
    $start.EnvironmentVariables['NEXUS_DEFER_BUILD_STAMP']='1'
    $process = [Diagnostics.Process]::Start($start)
    $copyOutput = $process.StandardOutput.BaseStream.CopyToAsync([Console]::OpenStandardOutput())
    $copyError = $process.StandardError.BaseStream.CopyToAsync([Console]::OpenStandardError())
    $process.WaitForExit(); $null = $copyOutput.GetAwaiter().GetResult(); $null = $copyError.GetAwaiter().GetResult()
    if($process.ExitCode -ne 0) { exit $process.ExitCode }
    $executableRelative = if($relative -eq $targets.editorProject) { $targets.editorExecutable } else { $targets.runtimeExecutable }
    $executablePath = Within $projectRoot (Resolve-OutputReference $executableRelative)
    & (Join-Path $PSScriptRoot 'Complete-GameDeployment.ps1') -ProjectFile $projectFilePath -EngineManifest $engineFilePath -OutputDirectory ([IO.Path]::GetDirectoryName($executablePath)) -Configuration $Configuration -BuildProfile $BuildProfile -LivePPDirectory $LivePPDirectory -SkipNativeBuild -Rebuild:$Rebuild -RebuildManaged:$RebuildManaged
    foreach($dependency in $engine.runtimeFiles) {
        $source = Within $engineRoot $dependency
        $destination = Join-Path ([IO.Path]::GetDirectoryName($executablePath)) ([IO.Path]::GetFileName($source))
        if([IO.File]::Exists($destination) -and (Content-Hash $source) -ceq (Content-Hash $destination)) { continue }
        try { Copy-Item -LiteralPath $source -Destination $destination -Force }
        catch { throw "Cannot update runtime dependency '$destination'. Close applications using a different version of this DLL before rebuilding." }
    }
}
# EditorとRuntimeは同じManaged配置を使う。最後のMetadata生成後に両方のstampを確定する。
# Build中の別作業者の設定変更は保存したまま失敗させ、その変更へ古い成果物のstampを付けない。
foreach($snapshot in @(@($projectFilePath,$projectSnapshot),@($engineFilePath,$engineSnapshot),@($targetsFile,$targetsSnapshot))) {
    if([Convert]::ToBase64String([IO.File]::ReadAllBytes($snapshot[0])) -cne [Convert]::ToBase64String($snapshot[1])) { throw 'Project / Engine / BuildTargets changed during Build. Configuration was preserved; rebuild explicitly.' }
}
foreach($relative in $projects) {
    $hostKind=if($relative -eq $targets.editorProject) { 'Editor' } else { 'Runtime' }
    $reference=if($hostKind -ceq 'Editor') { $targets.editorExecutable } else { $targets.runtimeExecutable }
    $executablePath=Within $projectRoot (Resolve-OutputReference $reference)
    $live=if($BuildProfile -ceq 'NativeLivePatch') { 'true' } else { 'false' }
    & (Join-Path $PSScriptRoot 'Write-GameBuildStamp.ps1') -ProjectFile $projectFilePath -EngineManifest $engineFilePath -Executable $executablePath -Configuration $Configuration -HostKind $hostKind -NativeModuleLivePP $live
}
exit 0
