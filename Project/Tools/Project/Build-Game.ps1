param(
    [Parameter(Mandatory=$true)][string]$ProjectFile,
    [Parameter(Mandatory=$true)][string]$EngineManifest,
    [ValidateSet('Editor','Runtime')][string]$Kind = 'Editor',
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$projectFilePath = [IO.Path]::GetFullPath($ProjectFile)
$engineFilePath = [IO.Path]::GetFullPath($EngineManifest)
$projectRoot = [IO.Path]::GetDirectoryName($projectFilePath)
$engineRoot = [IO.Path]::GetDirectoryName($engineFilePath)
$project = Get-Content -LiteralPath $projectFilePath -Raw -Encoding UTF8 | ConvertFrom-Json
$engine = Get-Content -LiteralPath $engineFilePath -Raw -Encoding UTF8 | ConvertFrom-Json
if($project.schemaVersion -ne 1 -or $engine.schemaVersion -ne 1 -or $engine.configuration -cne $Configuration) { throw 'Project/Engine build schema or configuration mismatch.' }
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
$targets = Get-Content -LiteralPath $targetsFile -Raw -Encoding UTF8 | ConvertFrom-Json
if($targets.schemaVersion -ne 1) { throw 'Unknown build targets schema.' }
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
$localProps = '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup><NexusEngineRoot>' + (Xml (Escape-Property $sdk)) + '</NexusEngineRoot><NexusEngineDataRoot>' + (Xml (Escape-Property $dataRoot)) + '</NexusEngineDataRoot><NexusEngineManifest>' + (Xml (Escape-Property $engineFilePath)) + '</NexusEngineManifest><NexusEngineAssociation>' + (Xml $engine.association) + '</NexusEngineAssociation></PropertyGroup></Project>'
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
foreach($relative in $projects) {
    $path = Within $projectRoot $relative
    $arguments = @($path, '/m', '/verbosity:minimal', "/p:Configuration=$Configuration", '/p:Platform=x64',
        ('/p:NexusEngineRoot=' + (Escape-Property $sdk)), ('/p:NexusEngineManifest=' + (Escape-Property $engineFilePath)),
        ('/p:NexusEngineAssociation=' + $engine.association), ('/p:NexusHostEngineId=' + (Escape-Property $engine.id)),
        ('/p:NexusHostEngineVersion=' + (Escape-Property $engine.version)), ('/p:NexusHostEngineAssociation=' + $engine.association))
    $start = [Diagnostics.ProcessStartInfo]::new(); $start.FileName = $msbuild; $start.WorkingDirectory = $projectRoot; $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $start.Arguments = ($arguments | ForEach-Object { Quote-Argument $_ }) -join ' '
    $environmentValues = @{}
    foreach($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) { $environmentValues[$entry.Key] = $entry.Value }
    $start.EnvironmentVariables.Clear(); foreach($key in $environmentValues.Keys) { $start.EnvironmentVariables[$key] = $environmentValues[$key] }
    $process = [Diagnostics.Process]::Start($start)
    $copyOutput = $process.StandardOutput.BaseStream.CopyToAsync([Console]::OpenStandardOutput())
    $copyError = $process.StandardError.BaseStream.CopyToAsync([Console]::OpenStandardError())
    $process.WaitForExit(); $null = $copyOutput.GetAwaiter().GetResult(); $null = $copyError.GetAwaiter().GetResult()
    if($process.ExitCode -ne 0) { exit $process.ExitCode }
    $executableRelative = if($relative -eq $targets.editorProject) { $targets.editorExecutable } else { $targets.runtimeExecutable }
    $executablePath = Within $projectRoot $executableRelative.Replace('{configuration}',$Configuration)
    foreach($dependency in $engine.runtimeFiles) {
        $source = Within $engineRoot $dependency
        $destination = Join-Path ([IO.Path]::GetDirectoryName($executablePath)) ([IO.Path]::GetFileName($source))
        if([IO.File]::Exists($destination) -and (Content-Hash $source) -ceq (Content-Hash $destination)) { continue }
        try { Copy-Item -LiteralPath $source -Destination $destination -Force }
        catch { throw "Cannot update runtime dependency '$destination'. Close applications using a different version of this DLL before rebuilding." }
    }
}
exit 0
