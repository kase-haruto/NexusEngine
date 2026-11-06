[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$EngineDirectory,
    [uri]$DownloadUrl,
    [Parameter(Mandatory=$true)][string]$ChangesFile,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
if($DownloadUrl -and ($DownloadUrl.Scheme -ne 'https' -or $DownloadUrl.UserInfo)) { throw 'DownloadUrl must be HTTPS without embedded credentials.' }
$root = (Resolve-Path -LiteralPath $EngineDirectory).Path
$manifest = Get-Content -LiteralPath (Join-Path $root 'NexusEngine.nxengine') -Raw -Encoding UTF8 | ConvertFrom-Json
if($manifest.association -ne 'installed') { throw 'Publish an Installed Engine SDK first; do not package a development repository.' }
foreach($value in @($manifest.id, $manifest.version)) { if($value -notmatch '\A[a-zA-Z0-9][a-zA-Z0-9._-]{0,99}\z') { throw 'Invalid identity.' } }
if($manifest.configuration -notin @('Debug','Develop','Release')) { throw 'Invalid configuration.' }
$changes = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $ChangesFile).Path, [Text.Encoding]::UTF8)
$output = [IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $output) { throw 'Choose a new output directory.' }
New-Item -ItemType Directory -Path $output | Out-Null
$zip = Join-Path $output "$($manifest.id)-$($manifest.version)-$($manifest.configuration).zip"
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($root, $zip)
$packageUrl = if($DownloadUrl) { $DownloadUrl.AbsoluteUri } else { [IO.Path]::GetFileName($zip) }
$release = [ordered]@{id=$manifest.id;version=$manifest.version;configuration=$manifest.configuration;changes=$changes;url=$packageUrl;sha256=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $zip).Length}
$catalogName = if($DownloadUrl) { 'releases.json' } else { 'releases.local.json' }
@{schemaVersion=1; releases=@($release)} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output $catalogName) -Encoding UTF8
if(-not $DownloadUrl) { Write-Warning 'Local package only: use releases.local.json in the launcher. Generate an HTTPS catalog after selecting the public URL.' }
Write-Output $zip
