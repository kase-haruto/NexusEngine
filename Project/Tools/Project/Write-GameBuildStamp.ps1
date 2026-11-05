param(
    [Parameter(Mandatory=$true)][string]$ProjectFile,
    [Parameter(Mandatory=$true)][string]$EngineManifest,
    [Parameter(Mandatory=$true)][string]$Executable,
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug',
    [ValidateSet('Editor','Runtime')][string]$HostKind = 'Runtime'
)
$ErrorActionPreference = 'Stop'
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
$output = $Executable + '.nxbuild'
$temporary = $output + '.' + [guid]::NewGuid().ToString() + '.tmp'
$backup = $temporary + '.bak'
try {
    [IO.File]::WriteAllText($temporary, ($stamp | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
    if([IO.File]::Exists($output)) { [IO.File]::Replace($temporary, $output, $backup) } else { [IO.File]::Move($temporary, $output) }
} finally {
    if([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    if([IO.File]::Exists($backup)) { [IO.File]::Delete($backup) }
}
