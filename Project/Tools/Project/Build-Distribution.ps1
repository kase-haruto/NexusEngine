[CmdletBinding()]
param(
    [uri]$CatalogUrl,
    [string]$CatalogFile,
    [string]$ProjectToolDirectory,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot ('../../../Generated/LauncherDistribution/' + [guid]::NewGuid().ToString('N'))),
    [switch]$FrameworkDependent
)
$ErrorActionPreference = 'Stop'
if($CatalogUrl -and $CatalogUrl.Scheme -ne 'https') { throw 'CatalogUrl must be HTTPS.' }
if($CatalogFile) { $CatalogFile = (Resolve-Path -LiteralPath $CatalogFile).Path }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$launcherSource = Join-Path $repository 'Project/Launcher'
if(-not $ProjectToolDirectory) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if(-not $msbuild) { throw 'MSBuild not found. Install Visual Studio C++ tools or specify -ProjectToolDirectory.' }
    & $msbuild (Join-Path $repository 'Project/Build/NexusProjectTool.vcxproj') /m:1 /t:Build /p:Configuration=Release /p:Platform=x64 /v:minimal
    if($LASTEXITCODE -ne 0) { throw 'NexusProjectTool build failed.' }
    $ProjectToolDirectory = Join-Path $repository 'Generated/Modules/x64/Release/NexusProjectTool'
}
$toolRoot = (Resolve-Path -LiteralPath $ProjectToolDirectory).Path
if(!(Test-Path -LiteralPath (Join-Path $toolRoot 'NexusProjectTool.exe'))) { throw 'ProjectToolDirectory must contain NexusProjectTool.exe and its runtime DLLs.' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $output) { throw 'Choose a new OutputDirectory; existing artifacts are never overwritten.' }
$launcherOutput = Join-Path $output 'Launcher'
$setupOutput = Join-Path $output 'Setup'
$selfContained = if($FrameworkDependent) { 'false' } else { 'true' }
New-Item -ItemType Directory -Path $output | Out-Null
& dotnet publish (Join-Path $launcherSource 'Standalone/Launcher.csproj') -c Release -r win-x64 --self-contained $selfContained -o $launcherOutput
if($LASTEXITCODE -ne 0) { throw 'Launcher publish failed.' }
$catalogAddress = if($CatalogUrl) { $CatalogUrl.AbsoluteUri } else { '' }
@{catalogUrl=$catalogAddress;catalogFile=$CatalogFile;projectTool='Tools/NexusProjectTool.exe'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $launcherOutput 'launcher.json') -Encoding UTF8
$tools = Join-Path $launcherOutput 'Tools'
New-Item -ItemType Directory -Path $tools | Out-Null
Copy-Item -LiteralPath (Join-Path $toolRoot 'NexusProjectTool.exe') -Destination $tools
Get-ChildItem -LiteralPath $toolRoot -Filter '*.dll' -File | Copy-Item -Destination $tools
$payload = Join-Path $output 'payload.zip'
Add-Type -AssemblyName System.IO.Compression.FileSystem
try {
    [IO.Compression.ZipFile]::CreateFromDirectory($launcherOutput, $payload)
    & dotnet publish (Join-Path $launcherSource 'Setup/LauncherSetup.csproj') -c Release -r win-x64 --self-contained $selfContained "-p:PayloadZip=$payload" -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o $setupOutput
    if($LASTEXITCODE -ne 0) { throw 'Setup publish failed.' }
} finally {
    if(Test-Path -LiteralPath $payload) { Remove-Item -LiteralPath $payload }
}
Write-Output (Join-Path $setupOutput 'launcherSetup.exe')
if(-not $CatalogUrl) { Write-Warning 'Preview build: no catalog URL configured. Specify -CatalogUrl before public distribution.' }
